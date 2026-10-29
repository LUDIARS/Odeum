#pragma once
// App-scoped, bounded audience history (custos/src/audience/room.ts). No account identifiers.
#include "client_message.hpp"
#include <deque>
#include <functional>
#include <map>

namespace odeum::runner {
inline constexpr std::size_t audience_history_limit = 200;

struct AudienceEvent {
    std::string id, app_id;
    long long ts = 0;
    std::string sender, kind, text, stamp;
};
Json to_json(const AudienceEvent& event);

class AudienceRoom {
public:
    using Listener = std::function<void(const AudienceEvent&)>;
    AudienceRoom(std::function<long long()> now, std::function<std::string()> new_id);
    std::vector<AudienceEvent> history(const std::string& app_id) const;
    Json history_json(const std::string& app_id) const;
    AudienceEvent append(const AudienceSubmission& submission, std::string_view sender);
    void subscribe(Listener listener) { listeners_.push_back(std::move(listener)); }
private:
    std::function<long long()> now_;
    std::function<std::string()> new_id_;
    std::map<std::string, std::deque<AudienceEvent>> events_;
    std::vector<Listener> listeners_;
};
}
