#include "tool_desk.hpp"
#include "invitations.hpp"
#include <algorithm>

namespace odeum::lesson {
namespace {
bool same(const ToolDesk::Holder& a, const ToolDesk::Holder& b) { return a.class_id == b.class_id && a.student == b.student && a.role == b.role; }
}
std::string_view tool_role_name(ToolDesk::Role role) { return role == ToolDesk::Role::presenter ? "presenter" : "student"; }

std::string ToolDesk::issue(const Holder& holder, Millis now) {
    std::erase_if(links_, [&](const auto& entry) { return entry.second.expires <= now || same(entry.second.holder, holder); });
    auto code = codes_();
    while (links_.contains(code)) code = codes_();
    links_.emplace(code, Link{holder, now + link_ms});
    return code;
}
std::string ToolDesk::redeem(std::string_view code, Millis now) {
    while (!failures_.empty() && failures_.front() <= now - 60'000) failures_.pop_front();
    if (failures_.size() >= max_failures_per_minute) throw ProtocolError("rate_limited", "Too many wrong codes; wait a minute");
    const auto normalised = relay::normalize_join_code(code);
    const auto found = normalised ? links_.find(*normalised) : links_.end();
    if (found == links_.end() || found->second.expires <= now) {
        if (found != links_.end()) links_.erase(found);
        failures_.push_back(now);
        throw ProtocolError("link_not_found", "No such link code; ask for a new one");
    }
    const auto holder = found->second.holder;
    links_.erase(found);
    // A participant who links a second machine keeps both; a third replaces the oldest.
    std::vector<std::pair<std::uint64_t, std::string>> held;
    for (const auto& [token, grant] : grants_) if (same(grant.holder, holder)) held.emplace_back(grant.order, token);
    std::sort(held.begin(), held.end());
    for (std::size_t i = 0; i + max_tokens_per_holder <= held.size(); ++i) grants_.erase(held[i].second);
    auto token = tokens_();
    while (grants_.contains(token)) token = tokens_();
    grants_.emplace(token, Grant{holder, ++next_});
    return token;
}
const ToolDesk::Holder& ToolDesk::holder(std::string_view token) const {
    const auto found = grants_.find(token);
    if (found == grants_.end()) throw ProtocolError("tool_unlinked", "This tool is not linked to a class");
    return found->second.holder;
}
bool ToolDesk::linked(const std::string& class_id, const std::string& student) const {
    return std::any_of(grants_.begin(), grants_.end(), [&](const auto& entry) {
        return entry.second.holder.class_id == class_id && entry.second.holder.student == student; });
}
void ToolDesk::revoke_student(const std::string& class_id, const std::string& student) {
    const auto match = [&](const auto& entry) { return entry.second.holder.class_id == class_id && entry.second.holder.student == student; };
    std::erase_if(links_, match);
    std::erase_if(grants_, match);
}
void ToolDesk::revoke_class(const std::string& class_id) {
    const auto match = [&](const auto& entry) { return entry.second.holder.class_id == class_id; };
    std::erase_if(links_, match);
    std::erase_if(grants_, match);
}
}
