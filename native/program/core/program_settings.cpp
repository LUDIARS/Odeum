#include "program_settings.hpp"
#include <stdexcept>

namespace odeum::program {
namespace {
void range(int value, int low, int high, const char* name) {
    if (value < low || value > high)
        throw std::invalid_argument(std::string(name) + " must be " + std::to_string(low) + ".." + std::to_string(high));
}

std::string profile_name(presenter::H264Profile profile) { return profile == presenter::H264Profile::main ? "main" : "high"; }

presenter::H264Profile profile_of(const std::string& name) {
    if (name == "main") return presenter::H264Profile::main;
    if (name == "high") return presenter::H264Profile::high;
    throw std::invalid_argument("video.profile must be main or high");
}

template<class T> void read(const Json& object, const char* key, T& target) {
    if (!object.contains(key)) return;
    try { target = object.at(key).get<T>(); }
    catch (const Json::exception&) { throw std::invalid_argument(std::string(key) + " has the wrong type"); }
}
}

void validate(const ProgramSettings& s) {
    range(s.video_bitrate_kbps, 6000, 10000, "video.bitrate_kbps");
    range(s.keyframe_interval_s, 1, 4, "video.keyframe_interval_s");
    if (s.profile == presenter::H264Profile::constrained_baseline) throw std::invalid_argument("video.profile must be main or high");
    range(s.b_frames, 0, 2, "video.b_frames");
    range(s.audio_bitrate_kbps, 64, 320, "audio.bitrate_kbps");
    range(s.return_bitrate_kbps, 300, 2500, "return.bitrate_kbps");
    range(s.return_audio_kbps, 16, 256, "return.audio_kbps");
    if (!(s.volume >= 0.0 && s.volume <= 2.0)) throw std::invalid_argument("volume must be 0..2");
    parse_rtmps_url(s.youtube_url);
}

ProgramSettings settings_from_json(const Json& json) {
    if (!json.is_object()) throw std::invalid_argument("Settings must be a JSON object");
    ProgramSettings s;
    if (json.contains("video")) {
        const auto& v = json.at("video");
        read(v, "bitrate_kbps", s.video_bitrate_kbps);
        read(v, "keyframe_interval_s", s.keyframe_interval_s);
        read(v, "b_frames", s.b_frames);
        std::string profile = profile_name(s.profile);
        read(v, "profile", profile);
        s.profile = profile_of(profile);
    }
    if (json.contains("audio")) read(json.at("audio"), "bitrate_kbps", s.audio_bitrate_kbps);
    if (json.contains("return")) {
        read(json.at("return"), "bitrate_kbps", s.return_bitrate_kbps);
        read(json.at("return"), "audio_kbps", s.return_audio_kbps);
    }
    if (json.contains("youtube")) read(json.at("youtube"), "url", s.youtube_url);
    read(json, "font", s.font);
    read(json, "volume", s.volume);
    if (json.contains("muted")) {
        const auto& muted = json.at("muted");
        if (!muted.is_array() || muted.size() != input_count) throw std::invalid_argument("muted must list 4 booleans");
        for (std::size_t i = 0; i < muted.size(); ++i) {
            if (!muted[i].is_boolean()) throw std::invalid_argument("muted must list 4 booleans");
            s.muted[i] = muted[i].get<bool>();
        }
    }
    validate(s);
    return s;
}

Json settings_to_json(const ProgramSettings& s) {
    return {
        {"video", {{"bitrate_kbps", s.video_bitrate_kbps}, {"keyframe_interval_s", s.keyframe_interval_s},
                   {"profile", profile_name(s.profile)}, {"b_frames", s.b_frames}}},
        {"audio", {{"bitrate_kbps", s.audio_bitrate_kbps}}},
        {"return", {{"bitrate_kbps", s.return_bitrate_kbps}, {"audio_kbps", s.return_audio_kbps}}},
        {"youtube", {{"url", s.youtube_url}}},
        {"font", s.font},
        {"volume", s.volume},
        {"muted", s.muted},
    };
}

RtmpEndpoint parse_rtmps_url(std::string_view url) {
    constexpr std::string_view scheme = "rtmps://";
    if (url.substr(0, scheme.size()) != scheme) throw std::invalid_argument("youtube.url must start with rtmps://");
    const auto rest = url.substr(scheme.size());
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos) throw std::invalid_argument("youtube.url needs an application path");
    const auto authority = rest.substr(0, slash);
    const auto app = rest.substr(slash + 1);
    for (char c : url) {
        if (static_cast<unsigned char>(c) <= ' ' || c == '?' || c == '#' || c == '@' || c == '\\')
            throw std::invalid_argument("youtube.url must not carry credentials, a query or spaces");
    }
    if (app.empty() || app.find('/') != std::string_view::npos) throw std::invalid_argument("youtube.url must end with a single application name");
    RtmpEndpoint endpoint;
    endpoint.port = 443;
    const auto colon = authority.rfind(':');
    endpoint.host = std::string(authority.substr(0, colon));
    if (colon != std::string_view::npos) {
        const auto digits = authority.substr(colon + 1);
        if (digits.empty() || digits.size() > 5 || digits.find_first_not_of("0123456789") != std::string_view::npos)
            throw std::invalid_argument("youtube.url has an invalid port");
        endpoint.port = std::stoi(std::string(digits));
        if (endpoint.port < 1 || endpoint.port > 65535) throw std::invalid_argument("youtube.url has an invalid port");
    }
    if (endpoint.host.empty()) throw std::invalid_argument("youtube.url has no host");
    endpoint.app = std::string(app);
    endpoint.tc_url = "rtmps://" + endpoint.host + ":" + std::to_string(endpoint.port) + "/" + endpoint.app;
    return endpoint;
}

void validate_stream_key(std::string_view key) {
    if (key.empty() || key.size() > 128) throw std::invalid_argument("The stream key must be 1..128 characters");
    for (char c : key) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) throw std::invalid_argument("The stream key may only contain letters, digits, '-' and '_'");
    }
}
}
