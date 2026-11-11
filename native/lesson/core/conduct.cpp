#include "conduct.hpp"
#include <algorithm>
#include <array>
#include <numeric>

namespace odeum::lesson {
namespace {
void drop_before(std::deque<Millis>& times, Millis cutoff) { while (!times.empty() && times.front() <= cutoff) times.pop_front(); }
}
void Conduct::forget(Record& record, Millis now) {
    while (!record.strikes.empty() && record.strikes.front().at <= now - memory_ms) record.strikes.pop_front();
}
int Conduct::points(const std::string& student, Millis now) const {
    const auto found = records_.find(student);
    if (found == records_.end()) return 0;
    int total = 0;
    for (const auto& strike : found->second.strikes) if (strike.at > now - memory_ms) total += strike.points;
    return total;
}
Conduct::Verdict Conduct::strike(const std::string& student, int amount, Millis now, const std::string& reason) {
    if (ejected_.contains(student)) return {};
    auto& record = records_[student];
    forget(record, now);
    record.strikes.push_back({now, amount});
    const int total = points(student, now);
    if (total >= eject_points) return eject(student, reason);
    if (total >= lock_points && !(record.locked && record.locked_until > now)) {
        // Each lockout doubles, so cooling off costs more every time it is needed.
        const auto duration = std::min<Millis>(first_lock_ms << std::min<std::size_t>(record.locks, 8), max_lock_ms);
        return lock(student, duration, now, reason);
    }
    if (!record.warned) { record.warned = true; return {Action::warn, 0, reason}; }
    return {};
}
Conduct::Verdict Conduct::post(const std::string& student, Millis now) {
    auto& record = records_[student];
    drop_before(record.posts, now - flood_window_ms);
    record.posts.push_back(now);
    if (record.posts.size() <= flood_posts) return {};
    record.posts.clear();  // one strike per burst, not one per extra message
    return strike(student, 1, now, "短い時間に投稿が多すぎます");
}
Conduct::Verdict Conduct::disruption(const std::string& student, int severity, Millis now) {
    return strike(student, std::clamp(severity, 1, 2), now, severity >= 2 ? "授業にふさわしくない言葉です" : "授業と関係のない投稿が続いています");
}
Conduct::Verdict Conduct::off_topic(const std::string& student, Millis now) {
    auto& record = records_[student];
    drop_before(record.chatter, now - chatter_window_ms);
    record.chatter.push_back(now);
    if (record.chatter.size() <= chatter_limit) return {};
    record.chatter.clear();
    return strike(student, 1, now, "授業と関係のない発言が続いています");
}
Conduct::Verdict Conduct::lock(const std::string& student, Millis duration, Millis now, std::string reason) {
    if (ejected_.contains(student)) return {};
    auto& record = records_[student];
    ++record.locks;
    if (record.locks >= eject_locks) return eject(student, std::move(reason));
    record.locked = true; record.warned = true;
    record.locked_until = now + std::clamp<Millis>(duration, 1000, max_lock_ms);
    return {Action::lock, record.locked_until, std::move(reason)};
}
void Conduct::unlock(const std::string& student) {
    const auto found = records_.find(student);
    if (found == records_.end()) return;
    found->second.locked = false; found->second.locked_until = 0;
    // The operator judged the student fit to continue: start again from a clean slate.
    found->second.strikes.clear();
}
Conduct::Verdict Conduct::eject(const std::string& student, std::string reason) {
    ejected_.insert(student);
    auto& record = records_[student];
    record.locked = false; record.locked_until = 0;
    return {Action::eject, 0, std::move(reason)};
}
void Conduct::readmit(const std::string& student) { ejected_.erase(student); records_.erase(student); }
std::vector<std::string> Conduct::expire(Millis now) {
    std::vector<std::string> out;
    for (auto& [student, record] : records_) {
        if (!record.locked || record.locked_until > now) continue;
        record.locked = false; record.locked_until = 0;
        out.push_back(student);
    }
    return out;
}
Conduct::Standing Conduct::standing(const std::string& student, Millis now) const {
    if (ejected_.contains(student)) return Standing::ejected;
    const auto found = records_.find(student);
    if (found == records_.end()) return Standing::good;
    if (found->second.locked && found->second.locked_until > now) return Standing::locked;
    return found->second.warned && points(student, now) > 0 ? Standing::warned : Standing::good;
}
bool Conduct::may_post(const std::string& student, Millis now) const {
    const auto state = standing(student, now);
    return state != Standing::locked && state != Standing::ejected;
}
Millis Conduct::locked_until(const std::string& student) const {
    const auto found = records_.find(student);
    return found == records_.end() ? 0 : found->second.locked_until;
}
std::string_view standing_name(Conduct::Standing standing) {
    constexpr std::array names{"good", "warned", "locked", "ejected"};
    return names.at(static_cast<std::size_t>(standing));
}
}
