// odeum-program for macOS: the operator panel as a tela::MacOSDesktopOverlay drawn by Pictor
// (Tela has no AppKit window host). The programme has no window; it is composed in memory.
#import <AppKit/AppKit.h>
#include "macos_program_platform.hpp"
#include "../../app/program_app.hpp"
#include "../../app/program_panel.hpp"
#include "../../app/program_settings_file.hpp"
#include <tela/macos_desktop_overlay.hpp>
#include <iostream>

namespace {
using namespace odeum::program;

void show(const std::string& text) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Odeum 番組制作";
    alert.informativeText = [NSString stringWithUTF8String:text.c_str()] ?: @"";
    [alert runModal];
}

std::string choose_font(const ProgramSettings& settings, const std::optional<std::string>& option, macos::MacOSProgramPlatform& platform) {
    const auto configured = option ? *option : settings.font;
    if (!configured.empty()) {
        if (!std::filesystem::is_regular_file(configured)) throw std::runtime_error("Font not found: " + configured);
        return configured;
    }
    for (const auto& candidate : platform.font_candidates()) if (std::filesystem::is_regular_file(candidate)) return candidate.string();
    throw std::runtime_error("No TrueType font found; pass --font <file.ttf> (a .ttf with Japanese glyphs)");
}
}

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        try {
            macos::MacOSProgramPlatform platform;
            const auto options = parse_program_options({argv + 1, argv + argc});
            if (options.help) { std::cout << program_usage() << '\n'; return 0; }
            const auto settings_path = platform.settings_path();
            auto settings = load_settings(settings_path);
            const auto font = choose_font(settings, options.font, platform);
            ProgramApp app(platform, std::move(settings), settings_path, options.launch_url, font);
            [NSApp finishLaunching];
            tela::PictorSurface panel_renderer(font);
            tela::DesktopOverlayOptions surface;
            surface.placement.width = program_panel_width;
            surface.placement.height = program_panel_height;
            surface.placement.corner = tela::DesktopCorner::top_left;
            surface.grip = program_panel_grip;
            tela::MacOSDesktopOverlay panel(app.panel_runtime(), panel_renderer, std::move(surface));
            while (app.running()) {
                @autoreleasepool {
                    app.frame(monotonic_ms(), wall_seconds(), true);
                    panel.synchronize();
                    NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                        untilDate:[NSDate dateWithTimeIntervalSinceNow:static_cast<double>(app.idle_wait()) / 1000.0]
                        inMode:NSDefaultRunLoopMode dequeue:YES];
                    if (event) [NSApp sendEvent:event];
                }
            }
            panel.hide();
            return 0;
        } catch (const std::exception& error) {
            show(std::string("起動できませんでした: ") + error.what());
            return 2;
        }
    }
}
