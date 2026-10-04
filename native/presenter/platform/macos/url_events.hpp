#pragma once
#include <functional>
#include <memory>
#include <string>

namespace odeum::presenter::macos {
// Receives odeum:// links through NSAppleEventManager's GetURL event (the URL scheme declared in
// Info.plist). Install before [NSApp finishLaunching] so the link that launched the app is not
// missed; LaunchServices delivers later links to the running app the same way.
class UrlEvents {
public:
    explicit UrlEvents(std::function<void(std::string)> received);
    ~UrlEvents();
    UrlEvents(const UrlEvents&) = delete;
    UrlEvents& operator=(const UrlEvents&) = delete;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
