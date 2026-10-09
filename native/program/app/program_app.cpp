#include "program_app.hpp"
#include "program_panel.hpp"
#include <chrono>

namespace odeum::program {
ProgramApp::ProgramApp(ProgramPlatform& platform, ProgramSettings settings, std::filesystem::path settings_path,
                       std::optional<std::string> launch_url, const std::string& font)
    : pending_launch_(std::move(launch_url)), layers_(font, program_width, program_height),
      controller_(queue_, platform, std::move(settings), std::move(settings_path), ProgramController::Hooks{[this] { running_ = false; }}) {
    panel_.theme(program_panel_theme());
}

void ProgramApp::deliver_launch(std::string url) {
    queue_.post([this, url = std::move(url)] { controller_.accept_launch(url); });
}

void ProgramApp::frame(presenter::Millis now, std::int64_t epoch_s, bool panel_grip) {
    controller_.tick(now, epoch_s);
    if (pending_launch_) {
        controller_.accept_launch(*pending_launch_);
        pending_launch_.reset();
    }
    queue_.drain();
    const auto scene = layout(controller_.state().scene(), controller_.state().connections(), program_width, program_height);
    controller_.publish_layers({layers_.plates(scene), layers_.reactions(controller_.overlay(), now)});
    panel_.document(program_panel_document(controller_, panel_grip));
}

presenter::Millis monotonic_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t wall_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}
