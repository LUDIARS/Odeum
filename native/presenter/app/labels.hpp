#pragma once
#include "../core/capture_source.hpp"
#include "../core/connection_monitor.hpp"
#include "../transport/media_sender.hpp"
#include <string>
#include <string_view>

namespace odeum::presenter {
// Japanese wording shown to the presenter. Kept in one place so codes from Core, the relay and
// the OS adapters never reach the screen raw.
std::string stamp_label(std::string_view kind);
std::string link_status_label(const ConnectionMonitor&, Millis now);
std::string media_state_label(MediaState);
std::string relay_error_label(std::string_view code);
std::string launch_error_label(std::string_view code);
std::string poll_error_label(std::string_view code);
std::string permission_label(CapturePermission);
std::string capture_kind_label(CaptureKind);
// 1234567 -> "1,234,567"
std::string grouped(std::int64_t value);
}
