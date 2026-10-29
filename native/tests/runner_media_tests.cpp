// custos/tests/bridge-h264.test.ts, bridge-stream.test.ts (headers / ffmpeg arguments), ice-servers.test.ts,
// scale.test.ts, screenshot-stream.test.ts + golden (h264, ffmpeg, ice, keys, frame headers, bridge state).
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <app/screenshot_streamer.hpp>
#include <core/bridge_contract.hpp>
#include <core/ffmpeg_args.hpp>
#include <core/h264.hpp>
#include <core/ice.hpp>
#include <core/keys.hpp>
#include <core/video_feedback_policy.hpp>

using namespace odeum::runner;
using namespace runner_test;

namespace {
std::string from_hex(const std::string& hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}
std::string to_hex(std::string_view bytes) {
    static constexpr char d[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : bytes) { out.push_back(d[c >> 4]); out.push_back(d[c & 15]); }
    return out;
}
Json strings(const std::vector<std::string>& v) { return Json(v); }
Json ice_json(const std::vector<IceServer>& servers) { return to_json(servers); }

class FakeSource final : public ScreenshotSource {
public:
    explicit FakeSource(boost::asio::io_context& io) : io_(io) {}
    int calls = 0, failures = 0;
    std::vector<std::optional<long long>> widths;
    void capture(const AppConfig&, std::function<void(PngResult)> done) override {
        ++calls;
        const bool fail = failures-- > 0;
        boost::asio::post(io_, [done, fail] { done(fail ? PngResult(std::in_place_index<1>, "boom") : PngResult(std::in_place_index<0>, std::string("\x89PNG\r\n\x1a\n", 8))); });
    }
    void scale(std::string png, std::optional<long long> max_width, std::function<void(PngResult)> done) override {
        widths.push_back(max_width);
        done(PngResult(std::in_place_index<0>, std::move(png)));
    }
private:
    boost::asio::io_context& io_;
};

AppConfig app(const std::string& id, double interval, bool capture = true) {
    AppConfig a;
    a.id = id;
    a.name = id;
    if (capture) { a.capture = CaptureConfig{}; a.capture->type = "window"; a.capture->interval_sec = interval; }
    a.in_app_bridge = InAppBridgeConfig{};
    if (!capture) a.in_app_bridge.reset();
    return a;
}
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.media", {
        {"bridge-h264 > golden RTP packetization (single NAL, FU-A, marker, sequence wrap, timestamps)", [] {
            const auto g = fixture("h264_cases.json");
            const auto au = from_hex(g["accessUnit"]);
            H264RtpPacketizer p(96, 1200, 65534, 0x12345678);
            Json packets = Json::array();
            for (const auto& packet : p.packetize(au, 1'234'567'890)) packets.push_back(to_hex(packet));
            check(packets == g["packets"], "packets");
            Json small = Json::array();
            H264RtpPacketizer s(96, 1200, 7, 1);
            for (const auto& packet : s.packetize(from_hex("00000141010203"), -5000)) small.push_back(to_hex(packet));
            check(small == g["small"], "negative timestamp " + small.dump() + " vs " + g["small"].dump());
            Json nals = Json::array();
            for (auto n : split_annex_b(au)) nals.push_back(to_hex(n));
            check(nals == g["nals"], "split");
            for (const auto& packet : p.packetize(au, 0)) check(packet.size() <= rtp_mtu, "MTU 1200");
        }},
        {"bridge-h264 > golden frame reader across arbitrary chunk boundaries; keepalives skipped", [] {
            const auto g = fixture("h264_cases.json");
            const auto stream = from_hex(g["frameStream"]);
            BridgeH264FrameReader reader;
            const std::vector<std::size_t> cuts{0, 7, 20, stream.size()};
            for (std::size_t i = 0; i + 1 < cuts.size(); ++i) {
                Json got = Json::array();
                for (const auto& f : reader.push(std::string_view(stream).substr(cuts[i], cuts[i + 1] - cuts[i])))
                    got.push_back(Json{{"data", to_hex(f.data)}, {"timestampUs", std::to_string(f.timestamp_us)}, {"keyframe", f.keyframe}});
                check(got == g["frames"][i], "chunk " + std::to_string(i) + ": " + got.dump());
            }
        }},
        {"boundary > bridge frame length: 16 MiB accepted, 16 MiB + 1 rejected before buffering", [] {
            auto header = [](std::uint32_t length) {
                std::string h(13, '\0');
                for (int i = 0; i < 4; ++i) h[i] = static_cast<char>((length >> (24 - 8 * i)) & 0xff);
                return h;
            };
            BridgeH264FrameReader ok;
            check(ok.push(header(max_bridge_frame_bytes)).empty(), "exact limit waits for the body");
            BridgeH264FrameReader too_large;
            throws([&] { too_large.push(header(max_bridge_frame_bytes + 1)); }, "limit + 1");
            BridgeH264FrameReader broken;
            throws([&] { broken.push(header(0xffffffffu)); }, "corrupt length");
        }},
        {"video-feedback-policy > golden bitrate and key frame decisions", [] {
            VideoFeedbackPolicy policy({3'000'000, 300'000, 6'000'000});
            for (const auto& step : fixture("h264_cases.json")["policy"]) {
                const auto op = step["op"].get<std::string>();
                Json result;
                if (op == "estimate") { auto r = policy.update_estimate(step["id"], step["bps"].get<double>(), step["now"]); result = r ? Json(*r) : Json(nullptr); }
                else if (op == "remove") { auto r = policy.remove_session(step["id"], step["now"]); result = r ? Json(*r) : Json(nullptr); }
                else result = policy.should_request_keyframe(step["now"]);
                check(result == step["result"], "step " + step.dump() + " got " + result.dump());
            }
        }},
        {"bridge-stream > golden ffmpeg argument arrays (bridge rawvideo, audio, desktop capture)", [] {
            const auto g = fixture("ffmpeg_cases.json");
            for (const auto& c : g["bridgeStream"]) {
                const BridgeStreamMeta meta{c["meta"]["width"], c["meta"]["height"], c["meta"]["pixfmt"], c["meta"]["fps"]};
                check(strings(bridge_stream_args(meta, *forced_encoder(c["encoder"].get<std::string>()), c["bitrate"], c["rtpPort"], "127.0.0.1", 96)) == c["args"], "bridge " + c["encoder"].dump());
            }
            check(strings(bridge_stream_args({1280, 720, "bgra", 1}, Encoder::x264, 3'000'000, 6000, "127.0.0.1", 96)) == g["bridgeStreamFps1"], "GOP at least 2");
            check(strings(audio_args(5006)) == g["audio"], "audio");
            CaptureConfig capture;
            capture.type = "window";
            capture.window_title = "Game";
            capture.fps = 30;
            const CaptureHost linux_host{Platform::linux_os, ":0.0", "1:none"};
            check(strings(capture_rtp_args(capture, linux_host, Encoder::x264, 4'000'000, "127.0.0.1", 5008, 96)) == g["captureLinuxX264"], "linux capture");
            capture.fps = 60;
            check(strings(capture_rtp_args(capture, linux_host, Encoder::x264, 4'000'000, "127.0.0.1", 5010, 96)) == g["captureLinuxFps60"], "fps 60");
            for (const auto& c : g["pngWidths"]) {
                const auto w = read_png_width(from_hex(c["hex"]));
                check(w ? Json(*w) == c["width"] : c["width"].is_null(), "png width " + c.dump());
            }
        }},
        {"screenshot > ffmpeg arguments per platform (gdigrab title / desktop, x11grab, avfoundation)", [] {
            CaptureConfig c;
            c.type = "window";
            c.window_title = "My Game";
            const auto win = screenshot_args(c, {Platform::windows_os, "", ""});
            check(win.at(5) == "1" && win.at(7) == "title=My Game" && win.back() == "-", "window title is one argument (no shell)");
            c.window_title = "";
            check(screenshot_args(c, {Platform::windows_os, "", ""}).at(7) == "desktop", "desktop");
            check(screenshot_args(c, {Platform::linux_os, ":1", ""}).at(7) == ":1", "x11grab display");
            check(screenshot_args(c, {Platform::macos, "", "2:none"}).at(7) == "2:none", "avfoundation");
            throws([&] { screenshot_args(c, {Platform::other, "", ""}); }, "unsupported platform");
            check(scale_args(640).at(10) == "scale='min(640,iw)':-1:flags=fast_bilinear", "scale filter");
            check(forced_encoder("NVENC") == Encoder::nvenc && !forced_encoder("auto") && !forced_encoder(""), "encoder override");
        }},
        {"bridge-stream > golden frame headers and bridge state", [] {
            for (const auto& c : fixture("frame_header_cases.json")) {
                std::map<std::string, std::string> headers;
                for (const auto& [k, v] : c["headers"].items()) headers[k] = v;
                const auto meta = parse_frame_headers(headers, c["fallback"]);
                const Json actual = meta ? Json{{"width", meta->width}, {"height", meta->height}, {"pixfmt", meta->pixfmt}, {"fps", meta->fps}} : Json(nullptr);
                check_json(actual, c["output"], "headers " + c["headers"].dump());
            }
            std::map<std::string, std::string> huge{{"x-frame-width", "100000"}, {"x-frame-height", "720"}, {"x-frame-pixfmt", "bgra"}};
            check(!parse_frame_headers(huge, 24), "boundary: frame dimensions beyond 8K are refused");
            for (const auto& c : fixture("bridge_state_cases.json")) {
                const auto s = parse_bridge_state(c["body"]);
                check_json(Json{{"isPlaying", s.is_playing}, {"state", s.state ? *s.state : Json(nullptr)}}, c["output"], "state " + c["body"].dump());
            }
        }},
        {"ergo-custos-client > golden HID key table; Android and desktop tables", [] {
            for (const auto& c : fixture("key_cases.json")) {
                const auto code = hid_key_code(c["name"].get<std::string>());
                check(code ? Json(*code) == c["hid"] : c["hid"].is_null(), "hid " + c.dump());
            }
            check(android_key_code("w") == 51 && android_key_code("0") == 7 && android_key_code("Back") == 4 && !android_key_code("F1"), "android");
            check(desktop_key("w")->virtual_key == 'W' && desktop_key("Up")->extended && desktop_key("F24")->virtual_key == 0x87 && !desktop_key("1"), "desktop (nut-js names)");
        }},
        {"ice-servers > golden STUN env, TURN response and peer flattening", [] {
            const auto g = fixture("ice_cases.json");
            for (const auto& c : g["stun"]) {
                std::optional<std::string> value;
                if (!c["value"].is_null()) value = c["value"].get<std::string>();
                check_json(ice_json(stun_servers_from_env(value)), c["output"], "stun " + c.dump());
            }
            for (const auto& c : g["turn"]) check_json(ice_json(parse_turn_response(c["body"])), c["output"], "turn " + c["body"].dump());
            std::vector<IceServer> servers{{{"stun:a:1", "turn:b:2?transport=udp", "turn:c:3?transport=tcp", "turns:d:5349"}, "u", "c"}, {{"stun:e:1"}, std::nullopt, std::nullopt}};
            Json flat = Json::array();  // werift's shape: one URL string per entry
            for (const auto& f : flatten_for_peer(servers)) {
                Json e{{"urls", f.urls.at(0)}};
                if (f.username) e["username"] = *f.username;
                if (f.credential) e["credential"] = *f.credential;
                flat.push_back(e);
            }
            check_json(flat, g["flatten"], "flatten");
            check(encode_uri_component("a b/é") == "a%20b%2F%C3%A9", "encodeURIComponent");
        }},
        {"ScreenshotStreamer > refcount, interval, failures, env override and prefs", [] {
            boost::asio::io_context io;
            FakeSource source(io);
            RuntimeStreamPrefs prefs;
            long long now = 0;
            ScreenshotStreamer streamer(io, source, prefs, std::nullopt, [&] { return ++now; });
            std::vector<std::string> frames;
            streamer.on_frame([&](const std::string& id, long long, const std::string&) { frames.push_back(id); });
            check(!streamer.acquire(app("none", 1, false), "k1"), "no capture, no bridge");
            check(!streamer.acquire(app("zero", 0), "k1"), "intervalSec=0 disables");
            source.failures = 1;
            check(streamer.acquire(app("demo", 0.05), "s1") && streamer.acquire(app("demo", 0.05), "s2"), "acquired twice");
            check(streamer.active_streams() == 1, "one timer per app");
            run_until(io, [&] { return frames.size() >= 2; }, std::chrono::milliseconds(2000));
            check(frames.size() >= 2 && source.calls >= 3, "frames continue after a failure");
            streamer.release("demo", "s1");
            check(streamer.active_streams() == 1, "still one subscriber");
            streamer.release_all("s2");
            check(streamer.active_streams() == 0, "last release stops");
            ScreenshotStreamer env(io, source, prefs, 0.05, [&] { return ++now; });
            check(env.effective_interval_sec(app("demo", 99)) == 0.05, "env overrides config");
            StreamPrefsPatch patch;
            patch.interval_sec = 2.0;
            patch.max_width = 320LL;
            prefs.apply(patch);
            check(env.effective_interval_sec(app("demo", 99)) == 2.0, "prefs override env");
            env.acquire(app("demo", 99), "k");
            run_until(io, [&] { return !source.widths.empty() && source.widths.back() == 320; }, std::chrono::milliseconds(1000));
            check(source.widths.back() == 320, "maxWidth is read at each frame");
            StreamPrefsPatch off;
            off.interval_sec = 0.0;
            prefs.apply(off);
            check(env.active_streams() == 0, "intervalSec 0 at runtime stops the stream");
        }},
    });
}
