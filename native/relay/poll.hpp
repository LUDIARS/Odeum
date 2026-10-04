#pragma once
#include "reactions.hpp"
#include <map>
#include <vector>

namespace odeum::relay {
class Poll {
public:
    void open(const Json& message, Millis now);
    Json answer(const std::string& sub, const std::string& poll_id, const std::vector<int>& choices);
    Json close(const std::string& poll_id);
    std::optional<Json> flush(Millis now);
    std::optional<Json> current() const;
private:
    Json definition_;
    std::map<std::string, std::vector<int>> answers_;
    Millis last_tally_ = 0;
    bool active_ = false, dirty_ = false;
    Json tally() const;
};
}
