#include "bridge_contract.hpp"
#include <cctype>
#include <cmath>

namespace odeum::runner {
BridgeState parse_bridge_state(const Json& body) {
    BridgeState out;
    if (!body.is_object()) return out;
    if (auto it = body.find("isPlaying"); it != body.end() && it->is_boolean()) out.is_playing = it->get<bool>();
    if (auto it = body.find("state"); it != body.end() && it->is_object()) out.state = *it;
    return out;
}

namespace {
// parseInt(text, 10): optional whitespace and sign, then the leading digits.
std::optional<long long> parse_int(const std::string& text) {
    std::size_t i = 0;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    bool negative = false;
    if (i < text.size() && (text[i] == '+' || text[i] == '-')) negative = text[i++] == '-';
    long long value = 0;
    std::size_t digits = 0;
    while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && digits < 12) value = value * 10 + (text[i++] - '0'), ++digits;
    if (digits == 0) return std::nullopt;
    return negative ? -value : value;
}

std::string header(const std::map<std::string, std::string>& headers, const char* name) {
    auto it = headers.find(name);
    return it == headers.end() ? std::string() : it->second;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
}

std::optional<BridgeStreamMeta> parse_frame_headers(const std::map<std::string, std::string>& h, int fallback_fps) {
    const auto width = parse_int(header(h, "x-frame-width"));
    const auto height = parse_int(header(h, "x-frame-height"));
    const auto fps = parse_int(header(h, "x-frame-fps"));
    const auto pixfmt = trim(header(h, "x-frame-pixfmt"));
    // Bounded so the frame size (width * height * 4) cannot overflow or exceed 8K.
    if (!width || *width <= 0 || *width > 7680) return std::nullopt;
    if (!height || *height <= 0 || *height > 4320) return std::nullopt;
    if (pixfmt.empty()) return std::nullopt;
    return BridgeStreamMeta{static_cast<int>(*width), static_cast<int>(*height), pixfmt, fps && *fps > 0 && *fps <= 240 ? static_cast<int>(*fps) : fallback_fps};
}

bool looks_like_png(std::string_view bytes) {
    return bytes.size() >= 8 && static_cast<unsigned char>(bytes[0]) == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G';
}

std::optional<BridgeTarget> resolve_bridge_target(const std::vector<AppConfig>& apps, const std::string& app_id) {
    if (app_id.empty()) return std::nullopt;
    for (const auto& app : apps) {
        if (app.id != app_id) continue;
        if (!app.in_app_bridge || app.in_app_bridge->kind != "unity") return std::nullopt;
        if (app.in_app_bridge->port <= 0) return std::nullopt;
        return BridgeTarget{app.in_app_bridge->host.empty() ? "127.0.0.1" : app.in_app_bridge->host, app.in_app_bridge->port};
    }
    return std::nullopt;
}

std::optional<int> parse_bridge_port_file(std::string_view contents) {
    const auto text = trim(std::string(contents));
    if (text.empty()) return std::nullopt;
    // Number(text): decimal digits only for a port; anything else is not an integer port.
    for (char c : text) if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
    if (text.size() > 5) return std::nullopt;
    const int port = std::stoi(text);
    if (port <= 0 || port > 65535) return std::nullopt;
    return port;
}

CompileWaitPhase next_compile_wait_phase(const CompileWaitState& state, bool reachable, bool is_compiling) {
    if (reachable) return is_compiling ? CompileWaitPhase::waiting : CompileWaitPhase::done;
    if (!state.ever_reachable) return CompileWaitPhase::unreachable;
    return state.unreachable_ms >= state.unreachable_budget_ms ? CompileWaitPhase::unreachable : CompileWaitPhase::waiting;
}

std::string form_encode(const std::vector<std::pair<std::string, std::string>>& pairs) {
    static constexpr char hex[] = "0123456789ABCDEF";
    auto encode = [](const std::string& text) {
        std::string out;
        for (unsigned char c : text) {
            if (std::isalnum(c) || c == '*' || c == '-' || c == '.' || c == '_') out.push_back(static_cast<char>(c));
            else if (c == ' ') out.push_back('+');
            else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
        }
        return out;
    };
    std::string out;
    for (const auto& [name, value] : pairs) {
        if (!out.empty()) out.push_back('&');
        out += encode(name) + "=" + encode(value);
    }
    return out;
}
}
