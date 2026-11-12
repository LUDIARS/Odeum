// custos/tests/shared-rtp-source.test.ts, video-pipeline.test.ts (selection) and the WebRTC session
// path end to end: a libdatachannel offerer (standing in for the browser) negotiates with the runner
// over the real /api/apps/:id/rtc/offer route while a fake Unity bridge streams framed H.264.
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/clock.hpp>
#include <adapters/process.hpp>
#include <core/rtcp.hpp>
#include <rtc/rtc.hpp>
#include <webrtc/audio_pipeline.hpp>
#include <webrtc/rtp_source_pool.hpp>
#include <webrtc/video_pipeline.hpp>
#include <webrtc/webrtc_broker.hpp>
#include <atomic>
#include <future>
#include <thread>

using namespace odeum::runner;
using namespace runner_test;
namespace asio = boost::asio;

namespace {
const char* chrome_like_offer = "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0 1\r\n"
    "m=video 9 UDP/TLS/RTP/SAVPF 96 102 108 127\r\nc=IN IP4 0.0.0.0\r\na=mid:0\r\na=recvonly\r\na=rtcp-mux\r\n"
    "a=ice-ufrag:abcd\r\na=ice-pwd:abcdefghijklmnopqrstuvwx\r\na=fingerprint:sha-256 00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF\r\na=setup:actpass\r\n"
    "a=rtpmap:96 VP8/90000\r\na=rtpmap:102 H264/90000\r\na=fmtp:102 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42001f\r\n"
    "a=rtpmap:108 H264/90000\r\na=fmtp:108 level-asymmetry-allowed=1;packetization-mode=0;profile-level-id=42e01f\r\n"
    "a=rtpmap:127 H264/90000\r\na=fmtp:127 level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f\r\n"
    "m=audio 9 UDP/TLS/RTP/SAVPF 111 0\r\nc=IN IP4 0.0.0.0\r\na=mid:1\r\na=recvonly\r\na=rtcp-mux\r\na=rtpmap:111 opus/48000/2\r\na=rtpmap:0 PCMU/8000\r\n";

class FakePipeline final : public RtpPipeline {
public:
    struct Shared { int starts = 0, stops = 0, keyframes = 0; std::vector<long long> bitrates; Events events; std::function<void(std::optional<StartError>)> started; };
    explicit FakePipeline(std::shared_ptr<Shared> shared, bool feedback) : shared_(std::move(shared)), feedback_(feedback) {}
    void start(const AppConfig&, int, Events events, std::function<void(std::optional<StartError>)> started) override {
        ++shared_->starts;
        shared_->events = std::move(events);
        shared_->started = std::move(started);
    }
    void stop() override { ++shared_->stops; }
    bool takes_feedback() const override { return feedback_; }
    void request_keyframe() override { ++shared_->keyframes; }
    void set_bitrate(long long bps) override { shared_->bitrates.push_back(bps); }
private:
    std::shared_ptr<Shared> shared_;
    bool feedback_;
};

AppConfig bridge_app() {
    return parse_apps_config(Json::parse(R"({"apps":[{"id":"u","name":"U","run":{"cwd":".","cmd":"x"},"inAppBridge":{"kind":"unity","port":1}}]})")).apps.at(0);
}

std::string pli(std::uint32_t media_ssrc) {
    std::string p(12, '\0');
    p[0] = static_cast<char>(0x81); p[1] = static_cast<char>(206); p[3] = 2;
    for (int i = 0; i < 4; ++i) p[8 + i] = static_cast<char>((media_ssrc >> (24 - 8 * i)) & 0xff);
    return p;
}

// An ergo bridge that streams raw BGRA frames (64x48 at 10 fps) for /stream.
class RawBridge {
public:
    explicit RawBridge(asio::io_context& io) : io_(io), acceptor_(io, {asio::ip::make_address("127.0.0.1"), 0}) { accept(); }
    unsigned short port() const { return acceptor_.local_endpoint().port(); }
private:
    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    void accept() {
        acceptor_.async_accept([this](boost::system::error_code error, asio::ip::tcp::socket socket) {
            if (error) return;
            auto s = std::make_shared<asio::ip::tcp::socket>(std::move(socket));
            auto buffer = std::make_shared<std::string>(4096, '\0');
            s->async_read_some(asio::buffer(*buffer), [this, s, buffer](boost::system::error_code e, std::size_t) {
                if (e) return;
                auto head = std::make_shared<std::string>("HTTP/1.1 200 OK\r\nX-Frame-Width: 64\r\nX-Frame-Height: 48\r\nX-Frame-Pixfmt: bgra\r\nX-Frame-Fps: 10\r\nConnection: close\r\n\r\n");
                asio::async_write(*s, asio::buffer(*head), [this, s, head](boost::system::error_code e2, std::size_t) { if (!e2) frame(s, 0); });
            });
            accept();
        });
    }
    void frame(std::shared_ptr<asio::ip::tcp::socket> s, int n) {
        auto pixels = std::make_shared<std::string>(64 * 48 * 4, static_cast<char>(n * 7));
        asio::async_write(*s, asio::buffer(*pixels), [this, s, pixels, n](boost::system::error_code e, std::size_t) {
            if (e) return;
            auto timer = std::make_shared<asio::steady_timer>(io_, std::chrono::milliseconds(100));
            timer->async_wait([this, s, timer, n](boost::system::error_code) { frame(s, n + 1); });
        });
    }
};

struct Browser {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::shared_ptr<rtc::Track> track;
    std::atomic<int> rtp{0};
    std::string offer;
    Browser() {
        pc = std::make_shared<rtc::PeerConnection>(rtc::Configuration{});
        rtc::Description::Video media_line("0", rtc::Description::Direction::RecvOnly);
        media_line.addH264Codec(102, std::string("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f"));
        track = pc->addTrack(media_line);
        track->onMessage([this](rtc::message_variant data) {
            if (auto* b = std::get_if<rtc::binary>(&data); b && !is_rtcp(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()))) ++rtp;
        });
        std::promise<void> gathered;
        pc->onGatheringStateChange([&gathered](rtc::PeerConnection::GatheringState s) { if (s == rtc::PeerConnection::GatheringState::Complete) gathered.set_value(); });
        pc->setLocalDescription(rtc::Description::Type::Offer);
        if (gathered.get_future().wait_for(std::chrono::seconds(10)) != std::future_status::ready) throw std::runtime_error("offer not gathered");
        pc->onGatheringStateChange(nullptr);
        offer = std::string(*pc->localDescription());
    }
};

// A Unity bridge that streams framed H.264 for /stream?codec=h264 until the client goes away.
class StreamingBridge {
public:
    explicit StreamingBridge(asio::io_context& io) : io_(io), acceptor_(io, {asio::ip::make_address("127.0.0.1"), 0}) { accept(); }
    unsigned short port() const { return acceptor_.local_endpoint().port(); }
    std::atomic<int> keyframe_requests{0}, streams{0};
    std::string last_target;
private:
    asio::io_context& io_;
    asio::ip::tcp::acceptor acceptor_;
    void accept() {
        acceptor_.async_accept([this](boost::system::error_code error, asio::ip::tcp::socket socket) {
            if (error) return;
            auto s = std::make_shared<asio::ip::tcp::socket>(std::move(socket));
            auto buffer = std::make_shared<std::string>(4096, '\0');
            s->async_read_some(asio::buffer(*buffer), [this, s, buffer](boost::system::error_code e, std::size_t n) {
                if (e) return;
                const auto request = buffer->substr(0, n);
                const auto target = request.substr(request.find(' ') + 1, request.find(' ', request.find(' ') + 1) - request.find(' ') - 1);
                if (target.rfind("/stream/keyframe", 0) == 0 || target.rfind("/stream/bitrate", 0) == 0) {
                    if (target.rfind("/stream/keyframe", 0) == 0) ++keyframe_requests;
                    auto reply = std::make_shared<std::string>("HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    asio::async_write(*s, asio::buffer(*reply), [s, reply](boost::system::error_code, std::size_t) {});
                    return;
                }
                last_target = target;
                ++streams;
                auto head = std::make_shared<std::string>("HTTP/1.1 200 OK\r\nX-Frame-Codec: h264\r\nConnection: close\r\n\r\n");
                asio::async_write(*s, asio::buffer(*head), [this, s, head](boost::system::error_code e2, std::size_t) { if (!e2) frames(s, 0); });
            });
            accept();
        });
    }
    void frames(std::shared_ptr<asio::ip::tcp::socket> s, std::int64_t ts) {
        std::string nal = std::string("\0\0\0\x01\x65", 5) + std::string(3000, '\x42');
        auto frame = std::make_shared<std::string>(13, '\0');
        const auto length = static_cast<std::uint32_t>(nal.size());
        for (int i = 0; i < 4; ++i) (*frame)[i] = static_cast<char>((length >> (24 - 8 * i)) & 0xff);
        for (int i = 0; i < 8; ++i) (*frame)[4 + i] = static_cast<char>((static_cast<std::uint64_t>(ts) >> (56 - 8 * i)) & 0xff);
        (*frame)[12] = 1;
        frame->append(nal);
        asio::async_write(*s, asio::buffer(*frame), [this, s, frame, ts](boost::system::error_code e, std::size_t) {
            if (e) return;
            auto timer = std::make_shared<asio::steady_timer>(io_, std::chrono::milliseconds(33));
            timer->async_wait([this, s, timer, ts](boost::system::error_code) { frames(s, ts + 33'000); });
        });
    }
};
}

int main(int argc, char** argv) {
    rtc::InitLogger(rtc::LogLevel::None);
    return run_all(argc, argv, "runner.webrtc", {
        {"webrtc-broker > picks the browser's H.264 packetization-mode=1 constrained baseline payload type and Opus", [] {
            const auto video = pick_h264(chrome_like_offer);
            check(video && video->mid == "0" && video->payload_type == 127, "42e01f mode 1 preferred over 42001f and mode 0");
            const auto audio = pick_opus(chrome_like_offer);
            check(audio && audio->mid == "1" && audio->payload_type == 111, "opus");
            std::string vp8_only = chrome_like_offer;
            vp8_only = vp8_only.substr(0, vp8_only.find("a=rtpmap:102")) + "m=audio 9 UDP/TLS/RTP/SAVPF 0\r\na=mid:1\r\na=rtpmap:0 PCMU/8000\r\n";
            check(!pick_h264(vp8_only) && !pick_opus(vp8_only), "nothing usable");
            const auto summary = sdp_summary(chrome_like_offer);
            check(summary["mLines"].size() == 2 && summary["rtpmaps"].size() == 4 && summary["fmtps"].size() == 3 && summary["setup"] == "a=setup:actpass", "summary");
        }},
        {"boundary > RTCP feedback parsing (PLI, FIR, REMB, truncated, compound) and RTP re-stamping", [] {
            check(parse_rtcp_feedback(pli(5)).keyframe_request, "PLI");
            std::string fir(20, '\0');
            fir[0] = static_cast<char>(0x84); fir[1] = static_cast<char>(206); fir[3] = 4;
            check(parse_rtcp_feedback(fir).keyframe_request, "FIR");
            check(!parse_rtcp_feedback(pli(5).substr(0, 8)).keyframe_request, "truncated PLI");
            std::string remb(24, '\0');
            remb[0] = static_cast<char>(0x8f); remb[1] = static_cast<char>(206); remb[3] = 5;
            remb.replace(12, 4, "REMB");
            remb[16] = 1; remb[17] = static_cast<char>((3 << 2) | 0x1); remb[18] = 0; remb[19] = 0;  // mantissa 0x10000, exponent 3
            check(parse_rtcp_feedback(remb).remb_bps == (0x10000ull << 3), "REMB");
            std::string rr(8, '\0');
            rr[0] = static_cast<char>(0x80); rr[1] = static_cast<char>(201); rr[3] = 1;
            const auto compound = parse_rtcp_feedback(rr + pli(7) + remb);
            check(compound.keyframe_request && compound.remb_bps, "compound");
            std::string lying = pli(1);
            lying[3] = 50;  // claims more words than present
            check(!parse_rtcp_feedback(lying).keyframe_request, "length beyond the packet");
            check(!parse_rtcp_feedback("\x80\x60\x00\x01").keyframe_request && !is_rtcp(std::string("\x80\x60\x00\x01", 4)), "RTP is not RTCP");
            std::string rtp(12, '\0');
            rtp[0] = static_cast<char>(0x80); rtp[1] = static_cast<char>(0x80 | 96);
            check(rewrite_rtp_header(rtp, 127, 0x01020304) && static_cast<unsigned char>(rtp[1]) == (0x80 | 127) && rtp[8] == 1 && rtp[11] == 4, "PT and SSRC, marker kept");
            std::string short_packet(11, '\0');
            check(!rewrite_rtp_header(short_packet, 1, 1), "short packet refused");
        }},
        {"shared-rtp-source > one source per app, key frame for late joiners, last release stops", [] {
            asio::io_context io;
            auto shared = std::make_shared<FakePipeline::Shared>();
            long long now = 0;
            SharedRtpSourcePool pool(io, [shared] { return std::make_unique<FakePipeline>(shared, true); }, "video", [&] { return now; });
            const auto app = bridge_app();
            std::vector<std::variant<int, std::string>> results;
            int packets_a = 0, packets_b = 0, ended = 0;
            pool.acquire(app, "a", RtpSink{[&](std::string_view) { ++packets_a; }, [&] { ++ended; }}, [&](auto r) { results.push_back(r); });
            pool.acquire(app, "b", RtpSink{[&](std::string_view) { ++packets_b; }, [&] { ++ended; }}, [&](auto r) { results.push_back(r); });
            check(shared->starts == 1, "one pipeline for both subscribers");
            shared->started(std::nullopt);
            run_until(io, [&] { return results.size() == 2; });
            check(results.size() == 2 && std::holds_alternative<int>(results[0]) && std::get<int>(results[0]) > 0, "both get the RTP port");
            check(shared->keyframes == 1, "key frame once the source runs");
            shared->events.packet("\x80\x60packet");
            check(packets_a == 1 && packets_b == 1, "fan out");
            pool.acquire(app, "c", RtpSink{[](std::string_view) {}, [] {}}, [&](auto r) { results.push_back(r); });
            run_until(io, [&] { return results.size() == 3; });
            check(shared->keyframes == 2, "late joiner gets a key frame");
            pool.picture_loss("u", "a");
            pool.picture_loss("u", "a");
            check(shared->keyframes == 3, "PLI coalesced within 500 ms");
            pool.estimate("u", "a", 1'000'000);
            check(shared->bitrates == std::vector<long long>{1'000'000}, "REMB lowers the bitrate");
            pool.release("u", "a");
            pool.release("u", "b");
            check(shared->stops == 0 && pool.subscribers("u") == 1, "still one subscriber");
            pool.release("u", "c");
            check(shared->stops == 1 && pool.subscribers("u") == 0 && ended == 0, "last release stops without end notices");
        }},
        {"shared-rtp-source > start failure, release while starting, and source exit", [] {
            asio::io_context io;
            auto shared = std::make_shared<FakePipeline::Shared>();
            SharedRtpSourcePool pool(io, [shared] { return std::make_unique<FakePipeline>(shared, false); }, "video", [] { return 0LL; });
            const auto app = bridge_app();
            std::vector<std::variant<int, std::string>> results;
            pool.acquire(app, "a", RtpSink{}, [&](auto r) { results.push_back(r); });
            shared->started(StartError{"video のパイプラインを起動できません (spawn ffmpeg ENOENT)"});
            check(results.size() == 1 && std::get<std::string>(results[0]).find("ENOENT") != std::string::npos && shared->stops == 1, "start failure reported, nothing left");
            pool.acquire(app, "b", RtpSink{}, [&](auto r) { results.push_back(r); });
            pool.release("u", "b");
            check(shared->stops == 1, "not stopped mid-start");
            shared->started(std::nullopt);
            run_until(io, [&] { return results.size() == 2; });
            check(shared->stops == 2 && std::holds_alternative<std::string>(results[1]), "stopped once the start settled");
            int ended = 0;
            pool.acquire(app, "c", RtpSink{[](std::string_view) {}, [&] { ++ended; }}, [&](auto r) { results.push_back(r); });
            shared->started(std::nullopt);
            run_until(io, [&] { return results.size() == 3; });
            shared->events.exit();
            check(ended == 1 && pool.subscribers("u") == 0, "source exit ends the sessions");
        }},
        {"video-pipeline > bridge H.264 first; 415 falls back to raw + ffmpeg (auto) but not for codec h264; ergo goes straight to raw", [] {
            asio::io_context io;
            FakeHttpServer bridge(io, [](const FakeHttpServer::Request& r) {
                if (std::string(r.target()).find("codec=h264") != std::string::npos) return FakeResponse{415, "no encoder", {}, {}};
                return FakeResponse{200, std::string(64 * 48 * 4, '\0'), {{"x-frame-width", "64"}, {"x-frame-height", "48"}, {"x-frame-pixfmt", "bgra"}, {"x-frame-fps", "10"}}, {}};
            });
            MediaTools media(io, "ffmpeg-missing", CaptureHost{Platform::linux_os, "", ""}, "x264");
            auto start = [&](const std::string& kind, const std::string& codec) {
                auto app = parse_apps_config(Json::parse(R"({"apps":[{"id":"a","name":"A","run":{"cwd":".","cmd":"x"},"inAppBridge":{"kind":")" + kind +
                    R"(","port":)" + std::to_string(bridge.port()) + R"(,"video":{"codec":")" + codec + R"("}}}]})")).apps.at(0);
                auto pipeline = make_video_pipeline(io, media, VideoSettings{});
                std::optional<std::optional<StartError>> result;
                pipeline->start(app, 5004, RtpPipeline::Events{}, [&](std::optional<StartError> e) { result = std::move(e); });
                run_until(io, [&] { return result.has_value(); });
                pipeline->stop();
                return result->value_or(StartError{"(started)"}).message;
            };
            const auto seen_before = bridge.seen.size();
            const auto fallback = start("unity", "auto");
            check(fallback.rfind("video のパイプラインを起動できません (spawn ffmpeg-missing ENOENT)", 0) == 0, "auto fell back to raw + ffmpeg: " + fallback);
            check(bridge.seen.size() == seen_before + 2 && bridge.seen[seen_before].find("codec=h264") != std::string::npos && bridge.seen[seen_before + 1] == "GET /stream?fps=24", "asked for h264 then raw");
            const auto strict = start("unity", "h264");
            check(strict == "bridge cannot stream h264 (HTTP 415): no encoder", "h264 only: no fallback: " + strict);
            const auto before_ergo = bridge.seen.size();
            start("ergo", "auto");
            check(bridge.seen.size() == before_ergo + 1 && bridge.seen.back() == "GET /stream?fps=24", "ergo bridges go straight to raw");
        }},
        {"audio-source > target pid from the bridge /health or the run process; reasons when unavailable", [] {
            asio::io_context io;
            FakeHttpServer with_pid(io, [](const FakeHttpServer::Request&) { return FakeResponse{200, R"({"status":"ok","isPlaying":false,"pid":4242})", {}, {}}; });
            FakeHttpServer without_pid(io, [](const FakeHttpServer::Request&) { return FakeResponse{200, R"({"status":"ok","isPlaying":false})", {}, {}}; });
            MediaTools media(io, "ffmpeg-missing", CaptureHost{Platform::linux_os, "", ""}, "x264");
            std::optional<long long> run_pid;
            AudioPipelineFactory factory(io, media, AudioSettings{"/nonexistent/ProcessLoopback.cs", "", "/tmp"}, [&](const std::string&) { return run_pid; });
            auto start = [&](const std::string& json) {
                auto app = parse_apps_config(Json::parse(json)).apps.at(0);
                auto pipeline = factory.make();
                std::optional<std::optional<StartError>> result;
                pipeline->start(app, 5006, RtpPipeline::Events{}, [&](std::optional<StartError> e) { result = std::move(e); });
                run_until(io, [&] { return result.has_value(); });
                return result->value_or(StartError{"(started)"}).message;
            };
            const std::string plain = R"({"apps":[{"id":"p","name":"P","run":{"cwd":".","cmd":"x"},"capture":{"type":"window"},"audio":{"source":"process-loopback"}}]})";
            check(start(R"({"apps":[{"id":"p","name":"P","run":{"cwd":".","cmd":"x"},"capture":{"type":"window"}}]})") == "app p has no audio config", "no audio config");
            check(start(plain) == "app p: 音声を取る対象のプロセスが動いていません (Run してから接続してください)", "no run process");
            run_pid = 77;
#ifdef _WIN32
            check(start(plain).find("ProcessLoopback.cs") != std::string::npos, "helper source missing");
#else
            check(start(plain) == "音声 (process-loopback) は Windows でだけ使えます", "Windows only");
#endif
            const auto bridge_json = [](unsigned short port) { return R"({"apps":[{"id":"b","name":"B","run":{"cwd":".","cmd":"x"},"inAppBridge":{"kind":"unity","port":)" + std::to_string(port) + R"(},"audio":{"source":"process-loopback"}}]})"; };
            check(start(bridge_json(without_pid.port())) == "app b: ブリッジの /health が pid を返しません (ブリッジが古い可能性があります)", "bridge without pid");
            check(start(bridge_json(with_pid.port())).find("pid") == std::string::npos, "bridge pid accepted (then the platform / helper decides)");
            check(with_pid.seen.at(0) == "GET /health", "pid read from /health");
        }},
        {"webrtc-broker > end to end: browser offer, answer, H.264 from the bridge reaches the browser, PLI reaches the bridge", [] {
            asio::io_context io;
            StreamingBridge bridge(io);
            auto app = parse_apps_config(Json::parse(R"({"apps":[{"id":"u","name":"U","run":{"cwd":".","cmd":"x"},"inAppBridge":{"kind":"unity","port":)" +
                std::to_string(bridge.port()) + R"(,"video":{"codec":"h264"}}}]})")).apps.at(0);
            RunnerSettings settings;
            settings.stun_urls = "";
            IceServerProvider ice(io, settings.stun_urls, "", "", epoch_ms);
            MediaTools media(io, "ffmpeg-not-used", CaptureHost{Platform::linux_os, ":0", ""}, "x264");
            auto broker = make_webrtc_broker(io, ice, media, settings, [](const std::string&) { return std::optional<long long>(); });
            std::thread worker([&] { auto guard = asio::make_work_guard(io); io.run_for(std::chrono::seconds(20)); });

            rtc::Configuration config;
            auto browser = std::make_shared<rtc::PeerConnection>(config);
            rtc::Description::Video media_line("0", rtc::Description::Direction::RecvOnly);
            media_line.addH264Codec(102, std::string("level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f"));
            auto track = browser->addTrack(media_line);
            std::atomic<int> rtp{0};
            track->onMessage([&](rtc::message_variant data) {
                if (auto* b = std::get_if<rtc::binary>(&data); b && !is_rtcp(std::string_view(reinterpret_cast<const char*>(b->data()), b->size()))) ++rtp;
            });
            std::promise<void> gathered;
            browser->onGatheringStateChange([&](rtc::PeerConnection::GatheringState s) { if (s == rtc::PeerConnection::GatheringState::Complete) gathered.set_value(); });
            browser->setLocalDescription(rtc::Description::Type::Offer);
            check(gathered.get_future().wait_for(std::chrono::seconds(10)) == std::future_status::ready, "offer gathered");
            const auto offer = std::string(*browser->localDescription());

            std::promise<std::variant<RtcAnswer, std::string>> answered;
            asio::post(io, [&] { broker->create_session(app, offer, [&](std::variant<RtcAnswer, std::string> r) { answered.set_value(std::move(r)); }); });
            auto future = answered.get_future();
            check(future.wait_for(std::chrono::seconds(10)) == std::future_status::ready, "answered");
            auto result = future.get();
            if (auto* error = std::get_if<std::string>(&result)) throw std::runtime_error("session failed: " + *error);
            const auto& answer = std::get<RtcAnswer>(result);
            check(answer.type == "answer" && answer.sdp.find("H264/90000") != std::string::npos && answer.sdp.find("a=sendonly") != std::string::npos, "send-only H.264 answer");
            browser->setRemoteDescription(rtc::Description(answer.sdp, rtc::Description::Type::Answer));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (rtp < 20 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(rtp >= 20, "RTP received by the browser: " + std::to_string(rtp.load()));
            check(bridge.last_target.find("codec=h264") != std::string::npos, "bridge asked for H.264: " + bridge.last_target);
            const int before = bridge.keyframe_requests;
            std::uint32_t ssrc = 0;
            const auto at = answer.sdp.find("a=ssrc:");
            if (at != std::string::npos) ssrc = static_cast<std::uint32_t>(std::stoul(answer.sdp.substr(at + 7)));
            std::this_thread::sleep_for(std::chrono::milliseconds(600));  // past the 500 ms coalescing window
            const auto request = pli(ssrc);
            track->send(reinterpret_cast<const std::byte*>(request.data()), request.size());
            const auto pli_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (bridge.keyframe_requests <= before && std::chrono::steady_clock::now() < pli_deadline) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(bridge.keyframe_requests > before, "browser PLI became POST /stream/keyframe");
            std::promise<Json> diag;
            asio::post(io, [&] { diag.set_value(broker->diagnostics()); });
            const auto diagnostics = diag.get_future().get();
            check(diagnostics.size() == 1 && diagnostics[0]["rtpReceived"].get<long long>() > 0, "diagnostics");
            std::promise<bool> closed;
            asio::post(io, [&] { closed.set_value(broker->close_session(answer.session_id)); });
            check(closed.get_future().get(), "closed");
            browser->close();
            io.stop();
            worker.join();
        }},
        {"webrtc-broker > end to end: raw BGRA from an ergo bridge is encoded by ffmpeg (libx264) and reaches the browser", [] {
            ProcessSpec probe{"ffmpeg", {"-hide_banner", "-encoders"}, {}, {}, false, true, true, false};
            asio::io_context probe_io;
            std::optional<CollectResult> probed;
            run_collect(probe_io, probe, std::chrono::seconds(10), 1 << 20, 64, std::nullopt, [&](CollectResult r) { probed = std::move(r); });
            run_until(probe_io, [&] { return probed.has_value(); }, std::chrono::seconds(10));
            if (!probed || std::holds_alternative<SpawnError>(*probed) || std::get<CollectedOutput>(*probed).stdout_bytes.find("libx264") == std::string::npos) {
                std::cout << "     (skipped: ffmpeg with libx264 is not installed)\n";
                return;
            }
            asio::io_context io;
            RawBridge bridge(io);
            auto app = parse_apps_config(Json::parse(R"({"apps":[{"id":"e","name":"E","run":{"cwd":".","cmd":"x"},"inAppBridge":{"kind":"ergo","port":)" +
                std::to_string(bridge.port()) + R"(,"fps":10}}]})")).apps.at(0);
            RunnerSettings settings;
            settings.stun_urls = "";
            IceServerProvider ice(io, settings.stun_urls, "", "", epoch_ms);
            MediaTools media(io, "ffmpeg", CaptureHost{Platform::linux_os, ":0", ""}, "x264");
            auto broker = make_webrtc_broker(io, ice, media, settings, [](const std::string&) { return std::optional<long long>(); });
            std::thread worker([&] { auto guard = asio::make_work_guard(io); io.run_for(std::chrono::seconds(30)); });
            Browser browser;
            std::promise<std::variant<RtcAnswer, std::string>> answered;
            asio::post(io, [&] { broker->create_session(app, browser.offer, [&](std::variant<RtcAnswer, std::string> r) { answered.set_value(std::move(r)); }); });
            auto future = answered.get_future();
            check(future.wait_for(std::chrono::seconds(15)) == std::future_status::ready, "answered");
            auto result = future.get();
            if (auto* error = std::get_if<std::string>(&result)) throw std::runtime_error("session failed: " + *error);
            browser.pc->setRemoteDescription(rtc::Description(std::get<RtcAnswer>(result).sdp, rtc::Description::Type::Answer));
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            while (browser.rtp < 5 && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(50));
            check(browser.rtp >= 5, "RTP from ffmpeg received by the browser: " + std::to_string(browser.rtp.load()));
            std::promise<void> done;
            asio::post(io, [&] { broker->shutdown(); done.set_value(); });
            done.get_future().wait();
            browser.pc->close();
            io.stop();
            worker.join();
        }},
    });
}
