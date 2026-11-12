// custos/tests/config.test.ts, apps-redact.test.ts, registry.test.ts + golden (config_cases, redact_cases).
#include "runner_check.hpp"
#include <core/app_config.hpp>
#include <core/app_view.hpp>
#include <core/registry.hpp>

using namespace odeum::runner;
using namespace runner_test;

namespace {
Json app(Json extra = Json::object()) {
    Json a{{"id", "demo"}, {"name", "Demo"}, {"run", {{"cwd", "."}, {"cmd", "x"}}}};
    for (auto& [k, v] : extra.items()) a[k] = v;
    return a;
}
AppConfig parse_one(const Json& a) { return parse_apps_config(Json{{"apps", Json::array({a})}}).apps.at(0); }
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.config", {
        {"appsRootSchema > minimal app passes (run only)", [] {
            auto a = parse_one(Json{{"id", "demo"}, {"name", "Demo"}, {"run", {{"cwd", "."}, {"cmd", "echo"}, {"args", {"hi"}}}}});
            check(a.target == "desktop" && a.input.buttons.empty() && a.input.allow_keyboard, "defaults");
        }},
        {"appsRootSchema > button must specify exactly one of key or action", [] {
            throws([] { parse_one(app({{"input", {{"buttons", {{{"label", "broken"}}}}}}})); }, "neither");
            throws([] { parse_one(app({{"input", {{"buttons", {{{"label", "both"}, {"key", "W"}, {"action", "kill"}}}}}}})); }, "both");
        }},
        {"appsRootSchema > rejects bad id", [] {
            throws([] { parse_one(Json{{"id", "Bad ID with spaces"}, {"name", "x"}, {"run", {{"cwd", "."}, {"cmd", "x"}}}}); }, "bad id");
        }},
        {"appsRootSchema > preserves explicit env / timeout", [] {
            auto a = parse_one(app({{"run", {{"cwd", "."}, {"cmd", "x"}, {"env", {{"FOO", "bar"}}}, {"timeoutSec", 60}}}}));
            check(a.run.env.size() == 1 && a.run.env[0].first == "FOO" && a.run.env[0].second == "bar" && a.run.timeout_sec == 60, "env/timeout");
        }},
        {"appsRootSchema > workingDir is the new explicit name; both workingDir and cwd populate the runtime cwd", [] {
            auto a = parse_one(app({{"run", {{"workingDir", "/tmp/work"}, {"cmd", "x"}}}}));
            check(a.run.working_dir == "/tmp/work", "workingDir");
            check(to_json(a)["run"]["cwd"] == "/tmp/work" && to_json(a)["run"]["workingDir"] == "/tmp/work", "both names");
        }},
        {"appsRootSchema > legacy cwd-only still parses (backward compat)", [] {
            check(parse_one(app({{"run", {{"cwd", "/legacy"}, {"cmd", "x"}}}})).run.working_dir == "/legacy", "cwd");
        }},
        {"appsRootSchema > workingDir wins when both are present", [] {
            check(parse_one(app({{"run", {{"workingDir", "/new"}, {"cwd", "/old"}, {"cmd", "x"}}}})).run.working_dir == "/new", "wins");
        }},
        {"appsRootSchema > missing workingDir AND cwd → reject", [] {
            throws([] { parse_one(app({{"run", {{"cmd", "x"}}}})); }, "missing dir");
        }},
        {"appsRootSchema > build / test も独立した workingDir を持てる", [] {
            auto a = parse_one(app({{"build", {{"workingDir", "/build/dir"}, {"cmd", "cmake"}}}, {"run", {{"workingDir", "/run/dir"}, {"cmd", "x"}}},
                {"test", {{"workingDir", "/test/dir"}, {"cmd", "ctest"}}}}));
            check(a.build->working_dir == "/build/dir" && a.run.working_dir == "/run/dir" && a.test->working_dir == "/test/dir", "dirs");
        }},
        {"inAppBridge / ergoCustos backward-compat > kind defaults and legacy normalisation", [] {
            auto u = parse_one(app({{"inAppBridge", {{"kind", "unity"}}}}));
            check(u.in_app_bridge->port == 17778 && u.in_app_bridge->fps == 24, "unity default");
            auto e = parse_one(app({{"ergoCustos", Json::object()}}));
            check(e.in_app_bridge->kind == "ergo" && e.in_app_bridge->port == 5198 && e.in_app_bridge->video.bitrate == 3'000'000, "ergo legacy");
            auto both = parse_one(app({{"ergoCustos", {{"port", 5198}}}, {"inAppBridge", {{"kind", "unity"}, {"port", 5199}}}}));
            check(both.in_app_bridge->kind == "unity" && both.in_app_bridge->port == 5199, "inAppBridge wins");
            check(!to_json(e).contains("ergoCustos"), "ergoCustos dropped");
            check(!parse_one(app()).in_app_bridge, "no bridge");
        }},
        {"inAppBridge / ergoCustos backward-compat > rejects invalid kind, port and bitrate range", [] {
            throws([] { parse_one(app({{"inAppBridge", {{"kind", "unknown"}}}})); }, "kind");
            throws([] { parse_one(app({{"inAppBridge", {{"kind", "unity"}, {"port", 99999}}}})); }, "port");
            throws([] { parse_one(app({{"inAppBridge", {{"kind", "unity"}, {"video", {{"bitrate", 9'000'000}}}}}})); }, "bitrate");
        }},
        {"loadAppsConfig > duplicate app id", [] {
            throws([] { parse_apps_config(Json{{"apps", Json::array({app(), app()})}}); }, "duplicate");
        }},
        {"golden > config_cases.json matches the zod output", [] {
            for (const auto& c : fixture("config_cases.json")) {
                const auto name = c["name"].get<std::string>();
                std::optional<AppsConfig> parsed;
                try { parsed = parse_apps_config(c["input"]); } catch (const ConfigError&) {}
                check(parsed.has_value() == c["ok"].get<bool>(), "acceptance differs: " + name);
                if (!parsed) continue;
                Json apps = Json::array();
                for (const auto& a : parsed->apps) apps.push_back(to_json(a));
                check_json(Json{{"version", 1}, {"apps", apps}}, c["output"], "output differs: " + name);
            }
        }},
        {"apps-redact > golden redact_cases.json", [] {
            for (const auto& c : fixture("redact_cases.json")) {
                auto a = parse_apps_config(c["input"]).apps.at(0);
                check_json(redact(a), c["output"], "redact differs: " + c["name"].get<std::string>());
            }
        }},
        {"apps-redact > host, port, env and cwd never appear", [] {
            auto a = parse_one(app({{"inAppBridge", {{"kind", "unity"}, {"host", "10.0.0.9"}, {"port", 9999}}},
                {"run", {{"cwd", "/secret/dir"}, {"cmd", "x"}, {"env", {{"TOKEN", "value"}}}}}}));
            const auto text = redact(a).dump();
            check(text.find("10.0.0.9") == std::string::npos && text.find("9999") == std::string::npos, "bridge address hidden");
            check(text.find("/secret/dir") == std::string::npos && text.find("TOKEN") == std::string::npos, "command hidden");
        }},
        {"AppsRegistry > initial idle status and lifecycle transitions notify listeners", [] {
            long long now = 100;
            AppsRegistry registry(parse_apps_config(Json{{"apps", Json::array({app()})}}).apps, [&] { return now; });
            check(registry.status("demo")->lifecycle == Lifecycle::idle && registry.status("demo")->last_change_at == 100, "initial");
            int events = 0;
            registry.on_status_changed([&](const std::string& id, const AppStatus& s) { ++events; check(id == "demo" && s.lifecycle == Lifecycle::running, "event"); });
            now = 200;
            StatusPatch patch;
            patch.pid = 42LL;
            check(registry.set_lifecycle("demo", Lifecycle::running, patch), "known app");
            check(!registry.set_lifecycle("nope", Lifecycle::running), "unknown app");
            check(events == 1 && registry.status("demo")->pid == 42 && registry.status("demo")->last_change_at == 200, "merged");
            check(to_json(*registry.status("demo"))["lastExitCode"].is_null(), "null exit code");
        }},
        {"boundary > apps.json budget: depth and size", [] {
            std::string deep(40, '[');
            deep += std::string(40, ']');
            throws([&] { parse_json(deep, config_file_limits); }, "depth 40 > 32");
            std::string ok_depth(32, '[');
            ok_depth += std::string(32, ']');
            parse_json(ok_depth, config_file_limits);
            std::string big(config_file_limits.bytes + 1, ' ');
            throws([&] { parse_json(big, config_file_limits); }, "size + 1");
            std::string exact = "\"" + std::string(config_file_limits.bytes - 2, 'a') + "\"";
            parse_json(exact, config_file_limits);
        }},
    });
}
