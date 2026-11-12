#include "auth_policy.hpp"
#include <cmath>

namespace odeum::runner {
AuthMode auth_mode(const AuthSettings& s) {
    if (s.open) return AuthMode::open;
    if (!s.cernere_url.empty()) return AuthMode::cernere;
    return s.auth_required ? AuthMode::stub : AuthMode::anonymous;
}

std::string describe(const AuthSettings& s) {
    switch (auth_mode(s)) {
        case AuthMode::open: return "open (CUSTOS_OPEN=1)";
        case AuthMode::cernere: return "cernere (" + s.cernere_url + ")";
        case AuthMode::stub: return "stub (token required)";
        case AuthMode::anonymous: return "anonymous (no auth)";
    }
    return "anonymous (no auth)";
}

bool is_placeholder_user(const std::string& id) {
    return id == "anon" || id == "dev-anon" || id == "stub";
}

AuthDecision decide(const AuthSettings& s, const std::string& token) {
    using Kind = AuthDecision::Kind;
    switch (auth_mode(s)) {
        case AuthMode::open: return {Kind::accept, VerifiedUser{"dev-anon", "dev", "dev@local", "general"}};
        case AuthMode::anonymous: return {Kind::accept, VerifiedUser{"anon", "anon", "anon@local", "general"}};
        case AuthMode::stub:
            if (token.empty()) return {Kind::reject, std::nullopt};
            return {Kind::accept, VerifiedUser{"stub", "stub", "stub@local", "general"}};
        case AuthMode::cernere:
            if (token.empty()) return {Kind::reject, std::nullopt};
            return {Kind::ask_cernere, std::nullopt};
    }
    return {Kind::reject, std::nullopt};
}

namespace {
// JavaScript String(value) for the scalar shapes an identity service returns.
std::optional<std::string> js_string(const Json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_boolean()) return value.get<bool>() ? "true" : "false";
    if (value.is_number_integer()) return value.dump();
    if (value.is_number_float()) {
        const double v = value.get<double>();
        if (std::isfinite(v) && std::floor(v) == v && std::fabs(v) < 1e15) return std::to_string(static_cast<long long>(v));
        return value.dump();
    }
    return std::nullopt;
}

// JavaScript truthiness of an id: missing, null, false, 0 and "" are not identities.
bool truthy(const Json& value) {
    if (value.is_null()) return false;
    if (value.is_boolean()) return value.get<bool>();
    if (value.is_string()) return !value.get_ref<const std::string&>().empty();
    if (value.is_number()) { const double v = value.get<double>(); return v != 0 && !std::isnan(v); }
    return true;
}
}

std::optional<VerifiedUser> user_from_verify_response(const Json& body) {
    if (!body.is_object()) return std::nullopt;
    auto user = body.find("user");
    if (user == body.end() || !user->is_object()) return std::nullopt;
    auto id = user->find("id");
    if (id == user->end() || !truthy(*id)) return std::nullopt;
    auto text = [&](const char* key, const char* fallback) -> std::optional<std::string> {
        auto it = user->find(key);
        if (it == user->end() || it->is_null()) return std::string(fallback);
        return js_string(*it);
    };
    auto id_text = js_string(*id);
    auto name = text("name", ""), email = text("email", ""), role = text("role", "general");
    if (!id_text || !name || !email || !role) return std::nullopt;
    return VerifiedUser{*id_text, *name, *email, *role};
}

std::string bearer_token(std::string_view header) {
    if (header.size() < 7) return {};
    static constexpr std::string_view scheme = "bearer";
    for (std::size_t i = 0; i < scheme.size(); ++i) {
        char c = header[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != scheme[i]) return {};
    }
    std::size_t i = scheme.size();
    auto space = [](char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; };
    if (i >= header.size() || !space(header[i])) return {};
    while (i < header.size() && space(header[i])) ++i;
    // /^Bearer\s+(.+)$/: the backtracking leaves at least one character for the token.
    if (i >= header.size()) {
        std::size_t last = header.size() - 1;
        if (last > scheme.size()) return std::string(header.substr(last));
        return {};
    }
    return std::string(header.substr(i));
}

bool is_loopback_host(std::string_view host) {
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}

bool refuses_bind(std::string_view host, const AuthSettings& s) {
    const bool anonymous = s.open || (s.cernere_url.empty() && !s.auth_required);
    return !is_loopback_host(host) && anonymous;
}
}
