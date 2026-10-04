#include "presenter_app.hpp"
#include "overlay_view.hpp"
#include "panel_view.hpp"
#include "placement_mapping.hpp"
#include <chrono>

namespace odeum::presenter {
PresenterApp::PresenterApp(Platform& platform, PresenterOptions options, std::filesystem::path settings_path)
    : pending_launch_(std::move(options.launch_url)),
      controller_(queue_, platform, std::move(options.settings), std::move(settings_path),
                  PresenterController::Hooks{
                      [this](const OverlayPlacement& placement) {
                          if (place_overlay_) place_overlay_(to_tela(placement, overlay_width, overlay_height));
                      },
                      [this] { running_ = false; }}) {
    overlay_.theme(overlay_theme());
    panel_.theme(panel_theme());
}

tela::DesktopPlacement PresenterApp::overlay_placement() const {
    return to_tela(controller_.settings().overlay, overlay_width, overlay_height);
}

void PresenterApp::overlay_moved(const tela::DesktopPlacement& placement) { controller_.overlay_moved(from_tela(placement)); }

void PresenterApp::deliver_launch(std::string url) {
    queue_.post([this, url = std::move(url)] { controller_.accept_launch(url); });
}

void PresenterApp::frame(Millis now, std::int64_t epoch_s, bool panel_grip) {
    // The clock goes first so a launch link from the command line is checked against real time.
    controller_.tick(now, epoch_s);
    if (pending_launch_) {
        controller_.accept_launch(*pending_launch_);
        pending_launch_.reset();
    }
    queue_.drain();
    overlay_.document(overlay_document({controller_.overlay(), now, controller_.settings().comments_visible}));
    panel_.document(panel_document(controller_, panel_grip));
}

Millis PresenterApp::idle_wait() const { return controller_.overlay().animating() ? 33 : 100; }

Millis monotonic_now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::int64_t epoch_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
}
