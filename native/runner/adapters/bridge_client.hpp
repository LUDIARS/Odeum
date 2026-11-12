#pragma once
// HTTP calls to the in-app bridge of a target app (always the operator-configured host:port from
// apps.json; no caller-supplied address). Port of ergo-custos-client.ts and unity/bridge-client.ts.
#include "../core/bridge_contract.hpp"
#include "http_client.hpp"
#include <boost/asio/io_context.hpp>
#include <functional>

namespace odeum::runner {
struct BridgeEndpoint {
    std::string host;
    int port = 0;
    std::string base_url() const;
};

// In-app bridge (ergo / Unity runtime): screenshot, key, pointer, state.
struct BridgeCallError {
    enum class Kind { unsupported, failed } kind;  // unsupported: 404 from /pointer or /state
    std::string message;
};
void bridge_fetch_screenshot(boost::asio::io_context& io, const BridgeEndpoint& ep, std::function<void(std::variant<std::string, BridgeCallError>)> done);
void bridge_send_key(boost::asio::io_context& io, const BridgeEndpoint& ep, int code, bool down, std::function<void(std::optional<BridgeCallError>)> done);
// x / y: top-left origin 0..1; button: left|right|middle; phase: down|up|move.
void bridge_send_pointer(boost::asio::io_context& io, const BridgeEndpoint& ep, double x, double y, const std::string& button,
    const std::string& phase, std::function<void(std::optional<BridgeCallError>)> done);
void bridge_fetch_state(boost::asio::io_context& io, const BridgeEndpoint& ep, std::function<void(std::variant<BridgeState, BridgeCallError>)> done);
// H.264 stream control (POST /stream/keyframe, /stream/bitrate), 2 s each.
void bridge_post_control(boost::asio::io_context& io, const BridgeEndpoint& ep, const std::string& path, const std::string& body,
    std::function<void(std::optional<std::string>)> done);

// Unity editor routes (/health, /editor/*): JSON in, JSON out.
struct UnityBridgeError {
    enum class Kind { unreachable, http, invalid_json } kind;
    unsigned status = 0;   // http
    std::string message;   // unreachable: "Unity bridge unreachable at <base>: <cause>"
    std::string body;      // http: the response text
};
using UnityResult = std::variant<Json, UnityBridgeError>;

class UnityBridgeClient {
public:
    UnityBridgeClient(boost::asio::io_context& io, BridgeEndpoint endpoint, std::string token = {}, std::chrono::milliseconds timeout = std::chrono::milliseconds(10'000));
    const BridgeEndpoint& endpoint() const { return endpoint_; }
    void get(const std::string& target, std::function<void(UnityResult)> done);
    void post(const std::string& target, const Json& body, std::function<void(UnityResult)> done);
    void health(std::function<void(UnityResult)> done) { get("/health", std::move(done)); }
    void status(std::function<void(UnityResult)> done) { get("/editor/status", std::move(done)); }
    void compile_status(std::function<void(UnityResult)> done) { get("/editor/compile-status", std::move(done)); }
    void request_compile(std::function<void(UnityResult)> done) { post("/editor/compile", Json::object(), std::move(done)); }
    void refresh(std::function<void(UnityResult)> done) { post("/editor/refresh", Json::object(), std::move(done)); }
    void log(long long since, const std::string& level, long long limit, std::function<void(UnityResult)> done);
    void play(std::function<void(UnityResult)> done) { post("/editor/play", Json::object(), std::move(done)); }
    void stop(std::function<void(UnityResult)> done) { post("/editor/stop", Json::object(), std::move(done)); }
    void scene(std::function<void(UnityResult)> done) { get("/editor/scene", std::move(done)); }
    void hierarchy(long long depth, const std::string& root, std::function<void(UnityResult)> done);
    void publish_capture(const std::string& caption, const std::string& source, std::function<void(UnityResult)> done);
private:
    boost::asio::io_context& io_;
    BridgeEndpoint endpoint_;
    std::string token_;
    std::chrono::milliseconds timeout_;
    void request(const std::string& method, const std::string& target, std::optional<Json> body, std::function<void(UnityResult)> done);
};
}
