#include "web_access.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace odeum::relay {
namespace {
std::vector<std::string> list(const char* key) {
    std::vector<std::string> result;
    if (const auto value = std::getenv(key)) {
        std::istringstream input(value); std::string item;
        while (std::getline(input, item, ',')) {
            auto begin = item.find_first_not_of(" \t"), end = item.find_last_not_of(" \t");
            if (begin == std::string::npos) throw std::runtime_error("Empty web access entry");
            result.push_back(item.substr(begin, end - begin + 1));
        }
        if (result.empty()) throw std::runtime_error("Empty web access configuration");
    }
    return result;
}
std::string host(std::string authority) {
    if (authority.empty() || authority.find_first_of("/@?#\\ \t\r\n*") != std::string::npos) throw std::runtime_error("Invalid HTTP authority");
    std::transform(authority.begin(), authority.end(), authority.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::size_t port = std::string::npos;
    if (authority.front() == '[') {
        auto end = authority.find(']'); if (end == std::string::npos) throw std::runtime_error("Invalid IPv6 authority");
        if (end + 1 < authority.size()) { if (authority[end + 1] != ':') throw std::runtime_error("Invalid port"); port = end + 1; }
    } else { port = authority.find(':'); }
    if (port != std::string::npos) {
        auto value = authority.substr(port + 1);
        if (value.empty() || value.size() > 5 || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); }) || std::stoul(value) == 0 || std::stoul(value) > 65535)
            throw std::runtime_error("Invalid authority port");
        authority.resize(port);
    }
    if (authority.empty()) throw std::runtime_error("Empty host");
    return authority;
}
void origin(const std::string& value) {
    auto prefix = value.starts_with("https://") ? 8u : value.starts_with("http://") ? 7u : 0u;
    if (!prefix) throw std::runtime_error("Invalid origin scheme");
    auto h = host(value.substr(prefix));
    if (prefix == 7 && h != "localhost" && h != "127.0.0.1" && h != "[::1]") throw std::runtime_error("Remote origin must use HTTPS");
}
}
WebAccess WebAccess::from_environment(unsigned port) {
    WebAccess result;
    result.hosts_ = {"localhost", "127.0.0.1", "[::1]"};
    for (auto value : list("LUDIARS_ALLOWED_HOSTS")) {
        const bool subdomains = value.starts_with('.');
        if (subdomains) value.erase(0, 1);
        if (value.find(':') != std::string::npos) throw std::runtime_error("Allowed host must omit port");
        result.hosts_.push_back((subdomains ? "." : "") + host(value));
    }
    if (const auto url = std::getenv("ODEUM_RELAY_PUBLIC_URL")) {
        std::string value(url); origin(value); result.origins_.push_back(value);
        result.hosts_.push_back(host(value.substr(value.find("://") + 3)));
    }
    for (const auto& value : list("ODEUM_RELAY_ALLOWED_ORIGINS")) { origin(value); result.origins_.push_back(value); }
    result.origins_.push_back("http://localhost:" + std::to_string(port));
    result.origins_.push_back("http://127.0.0.1:" + std::to_string(port));
    return result;
}
bool WebAccess::allows_host(std::string authority) const {
    try {
        auto value = host(std::move(authority));
        for (const auto& allowed : hosts_) {
            if (value == allowed) return true;
            if (allowed.starts_with('.') && (value == allowed.substr(1) || (value.size() > allowed.size() && value.ends_with(allowed)))) return true;
        }
    } catch (const std::exception&) { return false; }
    return false;
}
bool WebAccess::allows_origin(const std::string& value) const {
    return value.empty() || std::find(origins_.begin(), origins_.end(), value) != origins_.end();
}
bool WebAccess::allows_same_origin(const std::string& authority, const std::string& origin) const {
    if (origin.empty()) return allows_host(authority);
    return allows_host(authority) && (origin == "http://" + authority || origin == "https://" + authority);
}
}
