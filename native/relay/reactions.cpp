#include "reactions.hpp"
#include <algorithm>

namespace odeum::relay {
namespace {
void expire(std::deque<Millis>& values, Millis now, Millis interval) {
    while (!values.empty() && values.front() <= now - interval) values.pop_front();
}
}
ReactionResult Reactions::accept(const std::string& sub, const Message& message, Millis now) {
    auto& user = users_[sub]; user.last_seen = now;
    if (message.type == MessageType::good) {
        expire(user.goods, now, 1000);
        int accepted = std::min(message.body.at("count").get<int>(), 30 - static_cast<int>(user.goods.size()));
        for (int i = 0; i < accepted; ++i) user.goods.push_back(now);
        good_ += accepted; return {accepted, false};
    }
    const bool stamp = message.type == MessageType::stamp;
    auto& values = stamp ? user.stamps : user.comments;
    expire(values, now, stamp ? 1000 : 3000);
    if (values.size() >= (stamp ? 2u : 1u)) return {0, true};
    values.push_back(now);
    if (stamp) ++stamps_[message.body.at("kind").get<std::string>()];
    return {1, false};
}
std::optional<Json> Reactions::flush(Millis now) {
    if (now - last_flush_ < 250) return std::nullopt;
    last_flush_ = now;
    std::erase_if(users_, [now](const auto& entry) { return now - entry.second.last_seen >= 3000; });
    if (good_ == 0 && stamps_.empty()) return std::nullopt;
    Json result = {{"type", "reaction.burst"}, {"good", good_}, {"stamps", stamps_}};
    good_ = 0; stamps_.clear(); return result;
}
}
