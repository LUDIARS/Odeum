#pragma once
#include <odeum/message.hpp>
#include <deque>
#include <optional>
#include <unordered_map>

namespace odeum::relay {
using Millis = std::int64_t;
struct ReactionResult { int accepted; bool limited; };
class Reactions {
public:
    explicit Reactions(Millis now) : last_flush_(now) {}
    ReactionResult accept(const std::string& sub, const Message& message, Millis now);
    std::optional<Json> flush(Millis now);
private:
    struct Rate { std::deque<Millis> goods, stamps, comments; Millis last_seen = 0; };
    std::unordered_map<std::string, Rate> users_;
    Millis last_flush_;
    int good_ = 0;
    std::unordered_map<std::string, int> stamps_;
};
}
