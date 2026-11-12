#include "playlog_row.hpp"
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <cstdio>
#include <stdexcept>

namespace odeum::runner {
Json build_row(const PlayLogRowBase& base, const std::string& kind, const Json& fields, const std::optional<Json>& state) {
    Json row{{"run", base.run}, {"player", base.player}, {"app", base.app}, {"seq", base.seq}, {"t_ms", base.t_ms}, {"at", base.at}, {"kind", kind}};
    if (fields.is_object()) for (const auto& [key, value] : fields.items()) row[key] = value;
    if (state && state->is_object()) for (const auto& [key, value] : state->items()) row["state." + key] = value;
    return row;
}

std::string input_name(const PlayInput& input) {
    if (const auto* k = std::get_if<KeyInput>(&input)) return k->down ? "key:" + k->key : "key:" + k->key + ":up";
    if (const auto* b = std::get_if<ButtonInput>(&input)) return "button:" + b->id;
    const auto& p = std::get<PointerInput>(input);
    if (p.phase == "move") return "pointer:move";
    return p.phase == "down" ? "pointer:" + p.button : "pointer:" + p.button + ":up";
}

Json input_fields(const PlayInput& input) {
    const auto name = input_name(input);
    if (const auto* k = std::get_if<KeyInput>(&input)) return Json{{"input", name}, {"device", "key"}, {"key", k->key}, {"down", k->down}};
    if (const auto* b = std::get_if<ButtonInput>(&input)) return Json{{"input", name}, {"device", "button"}, {"button", b->id}};
    const auto& p = std::get<PointerInput>(input);
    return Json{{"input", name}, {"device", "pointer"}, {"x", js_number(p.x)}, {"y", js_number(p.y)}, {"button", p.button}, {"phase", p.phase},
        {"pos", Json::array({js_number(p.x), js_number(p.y), 0})}};
}

namespace {
struct Civil { int year, month, day, hour, minute, second, millis; };
// Howard Hinnant's days-from-civil inverse; valid for the whole proleptic Gregorian range.
Civil civil(long long epoch_ms) {
    long long ms = epoch_ms % 1000;
    long long seconds = epoch_ms / 1000;
    if (ms < 0) { ms += 1000; --seconds; }
    long long days = seconds / 86400;
    long long rem = seconds % 86400;
    if (rem < 0) { rem += 86400; --days; }
    days += 719468;
    const long long era = (days >= 0 ? days : days - 146096) / 146097;
    const long long doe = days - era * 146097;
    const long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long y = yoe + era * 400;
    const long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const long long mp = (5 * doy + 2) / 153;
    const long long d = doy - (153 * mp + 2) / 5 + 1;
    const long long m = mp < 10 ? mp + 3 : mp - 9;
    if (m <= 2) ++y;
    return {static_cast<int>(y), static_cast<int>(m), static_cast<int>(d), static_cast<int>(rem / 3600), static_cast<int>((rem % 3600) / 60),
        static_cast<int>(rem % 60), static_cast<int>(ms)};
}
}

std::string make_run_id(long long epoch_ms, const std::function<std::uint32_t(std::uint32_t)>& pick) {
    const auto c = civil(epoch_ms);
    char head[32];
    std::snprintf(head, sizeof head, "%04d%02d%02d-%02d%02d%02d-", c.year % 10000, c.month, c.day, c.hour, c.minute, c.second);
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::string id(head);
    for (int i = 0; i < 6; ++i) id.push_back(alphabet[pick(36) % 36]);
    return id;
}

bool is_valid_run_id(std::string_view run) {
    if (run.size() != 22) return false;
    for (std::size_t i = 0; i < run.size(); ++i) {
        const char c = run[i];
        if (i < 8 || (i >= 9 && i < 15)) { if (c < '0' || c > '9') return false; }
        else if (i == 8 || i == 15) { if (c != '-') return false; }
        else if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

std::string iso_time(long long epoch_ms) {
    const auto c = civil(epoch_ms);
    char text[40];
    std::snprintf(text, sizeof text, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", c.year, c.month, c.day, c.hour, c.minute, c.second, c.millis);
    return text;
}

std::string hash_player(std::string_view salt, std::string_view user_id) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (!HMAC(EVP_sha256(), salt.data(), static_cast<int>(salt.size()), reinterpret_cast<const unsigned char*>(user_id.data()), user_id.size(), digest, &length)) {
        throw std::runtime_error("HMAC failed");
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (unsigned int i = 0; i < 8 && i < length; ++i) { out.push_back(hex[digest[i] >> 4]); out.push_back(hex[digest[i] & 15]); }
    return out;
}
}
