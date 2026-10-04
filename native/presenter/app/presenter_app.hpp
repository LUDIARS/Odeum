#pragma once
#include "presenter_controller.hpp"
#include "../core/presenter_options.hpp"
#include <tela/runtime.hpp>
#include <tela/desktop_placement.hpp>
#include <functional>

namespace odeum::presenter {
// The OS-independent half of the program: the controller, the two Tela runtimes (overlay and
// panel) and the per-frame step. A platform main owns the surfaces bound to these runtimes and
// the event loop, and calls frame() on every iteration.
class PresenterApp {
public:
    PresenterApp(Platform& platform, PresenterOptions options, std::filesystem::path settings_path);
    PresenterApp(const PresenterApp&) = delete;
    PresenterApp& operator=(const PresenterApp&) = delete;

    tela::Runtime& overlay_runtime() noexcept { return overlay_; }
    tela::Runtime& panel_runtime() noexcept { return panel_; }
    PresenterController& controller() noexcept { return controller_; }
    EventQueue& queue() noexcept { return queue_; }
    // The overlay placement to start with (settings + command line).
    tela::DesktopPlacement overlay_placement() const;
    // Set by the platform: moves its overlay surface when a corner is chosen.
    void on_place_overlay(std::function<void(const tela::DesktopPlacement&)> place) { place_overlay_ = std::move(place); }
    // From the surface's DesktopOverlayOptions::moved.
    void overlay_moved(const tela::DesktopPlacement&);
    // Any thread: a launch link from the OS (URL scheme event, second instance).
    void deliver_launch(std::string url);

    // Runs queued work, advances the controller and redeclares both surfaces. `with_grip`
    // says whether the panel is a desktop surface that needs its own drag grip.
    void frame(Millis now, std::int64_t epoch_s, bool panel_grip);
    // How long the loop may sleep before the next frame: short while something moves.
    Millis idle_wait() const;
    bool running() const noexcept { return running_; }
private:
    EventQueue queue_;
    tela::Runtime overlay_, panel_;
    std::function<void(const tela::DesktopPlacement&)> place_overlay_;
    bool running_ = true;
    std::optional<std::string> pending_launch_;
    PresenterController controller_;
};

// Wall and monotonic clocks in the units the app uses.
Millis monotonic_now();
std::int64_t epoch_seconds();
}
