#include "json.hpp"
#include <lorica/input_budget.hpp>
#include <cmath>

namespace odeum::runner {
Json parse_json(std::string_view text, const JsonLimits& limits) {
    lorica::input_budget budget({limits.bytes, limits.elements, limits.elements, limits.depth});
    if (!budget.consume(text.size(), 0, 1)) throw BudgetExceeded("json: input too large");
    auto charge = [&budget](int depth, Json::parse_event_t event, Json&) {
        using Event = Json::parse_event_t;
        if (event == Event::object_start || event == Event::array_start) {
            // depth counts the containers already open; the new one nests one deeper.
            if (!budget.permits_depth(static_cast<std::size_t>(depth) + 1)) throw BudgetExceeded("json: nesting too deep");
            if (!budget.consume(0, 1, 1)) throw BudgetExceeded("json: too many elements");
        } else if (event == Event::value || event == Event::key) {
            if (!budget.consume(0, 1, 1)) throw BudgetExceeded("json: too many elements");
        }
        return true;
    };
    return Json::parse(text.begin(), text.end(), charge);
}

std::optional<Json> try_parse_json(std::string_view text, const JsonLimits& limits) {
    try {
        return parse_json(text, limits);
    } catch (const BudgetExceeded&) {
        return std::nullopt;
    } catch (const Json::exception&) {
        return std::nullopt;
    }
}

std::string dump(const Json& value) {
    return value.dump(-1, ' ', false, Json::error_handler_t::replace);
}

Json js_number(double value) {
    if (std::isfinite(value) && std::floor(value) == value && std::fabs(value) < 9007199254740992.0) return static_cast<long long>(value);
    return value;
}

std::string sanitize_utf8(std::string_view bytes) {
    std::string out;
    out.reserve(bytes.size());
    const auto* p = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t n = bytes.size();
    static constexpr char replacement[] = "\xEF\xBF\xBD";
    std::size_t i = 0;
    while (i < n) {
        const unsigned c = p[i];
        std::size_t length = 0;
        unsigned lower = 0x80, upper = 0xBF;
        if (c < 0x80) { out.push_back(static_cast<char>(c)); ++i; continue; }
        if (c >= 0xC2 && c <= 0xDF) length = 2;
        else if (c >= 0xE0 && c <= 0xEF) { length = 3; if (c == 0xE0) lower = 0xA0; if (c == 0xED) upper = 0x9F; }
        else if (c >= 0xF0 && c <= 0xF4) { length = 4; if (c == 0xF0) lower = 0x90; if (c == 0xF4) upper = 0x8F; }
        if (length == 0) { out.append(replacement, 3); ++i; continue; }
        // WHATWG decoding: a maximal valid prefix of an ill-formed sequence becomes one U+FFFD.
        std::size_t j = 1;
        for (; j < length && i + j < n; ++j) {
            const unsigned next = p[i + j];
            const unsigned lo = j == 1 ? lower : 0x80, hi = j == 1 ? upper : 0xBF;
            if (next < lo || next > hi) break;
        }
        if (j == length) { out.append(bytes.substr(i, length)); i += length; }
        else { out.append(replacement, 3); i += j; }
    }
    return out;
}
}
