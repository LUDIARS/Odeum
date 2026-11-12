#include "audience.hpp"
#include "text.hpp"

namespace odeum::runner {
Json to_json(const AudienceEvent& e) {
    Json out{{"id", e.id}, {"appId", e.app_id}, {"ts", e.ts}, {"sender", e.sender}, {"kind", e.kind}};
    if (e.kind == "message") out["text"] = e.text;
    else out["stamp"] = e.stamp;
    return out;
}

AudienceRoom::AudienceRoom(std::function<long long()> now, std::function<std::string()> new_id)
    : now_(std::move(now)), new_id_(std::move(new_id)) {}

std::vector<AudienceEvent> AudienceRoom::history(const std::string& app_id) const {
    auto it = events_.find(app_id);
    if (it == events_.end()) return {};
    return {it->second.begin(), it->second.end()};
}

Json AudienceRoom::history_json(const std::string& app_id) const {
    Json out = Json::array();
    for (const auto& event : history(app_id)) out.push_back(to_json(event));
    return out;
}

AudienceEvent AudienceRoom::append(const AudienceSubmission& submission, std::string_view sender) {
    AudienceEvent event{new_id_(), submission.app_id, now_(), utf16_prefix(sender, 60), submission.kind, submission.text, submission.stamp};
    auto& list = events_[event.app_id];
    list.push_back(event);
    while (list.size() > audience_history_limit) list.pop_front();
    for (const auto& listener : listeners_) listener(event);
    return event;
}
}
