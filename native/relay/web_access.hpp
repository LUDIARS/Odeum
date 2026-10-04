#pragma once
#include <string>
#include <vector>

namespace odeum::relay {
class WebAccess {
public:
    static WebAccess from_environment(unsigned port);
    bool allows_host(std::string authority) const;
    bool allows_origin(const std::string& origin) const;
private:
    std::vector<std::string> hosts_, origins_;
};
}
