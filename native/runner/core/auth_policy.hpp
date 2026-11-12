#pragma once
// Authentication policy of the former custos/src/auth (Cernere) and main.ts bind guard. Pure: the
// caller supplies environment values and performs the Cernere request when the decision says so.
#include "json.hpp"
#include <optional>
#include <string>

namespace odeum::runner {
struct AuthSettings {
    bool open = false;           // CUSTOS_OPEN=1
    std::string cernere_url;     // CERNERE_URL with one trailing '/' removed; empty = not configured
    bool auth_required = false;  // CUSTOS_AUTH_REQUIRED=1
};

enum class AuthMode { open, cernere, stub, anonymous };
AuthMode auth_mode(const AuthSettings& settings);
// "open (CUSTOS_OPEN=1)", "cernere (<url>)", "stub (token required)", "anonymous (no auth)"
std::string describe(const AuthSettings& settings);

struct VerifiedUser {
    std::string id, name, email, role;
};

// Users that stand for "no verified identity" (open / anonymous / stub modes).
bool is_placeholder_user(const std::string& id);

struct AuthDecision {
    enum class Kind { accept, reject, ask_cernere } kind;
    std::optional<VerifiedUser> user;  // set for accept
};
// The local part of CernereAuth.verify: everything except the network request.
AuthDecision decide(const AuthSettings& settings, const std::string& token);
// Cernere /api/auth/verify body -> user; nullopt when the user or its id is missing.
std::optional<VerifiedUser> user_from_verify_response(const Json& body);

// `Authorization: Bearer <token>` (case-insensitive scheme, any whitespace); empty when absent.
std::string bearer_token(std::string_view header);

bool is_loopback_host(std::string_view host);
// main.ts: binding a non-loopback host without authentication is refused at startup.
bool refuses_bind(std::string_view host, const AuthSettings& settings);
}
