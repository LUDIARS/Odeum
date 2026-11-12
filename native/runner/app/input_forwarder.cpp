#include "input_forwarder.hpp"
#include "../adapters/adb.hpp"
#include "../adapters/bridge_client.hpp"
#include "../adapters/log.hpp"
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <cmath>

namespace odeum::runner {
namespace {
constexpr std::size_t queue_limit = 256;
constexpr std::size_t adb_limit = 8;
BridgeEndpoint endpoint(const InAppBridgeConfig& bridge) { return {bridge.host, bridge.port}; }
}

double clamp01(double value) {
    if (!std::isfinite(value)) return 0;
    return value < 0 ? 0 : value > 1 ? 1 : value;
}

InputForwarder::InputForwarder(boost::asio::io_context& io, std::unique_ptr<DesktopInput> desktop, std::string adb)
    : io_(io), desktop_(std::move(desktop)), adb_(std::move(adb)) {}

void InputForwarder::enqueue(const std::string& app_id, Job job) {
    auto& queue = queues_[app_id];
    if (queue.size() >= queue_limit) {
        log::warn("input", "input queue full — event dropped", {{"appId", app_id}});
        return;
    }
    queue.push_back(std::move(job));
    if (!busy_.contains(app_id)) drain(app_id);
}

void InputForwarder::drain(const std::string& app_id) {
    auto& queue = queues_[app_id];
    if (queue.empty()) { busy_.erase(app_id); return; }
    busy_.insert(app_id);
    auto job = std::move(queue.front());
    queue.pop_front();
    job([this, app_id] { boost::asio::post(io_, [this, app_id] { drain(app_id); }); });
}

void InputForwarder::adb(const AppConfig& app, std::vector<std::string> args, std::string what) {
    if (adb_running_ >= adb_limit) {
        log::warn("android-input", "too many adb processes — event dropped", {{"appId", app.id}});
        return;
    }
    ++adb_running_;
    std::optional<std::string> serial;
    if (app.capture && app.capture->android_serial) serial = app.capture->android_serial;
    run_adb(io_, adb_, serial, std::move(args), [this, what](AdbResult result) {
        --adb_running_;
        if (!result.ok) log::warn("android-input", what + " failed", {{"stderr", result.stderr_text.substr(0, 500)}});
    });
}

void InputForwarder::send_key(const AppConfig& app, const std::string& key, bool down) {
    if (!app.input.allow_keyboard) {
        log::warn("input", "keyboard input disabled by app config", {{"appId", app.id}});
        return;
    }
    if (app.target == "android") {
        if (!down) return;  // keyevent is a press + release
        const auto code = android_key_code(key);
        if (!code) { log::warn("android-input", "no Android keycode mapping", {{"key", key}}); return; }
        adb(app, {"shell", "input", "keyevent", std::to_string(*code)}, "adb keyevent");
        return;
    }
    if (app.in_app_bridge) {
        const auto code = hid_key_code(key);
        if (!code) { log::warn("input", "unknown key name — not in HID KeyCode table", {{"key", key}}); return; }
        const auto ep = endpoint(*app.in_app_bridge);
        enqueue(app.id, [this, ep, code = *code, down, app_id = app.id, key](std::function<void()> next) {
            bridge_send_key(io_, ep, code, down, [next, app_id, key, down](std::optional<BridgeCallError> error) {
                if (error) log::warn("input", "in-app bridge key send failed", {{"appId", app_id}, {"key", key}, {"down", down}, {"msg", error->message}});
                next();
            });
        });
        return;
    }
    if (!desktop_->available()) {
        log::info("input", "[input:dev] key event (no injector)", {{"appId", app.id}, {"key", key}, {"down", down}});
        return;
    }
    const auto resolved = desktop_key(key);
    if (!resolved) { log::warn("input", "unknown key name — ignored", {{"key", key}}); return; }
    if (!desktop_->key(*resolved, down)) log::warn("input", "key injection failed", {{"appId", app.id}, {"key", key}});
}

void InputForwarder::tap_key(const AppConfig& app, const std::string& key) {
    send_key(app, key, true);
    auto timer = std::make_shared<boost::asio::steady_timer>(io_, std::chrono::milliseconds(50));
    timer->async_wait([this, timer, app, key](const boost::system::error_code& error) {
        if (!error) send_key(app, key, false);
    });
}

void InputForwarder::send_click(const AppConfig& app, double x, double y, const std::string& button) {
    if (!app.input.allow_mouse) {
        log::warn("input", "mouse input disabled by app config", {{"appId", app.id}});
        return;
    }
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    if (app.target == "android") {
        adb(app, {"shell", "input", "tap", std::to_string(std::llround(x)), std::to_string(std::llround(y))}, "adb tap");
        return;
    }
    if (!desktop_->available()) {
        log::info("input", "[input:dev] click (no injector)", {{"appId", app.id}, {"x", js_number(x)}, {"y", js_number(y)}, {"button", button}});
        return;
    }
    if (!desktop_->click(x, y, button)) log::warn("input", "click injection failed", {{"appId", app.id}});
}

void InputForwarder::warn_pointer_once(const std::string& app_id, const char* message) {
    if (pointer_warned_.insert(app_id).second) log::warn("input", message, {{"appId", app_id}});
}

void InputForwarder::send_pointer(const AppConfig& app, double x, double y, const std::string& button, const std::string& phase) {
    if (!app.input.allow_mouse) {
        log::warn("input", "mouse input disabled by app config", {{"appId", app.id}});
        return;
    }
    if (!app.in_app_bridge) {
        warn_pointer_once(app.id, "pointer needs inAppBridge — ignored");
        return;
    }
    const auto ep = endpoint(*app.in_app_bridge);
    enqueue(app.id, [this, ep, x = clamp01(x), y = clamp01(y), button, phase, app_id = app.id](std::function<void()> next) {
        bridge_send_pointer(io_, ep, x, y, button, phase, [this, next, app_id, phase](std::optional<BridgeCallError> error) {
            if (error && error->kind == BridgeCallError::Kind::unsupported) warn_pointer_once(app_id, "in-app bridge has no /pointer — ignored");
            else if (error) log::warn("input", "in-app bridge pointer send failed", {{"appId", app_id}, {"phase", phase}, {"msg", error->message}});
            next();
        });
    });
}
}
