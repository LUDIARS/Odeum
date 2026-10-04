#include "url_scheme_registration.hpp"
#include <windows.h>
#include <stdexcept>
#include <string>

namespace odeum::presenter::windows {
namespace {
constexpr const wchar_t* root = L"Software\\Classes\\odeum";

void set_value(const std::wstring& key, const wchar_t* name, const std::wstring& value) {
    HKEY handle = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &handle, nullptr) != ERROR_SUCCESS)
        throw std::runtime_error("Cannot create the odeum:// registry key");
    const auto bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const auto result = RegSetValueExW(handle, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    RegCloseKey(handle);
    if (result != ERROR_SUCCESS) throw std::runtime_error("Cannot write the odeum:// registry value");
}
}

void register_url_scheme(const std::filesystem::path& executable) {
    const std::wstring key = root;
    const auto quoted = L"\"" + executable.wstring() + L"\"";
    set_value(key, nullptr, L"URL:Odeum Presenter");
    set_value(key, L"URL Protocol", L"");
    set_value(key + L"\\DefaultIcon", nullptr, quoted + L",0");
    // "%1" keeps the whole link as one argument even though it contains & and =.
    set_value(key + L"\\shell\\open\\command", nullptr, quoted + L" \"%1\"");
}

void unregister_url_scheme() {
    const auto result = RegDeleteTreeW(HKEY_CURRENT_USER, root);
    if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Cannot remove the odeum:// registry key");
}
}
