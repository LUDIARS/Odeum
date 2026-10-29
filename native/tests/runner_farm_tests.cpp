// custos/tests/farm-pool.test.ts, farm-store.test.ts, farm-host-auth.test.ts + golden (farm_cases).
// farm-routes.test.ts is covered through the HTTP suite (runner_http_tests.cpp).
#include "runner_check.hpp"
#include <adapters/farm_store.hpp>
#include <core/farm_host_auth.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>

using namespace odeum::runner;
using namespace runner_test;
namespace fs = std::filesystem;

namespace {
class MemoryStore final : public FarmStore {
public:
    std::string text = R"({"version":1,"devices":{}})";
    void transaction(const std::function<void(FarmState&)>& action) override {
        auto state = parse_farm_state(Json::parse(text));
        const auto before = dump(to_json(state));
        action(state);
        const auto after = dump(to_json(state));
        if (after != before) text = after;
    }
};

std::string read(const fs::path& p) { std::ifstream in(p); std::stringstream s; s << in.rdbuf(); return s.str(); }

struct TempState {
    fs::path dir, path;
    TempState() {
        static int n = 0;
        dir = fs::temp_directory_path() / ("odeum-farm-" + std::to_string(::getpid()) + "-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::create_directories(dir);
        path = dir / "state.json";
        std::ofstream(path) << R"({"version":1,"devices":{}})";
    }
    ~TempState() { fs::remove_all(dir); }
};

FarmConfig config() {
    return parse_farm_config(Json::parse(R"({"version":1,"heartbeatTimeoutSec":30,"hosts":[{"id":"host","tokenEnv":"HOST_TOKEN"}],"devices":[
        {"id":"pixel","hostId":"host","platform":"android","serial":"a","model":"Pixel","osVersion":"16"},
        {"id":"iphone","hostId":"host","platform":"ios","serial":"b","model":"iPhone","osVersion":"26"}]})"));
}
std::vector<HostObservation> ready(long long generation = 1) { return {{"pixel", true, generation}, {"iphone", true, 1}}; }

template<class F> std::string farm_code(F action) {
    try { action(); } catch (const FarmError& e) { return e.code(); } catch (const std::exception& e) { return std::string("other:") + e.what(); }
    return "none";
}
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.farm", {
        {"golden > DevicePool operation sequence and committed state", [] {
            const auto g = fixture("farm_cases.json");
            MemoryStore store;
            long long now = 1'000'000;
            int ids = 0;
            auto make_id = [&] { char buf[40]; std::snprintf(buf, sizeof buf, "00000000-0000-4000-8000-%012d", ++ids); return std::string(buf); };
            DevicePool pool(parse_farm_config(g["config"]), store, [&] { return now; }, make_id);
            for (const auto& step : g["steps"]) {
                if (step.contains("advance")) now += step["advance"].get<long long>();
                const auto op = step["op"].get<std::string>();
                const auto& a = step["args"];
                Json value;
                std::string error;
                try {
                    if (op == "list") value = pool.list();
                    if (op == "report") value = pool.report(a[0], parse_host_report(a[1]));
                    if (op == "acquire") value = pool.acquire(a[0], a[1], a[2]);
                    if (op == "renew") value = pool.renew(a[0], a[1], a[2], a[3]);
                    if (op == "release") value = pool.release(a[0], a[1], a[2]);
                } catch (const FarmError& e) { error = e.code(); }
                check(error.empty() == step["ok"].get<bool>(), op + " outcome " + error + " " + step.dump());
                if (!error.empty()) check(error == step["error"].get<std::string>(), "error code " + error);
                else check_json(value, step["value"], "value of " + op);
                check_json(Json::parse(store.text), step["state"], "state after " + op);
            }
        }},
        {"golden > configuration and request body contracts", [] {
            const auto g = fixture("farm_cases.json");
            for (const auto& c : g["schema"]) {
                std::optional<FarmConfig> parsed;
                try { parsed = parse_farm_config(c["input"]); } catch (const FarmContractError&) {}
                check(parsed.has_value() == c["ok"].get<bool>(), "config " + c["input"].dump());
                if (parsed) check(parsed->devices[0].serial == "a" && parsed->devices[0].tags.at(0) == "t1", "text fields trimmed");
            }
            auto accepts = [](const std::function<void()>& f) { try { f(); return true; } catch (const FarmError&) { return false; } };
            for (const auto& c : g["bodies"]["report"]) check(accepts([&] { parse_host_report(c["body"]); }) == c["ok"].get<bool>(), "report " + c.dump());
            for (const auto& c : g["bodies"]["acquire"]) check(accepts([&] { parse_acquire(c["body"]); }) == c["ok"].get<bool>(), "acquire " + c.dump());
            for (const auto& c : g["bodies"]["release"]) check(accepts([&] { parse_release(c["body"]); }) == c["ok"].get<bool>(), "release " + c.dump());
            for (const auto& c : g["bodies"]["renew"]) check(accepts([&] { parse_renew(c["body"]); }) == c["ok"].get<bool>(), "renew " + c.dump());
        }},
        {"self-hosted device pool > two coordinators cannot reserve the same device; both platforms share the pool", [] {
            TempState t;
            long long now = 1'000'000;
            FileFarmStore store(t.path.string()), second_store(t.path.string());
            DevicePool pool(config(), store, [&] { return now; }, [] { static int i = 0; char b[40]; std::snprintf(b, sizeof b, "00000000-0000-4000-8000-%012d", ++i); return std::string(b); });
            pool.report("host", ready());
            DevicePool second(config(), second_store, [&] { return now; }, [] { return std::string("00000000-0000-4000-8000-999999999999"); });
            pool.acquire("pixel", "alice", 60);
            check(farm_code([&] { second.acquire("pixel", "bob", 60); }) == "device_unavailable", "same device");
            check(second.acquire("iphone", "bob", 60)["lease"]["ownerId"] == "bob", "other platform");
        }},
        {"self-hosted device pool > changing an identity never transfers an existing reservation", [] {
            TempState t;
            FileFarmStore store(t.path.string());
            long long now = 1'000'000;
            DevicePool pool(config(), store, [&] { return now; }, [] { return std::string("00000000-0000-4000-8000-000000000001"); });
            pool.report("host", ready());
            pool.acquire("pixel", "alice", 60);
            auto changed = config();
            for (auto& d : changed.devices) d.serial += "-new";
            bool refused = false;
            try { DevicePool again(changed, store, [&] { return now; }, [] { return std::string(); }); } catch (const FarmContractError& e) { refused = std::string(e.what()).find("identity change") != std::string::npos; }
            check(refused, "identity change refused");
        }},
        {"farm store > a lock held by another writer is not stolen or deleted", [] {
            TempState t;
            FileFarmStore store(t.path.string());
            std::ofstream(t.path.string() + ".lock") << "other writer";
            check(farm_code([&] { store.transaction([](FarmState&) {}); }) == "farm_store_locked", "locked");
            check(read(t.path.string() + ".lock") == "other writer", "lock untouched");
        }},
        {"farm store > corrupt or missing state fails closed and releases its own lock", [] {
            TempState t;
            FileFarmStore store(t.path.string());
            std::ofstream(t.path) << "broken";
            check(farm_code([&] { store.transaction([](FarmState&) {}); }) != "none", "corrupt refused");
            check(read(t.path) == "broken" && !fs::exists(t.path.string() + ".lock"), "unchanged, lock released");
            fs::remove(t.path);
            check(farm_code([&] { store.transaction([](FarmState&) {}); }) != "none" && !fs::exists(t.path), "missing refused, not recreated");
        }},
        {"farm store > failed transactions never publish partial state", [] {
            TempState t;
            FileFarmStore store(t.path.string());
            const auto before = read(t.path);
            check(farm_code([&] { store.transaction([](FarmState& s) { s.devices.emplace_back("x", FarmDeviceState{}); throw std::runtime_error("failed"); }); }) == "other:failed", "propagates");
            check(read(t.path) == before && !fs::exists(t.path.string() + ".lock"), "unchanged");
        }},
        {"farm store > unexplained pending writes prevent publishing a new reservation", [] {
            TempState t;
            FileFarmStore store(t.path.string());
            std::ofstream(t.path.string() + ".pending") << "crash residue";
            check(farm_code([&] { store.transaction([](FarmState& s) {
                FarmDeviceState d;
                d.host_id = "host"; d.platform = "android"; d.serial = "a";
                s.devices.emplace_back("pixel", d);
            }); }) != "none", "refused");
            check(read(t.path.string() + ".pending") == "crash residue" && Json::parse(read(t.path))["devices"].empty(), "nothing published");
            check(!fs::exists(t.path.string() + ".lock"), "lock released");
        }},
        {"farm-host-auth > host credentials are distinct, required and restricted to one host", [] {
            const std::vector<FarmHost> hosts{{"a", "HOST_A"}, {"b", "HOST_B"}};
            const std::string a(40, 'a'), b(40, 'b');
            auto env = [&](const char* name) -> std::optional<std::string> { if (std::string(name) == "HOST_A") return a; if (std::string(name) == "HOST_B") return b; return std::nullopt; };
            FarmHostAuth auth(hosts, env);
            check(auth.verify("a", "Bearer " + a) && !auth.verify("b", "Bearer " + a) && !auth.verify("unknown", "Bearer " + a), "per host");
            check(!auth.verify("a", "Bearer invalid") && !auth.verify("a", "bearer " + a) && !auth.verify("a", "Bearer  " + a), "format");
            throws([&] { FarmHostAuth none(hosts, [](const char*) { return std::optional<std::string>(); }); }, "missing");
            throws([&] { FarmHostAuth same(hosts, [&](const char*) { return std::optional<std::string>(a); }); }, "distinct");
            throws([&] { FarmHostAuth spaced(hosts, [&](const char* n) { return std::optional<std::string>(std::string(n) == "HOST_A" ? " " + a : b); }); }, "whitespace");
            throws([&] { FarmHostAuth short_token(hosts, [&](const char* n) { return std::optional<std::string>(std::string(n) == "HOST_A" ? std::string(31, 'x') : b); }); }, "31 characters");
            FarmHostAuth exact(hosts, [&](const char* n) { return std::optional<std::string>(std::string(n) == "HOST_A" ? std::string(32, 'x') : std::string(512, 'y')); });
            check(exact.verify("a", "Bearer " + std::string(32, 'x')) && exact.verify("b", "Bearer " + std::string(512, 'y')), "32 and 512 accepted");
            check(!exact.verify("b", "Bearer " + std::string(513, 'y')), "513 rejected");
        }},
    });
}
