// libFuzzer entry: a plot file (lesson/input_limits.cpp with plot_file_limits, then parse_plot and
// the timeline built from it), as odeum-lesson reads ODEUM_LESSON_PLOT and its CLI arguments.
#include "catalog.hpp"
#include "input_limits.hpp"
#include "model.hpp"
#include "timeline.hpp"
#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    using namespace odeum::lesson;
    static const auto model = make_provisional_model();
    try {
        const auto plot = parse_plot(parse_plot_text({reinterpret_cast<const char*>(data), size}));
        (void)build_timeline(plot, *model);
    } catch (const odeum::ProtocolError&) {
    }
    return 0;
}
