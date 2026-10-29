#pragma once
// Host keyboard / mouse injection for desktop apps without an in-app bridge. Replaces the optional
// @nut-tree-fork/nut-js dependency with the OS API: Win32 SendInput. Other platforms have no
// injector and fall back to logging the event, as the former runner did when nut-js was absent.
#include "../core/keys.hpp"
#include <memory>
#include <string>

namespace odeum::runner {
class DesktopInput {
public:
    virtual ~DesktopInput() = default;
    virtual bool available() const = 0;
    virtual bool key(const DesktopKey& key, bool down) = 0;
    // Moves to screen pixel (x, y) and clicks; "right" right-clicks, anything else left-clicks.
    virtual bool click(double x, double y, const std::string& button) = 0;
};
std::unique_ptr<DesktopInput> make_desktop_input();
}
