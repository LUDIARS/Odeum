#include "single_instance.hpp"
#include <windows.h>
#include <stdexcept>

namespace odeum::presenter::windows {
namespace {
constexpr const wchar_t* class_name = L"OdeumPresenterInstance";
constexpr ULONG_PTR launch_message = 0x4f444c4b; // 'ODLK'
constexpr std::size_t max_link_bytes = 8192;

LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lparam);
        auto* self = reinterpret_cast<std::function<void(std::string)>*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && data && data->dwData == launch_message && data->cbData <= max_link_bytes) {
            (*self)(std::string(static_cast<const char*>(data->lpData), data->cbData));
            return TRUE;
        }
        return FALSE;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}
}

SingleInstance::SingleInstance(std::function<void(std::string)> received) : received_(std::move(received)) {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = procedure;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = class_name;
    RegisterClassW(&window_class);
    auto window = CreateWindowExW(0, class_name, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, window_class.hInstance, nullptr);
    if (!window) throw std::runtime_error("Cannot create the presenter's instance window");
    SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&received_));
    window_ = window;
}

SingleInstance::~SingleInstance() {
    if (window_) DestroyWindow(static_cast<HWND>(window_));
}

bool SingleInstance::forward(const std::string& url) {
    if (url.size() > max_link_bytes) return false;
    const auto window = FindWindowExW(HWND_MESSAGE, nullptr, class_name, nullptr);
    if (!window) return false;
    COPYDATASTRUCT data{launch_message, static_cast<DWORD>(url.size()), const_cast<char*>(url.data())};
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data), SMTO_ABORTIFHUNG, 3000, &result) && result == TRUE;
}
}
