#pragma once
#include <functional>
#include <string>

namespace odeum::presenter::windows {
// Windows starts a new process for every odeum:// link. The first presenter owns a hidden
// message window; a later process hands its link over with WM_COPYDATA and exits, so a link
// opened while presenting replaces the ticket in the running app.
class SingleInstance {
public:
    // Called on the UI thread with the forwarded link (UTF-8).
    explicit SingleInstance(std::function<void(std::string)> received);
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    // True when a running presenter took the link (the caller then exits).
    static bool forward(const std::string& url);
private:
    std::function<void(std::string)> received_;
    void* window_ = nullptr;
};
}
