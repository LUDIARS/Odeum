#pragma once
#include "plot.hpp"

namespace odeum::lesson {
// Courses compiled into the binary (native/lesson/courses).
std::vector<Plot> builtin_plots();
// A plot written by a lesson author. Throws ProtocolError("invalid_plot") or std::runtime_error (unreadable file).
Plot load_plot_file(const std::string& path);
}
