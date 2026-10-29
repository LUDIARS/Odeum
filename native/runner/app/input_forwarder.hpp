#pragma once
// Virtual key / click / pointer delivery (custos/src/input/forwarder.ts): Android through adb, apps
// with an in-app bridge through its /key and /pointer, other desktop apps through the OS injector.
// Bridge input is sent in order, one request at a time per app (queue of 256; beyond: dropped and
// logged). At most 8 adb processes run at once (beyond: dropped and logged).
#include "../adapters/desktop_input.hpp"
#include "../core/app_config.hpp"
#include <boost/asio/io_context.hpp>
#include <deque>
#include <functional>
#include <map>
#include <set>

namespace odeum::runner {
class InputForwarder {
public:
    InputForwarder(boost::asio::io_context& io, std::unique_ptr<DesktopInput> desktop, std::string adb);
    void send_key(const AppConfig& app, const std::string& key, bool down);
    void send_click(const AppConfig& app, double x, double y, const std::string& button);
    void send_pointer(const AppConfig& app, double x, double y, const std::string& button, const std::string& phase);
    // press, 50 ms, release (a virtual pad button)
    void tap_key(const AppConfig& app, const std::string& key);
private:
    using Job = std::function<void(std::function<void()> next)>;
    boost::asio::io_context& io_;
    std::unique_ptr<DesktopInput> desktop_;
    std::string adb_;
    std::map<std::string, std::deque<Job>> queues_;
    std::set<std::string> busy_;
    std::set<std::string> pointer_warned_;
    std::size_t adb_running_ = 0;
    void enqueue(const std::string& app_id, Job job);
    void drain(const std::string& app_id);
    void adb(const AppConfig& app, std::vector<std::string> args, std::string what);
    void warn_pointer_once(const std::string& app_id, const char* message);
};
double clamp01(double value);
}
