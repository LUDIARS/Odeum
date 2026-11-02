// custos/tests/unity-bridge.test.ts + golden (unity_cases) + the /api/unity proxy and bridge client
// against a fake bridge.
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/bridge_client.hpp>
#include <app/unity_proxy.hpp>
#include <core/unityctl.hpp>

using namespace odeum::runner;
using namespace runner_test;

namespace {
AppConfig unity_app(const std::string& id, int port) {
    return parse_apps_config(Json{{"apps", Json::array({Json{{"id", id}, {"name", id}, {"run", {{"cwd", "."}, {"cmd", "x"}}},
        {"inAppBridge", {{"kind", "unity"}, {"port", port}}}}})}}).apps.at(0);
}
Json args_json(const UnityctlArgs& a) {
    return Json{{"command", a.command}, {"host", a.host}, {"port", a.port ? Json(*a.port) : Json(nullptr)}, {"token", a.token}, {"json", a.json},
        {"wait", a.wait}, {"since", a.since}, {"level", a.level}, {"limit", a.limit}, {"depth", a.depth}, {"root", a.root}, {"caption", a.caption}, {"source", a.source}};
}
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.unity", {
        {"parseUnityctlArgs > golden argument parsing and errors", [] {
            for (const auto& c : Json(fixture("unity_cases.json")["args"])) {
                std::vector<std::string> argv;
                for (const auto& a : c["argv"]) argv.push_back(a);
                if (c["ok"].get<bool>()) check_json(args_json(parse_unityctl_args(argv)), c["value"], "args " + c["argv"].dump());
                else {
                    std::string message;
                    try { parse_unityctl_args(argv); } catch (const UnityctlArgError& e) { message = e.what(); }
                    check(message == c["error"].get<std::string>(), "error " + message + " vs " + c["error"].dump());
                }
            }
        }},
        {"unityctl render > golden status, compile, log and JSON rendering", [] {
            const auto r = fixture("unity_cases.json")["render"];
            const Json status{{"unityVersion", "6000.0.1f1"}, {"projectPath", "/p"}, {"isPlaying", true}, {"isPaused", true}, {"isCompiling", true}, {"isReloading", true},
                {"activeScene", ""}, {"dirtyScenes", {"A", "B"}}};
            auto edit = status;
            edit["isPlaying"] = false; edit["isCompiling"] = false; edit["isReloading"] = false; edit["activeScene"] = "Main"; edit["dirtyScenes"] = Json::array();
            check(render_status(status) == r["status"].get<std::string>() && render_status(edit) == r["statusEdit"].get<std::string>(), "status");
            const Json compile{{"isCompiling", false}, {"errorCount", 1}, {"warningCount", 1}, {"finishedAt", ""},
                {"errors", {{{"file", "Assets/A.cs"}, {"line", 12}, {"column", 3}, {"message", "boom"}, {"assembly", "Game.Editor"}}}},
                {"warnings", {{{"file", ""}, {"line", 0}, {"column", 0}, {"message", "w"}, {"assembly", ""}}}}};
            check(render_compile_status(compile) == r["compile"].get<std::string>(), "compile " + render_compile_status(compile));
            auto compiling = compile; compiling["isCompiling"] = true;
            auto clean = compile; clean["errorCount"] = 0; clean["warningCount"] = 0;
            check(render_compile_status(compiling) == r["compiling"].get<std::string>() && render_compile_status(clean) == r["clean"].get<std::string>(), "states");
            check(compile_exit_code(compile) == r["exit"][0] && compile_exit_code(clean) == r["exit"][1], "exit codes");
            const Json log{{"entries", {{{"seq", 1}, {"ts", "t"}, {"level", "error"}, {"message", "m"}, {"stackTrace", ""}}, {{"seq", 2}, {"ts", "u"}, {"level", "log"}, {"message", "n"}, {"stackTrace", ""}}}}, {"nextSeq", 3}};
            check(render_log(log) == r["log"].get<std::string>() && render_log(Json{{"entries", Json::array()}, {"nextSeq", 9}}) == r["emptyLog"].get<std::string>(), "log");
            check(render_json(Json{{"a", {1, {{"b", "c"}}}}, {"d", Json::object()}, {"e", Json::array()}, {"f", "日本"}}) == r["json"].get<std::string>(), "JSON.stringify(v, null, 2)");
        }},
        {"nextCompileWaitPhase / parseBridgePortFile / resolveBridgeTarget > golden", [] {
            const auto g = fixture("unity_cases.json");
            const char* names[] = {"waiting", "done", "unreachable"};
            for (const auto& c : g["phases"]) {
                CompileWaitState s{c["ever"], c["ms"], 90'000};
                check(names[static_cast<int>(next_compile_wait_phase(s, c["reachable"], c["compiling"]))] == c["phase"].get<std::string>(), "phase " + c.dump());
            }
            for (const auto& c : g["ports"]) {
                const auto port = parse_bridge_port_file(c["text"].get<std::string>());
                check(port ? Json(*port) == c["port"] : c["port"].is_null(), "port " + c.dump());
            }
            std::vector<AppConfig> apps{unity_app("u", 17778)};
            auto ergo = unity_app("e", 5201); ergo.in_app_bridge->kind = "ergo";
            auto plain = unity_app("plain", 1); plain.in_app_bridge.reset();
            auto h = unity_app("h", 9); h.in_app_bridge->host = "";
            apps.push_back(ergo); apps.push_back(plain); apps.push_back(h);
            for (const auto& c : g["targets"]) {
                const auto t = resolve_bridge_target(apps, c["id"]);
                const Json actual = t ? Json{{"host", t->host}, {"port", t->port}} : Json(nullptr);
                check_json(actual, c["target"], "target " + c.dump());
            }
        }},
        {"unity-routes > proxies up / down / busy and refuses spectators", [] {
            boost::asio::io_context io;
            int status_code = 200;
            FakeHttpServer bridge(io, [&](const FakeHttpServer::Request& r) {
                if (r.target() == "/editor/status") return FakeResponse{static_cast<unsigned>(status_code), status_code == 200 ? R"({"isPlaying":false})" : "main thread busy", {}, {}};
                return FakeResponse{200, "", {}, {}};
            });
            AppsRegistry registry({unity_app("u", bridge.port()), [] { auto a = unity_app("e", 1); a.in_app_bridge->kind = "ergo"; return a; }()}, [] { return 0LL; });
            ControlLease lease;
            UnityProxy proxy(registry, lease, [&io](const BridgeTarget& t) { return std::make_shared<UnityBridgeClient>(io, BridgeEndpoint{t.host, t.port}); });
            auto call = [&](const std::string& method, const std::string& app, const std::string& action, std::optional<std::string> client = std::nullopt) {
                std::optional<ServerResponse> response;
                ServerRequest request;
                request.method = method;
                if (client) request.headers["x-custos-client"] = *client;
                check(proxy.handle(request, app, action, [&](ServerResponse r) { response = std::move(r); }), "route matched");
                run_until(io, [&] { return response.has_value(); });
                return *response;
            };
            auto up = call("GET", "u", "status");
            check(up.status == 200 && up.body == R"({"bridge":"up","data":{"isPlaying":false}})", "up " + up.body);
            status_code = 504;
            auto busy = call("GET", "u", "status");
            check(busy.status == 504 && busy.body == R"({"bridge":"busy","error":"main thread busy"})", "busy " + busy.body);
            check(call("GET", "e", "status").status == 404, "ergo bridges are not Unity");
            auto compile = call("POST", "u", "compile");
            check(compile.body == R"({"bridge":"up","data":{"requested":true}})", "compile wraps the result");
            lease.acquire("u", "player");
            auto refused = call("POST", "u", "play", std::string("spectator"));
            check(refused.status == 409 && refused.body.find("spectating") != std::string::npos, "spectator refused");
            check(call("POST", "u", "play", std::string("player")).status == 200, "player allowed");
            check(call("GET", "u", "status", std::string("spectator")).status == 504, "reads stay open to spectators");
            bridge.close();
            AppsRegistry gone({unity_app("u", 1)}, [] { return 0LL; });
            UnityProxy down_proxy(gone, lease, [&io](const BridgeTarget& t) { return std::make_shared<UnityBridgeClient>(io, BridgeEndpoint{t.host, t.port}); });
            std::optional<ServerResponse> down;
            ServerRequest get;
            get.method = "GET";
            down_proxy.handle(get, "u", "health", [&](ServerResponse r) { down = std::move(r); });
            run_until(io, [&] { return down.has_value(); });
            check(down->status == 200 && down->body.find("\"bridge\":\"down\"") != std::string::npos && down->body.find("Unity bridge unreachable at http://127.0.0.1:1") != std::string::npos, "down " + down->body);
        }},
        {"unity-routes > hierarchy and log queries are forwarded as before", [] {
            boost::asio::io_context io;
            FakeHttpServer bridge(io, [](const FakeHttpServer::Request&) { return FakeResponse{200, "{}", {}, {}}; });
            AppsRegistry registry({unity_app("u", bridge.port())}, [] { return 0LL; });
            ControlLease lease;
            UnityProxy proxy(registry, lease, [&io](const BridgeTarget& t) { return std::make_shared<UnityBridgeClient>(io, BridgeEndpoint{t.host, t.port}); });
            auto get = [&](const std::string& action, std::map<std::string, std::string> query) {
                std::optional<ServerResponse> response;
                ServerRequest request;
                request.method = "GET";
                request.query = std::move(query);
                proxy.handle(request, "u", action, [&](ServerResponse r) { response = std::move(r); });
                run_until(io, [&] { return response.has_value(); });
            };
            get("hierarchy", {});
            get("hierarchy", {{"depth", "abc"}, {"root", "Main Camera"}});
            get("log", {{"since", "5"}, {"level", "error"}});
            get("log", {{"limit", ""}});
            check(bridge.seen.at(0) == "GET /editor/hierarchy?depth=2", bridge.seen.at(0));
            check(bridge.seen.at(1) == "GET /editor/hierarchy?depth=2&root=Main+Camera", bridge.seen.at(1));
            check(bridge.seen.at(2) == "GET /editor/log?since=5&limit=200&level=error", bridge.seen.at(2));
            check(bridge.seen.at(3) == "GET /editor/log?since=0&limit=0", bridge.seen.at(3));
        }},
        {"ergo-custos-client > /screenshot must be a PNG; /pointer 404 is unsupported", [] {
            boost::asio::io_context io;
            FakeHttpServer bridge(io, [](const FakeHttpServer::Request& r) {
                if (r.target() == "/screenshot") return FakeResponse{200, "not a png", {}, {}};
                if (r.target() == "/pointer") return FakeResponse{404, "", {}, {}};
                return FakeResponse{204, "", {}, {}};
            });
            const BridgeEndpoint ep{"127.0.0.1", bridge.port()};
            std::optional<std::variant<std::string, BridgeCallError>> shot;
            bridge_fetch_screenshot(io, ep, [&](auto r) { shot = std::move(r); });
            std::optional<std::optional<BridgeCallError>> pointer, key;
            bridge_send_pointer(io, ep, 0.5, 0.25, "middle", "down", [&](auto r) { pointer = r; });
            bridge_send_key(io, ep, 44, true, [&](auto r) { key = r; });
            run_until(io, [&] { return shot && pointer && key; });
            check(std::get<BridgeCallError>(*shot).message == "in-app bridge /screenshot did not return a PNG", "png check");
            check(pointer->has_value() && (*pointer)->kind == BridgeCallError::Kind::unsupported, "pointer unsupported");
            check(!key->has_value(), "204 is success");
            check(std::find(bridge.bodies.begin(), bridge.bodies.end(), R"({"x":0.5,"y":0.25,"button":2,"phase":"down"})") != bridge.bodies.end(), "pointer body");
            check(std::find(bridge.bodies.begin(), bridge.bodies.end(), R"({"code":44,"down":true})") != bridge.bodies.end(), "key body");
        }},
    });
}
