#include "check.hpp"
#include "core/producer_routing.hpp"
#include "core/program_settings.hpp"
#include "core/publish_supervisor.hpp"
using namespace odeum::program;

namespace {
bool refused(ProgramSettings s) {
    try { validate(s); } catch (const std::invalid_argument&) { return true; }
    return false;
}

template<class Field> void bounds(Field field, int low, int high, const char* description) {
    ProgramSettings s;
    field(s) = low; check(!refused(s), description);
    field(s) = high; check(!refused(s), description);
    field(s) = low - 1; check(refused(s), description);
    field(s) = high + 1; check(refused(s), description);
}

void supervisor() {
    PublishSupervisor publish;
    check(publish.status() == PublishStatus::stopped && publish.failed(0) == 0, "Failures while stopped schedule nothing");
    publish.start();
    check(publish.status() == PublishStatus::connecting && publish.headers_pending(), "Start connects at once");
    Millis now = 0;
    const Millis expected[] = {1000, 2000, 4000, 8000, 16000, 30000, 30000};
    for (auto delay : expected) {
        check(publish.failed(now) == delay, "Backoff from 1 s doubling to 30 s");
        check(!publish.due(now + delay - 1) && publish.due(now + delay), "Retry time");
        now += delay;
        publish.retrying();
    }
    check(publish.reconnects() == 7, "Reconnections counted");
    publish.live();
    publish.headers_sent();
    check(publish.status() == PublishStatus::live && !publish.headers_pending(), "Live");
    check(publish.failed(now) == 1000 && publish.headers_pending(), "A publish that started resets the backoff and resends headers");
    publish.stop();
    check(!publish.due(now + 60000) && publish.status() == PublishStatus::stopped, "Stop cancels the retry");
}

void settings() {
    ProgramSettings defaults;
    check(!refused(defaults) && defaults.video_bitrate_kbps == 8000 && defaults.keyframe_interval_s == 2 && defaults.b_frames == 2 &&
          defaults.audio_bitrate_kbps == 128 && defaults.return_bitrate_kbps == 1200, "Design defaults");
    bounds([](ProgramSettings& s) -> int& { return s.video_bitrate_kbps; }, 6000, 10000, "Programme bitrate 6..10 Mbps");
    bounds([](ProgramSettings& s) -> int& { return s.keyframe_interval_s; }, 1, 4, "Keyframe interval 1..4 s");
    bounds([](ProgramSettings& s) -> int& { return s.b_frames; }, 0, 2, "B-frames 0..2");
    bounds([](ProgramSettings& s) -> int& { return s.audio_bitrate_kbps; }, 64, 320, "AAC bitrate");
    bounds([](ProgramSettings& s) -> int& { return s.return_bitrate_kbps; }, 300, 2500, "Return bitrate");
    bounds([](ProgramSettings& s) -> int& { return s.return_audio_kbps; }, 16, 256, "Return audio bitrate");
    ProgramSettings s;
    s.volume = 2.0; check(!refused(s), "Volume 2");
    s.volume = 2.01; check(refused(s), "Volume above 2");
    s.volume = -0.01; check(refused(s), "Negative volume");
    s = {};
    s.profile = odeum::presenter::H264Profile::constrained_baseline;
    check(refused(s), "The programme is Main or High");

    s = {};
    s.video_bitrate_kbps = 9000;
    s.profile = odeum::presenter::H264Profile::main;
    s.muted = {false, true, false, true};
    s.font = "C:/fonts/a.ttf";
    check(settings_from_json(settings_to_json(s)) == s, "JSON round trip");
    check(settings_from_json(odeum::Json::object()) == ProgramSettings{}, "Empty file gives defaults");
    bool threw = false;
    try { settings_from_json({{"video", {{"profile", "baseline"}}}}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Unknown profile refused");
    threw = false;
    try { settings_from_json({{"video", {{"bitrate_kbps", "fast"}}}}); } catch (const std::invalid_argument&) { threw = true; }
    check(threw, "Wrong type refused");

    const auto youtube = parse_rtmps_url("rtmps://a.rtmps.youtube.com:443/live2");
    check(youtube == RtmpEndpoint{"a.rtmps.youtube.com", 443, "live2", "rtmps://a.rtmps.youtube.com:443/live2"}, "YouTube ingest");
    check(parse_rtmps_url("rtmps://ingest.example/app").port == 443, "Default port 443");
    for (const char* bad : {"rtmp://a.rtmps.youtube.com/live2", "rtmps://a.rtmps.youtube.com:443/", "rtmps://user:pw@host/live2",
                            "rtmps://host/live2?key=x", "rtmps://host:0/live2", "rtmps://host:99999/live2", "rtmps://host/a/b", "rtmps:///live2"}) {
        threw = false;
        try { parse_rtmps_url(bad); } catch (const std::invalid_argument&) { threw = true; }
        check(threw, "Invalid ingest URL refused");
    }

    validate_stream_key("abcd-efgh-1234-5678-9xyz");
    for (std::string bad : {std::string(), std::string(129, 'a'), std::string("abc def"), std::string("secret/key")}) {
        try { validate_stream_key(bad); throw std::runtime_error("Expected the key to be refused"); }
        catch (const std::invalid_argument& error) {
            check(bad.size() < 3 || std::string(error.what()).find(bad) == std::string::npos, "The error never repeats the key");
        }
    }
}

void routing() {
    check(input_of_mid("input1-0") == 0 && input_of_mid("input4-12") == 3, "Relay mids map to inputs");
    check(!input_of_mid("program-1") && !input_of_mid("input5-1") && !input_of_mid("video") && !input_of_mid("input2-") &&
          !input_of_mid("input2-x"), "Other mids are not inputs");
    using odeum::Message;
    using odeum::MessageType;
    check(side_of(Message{MessageType::sdp, {{"type", "sdp"}, {"sdp", {{"type", "offer"}, {"sdp", "v=0"}}}}}) == PeerSide::receiver, "Relay offer");
    check(side_of(Message{MessageType::sdp, {{"type", "sdp"}, {"sdp", {{"type", "answer"}, {"sdp", "v=0"}}}}}) == PeerSide::sender, "Relay answer");
    check(side_of(Message{MessageType::candidate, {{"type", "candidate"}, {"candidate", "c"}, {"mid", "input3-2"}}}) == PeerSide::receiver,
          "Candidate for a relay mid");
    check(side_of(Message{MessageType::candidate, {{"type", "candidate"}, {"candidate", "c"}, {"mid", "video"}}}) == PeerSide::sender,
          "Candidate for the programme");
    const auto live = presence_inputs({{"slots", {{"input1", true}, {"input2", false}, {"input4", true}, {"program", true}}}});
    check(live[0] && !live[1] && !live[2] && live[3], "Presence slots");
    check(!presence_inputs({{"presenter_connected", true}})[0], "Presence without slots");
}
}

int main() { return run([] { supervisor(); settings(); routing(); }); }
