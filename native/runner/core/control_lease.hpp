#pragma once
// One remote player per app; other connections spectate (custos/src/input/control-lease.ts).
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
// player | spectator | free
const char* role_name(const std::optional<std::string>& holder, const std::string& client_id);

class ControlLease {
public:
    using Listener = std::function<void(const std::string& app_id, const std::optional<std::string>& holder)>;
    // True when nobody holds the app or the client already does.
    bool acquire(const std::string& app_id, const std::string& client_id);
    void release(const std::string& app_id, const std::string& client_id);
    void release_all(const std::string& client_id);
    std::optional<std::string> holder(const std::string& app_id) const;
    const char* role_of(const std::string& app_id, const std::string& client_id) const;
    // Operations that do not take the lease (Unity Play/Stop): allowed when nobody plays or the
    // caller is the player. A caller without a client id is allowed only when nobody plays.
    bool may_operate(const std::string& app_id, const std::optional<std::string>& client_id) const;
    std::size_t subscribe(Listener listener);
    void unsubscribe(std::size_t token);
private:
    std::map<std::string, std::string> holders_;
    std::map<std::size_t, Listener> listeners_;
    std::size_t next_token_ = 0;
    void emit(const std::string& app_id, const std::optional<std::string>& holder);
};
}
