// odeum-presenter for Windows: the desktop overlay (tela::WindowsDesktopOverlay, kept out of
// capture) and the control panel (tela::WindowsView), both drawn by Pictor.
#include "single_instance.hpp"
#include "url_scheme_registration.hpp"
#include "windows_platform.hpp"
#include "windows_text.hpp"
#include "../../app/overlay_view.hpp"
#include "../../app/panel_view.hpp"
#include "../../app/presenter_app.hpp"
#include "../../app/settings_file.hpp"
#include <tela/windows_desktop_overlay.hpp>
#include <tela/windows_view.hpp>
#include <windows.h>
#include <shellapi.h>
#include <roapi.h>

namespace {
using namespace odeum::presenter;

std::vector<std::string> arguments() {
    int count = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> result;
    for (int i = 1; i < count; ++i) result.push_back(windows::narrow(argv[i]));
    LocalFree(argv);
    return result;
}

std::filesystem::path executable() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length < path.size()) { path.resize(length); return path; }
        path.resize(path.size() * 2);
    }
}

void show(const std::string& text, bool error) {
    MessageBoxW(nullptr, windows::widen(text).c_str(), L"Odeum 発表者", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

int run(const PresenterOptions& options, windows::WindowsPlatform& platform, const std::filesystem::path& settings_path) {
    const auto font = resolve_font(options.settings, platform.font_candidates()).string();
    PresenterApp app(platform, options, settings_path);
    windows::SingleInstance instance([&app](std::string url) { app.deliver_launch(std::move(url)); });
    tela::PictorSurface overlay_renderer(font), panel_renderer(font);
    tela::DesktopOverlayOptions surface;
    surface.placement = app.overlay_placement();
    surface.grip = overlay_grip;
    surface.moved = [&app](const tela::DesktopPlacement& placement) { app.overlay_moved(placement); };
    // The audience must not see their own reactions echoed back in the stream.
    surface.exclude_from_capture = true;
    tela::WindowsDesktopOverlay overlay(app.overlay_runtime(), overlay_renderer, std::move(surface));
    app.on_place_overlay([&overlay](const tela::DesktopPlacement& placement) { overlay.place(placement); });
    tela::WindowsView panel(app.panel_runtime(), panel_renderer, "Odeum 発表者", static_cast<int>(panel_width),
                            static_cast<int>(panel_height), false);
    // The panel stays off the stream too; Windows Graphics Capture honours the affinity.
    if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(panel.native_window()), WDA_EXCLUDEFROMCAPTURE))
        throw std::runtime_error("Cannot keep the control panel out of screen capture (Windows 10 2004 or later is required)");
    while (app.running()) {
        if (!panel.pump()) break;
        app.frame(monotonic_now(), epoch_seconds(), false);
        overlay.synchronize();
        panel.synchronize();
        MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(app.idle_wait()), QS_ALLINPUT);
    }
    return 0;
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        // Windows Graphics Capture and Media Foundation are used from several threads.
        if (FAILED(RoInitialize(RO_INIT_MULTITHREADED))) throw std::runtime_error("Cannot initialise the Windows Runtime");
        windows::WindowsPlatform platform;
        const auto settings_path = platform.settings_path();
        auto options = parse_options(arguments(), load_settings(settings_path));
        switch (options.action) {
        case StartupAction::help: show(usage(), false); return 0;
        case StartupAction::register_url_scheme:
            windows::register_url_scheme(executable());
            show("odeum:// リンクをこのアプリで開くよう登録しました", false);
            return 0;
        case StartupAction::unregister_url_scheme:
            windows::unregister_url_scheme();
            show("odeum:// リンクの登録を外しました", false);
            return 0;
        case StartupAction::run: break;
        }
        if (options.launch_url && windows::SingleInstance::forward(*options.launch_url)) return 0;
        return run(options, platform, settings_path);
    } catch (const std::exception& error) {
        show(std::string("起動できませんでした: ") + error.what(), true);
        return 2;
    }
}
