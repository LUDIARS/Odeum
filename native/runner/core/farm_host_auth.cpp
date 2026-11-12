#include "farm_host_auth.hpp"
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <set>

namespace odeum::runner {
std::array<unsigned char, 32> sha256(std::string_view data) {
    std::array<unsigned char, 32> out{};
    unsigned int length = 0;
    if (EVP_Digest(data.data(), data.size(), out.data(), &length, EVP_sha256(), nullptr) != 1 || length != out.size()) {
        throw std::runtime_error("SHA-256 unavailable");
    }
    return out;
}

namespace {
bool has_space(std::string_view text) {
    for (char c : text) if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') return true;
    return false;
}
}

FarmHostAuth::FarmHostAuth(const std::vector<FarmHost>& hosts, const std::function<std::optional<std::string>(const char*)>& env) {
    std::set<std::array<unsigned char, 32>> seen;
    for (const auto& host : hosts) {
        const auto token = env(host.token_env.c_str());
        if (!token || token->size() < 32 || token->size() > 512 || has_space(*token)) {
            throw FarmContractError("missing or invalid credential for farm host " + host.id);
        }
        const auto digest = sha256(*token);
        if (!seen.insert(digest).second) throw FarmContractError("farm hosts must have distinct credentials");
        digests_.emplace(host.id, digest);
    }
}

bool FarmHostAuth::verify(const std::string& host_id, std::string_view authorization) const {
    static constexpr std::string_view prefix = "Bearer ";
    if (authorization.substr(0, prefix.size()) != prefix) return false;
    const auto token = authorization.substr(prefix.size());
    if (token.size() < 32 || token.size() > 512 || has_space(token)) return false;
    auto it = digests_.find(host_id);
    if (it == digests_.end()) return false;
    const auto digest = sha256(token);
    return CRYPTO_memcmp(digest.data(), it->second.data(), digest.size()) == 0;
}
}
