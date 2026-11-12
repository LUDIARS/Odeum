#pragma once
// Key name tables. A single character is upper-cased first ("w" -> "W"), as before.
//  - HID usage codes for the in-app bridges (ergo-custos-client.ts KEY_TABLE; keep in sync with
//    unity/.../HidKeyMap.cs and ergo types.h)
//  - Android KeyEvent codes (input/android.ts)
//  - Windows virtual keys for the names the former nut-js Key enum accepted
#include <optional>
#include <string_view>

namespace odeum::runner {
std::optional<int> hid_key_code(std::string_view name);
std::optional<int> android_key_code(std::string_view name);

struct DesktopKey {
    int virtual_key;
    bool extended;  // KEYEVENTF_EXTENDEDKEY (arrows, navigation block, right modifiers, ...)
};
std::optional<DesktopKey> desktop_key(std::string_view name);
}
