#pragma once
// Self-hosted device farm control plane (custos/src/farm/*): configuration and wire contracts,
// device availability / fencing, and the pool that owns leases. Storage is behind FarmStore.
#include "json.hpp"
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace odeum::runner {
class FarmError : public std::runtime_error {
public:
    FarmError(std::string code, unsigned status) : std::runtime_error(code), code_(std::move(code)), status_(status) {}
    const std::string& code() const { return code_; }
    unsigned status() const { return status_; }
private:
    std::string code_;
    unsigned status_;
};
// A configuration or state that violates the contract (startup / storage failure, not a client error).
class FarmContractError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct FarmHost { std::string id, token_env; };
struct FarmDevice {
    std::string id, host_id, platform, serial, model, os_version;
    std::vector<std::string> tags;
};
struct FarmConfig {
    int heartbeat_timeout_sec = 60;
    std::vector<FarmHost> hosts;
    std::vector<FarmDevice> devices;
};
FarmConfig parse_farm_config(const Json& value);  // throws FarmContractError

struct FarmLease {
    std::string id, owner_id;
    long long generation = 1, expires_at = 0;
};
struct FarmDeviceState {
    std::string host_id, platform, serial;
    long long generation = 1;
    std::optional<long long> last_seen_at;
    bool connected = false, needs_cleanup = true;
    std::string reason = "not-reported";  // not-reported | ready | leased | released | expired | disconnected | host-timeout
    std::optional<FarmLease> lease;
};
struct FarmState {
    std::vector<std::pair<std::string, FarmDeviceState>> devices;  // file order
    FarmDeviceState* find(const std::string& id);
};
FarmState parse_farm_state(const Json& value);  // throws FarmContractError
Json to_json(const FarmState& state);

struct HostObservation { std::string id; bool connected; std::optional<long long> ready_generation; };
// Request bodies; throw FarmError("invalid_body", 400).
std::vector<HostObservation> parse_host_report(const Json& body);
long long parse_acquire(const Json& body);                            // ttlSec
std::string parse_release(const Json& body);                          // leaseId
std::pair<std::string, long long> parse_renew(const Json& body);      // leaseId, ttlSec
bool is_farm_id(std::string_view id);
bool is_uuid(std::string_view id);

void fence_device(FarmDeviceState& state, const std::string& reason);
void reconcile_device(FarmDeviceState& state, long long now, long long timeout_ms);
std::string device_availability(const FarmDeviceState& state);  // offline | leased | cleanup | available

class FarmStore {
public:
    virtual ~FarmStore() = default;
    // Runs `action` on the committed state and commits the result when it changed. Throws
    // FarmError("farm_store_locked", 503) when another transaction holds the lock.
    virtual void transaction(const std::function<void(FarmState&)>& action) = 0;
};

class DevicePool {
public:
    DevicePool(FarmConfig config, FarmStore& store, std::function<long long()> now, std::function<std::string()> new_id);
    Json list();
    Json report(const std::string& host_id, const std::vector<HostObservation>& report);
    Json acquire(const std::string& device_id, const std::string& owner_id, long long ttl_sec);
    Json renew(const std::string& device_id, const std::string& owner_id, const std::string& lease_id, long long ttl_sec);
    Json release(const std::string& device_id, const std::string& owner_id, const std::string& lease_id);
    const FarmConfig& config() const { return config_; }
private:
    FarmConfig config_;
    FarmStore& store_;
    std::function<long long()> now_;
    std::function<std::string()> new_id_;
    Json change(const std::function<Json(FarmState&, long long)>& action);
    const FarmDevice& definition(const std::string& id) const;
    FarmDeviceState& require(FarmState& state, const std::string& id) const;
    FarmLease& owned_lease(FarmDeviceState& state, const std::string& owner_id, const std::string& lease_id) const;
    Json view(const FarmDevice& device, const FarmDeviceState& state) const;
};
}
