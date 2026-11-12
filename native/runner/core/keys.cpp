#include "keys.hpp"
#include <array>
#include <string>

namespace odeum::runner {
namespace {
std::string normalise(std::string_view name) {
    std::string n(name);
    if (n.size() == 1 && n[0] >= 'a' && n[0] <= 'z') n[0] = static_cast<char>(n[0] - 'a' + 'A');
    return n;
}
struct Named { std::string_view name; int code; };
}

std::optional<int> hid_key_code(std::string_view name) {
    const auto n = normalise(name);
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return 4 + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '1' && n[0] <= '9') return 30 + (n[0] - '1');
    if (n == "0" || n == "Num0") return 39;
    if (n.size() == 4 && n.rfind("Num", 0) == 0 && n[3] >= '1' && n[3] <= '9') return 30 + (n[3] - '1');
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'F') {
        int f = 0;
        for (std::size_t i = 1; i < n.size(); ++i) { if (n[i] < '0' || n[i] > '9') { f = -1; break; } f = f * 10 + (n[i] - '0'); }
        if (f >= 1 && f <= 12 && !(n.size() == 3 && n[1] == '0')) return 58 + (f - 1);
    }
    static constexpr std::array<Named, 18> named{{
        {"Enter", 40}, {"Return", 40}, {"Escape", 41}, {"Esc", 41}, {"Backspace", 42}, {"Tab", 43}, {"Space", 44},
        {"Right", 79}, {"Left", 80}, {"Down", 81}, {"Up", 82},
        {"LCtrl", 224}, {"LShift", 225}, {"LAlt", 226}, {"LSuper", 227}, {"Ctrl", 224}, {"Shift", 225}, {"Alt", 226},
    }};
    for (const auto& entry : named) if (entry.name == n) return entry.code;
    return std::nullopt;
}

std::optional<int> android_key_code(std::string_view name) {
    const auto n = normalise(name);
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return 29 + (n[0] - 'A');
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return 7 + (n[0] - '0');
    static constexpr std::array<Named, 25> named{{
        {"Up", 19}, {"Down", 20}, {"Left", 21}, {"Right", 22}, {"DPadUp", 19}, {"DPadDown", 20}, {"DPadLeft", 21}, {"DPadRight", 22},
        {"Enter", 66}, {"Tab", 61}, {"Space", 62}, {"Backspace", 67}, {"Escape", 111},
        {"Back", 4}, {"Home", 3}, {"Menu", 82}, {"VolumeUp", 24}, {"VolumeDown", 25}, {"Power", 26}, {"Camera", 27},
        {"MediaPlay", 126}, {"MediaPause", 127}, {"MediaPlayPause", 85}, {"MediaNext", 87}, {"MediaPrev", 88}
    }};
    for (const auto& entry : named) if (entry.name == n) return entry.code;
    return std::nullopt;
}

std::optional<DesktopKey> desktop_key(std::string_view name) {
    const auto n = normalise(name);
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return DesktopKey{n[0], false};
    if (n.size() == 4 && n.rfind("Num", 0) == 0 && n[3] >= '0' && n[3] <= '9') return DesktopKey{n[3], false};
    if (n.size() == 7 && n.rfind("NumPad", 0) == 0 && n[6] >= '0' && n[6] <= '9') return DesktopKey{0x60 + (n[6] - '0'), false};
    if (n.size() >= 2 && n.size() <= 3 && n[0] == 'F') {
        int f = 0;
        for (std::size_t i = 1; i < n.size(); ++i) { if (n[i] < '0' || n[i] > '9') { f = -1; break; } f = f * 10 + (n[i] - '0'); }
        if (f >= 1 && f <= 24 && !(n.size() == 3 && n[1] == '0')) return DesktopKey{0x70 + (f - 1), false};
    }
    struct Vk { std::string_view name; int vk; bool extended; };
    static constexpr std::array<Vk, 52> named{{
        {"Escape", 0x1B, false}, {"Space", 0x20, false}, {"Enter", 0x0D, false}, {"Return", 0x0D, false}, {"Tab", 0x09, false},
        {"Backspace", 0x08, false}, {"Up", 0x26, true}, {"Down", 0x28, true}, {"Left", 0x25, true}, {"Right", 0x27, true},
        {"LeftShift", 0xA0, false}, {"RightShift", 0xA1, false}, {"LeftControl", 0xA2, false}, {"RightControl", 0xA3, true},
        {"LeftAlt", 0xA4, false}, {"RightAlt", 0xA5, true}, {"LeftSuper", 0x5B, true}, {"LeftWin", 0x5B, true}, {"LeftCmd", 0x5B, true},
        {"RightSuper", 0x5C, true}, {"RightWin", 0x5C, true}, {"RightCmd", 0x5C, true}, {"Menu", 0x5D, true},
        {"Insert", 0x2D, true}, {"Delete", 0x2E, true}, {"Home", 0x24, true}, {"End", 0x23, true}, {"PageUp", 0x21, true}, {"PageDown", 0x22, true},
        {"CapsLock", 0x14, false}, {"NumLock", 0x90, true}, {"ScrollLock", 0x91, false}, {"Pause", 0x13, false}, {"Print", 0x2C, true},
        {"Minus", 0xBD, false}, {"Equal", 0xBB, false}, {"LeftBracket", 0xDB, false}, {"RightBracket", 0xDD, false}, {"Backslash", 0xDC, false},
        {"Semicolon", 0xBA, false}, {"Quote", 0xDE, false}, {"Comma", 0xBC, false}, {"Period", 0xBE, false}, {"Slash", 0xBF, false}, {"Grave", 0xC0, false},
        {"Add", 0x6B, false}, {"Subtract", 0x6D, false}, {"Multiply", 0x6A, false}, {"Divide", 0x6F, true}, {"Decimal", 0x6E, false}, {"Clear", 0x0C, false},
        {"AudioMute", 0xAD, false},
    }};
    for (const auto& entry : named) if (entry.name == n) return DesktopKey{entry.vk, entry.extended};
    if (n == "AudioVolDown") return DesktopKey{0xAE, false};
    if (n == "AudioVolUp") return DesktopKey{0xAF, false};
    if (n == "AudioNext") return DesktopKey{0xB0, false};
    if (n == "AudioPrev") return DesktopKey{0xB1, false};
    if (n == "AudioStop") return DesktopKey{0xB2, false};
    if (n == "AudioPlay" || n == "AudioPause") return DesktopKey{0xB3, false};
    return std::nullopt;
}
}
