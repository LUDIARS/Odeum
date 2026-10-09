#pragma once
#include "../core/layout.hpp"
#include "../core/publish_supervisor.hpp"
#include "../transport/youtube_publisher.hpp"
#include <string>
#include <string_view>

namespace odeum::program {
// Japanese wording for the operator. Codes from Core, the relay and the ingest never reach the
// panel raw, and nothing here ever contains the stream key.
std::string scene_label(const Scene& scene);
std::string publish_status_label(const PublishSupervisor& supervisor, presenter::Millis now);
std::string publish_error_label(std::string_view code);
std::string publish_stats_label(const PublishStats& stats);
std::string producer_launch_error_label(std::string_view code);
std::string producer_relay_error_label(std::string_view code);
}
