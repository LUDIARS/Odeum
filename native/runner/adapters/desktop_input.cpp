#include "desktop_input.hpp"
#include <cmath>
#ifdef _WIN32
#include <windows.h>
#endif

namespace odeum::runner {
namespace {
#ifdef _WIN32
class Win32Input final : public DesktopInput {
public:
    bool available() const override { return true; }
    bool key(const DesktopKey& key, bool down) override {
        INPUT input{};
        input.type = INPUT_KEYBOARD;
        input.ki.wVk = static_cast<WORD>(key.virtual_key);
        // Scan codes as well: games reading raw input / DirectInput ignore virtual-key-only events.
        input.ki.wScan = static_cast<WORD>(MapVirtualKeyW(static_cast<UINT>(key.virtual_key), MAPVK_VK_TO_VSC));
        input.ki.dwFlags = (input.ki.wScan ? KEYEVENTF_SCANCODE : 0) | (key.extended ? KEYEVENTF_EXTENDEDKEY : 0) | (down ? 0 : KEYEVENTF_KEYUP);
        return SendInput(1, &input, sizeof(INPUT)) == 1;
    }
    bool click(double x, double y, const std::string& button) override {
        if (!std::isfinite(x) || !std::isfinite(y)) return false;
        const int left = GetSystemMetrics(SM_XVIRTUALSCREEN), top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int width = GetSystemMetrics(SM_CXVIRTUALSCREEN), height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (width <= 1 || height <= 1) return false;
        auto normalise = [](double value, int origin, int extent) {
            const double v = std::round((value - origin) * 65535.0 / (extent - 1));
            return static_cast<LONG>(v < 0 ? 0 : v > 65535 ? 65535 : v);
        };
        INPUT inputs[3]{};
        inputs[0].type = INPUT_MOUSE;
        inputs[0].mi.dx = normalise(x, left, width);
        inputs[0].mi.dy = normalise(y, top, height);
        inputs[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        const bool right = button == "right";
        inputs[1].type = INPUT_MOUSE;
        inputs[1].mi.dwFlags = right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN;
        inputs[2].type = INPUT_MOUSE;
        inputs[2].mi.dwFlags = right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP;
        return SendInput(3, inputs, sizeof(INPUT)) == 3;
    }
};
#endif

class NoInput final : public DesktopInput {
public:
    bool available() const override { return false; }
    bool key(const DesktopKey&, bool) override { return false; }
    bool click(double, double, const std::string&) override { return false; }
};
}

std::unique_ptr<DesktopInput> make_desktop_input() {
#ifdef _WIN32
    return std::make_unique<Win32Input>();
#else
    return std::make_unique<NoInput>();
#endif
}
}
