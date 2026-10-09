#include "windows_shell.hpp"
#include "windows_text.hpp"
#include <windows.h>
#include <shlobj.h>
#include <stdexcept>

namespace odeum::presenter::windows {
namespace {
std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &path))) {
        CoTaskMemFree(path);
        throw std::runtime_error("Cannot locate the user's application data folder");
    }
    std::filesystem::path result(path);
    CoTaskMemFree(path);
    return result;
}
}

std::string clipboard_text() {
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(nullptr)) return {};
    std::string text;
    if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* wide = static_cast<const wchar_t*>(GlobalLock(data))) {
            // GlobalSize bounds the read in case the clipboard text lacks its terminator.
            const auto limit = GlobalSize(data) / sizeof(wchar_t);
            std::size_t length = 0;
            while (length < limit && wide[length]) ++length;
            text = narrow(std::wstring_view(wide, length));
            GlobalUnlock(data);
        }
    }
    CloseClipboard();
    return text;
}

std::filesystem::path roaming_app_data() { return known_folder(FOLDERID_RoamingAppData); }
std::filesystem::path local_app_data() { return known_folder(FOLDERID_LocalAppData); }

std::vector<std::filesystem::path> system_font_candidates() {
    std::vector<std::filesystem::path> fonts;
    wchar_t windows[MAX_PATH]{};
    if (GetWindowsDirectoryW(windows, MAX_PATH)) {
        const std::filesystem::path folder = std::filesystem::path(windows) / L"Fonts";
        for (const auto* name : {L"NotoSansJP-VF.ttf", L"NotoSansJP-Regular.ttf", L"BIZ-UDGothicR.ttf", L"segoeui.ttf"})
            fonts.push_back(folder / name);
    }
    return fonts;
}
}
