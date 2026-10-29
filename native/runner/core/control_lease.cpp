#include "control_lease.hpp"

namespace odeum::runner {
const char* role_name(const std::optional<std::string>& holder, const std::string& client_id) {
    if (!holder) return "free";
    return *holder == client_id ? "player" : "spectator";
}

bool ControlLease::acquire(const std::string& app_id, const std::string& client_id) {
    auto it = holders_.find(app_id);
    if (it != holders_.end()) return it->second == client_id;
    holders_.emplace(app_id, client_id);
    emit(app_id, client_id);
    return true;
}

void ControlLease::release(const std::string& app_id, const std::string& client_id) {
    auto it = holders_.find(app_id);
    if (it == holders_.end() || it->second != client_id) return;
    holders_.erase(it);
    emit(app_id, std::nullopt);
}

void ControlLease::release_all(const std::string& client_id) {
    std::vector<std::string> held;
    for (const auto& [app_id, holder] : holders_) if (holder == client_id) held.push_back(app_id);
    for (const auto& app_id : held) release(app_id, client_id);
}

std::optional<std::string> ControlLease::holder(const std::string& app_id) const {
    auto it = holders_.find(app_id);
    if (it == holders_.end()) return std::nullopt;
    return it->second;
}

const char* ControlLease::role_of(const std::string& app_id, const std::string& client_id) const {
    return role_name(holder(app_id), client_id);
}

bool ControlLease::may_operate(const std::string& app_id, const std::optional<std::string>& client_id) const {
    auto it = holders_.find(app_id);
    return it == holders_.end() || (client_id && it->second == *client_id);
}

std::size_t ControlLease::subscribe(Listener listener) {
    listeners_.emplace(++next_token_, std::move(listener));
    return next_token_;
}

void ControlLease::unsubscribe(std::size_t token) { listeners_.erase(token); }

void ControlLease::emit(const std::string& app_id, const std::optional<std::string>& holder) {
    auto copy = listeners_;  // a listener may subscribe or unsubscribe
    for (const auto& [token, listener] : copy) listener(app_id, holder);
}
}
