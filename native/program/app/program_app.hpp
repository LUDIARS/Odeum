#pragma once
#include "program_controller.hpp"
#include "../render/layer_renderer.hpp"
#include <tela/runtime.hpp>

namespace odeum::program {
// The OS-independent half of odeum-program: the controller, the panel's Tela runtime, the
// programme's text layers and the per-frame step. A platform main owns the panel surface and
// the event loop, and calls frame() on every iteration.
class ProgramApp {
public:
    ProgramApp(ProgramPlatform& platform, ProgramSettings settings, std::filesystem::path settings_path,
               std::optional<std::string> launch_url, const std::string& font);
    ProgramApp(const ProgramApp&) = delete;
    ProgramApp& operator=(const ProgramApp&) = delete;

    tela::Runtime& panel_runtime() noexcept { return panel_; }
    presenter::EventQueue& queue() noexcept { return queue_; }
    // Any thread: a launch link from the OS (URL scheme event, second instance).
    void deliver_launch(std::string url);
    // Runs queued work, advances the controller, redraws the programme's text layers and
    // redeclares the panel.
    void frame(presenter::Millis now, std::int64_t epoch_s, bool panel_grip);
    // The reaction layer animates, so the loop keeps a short wait.
    presenter::Millis idle_wait() const noexcept { return 33; }
    bool running() const noexcept { return running_; }
private:
    presenter::EventQueue queue_;
    tela::Runtime panel_;
    bool running_ = true;
    std::optional<std::string> pending_launch_;
    LayerRenderer layers_;
    ProgramController controller_;
};

// Monotonic milliseconds and wall-clock seconds, the clocks frame() takes.
presenter::Millis monotonic_ms();
std::int64_t wall_seconds();
}
