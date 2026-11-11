#include <odeum/json_limits.hpp>
#include <lorica/input_budget.hpp>

namespace odeum {
namespace {
[[noreturn]] void exceeded(const char* what) { throw ProtocolError("invalid_message", what); }
}
Json parse_json_within(std::string_view text, const JsonLimits& limits) {
    lorica::input_budget budget({limits.bytes, limits.elements, limits.elements + 1, limits.depth});
    if (!budget.consume(text.size(), 0, 1)) exceeded("JSON text exceeds its limit");
    auto charge = [&budget](int depth, Json::parse_event_t event, Json&) {
        using Event = Json::parse_event_t;
        if (event == Event::object_start || event == Event::array_start) {
            // depth counts the containers already open; the new one nests one deeper.
            if (!budget.permits_depth(static_cast<std::size_t>(depth) + 1)) exceeded("JSON nesting exceeds its limit");
            if (!budget.consume(0, 1, 1)) exceeded("JSON has too many values");
        } else if (event == Event::value || event == Event::key) {
            if (!budget.consume(0, 1, 1)) exceeded("JSON has too many values");
        }
        return true;
    };
    return Json::parse(text.begin(), text.end(), charge);
}
}
