#pragma once
#include <string>
#include <vector>

namespace odeum::relay {
class WebAccess {
public:
    static WebAccess from_environment(unsigned port);
    // Same rules for another Odeum service with its own variable names (odeum-lesson).
    static WebAccess from_environment(unsigned port, const char* public_url_variable, const char* origins_variable);
    bool allows_host(std::string authority) const;
    bool allows_origin(const std::string& origin) const;
    // Pages served by the relay itself (guest join, program overlay): Origin must name the allowed Host.
    bool allows_same_origin(const std::string& authority, const std::string& origin) const;
private:
    std::vector<std::string> hosts_, origins_;
};
}
