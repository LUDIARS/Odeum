// odeum-presenter for macOS: two tela::MacOSDesktopOverlay surfaces drawn by Pictor, the
// reaction overlay and the control panel, both kept out of screen capture. The app runs as an
// accessory (no Dock icon) and receives odeum:// links through its URL scheme.
#import <AppKit/AppKit.h>
#include "macos_platform.hpp"
#include "url_events.hpp"
#include "../../app/overlay_view.hpp"
#include "../../app/panel_view.hpp"
#include "../../app/presenter_app.hpp"
#include "../../app/settings_file.hpp"
#include <tela/macos_desktop_overlay.hpp>
#include <iostream>
#include <optional>

namespace {
using namespace odeum::presenter;

std::vector<std::string> arguments(int argc, char** argv) { return {argv + 1, argv + argc}; }

void show(const std::string& text) {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Odeum 発表者";
    alert.informativeText = [NSString stringWithUTF8String:text.c_str()] ?: @"";
    [alert runModal];
}

int run(const PresenterOptions& options, macos::MacOSPlatform& platform, const std::filesystem::path& settings_path) {
    const auto font = resolve_font(options.settings, platform.font_candidates()).string();
    PresenterApp app(platform, options, settings_path);
    // Links arriving before the surfaces exist wait in the app's queue like any later one.
    macos::UrlEvents links([&app](std::string url) { app.deliver_launch(std::move(url)); });
    [NSApp finishLaunching];
    tela::PictorSurface overlay_renderer(font), panel_renderer(font);
    tela::DesktopOverlayOptions surface;
    surface.placement = app.overlay_placement();
    surface.grip = overlay_grip;
    surface.moved = [&app](const tela::DesktopPlacement& placement) { app.overlay_moved(placement); };
    surface.exclude_from_capture = true;
    tela::MacOSDesktopOverlay overlay(app.overlay_runtime(), overlay_renderer, std::move(surface));
    app.on_place_overlay([&overlay](const tela::DesktopPlacement& placement) { overlay.place(placement); });
    // Tela has no AppKit window host, so the panel is a second desktop surface with its own grip.
    tela::DesktopOverlayOptions panel_surface;
    panel_surface.placement.width = panel_width;
    panel_surface.placement.height = panel_height;
    panel_surface.placement.corner = tela::DesktopCorner::top_left;
    panel_surface.grip = panel_grip;
    panel_surface.exclude_from_capture = true;
    tela::MacOSDesktopOverlay panel(app.panel_runtime(), panel_renderer, std::move(panel_surface));
    while (app.running()) {
        @autoreleasepool {
            app.frame(monotonic_now(), epoch_seconds(), true);
            overlay.synchronize();
            panel.synchronize();
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                untilDate:[NSDate dateWithTimeIntervalSinceNow:static_cast<double>(app.idle_wait()) / 1000.0]
                inMode:NSDefaultRunLoopMode dequeue:YES];
            if (event) [NSApp sendEvent:event];
        }
    }
    overlay.hide();
    panel.hide();
    return 0;
}
}

int main(int argc, char** argv) {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        try {
            macos::MacOSPlatform platform;
            const auto settings_path = platform.settings_path();
            auto options = parse_options(arguments(argc, argv), load_settings(settings_path));
            switch (options.action) {
            case StartupAction::help: std::cout << usage(); return 0;
            case StartupAction::register_url_scheme:
            case StartupAction::unregister_url_scheme:
                // The bundle's Info.plist declares the scheme; LaunchServices registers it on install.
                std::cout << "On macOS the odeum:// scheme comes from the app bundle's Info.plist.\n";
                return 0;
            case StartupAction::run: break;
            }
            return run(options, platform, settings_path);
        } catch (const std::exception& error) {
            show(std::string("起動できませんでした: ") + error.what());
            return 2;
        }
    }
}
