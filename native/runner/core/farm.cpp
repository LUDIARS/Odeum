#include "farm.hpp"
#include "playlog_row.hpp"
#include "text.hpp"
#include <cmath>
#include <set>

namespace odeum::runner {
namespace {
template<class E> [[noreturn]] void reject(const std::string& message);
template<> [[noreturn]] void reject<FarmContractError>(const std::string& message) { throw FarmContractError(message); }
template<> [[noreturn]] void reject<FarmError>(const std::string&) { throw FarmError("invalid_body", 400); }

template<class E> void strict(const Json& value, std::initializer_list<const char*> keys, const std::string& path) {
    if (!value.is_object()) reject<E>(path + ": expected an object");
    for (const auto& [key, item] : value.items()) {
        bool known = false;
        for (const auto* k : keys) known |= key == k;
        if (!known) reject<E>(path + ": unrecognized key " + key);
    }
}

// Returns a pointer: GCC 13 -Wdangling-reference misreads a reference result with a string temporary.
template<class E> const Json* need(const Json& object, const char* key, const std::string& path) {
    auto it = object.find(key);
    if (it == object.end()) reject<E>(path + "." + key + ": required");
    return &*it;
}

template<class E> long long integer(const Json& value, double min, double max, const std::string& path) {
    if (!value.is_number()) reject<E>(path + ": expected a number");
    const double v = value.get<double>();
    if (!std::isfinite(v) || std::floor(v) != v || v < min || v > max) reject<E>(path + ": out of range");
    return static_cast<long long>(v);
}

template<class E> bool boolean(const Json& value, const std::string& path) {
    if (!value.is_boolean()) reject<E>(path + ": expected a boolean");
    return value.get<bool>();
}

template<class E> std::string text(const Json& value, const std::string& path) {
    if (!value.is_string()) reject<E>(path + ": expected a string");
    auto trimmed = js_trim(value.get<std::string>());
    const auto length = utf16_length(trimmed);
    if (length < 1 || length > 160) reject<E>(path + ": length");
    return trimmed;
}

template<class E> std::string farm_id(const Json& value, const std::string& path) {
    if (!value.is_string() || !is_farm_id(value.get<std::string>())) reject<E>(path + ": invalid id");
    return value.get<std::string>();
}

template<class E> std::string platform(const Json& value, const std::string& path) {
    if (!value.is_string() || (value != "android" && value != "ios")) reject<E>(path + ": invalid platform");
    return value.get<std::string>();
}

bool token_env_name(const std::string& name) {
    if (name.size() < 2 || name[0] < 'A' || name[0] > 'Z') return false;
    for (char c : name) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

const std::set<std::string> reasons{"not-reported", "ready", "leased", "released", "expired", "disconnected", "host-timeout"};

FarmDeviceState parse_device_state(const Json& value, const std::string& path) {
    using E = FarmContractError;
    strict<E>(value, {"hostId", "platform", "serial", "generation", "lastSeenAt", "connected", "needsCleanup", "reason", "lease"}, path);
    FarmDeviceState s;
    s.host_id = farm_id<E>(*need<E>(value, "hostId", path), path + ".hostId");
    s.platform = platform<E>(*need<E>(value, "platform", path), path + ".platform");
    s.serial = text<E>(*need<E>(value, "serial", path), path + ".serial");
    s.generation = integer<E>(*need<E>(value, "generation", path), 1, 9e15, path + ".generation");
    const auto& seen = *need<E>(value, "lastSeenAt", path);
    if (!seen.is_null()) s.last_seen_at = integer<E>(seen, 0, 9e15, path + ".lastSeenAt");
    s.connected = boolean<E>(*need<E>(value, "connected", path), path + ".connected");
    s.needs_cleanup = boolean<E>(*need<E>(value, "needsCleanup", path), path + ".needsCleanup");
    const auto& reason = *need<E>(value, "reason", path);
    if (!reason.is_string() || !reasons.contains(reason.get<std::string>())) throw FarmContractError(path + ".reason: invalid");
    s.reason = reason.get<std::string>();
    const auto& lease = *need<E>(value, "lease", path);
    if (!lease.is_null()) {
        strict<E>(lease, {"id", "ownerId", "generation", "expiresAt"}, path + ".lease");
        FarmLease l;
        const auto& id = *need<E>(lease, "id", path + ".lease");
        if (!id.is_string() || !is_uuid(id.get<std::string>())) throw FarmContractError(path + ".lease.id: invalid");
        l.id = id.get<std::string>();
        const auto& owner = *need<E>(lease, "ownerId", path + ".lease");
        if (!owner.is_string() || owner.get<std::string>().empty()) throw FarmContractError(path + ".lease.ownerId: invalid");
        l.owner_id = owner.get<std::string>();
        l.generation = integer<E>(*need<E>(lease, "generation", path + ".lease"), 1, 9e15, path + ".lease.generation");
        l.expires_at = integer<E>(*need<E>(lease, "expiresAt", path + ".lease"), 0, 9e15, path + ".lease.expiresAt");
        s.lease = l;
    }
    return s;
}

void check_consistency(const FarmDeviceState& s, const std::string& path) {
    if (s.lease && (s.lease->generation != s.generation || s.needs_cleanup || !s.connected || !s.last_seen_at || s.reason != "leased")) {
        throw FarmContractError(path + ": inconsistent lease state");
    }
}

Json state_json(const FarmDeviceState& s) {
    Json lease = nullptr;
    if (s.lease) lease = Json{{"id", s.lease->id}, {"ownerId", s.lease->owner_id}, {"generation", s.lease->generation}, {"expiresAt", s.lease->expires_at}};
    return Json{{"hostId", s.host_id}, {"platform", s.platform}, {"serial", s.serial}, {"generation", s.generation},
        {"lastSeenAt", s.last_seen_at ? Json(*s.last_seen_at) : Json(nullptr)}, {"connected", s.connected}, {"needsCleanup", s.needs_cleanup},
        {"reason", s.reason}, {"lease", lease}};
}
}

bool is_farm_id(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    auto ok = [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); };
    if (!ok(id.front())) return false;
    for (char c : id.substr(1)) if (!ok(c) && c != '-') return false;
    return true;
}

bool is_uuid(std::string_view id) {
    if (id.size() != 36) return false;
    for (std::size_t i = 0; i < id.size(); ++i) {
        const char c = id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) { if (c != '-') return false; continue; }
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return false;
    }
    return true;
}

FarmConfig parse_farm_config(const Json& value) {
    using E = FarmContractError;
    strict<E>(value, {"version", "heartbeatTimeoutSec", "hosts", "devices"}, "config");
    if (*need<E>(value, "version", "config") != 1) throw FarmContractError("config.version: expected 1");
    FarmConfig config;
    if (auto it = value.find("heartbeatTimeoutSec"); it != value.end()) config.heartbeat_timeout_sec = static_cast<int>(integer<E>(*it, 10, 300, "config.heartbeatTimeoutSec"));
    const auto& hosts = *need<E>(value, "hosts", "config");
    if (!hosts.is_array() || hosts.empty() || hosts.size() > 100) throw FarmContractError("config.hosts: 1..100 entries");
    for (const auto& host : hosts) {
        strict<E>(host, {"id", "tokenEnv"}, "config.hosts[]");
        const auto& env = *need<E>(host, "tokenEnv", "config.hosts[]");
        if (!env.is_string() || !token_env_name(env.get<std::string>())) throw FarmContractError("config.hosts[].tokenEnv: invalid");
        config.hosts.push_back({farm_id<E>(*need<E>(host, "id", "config.hosts[]"), "config.hosts[].id"), env.get<std::string>()});
    }
    const auto& devices = *need<E>(value, "devices", "config");
    if (!devices.is_array() || devices.empty() || devices.size() > 1000) throw FarmContractError("config.devices: 1..1000 entries");
    for (const auto& d : devices) {
        const std::string p = "config.devices[]";
        strict<E>(d, {"id", "hostId", "platform", "serial", "model", "osVersion", "tags"}, p);
        FarmDevice device{farm_id<E>(*need<E>(d, "id", p), p + ".id"), farm_id<E>(*need<E>(d, "hostId", p), p + ".hostId"),
            platform<E>(*need<E>(d, "platform", p), p + ".platform"), text<E>(*need<E>(d, "serial", p), p + ".serial"),
            text<E>(*need<E>(d, "model", p), p + ".model"), text<E>(*need<E>(d, "osVersion", p), p + ".osVersion"), {}};
        if (auto tags = d.find("tags"); tags != d.end()) {
            if (!tags->is_array() || tags->size() > 32) throw FarmContractError(p + ".tags: at most 32");
            for (const auto& tag : *tags) device.tags.push_back(text<E>(tag, p + ".tags[]"));
        }
        config.devices.push_back(std::move(device));
    }
    auto unique = [](const std::vector<std::string>& values, const char* label) {
        std::set<std::string> seen(values.begin(), values.end());
        if (seen.size() != values.size()) throw FarmContractError(std::string("duplicate ") + label);
    };
    std::vector<std::string> host_ids, envs, device_ids, physical;
    for (const auto& h : config.hosts) { host_ids.push_back(h.id); envs.push_back(h.token_env); }
    for (const auto& d : config.devices) { device_ids.push_back(d.id); physical.push_back(d.platform + "\n" + d.serial); }
    unique(host_ids, "host id");
    unique(envs, "host token environment name");
    unique(device_ids, "device id");
    unique(physical, "physical device");
    for (const auto& d : config.devices) {
        bool known = false;
        for (const auto& h : config.hosts) known |= h.id == d.host_id;
        if (!known) throw FarmContractError("unknown host for device " + d.id);
    }
    return config;
}

FarmDeviceState* FarmState::find(const std::string& id) {
    for (auto& [key, state] : devices) if (key == id) return &state;
    return nullptr;
}

FarmState parse_farm_state(const Json& value) {
    using E = FarmContractError;
    strict<E>(value, {"version", "devices"}, "state");
    if (*need<E>(value, "version", "state") != 1) throw FarmContractError("state.version: expected 1");
    const auto& devices = *need<E>(value, "devices", "state");
    if (!devices.is_object()) throw FarmContractError("state.devices: expected an object");
    FarmState state;
    for (const auto& [id, item] : devices.items()) {
        if (!is_farm_id(id)) throw FarmContractError("state.devices: invalid id");
        auto parsed = parse_device_state(item, "state.devices." + id);
        check_consistency(parsed, "state.devices." + id);
        state.devices.emplace_back(id, std::move(parsed));
    }
    return state;
}

Json to_json(const FarmState& state) {
    Json devices = Json::object();
    for (const auto& [id, s] : state.devices) devices[id] = state_json(s);
    return Json{{"version", 1}, {"devices", devices}};
}

std::vector<HostObservation> parse_host_report(const Json& body) {
    using E = FarmError;
    strict<E>(body, {"devices"}, "report");
    const auto& devices = *need<E>(body, "devices", "report");
    if (!devices.is_array() || devices.size() > 1000) throw FarmError("invalid_body", 400);
    std::vector<HostObservation> out;
    for (const auto& d : devices) {
        strict<E>(d, {"id", "connected", "readyGeneration"}, "report.devices[]");
        HostObservation o{farm_id<E>(*need<E>(d, "id", "r"), "r"), boolean<E>(*need<E>(d, "connected", "r"), "r"), std::nullopt};
        if (auto it = d.find("readyGeneration"); it != d.end()) o.ready_generation = integer<E>(*it, 1, 9e15, "r");
        out.push_back(std::move(o));
    }
    return out;
}

long long parse_acquire(const Json& body) {
    strict<FarmError>(body, {"ttlSec"}, "acquire");
    return integer<FarmError>(*need<FarmError>(body, "ttlSec", "acquire"), 10, 1800, "ttlSec");
}

std::string parse_release(const Json& body) {
    strict<FarmError>(body, {"leaseId"}, "release");
    const auto& id = *need<FarmError>(body, "leaseId", "release");
    if (!id.is_string() || !is_uuid(id.get<std::string>())) throw FarmError("invalid_body", 400);
    return id.get<std::string>();
}

std::pair<std::string, long long> parse_renew(const Json& body) {
    strict<FarmError>(body, {"leaseId", "ttlSec"}, "renew");
    const auto& id = *need<FarmError>(body, "leaseId", "renew");
    if (!id.is_string() || !is_uuid(id.get<std::string>())) throw FarmError("invalid_body", 400);
    return {id.get<std::string>(), integer<FarmError>(*need<FarmError>(body, "ttlSec", "renew"), 10, 1800, "ttlSec")};
}

void fence_device(FarmDeviceState& state, const std::string& reason) {
    state.generation += 1;
    state.lease.reset();
    state.needs_cleanup = true;
    state.reason = reason;
}

void reconcile_device(FarmDeviceState& state, long long now, long long timeout_ms) {
    if (state.connected && (!state.last_seen_at || now < *state.last_seen_at || now - *state.last_seen_at >= timeout_ms)) {
        state.connected = false;
        fence_device(state, "host-timeout");
    } else if (state.lease && state.lease->expires_at <= now) {
        fence_device(state, "expired");
    }
}

std::string device_availability(const FarmDeviceState& state) {
    if (!state.connected) return "offline";
    if (state.lease) return "leased";
    return state.needs_cleanup ? "cleanup" : "available";
}

DevicePool::DevicePool(FarmConfig config, FarmStore& store, std::function<long long()> now, std::function<std::string()> new_id)
    : config_(std::move(config)), store_(store), now_(std::move(now)), new_id_(std::move(new_id)) {
    change([](FarmState&, long long) { return Json(); });
}

const FarmDevice& DevicePool::definition(const std::string& id) const {
    for (const auto& d : config_.devices) if (d.id == id) return d;
    throw FarmError("unknown_device", 404);
}

FarmDeviceState& DevicePool::require(FarmState& state, const std::string& id) const {
    definition(id);
    auto* device = state.find(id);
    if (!device) throw FarmContractError("missing farm device state");
    return *device;
}

FarmLease& DevicePool::owned_lease(FarmDeviceState& state, const std::string& owner_id, const std::string& lease_id) const {
    if (js_trim(owner_id).empty()) throw FarmError("owner_required", 403);
    if (!state.lease || state.lease->id != lease_id) throw FarmError("stale_lease", 409);
    if (state.lease->owner_id != owner_id) throw FarmError("lease_owner_mismatch", 403);
    return *state.lease;
}

Json DevicePool::view(const FarmDevice& d, const FarmDeviceState& s) const {
    Json lease = nullptr;
    if (s.lease) lease = Json{{"id", s.lease->id}, {"ownerId", s.lease->owner_id}, {"generation", s.lease->generation}, {"expiresAt", iso_time(s.lease->expires_at)}};
    return Json{{"id", d.id}, {"hostId", d.host_id}, {"platform", d.platform}, {"serial", d.serial}, {"model", d.model}, {"osVersion", d.os_version},
        {"tags", d.tags}, {"availability", device_availability(s)}, {"generation", s.generation},
        {"lastSeenAt", s.last_seen_at ? Json(iso_time(*s.last_seen_at)) : Json(nullptr)}, {"reason", s.reason}, {"lease", lease}};
}

Json DevicePool::change(const std::function<Json(FarmState&, long long)>& action) {
    Json result;
    store_.transaction([&](FarmState& state) {
        const auto now = now_();
        for (const auto& [id, s] : state.devices) {
            bool known = false;
            for (const auto& d : config_.devices) known |= d.id == id;
            if (!known) throw FarmContractError("farm inventory removal requires an explicit state migration");
        }
        for (const auto& d : config_.devices) {
            auto* current = state.find(d.id);
            if (!current) {
                FarmDeviceState fresh;
                fresh.host_id = d.host_id;
                fresh.platform = d.platform;
                fresh.serial = d.serial;
                state.devices.emplace_back(d.id, fresh);
                current = &state.devices.back().second;
            }
            if (current->host_id != d.host_id || current->platform != d.platform || current->serial != d.serial) {
                throw FarmContractError("farm device identity change requires an explicit state migration");
            }
            reconcile_device(*current, now, static_cast<long long>(config_.heartbeat_timeout_sec) * 1000);
        }
        result = action(state, now);
        for (const auto& [id, s] : state.devices) check_consistency(s, id);
    });
    return result;
}

Json DevicePool::list() {
    return change([this](FarmState& state, long long) {
        Json out = Json::array();
        for (const auto& d : config_.devices) out.push_back(view(d, require(state, d.id)));
        return out;
    });
}

Json DevicePool::report(const std::string& host_id, const std::vector<HostObservation>& report) {
    std::vector<const FarmDevice*> devices;
    for (const auto& d : config_.devices) if (d.host_id == host_id) devices.push_back(&d);
    bool known = false;
    for (const auto& h : config_.hosts) known |= h.id == host_id;
    if (!known) throw FarmError("unknown_host", 404);
    std::set<std::string> ids;
    for (const auto& o : report) ids.insert(o.id);
    bool exact = ids.size() == report.size() && ids.size() == devices.size();
    for (const auto* d : devices) exact = exact && ids.contains(d->id);
    if (!exact) throw FarmError("report_must_include_exact_host_inventory", 400);
    return change([&](FarmState& state, long long now) {
        for (const auto& o : report) {
            auto& current = require(state, o.id);
            if (!o.connected && current.connected) fence_device(current, "disconnected");
            current.connected = o.connected;
            current.last_seen_at = now;
            // A heartbeat alone cannot clear quarantine or override a reservation.
            if (current.connected && !current.lease && o.ready_generation == current.generation) {
                current.needs_cleanup = false;
                current.reason = "ready";
            }
        }
        Json out = Json::array();
        for (const auto* d : devices) out.push_back(view(*d, require(state, d->id)));
        return out;
    });
}

Json DevicePool::acquire(const std::string& device_id, const std::string& owner_id, long long ttl_sec) {
    if (ttl_sec < 10 || ttl_sec > 1800) throw FarmError("invalid_body", 400);
    if (js_trim(owner_id).empty()) throw FarmError("owner_required", 403);
    return change([&](FarmState& state, long long now) {
        auto& current = require(state, device_id);
        if (device_availability(current) != "available") throw FarmError("device_unavailable", 409);
        current.generation += 1;
        current.lease = FarmLease{new_id_(), owner_id, current.generation, now + ttl_sec * 1000};
        current.reason = "leased";
        return view(definition(device_id), current);
    });
}

Json DevicePool::renew(const std::string& device_id, const std::string& owner_id, const std::string& lease_id, long long ttl_sec) {
    if (ttl_sec < 10 || ttl_sec > 1800 || !is_uuid(lease_id)) throw FarmError("invalid_body", 400);
    return change([&](FarmState& state, long long now) {
        auto& current = require(state, device_id);
        owned_lease(current, owner_id, lease_id).expires_at = now + ttl_sec * 1000;
        return view(definition(device_id), current);
    });
}

Json DevicePool::release(const std::string& device_id, const std::string& owner_id, const std::string& lease_id) {
    if (!is_uuid(lease_id)) throw FarmError("invalid_body", 400);
    return change([&](FarmState& state, long long) {
        auto& current = require(state, device_id);
        owned_lease(current, owner_id, lease_id);
        fence_device(current, "released");
        return view(definition(device_id), current);
    });
}
}
