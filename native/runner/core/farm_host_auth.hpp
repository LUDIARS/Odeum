#pragma once
// Farm host credentials (custos/src/farm/host-auth.ts): one bearer token per host from the
// environment variable its configuration names. Only SHA-256 digests are kept; comparison is
// constant time. Tokens are never logged or serialised.
#include "farm.hpp"
#include <array>
#include <map>

namespace odeum::runner {
class FarmHostAuth {
public:
    // Throws FarmContractError for a missing / malformed token or two hosts sharing one.
    FarmHostAuth(const std::vector<FarmHost>& hosts, const std::function<std::optional<std::string>(const char*)>& env);
    bool verify(const std::string& host_id, std::string_view authorization) const;
private:
    std::map<std::string, std::array<unsigned char, 32>> digests_;
};
std::array<unsigned char, 32> sha256(std::string_view data);
}
