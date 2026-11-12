#pragma once
// The WebRTC side as the routes see it (implemented in native/runner/webrtc with libdatachannel).
#include "../core/app_config.hpp"
#include <functional>
#include <optional>
#include <string>
#include <variant>

namespace odeum::runner {
struct RtcAnswer {
    std::string session_id, sdp, type;
    std::optional<std::string> audio_error;  // audio could not be added; video still streams
};

class RtcService {
public:
    virtual ~RtcService() = default;
    // index 1: error message
    virtual void create_session(const AppConfig& app, std::string offer_sdp, std::function<void(std::variant<RtcAnswer, std::string>)> done) = 0;
    virtual bool close_session(const std::string& session_id) = 0;
    virtual void close_all_for_app(const std::string& app_id) = 0;
    virtual Json diagnostics() const = 0;
    virtual void shutdown() = 0;
};
}
