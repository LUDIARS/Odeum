#include "webrtc_broker.hpp"
#include "../adapters/clock.hpp"
#include "../adapters/log.hpp"
#include "../core/rtcp.hpp"
#include "audio_pipeline.hpp"
#include "rtp_source_pool.hpp"
#include "video_pipeline.hpp"
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <rtc/rtc.hpp>
#include <algorithm>
#include <cctype>
#include <deque>
#include <sstream>

namespace odeum::runner {
namespace asio = boost::asio;

namespace {
constexpr std::size_t max_sessions = 32;
constexpr std::size_t diag_history_max = 10;
constexpr auto ice_disconnect_grace = std::chrono::milliseconds(10'000);
constexpr auto gathering_timeout = std::chrono::milliseconds(5'000);
constexpr const char* h264_profile = "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f";

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string joined_fmtp(const rtc::Description::Media::RtpMap& map) {
    std::string all;
    for (const auto& f : map.fmtps) all += lower(f) + ";";
    return all;
}

const char* state_name(rtc::PeerConnection::State s) {
    switch (s) {
        case rtc::PeerConnection::State::New: return "new";
        case rtc::PeerConnection::State::Connecting: return "connecting";
        case rtc::PeerConnection::State::Connected: return "connected";
        case rtc::PeerConnection::State::Disconnected: return "disconnected";
        case rtc::PeerConnection::State::Failed: return "failed";
        case rtc::PeerConnection::State::Closed: return "closed";
    }
    return "unknown";
}

const char* ice_name(rtc::PeerConnection::IceState s) {
    switch (s) {
        case rtc::PeerConnection::IceState::New: return "new";
        case rtc::PeerConnection::IceState::Checking: return "checking";
        case rtc::PeerConnection::IceState::Connected: return "connected";
        case rtc::PeerConnection::IceState::Completed: return "completed";
        case rtc::PeerConnection::IceState::Failed: return "failed";
        case rtc::PeerConnection::IceState::Disconnected: return "disconnected";
        case rtc::PeerConnection::IceState::Closed: return "closed";
    }
    return "unknown";
}

struct Diag {
    std::string session_id, app_id;
    long long created_at = 0;
    std::optional<long long> closed_at, first_rtp_at_ms;
    int rtp_port = 0;
    long long rtp_received = 0, audio_rtp_received = 0;
    std::vector<std::string> pc_states, ice_states, ffmpeg_stderr;
    std::size_t answer_sdp_lines = 0;
    std::optional<std::string> audio_error;
    Json to_json() const {
        Json out{{"sessionId", session_id}, {"appId", app_id}, {"createdAt", created_at}};
        if (closed_at) out["closedAt"] = *closed_at;
        out["rtpPort"] = rtp_port;
        out["rtpReceived"] = rtp_received;
        if (first_rtp_at_ms) out["firstRtpAtMs"] = *first_rtp_at_ms;
        out["pcStates"] = pc_states;
        out["iceStates"] = ice_states;
        out["ffmpegStderr"] = ffmpeg_stderr;
        out["answerSdpLines"] = answer_sdp_lines;
        out["audioRtpReceived"] = audio_rtp_received;
        if (audio_error) out["audioError"] = *audio_error;
        return out;
    }
};

struct Session {
    std::string id, app_id;
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> video, audio;
    int video_pt = 96, audio_pt = 111;
    std::uint32_t video_ssrc = 0, audio_ssrc = 0;
    std::shared_ptr<Diag> diag;
    std::unique_ptr<asio::steady_timer> grace, gathering, no_rtp;
    std::function<void(std::string)> answered;  // set until the answer is produced
    bool closed = false;
};

class Broker final : public RtcService {
public:
    Broker(asio::io_context& io, IceServerProvider& ice, MediaTools& media, const RunnerSettings& settings, std::function<std::optional<long long>(const std::string&)> run_pid)
        : io_(io), ice_(ice),
          audio_factory_(io, media, AudioSettings{settings.process_loopback_source, settings.windir, settings.temp_dir}, std::move(run_pid)),
          video_(io, [&io, &media, bitrate = settings.video_bitrate] { return make_video_pipeline(io, media, VideoSettings{bitrate}); }, "video", monotonic_ms),
          audio_(io, [this] { return audio_factory_.make(); }, "audio", monotonic_ms) {}
    ~Broker() override { shutdown(); }

    void create_session(const AppConfig& app, std::string offer, std::function<void(std::variant<RtcAnswer, std::string>)> done) override {
        if (!app.capture && !app.in_app_bridge) return done("app " + app.id + " has neither capture nor inAppBridge config");
        if (sessions_.size() >= max_sessions) return done("too many WebRTC sessions (" + std::to_string(max_sessions) + ")");
        std::optional<OfferCodec> video, audio;
        try {
            video = pick_h264(offer);
            // Audio only when the app has it and the browser offered an audio section.
            if (app.audio && offer.find("m=audio") != std::string::npos) audio = pick_opus(offer);
        } catch (const std::exception&) {
            return done("invalid SDP offer");
        }
        if (!video) return done("the offer has no H.264 video (packetization-mode=1, baseline)");
        ice_.get([this, app, offer = std::move(offer), video = *video, audio, done](std::vector<IceServer> servers) {
            try {
                open(app, offer, video, audio, servers, done);
            } catch (const std::exception& error) {
                done(std::string("WebRTC negotiation failed: ") + error.what());
            }
        });
    }

    bool close_session(const std::string& id) override {
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return false;
        auto session = it->second;
        sessions_.erase(it);  // before close(): state callbacks must not close twice
        session->closed = true;
        for (auto* timer : {session->grace.get(), session->gathering.get(), session->no_rtp.get()}) if (timer) timer->cancel();
        video_.release(session->app_id, id);
        audio_.release(session->app_id, id);
        try { session->pc->resetCallbacks(); session->pc->close(); } catch (const std::exception&) { /* already closed */ }
        session->diag->closed_at = epoch_ms();
        log::info("webrtc-broker", "capture session closed", {{"sessionId", id}, {"rtpReceived", session->diag->rtp_received}});
        if (session->answered) { auto answered = std::move(session->answered); session->answered = nullptr; answered("session closed before the answer"); }
        return true;
    }

    void close_all_for_app(const std::string& app_id) override {
        std::vector<std::string> ids;
        for (const auto& [id, s] : sessions_) if (s->app_id == app_id) ids.push_back(id);
        for (const auto& id : ids) close_session(id);
    }

    Json diagnostics() const override {
        Json out = Json::array();
        for (auto it = history_.rbegin(); it != history_.rend(); ++it) out.push_back((*it)->to_json());
        return out;
    }

    void shutdown() override {
        std::vector<std::string> ids;
        for (const auto& [id, s] : sessions_) ids.push_back(id);
        for (const auto& id : ids) close_session(id);
        video_.shutdown();
        audio_.shutdown();
    }

private:
    asio::io_context& io_;
    IceServerProvider& ice_;
    AudioPipelineFactory audio_factory_;
    SharedRtpSourcePool video_, audio_;
    std::map<std::string, std::shared_ptr<Session>> sessions_;
    std::deque<std::shared_ptr<Diag>> history_;

    void open(const AppConfig& app, const std::string& offer, const OfferCodec& video, const std::optional<OfferCodec>& audio,
        const std::vector<IceServer>& servers, std::function<void(std::variant<RtcAnswer, std::string>)> done) {
        rtc::Configuration config;
        for (const auto& server : flatten_for_peer(servers)) {
            rtc::IceServer ice(server.urls.at(0));
            if (server.username) ice.username = *server.username;
            if (server.credential) ice.password = *server.credential;
            config.iceServers.push_back(std::move(ice));
        }
        config.disableAutoNegotiation = true;
        auto session = std::make_shared<Session>();
        session->id = random_uuid();
        session->app_id = app.id;
        session->diag = std::make_shared<Diag>();
        session->diag->session_id = session->id;
        session->diag->app_id = app.id;
        session->diag->created_at = epoch_ms();
        session->pc = std::make_shared<rtc::PeerConnection>(config);
        session->video_pt = video.payload_type;
        session->video_ssrc = 1 + random_below(0x7ffffffe);

        rtc::Description::Video video_media(video.mid, rtc::Description::Direction::SendOnly);
        video_media.addH264Codec(video.payload_type, std::string(h264_profile));
        if (auto* map = video_media.rtpMap(video.payload_type)) {
            for (const char* fb : {"nack", "nack pli", "ccm fir", "goog-remb"}) map->addFeedback(fb);
        }
        video_media.addSSRC(session->video_ssrc, std::string("custos"), std::string("custos"), std::string("custos-video"));
        session->video = session->pc->addTrack(video_media);
        if (audio) {
            session->audio_pt = audio->payload_type;
            session->audio_ssrc = 1 + random_below(0x7ffffffe);
            rtc::Description::Audio audio_media(audio->mid, rtc::Description::Direction::SendOnly);
            audio_media.addOpusCodec(audio->payload_type, std::string("minptime=10;useinbandfec=1"));
            audio_media.addSSRC(session->audio_ssrc, std::string("custos"), std::string("custos"), std::string("custos-audio"));
            session->audio = session->pc->addTrack(audio_media);
        }
        rtc::Description remote(offer, rtc::Description::Type::Offer);
        // Every other offered section is rejected (port 0) instead of being answered send-only.
        for (int i = 0; i < remote.mediaCount(); ++i) {
            auto entry = remote.media(i);
            auto* media = std::get_if<rtc::Description::Media*>(&entry);
            if (!media) continue;
            const auto mid = (*media)->mid();
            if (mid == video.mid || (audio && mid == audio->mid)) continue;
            auto rejected = (*media)->reciprocate();
            rejected.markRemoved();
            (void)session->pc->addTrack(std::move(rejected));
        }
        watch(session);
        std::weak_ptr<Session> weak_session = session;
        session->answered = [this, weak_session, app, audio, done](std::string sdp) {
            if (sdp.empty() || sdp.rfind("session closed", 0) == 0) return done(sdp.empty() ? std::string("ICE gathering produced no answer") : sdp);
            if (auto s = weak_session.lock()) attach_sources(s, app, audio.has_value(), std::move(sdp), done);
            else done(std::string("session closed before the answer"));
        };
        log::info("webrtc-broker", "received offer", {{"sessionId", session->id}});
        try {
            session->pc->setRemoteDescription(std::move(remote));
            session->pc->setLocalDescription(rtc::Description::Type::Answer);
        } catch (...) {
            session->answered = nullptr;
            session->pc->resetCallbacks();
            session->pc->close();
            throw;
        }
        sessions_[session->id] = session;
        history_.push_back(session->diag);
        while (history_.size() > diag_history_max) history_.pop_front();
        session->gathering = std::make_unique<asio::steady_timer>(io_, gathering_timeout);
        std::weak_ptr<Session> weak = session;
        session->gathering->async_wait([this, weak](const boost::system::error_code& error) {
            if (error) return;
            if (auto s = weak.lock()) answer(s);  // gathering incomplete: answer with what we have
        });
    }

    void answer(const std::shared_ptr<Session>& session) {
        if (!session->answered || session->closed) return;
        auto answered = std::move(session->answered);
        session->answered = nullptr;
        if (session->gathering) session->gathering->cancel();
        const auto local = session->pc->localDescription();
        const auto sdp = local ? std::string(*local) : std::string();
        session->diag->answer_sdp_lines = static_cast<std::size_t>(std::count(sdp.begin(), sdp.end(), '\n')) + 1;
        auto summary = sdp_summary(sdp);
        summary["sessionId"] = session->id;
        log::info("webrtc-broker", "answer SDP summary", summary);
        answered(sdp);
    }

    void watch(const std::shared_ptr<Session>& session) {
        std::weak_ptr<Session> weak = session;
        auto post = [this, weak](std::function<void(const std::shared_ptr<Session>&)> f) {
            asio::post(io_, [weak, f = std::move(f)] { if (auto s = weak.lock(); s && !s->closed) f(s); });
        };
        session->pc->onGatheringStateChange([post, this](rtc::PeerConnection::GatheringState state) {
            post([this, state](const std::shared_ptr<Session>& s) {
                s->diag->ice_states.push_back(std::to_string(epoch_ms() - s->diag->created_at) + "ms:gather=" +
                    (state == rtc::PeerConnection::GatheringState::Complete ? "complete" : state == rtc::PeerConnection::GatheringState::InProgress ? "gathering" : "new"));
                if (state == rtc::PeerConnection::GatheringState::Complete) answer(s);
            });
        });
        session->pc->onStateChange([post, this](rtc::PeerConnection::State state) {
            post([this, state](const std::shared_ptr<Session>& s) {
                s->diag->pc_states.push_back(std::to_string(epoch_ms() - s->diag->created_at) + "ms:" + state_name(state));
                log::info("webrtc-broker", "pc state", {{"sessionId", s->id}, {"state", state_name(state)}});
                // Key frames sent before DTLS came up never arrived: ask for a fresh one.
                if (state == rtc::PeerConnection::State::Connected) video_.picture_loss(s->app_id, s->id);
                if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) close_session(s->id);
            });
        });
        session->pc->onIceStateChange([post, this](rtc::PeerConnection::IceState state) {
            post([this, state](const std::shared_ptr<Session>& s) {
                s->diag->ice_states.push_back(std::to_string(epoch_ms() - s->diag->created_at) + "ms:conn=" + ice_name(state));
                if (s->grace) s->grace->cancel();
                if (state == rtc::PeerConnection::IceState::Failed || state == rtc::PeerConnection::IceState::Closed) close_session(s->id);
                else if (state == rtc::PeerConnection::IceState::Disconnected) {
                    // A short loss of connectivity can recover; a browser that left never does.
                    s->grace = std::make_unique<asio::steady_timer>(io_, ice_disconnect_grace);
                    std::weak_ptr<Session> weak = s;
                    s->grace->async_wait([this, weak](const boost::system::error_code& error) {
                        if (error) return;
                        if (auto session = weak.lock()) {
                            log::info("webrtc-broker", "ICE stayed disconnected — closing session", {{"sessionId", session->id}});
                            close_session(session->id);
                        }
                    });
                }
            });
        });
        session->video->onMessage([post, this](rtc::message_variant data) {
            const auto* bytes = std::get_if<rtc::binary>(&data);
            if (!bytes || bytes->size() > 1500) return;
            const auto feedback = parse_rtcp_feedback(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
            if (!feedback.keyframe_request && !feedback.remb_bps) return;
            post([this, feedback](const std::shared_ptr<Session>& s) {
                if (feedback.keyframe_request) video_.picture_loss(s->app_id, s->id);
                if (feedback.remb_bps && *feedback.remb_bps > 0) video_.estimate(s->app_id, s->id, static_cast<double>(*feedback.remb_bps));
            });
        });
    }

    static void send(const std::shared_ptr<rtc::Track>& track, std::string_view packet, int payload_type, std::uint32_t ssrc) {
        if (!track || !track->isOpen()) return;
        std::string copy(packet);
        if (!rewrite_rtp_header(copy, payload_type, ssrc)) return;
        try {
            track->send(reinterpret_cast<const std::byte*>(copy.data()), copy.size());
        } catch (const std::exception&) {
            // A concurrent transport close drops this packet; no payload is logged.
        }
    }

    void attach_sources(const std::shared_ptr<Session>& session, const AppConfig& app, bool with_audio, std::string sdp,
        std::function<void(std::variant<RtcAnswer, std::string>)> done) {
        std::weak_ptr<Session> weak = session;
        RtpSink sink;
        sink.packet = [weak](std::string_view packet) {
            auto s = weak.lock();
            if (!s || s->closed) return;
            send(s->video, packet, s->video_pt, s->video_ssrc);
            if (++s->diag->rtp_received == 1) {
                s->diag->first_rtp_at_ms = epoch_ms() - s->diag->created_at;
                log::info("webrtc-broker", "first RTP datagram arrived", {{"sessionId", s->id}, {"firstRtpAtMs", *s->diag->first_rtp_at_ms}});
            }
        };
        sink.end = [this, weak] { if (auto s = weak.lock()) close_session(s->id); };
        video_.acquire(app, session->id, sink, [this, weak, app, with_audio, sdp = std::move(sdp), done](std::variant<int, std::string> result) {
            auto s = weak.lock();
            if (!s || s->closed) return done(std::string("video source for " + app.id + " ended while starting"));
            if (auto* error = std::get_if<std::string>(&result)) {
                close_session(s->id);
                return done(*error);
            }
            s->diag->rtp_port = std::get<int>(result);
            s->diag->ffmpeg_stderr = video_.stderr_tail(app.id);
            auto finish = [this, weak, done, sdp, app](std::optional<std::string> audio_error) {
                auto s = weak.lock();
                if (!s || s->closed) return done(std::string("session closed while starting"));
                s->diag->audio_error = audio_error;
                log::info("webrtc-broker", "capture session opened", {{"sessionId", s->id}, {"appId", app.id}, {"subscribers", video_.subscribers(app.id)}, {"audio", s->audio && !audio_error}});
                arm_no_rtp_warning(s);
                done(RtcAnswer{s->id, sdp, "answer", audio_error});
            };
            if (!with_audio || !s->audio) return finish(std::nullopt);
            RtpSink audio_sink;
            audio_sink.packet = [weak](std::string_view packet) {
                auto session = weak.lock();
                if (!session || session->closed) return;
                send(session->audio, packet, session->audio_pt, session->audio_ssrc);
                ++session->diag->audio_rtp_received;
            };
            audio_sink.end = [weak, app] { if (auto session = weak.lock()) log::warn("webrtc-broker", "audio source ended — video keeps streaming", {{"sessionId", session->id}, {"appId", app.id}}); };
            // Audio failures leave video streaming; the reason goes back to the browser log.
            audio_.acquire(app, s->id, audio_sink, [finish](std::variant<int, std::string> r) {
                if (auto* error = std::get_if<std::string>(&r)) {
                    log::warn("webrtc-broker", "audio unavailable — streaming video only", {{"err", *error}});
                    return finish(*error);
                }
                finish(std::nullopt);
            });
        });
    }

    void arm_no_rtp_warning(const std::shared_ptr<Session>& session) {
        session->no_rtp = std::make_unique<asio::steady_timer>(io_, std::chrono::seconds(5));
        std::weak_ptr<Session> weak = session;
        session->no_rtp->async_wait([this, weak](const boost::system::error_code& error) {
            if (error) return;
            auto s = weak.lock();
            if (!s || s->closed || s->diag->rtp_received > 0) return;
            log::warn("webrtc-broker", "no RTP after 5s — check ffmpeg / window title",
                {{"sessionId", s->id}, {"pcStates", s->diag->pc_states}, {"iceStates", s->diag->ice_states}, {"ffmpegTail", video_.stderr_tail(s->app_id)}});
        });
    }
};
}

std::optional<OfferCodec> pick_h264(const std::string& offer_sdp) {
    rtc::Description description(offer_sdp, rtc::Description::Type::Offer);
    for (int i = 0; i < description.mediaCount(); ++i) {
        auto entry = description.media(i);
        auto* media = std::get_if<rtc::Description::Media*>(&entry);
        if (!media || (*media)->type() != "video" || (*media)->isRemoved()) continue;
        int best = -1, rank = 0;
        for (int pt : (*media)->payloadTypes()) {
            if (!(*media)->hasPayloadType(pt)) continue;  // static payload types carry no rtpmap
            const auto* map = (*media)->rtpMap(pt);
            if (lower(map->format) != "h264" || map->clockRate != 90000) continue;
            const auto fmtp = joined_fmtp(*map);
            if (fmtp.find("packetization-mode=1") == std::string::npos) continue;
            const auto at = fmtp.find("profile-level-id=");
            const auto profile = at == std::string::npos ? std::string() : fmtp.substr(at + 17, 6);
            const int score = profile == "42e01f" ? 4 : (profile.rfind("42e0", 0) == 0 || profile.rfind("42c0", 0) == 0) ? 3 : profile.rfind("4200", 0) == 0 ? 2 : 0;
            if (score > rank) { rank = score; best = pt; }
        }
        if (best >= 0) return OfferCodec{(*media)->mid(), best};
        return std::nullopt;  // the first video section decides, as werift answered one video track
    }
    return std::nullopt;
}

std::optional<OfferCodec> pick_opus(const std::string& offer_sdp) {
    rtc::Description description(offer_sdp, rtc::Description::Type::Offer);
    for (int i = 0; i < description.mediaCount(); ++i) {
        auto entry = description.media(i);
        auto* media = std::get_if<rtc::Description::Media*>(&entry);
        if (!media || (*media)->type() != "audio" || (*media)->isRemoved()) continue;
        for (int pt : (*media)->payloadTypes()) {
            if (!(*media)->hasPayloadType(pt)) continue;
            const auto* map = (*media)->rtpMap(pt);
            if (lower(map->format) == "opus" && map->clockRate == 48000) return OfferCodec{(*media)->mid(), pt};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

Json sdp_summary(const std::string& sdp) {
    Json m = Json::array(), rtpmaps = Json::array(), fmtps = Json::array();
    int candidates = 0;
    std::string setup;
    std::istringstream in(sdp);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("m=", 0) == 0) m.push_back(line);
        else if (line.rfind("a=rtpmap:", 0) == 0) {
            const auto l = lower(line);
            if (l.find("h264") != std::string::npos || l.find("vp8") != std::string::npos || l.find("vp9") != std::string::npos || l.find("av1") != std::string::npos) rtpmaps.push_back(line);
        } else if (line.rfind("a=fmtp:", 0) == 0) fmtps.push_back(line);
        else if (line.rfind("a=candidate:", 0) == 0) ++candidates;
        else if (line.rfind("a=setup:", 0) == 0) setup = line;
    }
    return Json{{"mLines", m}, {"rtpmaps", rtpmaps}, {"fmtps", fmtps}, {"candidates", candidates}, {"setup", setup}};
}

std::unique_ptr<RtcService> make_webrtc_broker(asio::io_context& io, IceServerProvider& ice, MediaTools& media, const RunnerSettings& settings,
    std::function<std::optional<long long>(const std::string&)> run_pid) {
    return std::make_unique<Broker>(io, ice, media, settings, std::move(run_pid));
}

void init_webrtc_logging() {
    // libdatachannel diagnostics stay off: SDP and candidates must not reach the logs.
    rtc::InitLogger(rtc::LogLevel::None);
}

bool finish_webrtc(std::chrono::milliseconds timeout) {
    return rtc::Cleanup().wait_for(timeout) == std::future_status::ready;
}
}
