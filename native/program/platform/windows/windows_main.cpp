// odeum-program for Windows: the operator panel (tela::WindowsView, drawn by Pictor). The
// programme itself has no window; it is composed in memory.
#include "windows_program_platform.hpp"
#include "../../app/program_app.hpp"
#include "../../app/program_panel.hpp"
#include "../../app/program_settings_file.hpp"
#include "platform/windows/windows_text.hpp"
#include <tela/pictor_surface.hpp>
#include <tela/windows_view.hpp>
#include <windows.h>
#include <shellapi.h>
#include <objbase.h>

namespace {
using namespace odeum::program;
namespace text = odeum::presenter::windows;

std::vector<std::string> arguments() {
    int count = 0;
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> result;
    for (int i = 1; i < count; ++i) result.push_back(text::narrow(argv[i]));
    LocalFree(argv);
    return result;
}

void show(const std::string& message, bool error) {
    MessageBoxW(nullptr, text::widen(message).c_str(), L"Odeum 番組制作", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

std::string choose_font(const ProgramSettings& settings, const std::optional<std::string>& option, windows::WindowsProgramPlatform& platform) {
    const auto configured = option ? *option : settings.font;
    if (!configured.empty()) {
        const std::filesystem::path font(std::u8string(configured.begin(), configured.end()));
        if (!std::filesystem::is_regular_file(font)) throw std::runtime_error("Font not found: " + configured);
        return font.string();
    }
    for (const auto& candidate : platform.font_candidates()) if (std::filesystem::is_regular_file(candidate)) return candidate.string();
    throw std::runtime_error("No TrueType font found; pass --font <file.ttf> (a .ttf with Japanese glyphs)");
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    try {
        // Media Foundation is used from the input, clock and publish threads.
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) throw std::runtime_error("Cannot initialise COM");
        windows::WindowsProgramPlatform platform;
        const auto options = parse_program_options(arguments());
        if (options.help) { show(program_usage(), false); return 0; }
        const auto settings_path = platform.settings_path();
        auto settings = load_settings(settings_path);
        const auto font = choose_font(settings, options.font, platform);
        ProgramApp app(platform, std::move(settings), settings_path, options.launch_url, font);
        tela::PictorSurface panel_renderer(font);
        tela::WindowsView panel(app.panel_runtime(), panel_renderer, "Odeum 番組制作", static_cast<int>(program_panel_width),
                                static_cast<int>(program_panel_height), false);
        while (app.running()) {
            if (!panel.pump()) break;
            app.frame(monotonic_ms(), wall_seconds(), false);
            panel.synchronize();
            MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(app.idle_wait()), QS_ALLINPUT);
        }
        return 0;
    } catch (const std::exception& error) {
        show(std::string("起動できませんでした: ") + error.what(), true);
        return 2;
    }
}
