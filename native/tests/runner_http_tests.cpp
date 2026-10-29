// The assembled server over real sockets: custos/tests/farm-routes.test.ts, the playlog / settings /
// apps routes, the WebSocket broker (audience-broker.test.ts, control), static serving, and the
// boundary / security checks added by the port (body and header limits, Host / Origin).
#include "runner_check.hpp"
#include "runner_fake_http.hpp"
#include <adapters/clock.hpp>
#include <adapters/farm_store.hpp>
#include <adapters/playlog_files.hpp>
#include <app/api_router.hpp>
#include <app/http_server.hpp>
#include <app/ws_broker.hpp>
#include <boost/beast/websocket.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <unistd.h>

using namespace odeum::runner;
using namespace runner_test;
namespace beast = boost::beast;
namespace http = beast::http;
namespace fs = std::filesystem;

namespace {
struct Options {
    AuthSettings auth;
    bool farm = false;
    bool loopback_policy = true;
};

struct Runner {
    boost::asio::io_context io;
    fs::path dir;
    std::unique_ptr<FakeHttpServer> cernere;
    std::unique_ptr<AppsRegistry> registry;
    std::unique_ptr<AppsRunner> runner;
    std::unique_ptr<CernereAuth> auth;
    std::unique_ptr<MediaTools> media;
    std::unique_ptr<ScreenshotSource> screenshots;
    RuntimeStreamPrefs prefs;
    std::unique_ptr<ScreenshotStreamer> streamer;
    ControlLease control;
    std::unique_ptr<AudienceRoom> audience;
    std::unique_ptr<InputForwarder> input;
    std::unique_ptr<PlayLogRecorder> play_log;
    std::unique_ptr<IceServerProvider> ice;
    std::unique_ptr<UnityProxy> unity;
    std::unique_ptr<FarmRuntime> farm;
    std::unique_ptr<ApiRouter> router;
    std::unique_ptr<WsBroker> broker;
    std::unique_ptr<HttpServer> server;
    std::uint16_t port = 0, second_port = 0;
    std::thread thread;
    std::string farm_token = std::string(40, 'x');

    explicit Runner(Options o) {
        static int n = 0;
        dir = fs::temp_directory_path() / ("odeum-http-" + std::to_string(::getpid()) + "-" + std::to_string(++n));
        fs::remove_all(dir);
        fs::create_directories(dir / "plays" / "demo");
        fs::create_directories(dir / "gaze" / "wasm");
        std::ofstream(dir / "plays" / "demo" / "20261011-000000-abcdef.jsonl") << "{\"seq\":0}\n";
        std::ofstream(dir / "gaze" / "NOTICE.txt") << "notice";
        std::ofstream(dir / "gaze" / "secret.txt") << "secret";
        if (o.farm) {
            cernere = std::make_unique<FakeHttpServer>(io, [](const FakeHttpServer::Request& r) {
                const auto auth = std::string(r[http::field::authorization]);
                if (auth == "Bearer alice" || auth == "Bearer bob") return FakeResponse{200, R"({"user":{"id":")" + auth.substr(7) + R"("}})", {}, {}};
                return FakeResponse{401, "{}", {}, {}};
            });
            o.auth.cernere_url = "http://127.0.0.1:" + std::to_string(cernere->port());
        }
        auto apps = parse_apps_config(Json::parse(R"({"apps":[{"id":"demo","name":"Demo","run":{"cwd":".","cmd":"sh","args":["-c","echo hi"]},
            "build":{"cwd":".","cmd":"sh","args":["-c","sleep 5"]},"input":{"buttons":[{"label":"Big Jump","key":"Space"},{"label":"Quit","action":"kill"}]},"playLog":{}}]})"));
        registry = std::make_unique<AppsRegistry>(apps.apps, epoch_ms);
        runner = std::make_unique<AppsRunner>(io, *registry, [this](const ProcessSpec& s, ChildProcess::Handlers h) { return ChildProcess::spawn(io, s, std::move(h)); }, random_uuid);
        auth = std::make_unique<CernereAuth>(io, o.auth, epoch_ms);
        media = std::make_unique<MediaTools>(io, "ffmpeg-missing-for-tests", CaptureHost{Platform::linux_os, ":0", ""}, "x264");
        screenshots = make_screenshot_source(io, *media);
        streamer = std::make_unique<ScreenshotStreamer>(io, *screenshots, prefs, std::nullopt, epoch_ms);
        audience = std::make_unique<AudienceRoom>(epoch_ms, random_uuid);
        input = std::make_unique<InputForwarder>(io, make_desktop_input(), "adb");
        play_log = std::make_unique<PlayLogRecorder>(io, PlayLogDeps{(dir / "plays").string(), [this](const std::string& id) { return registry->config(id); },
            [](const std::string& u) { return hash_player("salt", u); }, epoch_ms, [](const std::string& p) { return open_play_log_file(p); }, random_below,
            [this](const BridgeEndpoint& ep, auto done) { bridge_fetch_state(io, ep, std::move(done)); }});
        ice = std::make_unique<IceServerProvider>(io, std::nullopt, "", "", epoch_ms);
        unity = std::make_unique<UnityProxy>(*registry, control, [this](const BridgeTarget& t) { return std::make_shared<UnityBridgeClient>(io, BridgeEndpoint{t.host, t.port}); });
        if (o.farm) {
            std::ofstream(dir / "state.json") << R"({"version":1,"devices":{}})";
            farm = std::make_unique<FarmRuntime>();
            auto config = parse_farm_config(Json::parse(R"({"version":1,"hosts":[{"id":"host","tokenEnv":"HOST_TOKEN"}],
                "devices":[{"id":"pixel","hostId":"host","platform":"android","serial":"a","model":"Pixel","osVersion":"16"}]})"));
            farm->host_auth = std::make_unique<FarmHostAuth>(config.hosts, [this](const char*) { return std::optional<std::string>(farm_token); });
            farm->store = std::make_unique<FileFarmStore>((dir / "state.json").string());
            farm->pool = std::make_unique<DevicePool>(std::move(config), *farm->store, epoch_ms, random_uuid);
        }
        router = std::make_unique<ApiRouter>(RouterDeps{*registry, *runner, prefs, *auth, *unity, *media, *screenshots, *ice, nullptr, farm.get(),
            (dir / "plays").string(), (dir / "gaze").string(), false});
        broker = std::make_unique<WsBroker>(WsBrokerDeps{*registry, *runner, *streamer, play_log.get(), control, *audience, *input, *auth, epoch_ms, random_uuid});
        WebAccessPolicy access{o.loopback_policy, {}, {}, "127.0.0.1"};
        ServerLimits limits;
        limits.ws_messages_per_second = 50;
        server = std::make_unique<HttpServer>(io, *router, *broker, access, limits);
        port = server->listen("127.0.0.1", 0, "backend");
        second_port = server->listen("127.0.0.1", 0, "frontend");
        thread = std::thread([this] { io.run(); });
    }
    ~Runner() {
        boost::asio::post(io, [this] { server->stop(); play_log->shutdown(); runner->shutdown(); streamer->shutdown(); io.stop(); });
        thread.join();
        fs::remove_all(dir);
    }
};

struct Reply {
    unsigned status;
    std::string body;
    std::map<std::string, std::string> headers;
    std::string header(const std::string& name) const { auto it = headers.find(name); return it == headers.end() ? std::string() : it->second; }
};

Reply request(std::uint16_t port, const std::string& method, const std::string& target, const std::string& body = {},
    std::vector<std::pair<std::string, std::string>> headers = {}) {
    boost::asio::io_context io;
    beast::tcp_stream stream(io);
    stream.connect({boost::asio::ip::make_address("127.0.0.1"), port});
    http::request<http::string_body> req(http::string_to_verb(method), target, 11);
    req.set(http::field::host, "127.0.0.1:" + std::to_string(port));
    for (const auto& [k, v] : headers) req.set(k, v);
    if (!body.empty() || method == "POST" || method == "PUT") { req.body() = body; req.prepare_payload(); }
    http::write(stream, req);
    beast::flat_buffer buffer;
    http::response_parser<http::string_body> parser;
    parser.body_limit(64 * 1024 * 1024);
    beast::error_code error;
    http::read(stream, buffer, parser, error);
    Reply out{parser.get().result_int(), parser.get().body(), {}};
    for (const auto& f : parser.get()) {
        std::string name(f.name_string());
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        out.headers[name] = std::string(f.value());
    }
    return out;
}

Json json(const Reply& r) { return Json::parse(r.body); }

struct WsClient {
    boost::asio::io_context io;
    beast::websocket::stream<beast::tcp_stream> ws{io};
    WsClient(std::uint16_t port, const std::string& target = "/ws", const std::string& origin = {}) {
        beast::get_lowest_layer(ws).connect({boost::asio::ip::make_address("127.0.0.1"), port});
        ws.set_option(beast::websocket::stream_base::decorator([origin](beast::websocket::request_type& r) { if (!origin.empty()) r.set(http::field::origin, origin); }));
        ws.handshake("127.0.0.1:" + std::to_string(port), target);
        beast::get_lowest_layer(ws).expires_never();
    }
    void send(const std::string& text) { ws.text(true); ws.write(boost::asio::buffer(text)); }
    // Next message, or the close code as {"closed": code}.
    Json next(std::chrono::milliseconds timeout = std::chrono::milliseconds(3000)) {
        beast::get_lowest_layer(ws).expires_after(timeout);
        beast::flat_buffer buffer;
        beast::error_code error;
        ws.read(buffer, error);
        if (error) return Json{{"closed", ws.reason().code}};
        return Json::parse(beast::buffers_to_string(buffer.data()));
    }
    Json until(const std::string& type) {
        for (int i = 0; i < 50; ++i) { auto m = next(); if (m.contains("closed") || m["type"] == type) return m; }
        return Json{{"missing", type}};
    }
};
}

int main(int argc, char** argv) {
    return run_all(argc, argv, "runner.http", {
        {"app > /api/health on both ports, no-store headers, unknown paths 404", [] {
            Runner r({});
            for (auto port : {r.port, r.second_port}) {
                const auto health = request(port, "GET", "/api/health");
                check(health.status == 200 && health.body == R"({"status":"ok","service":"custos","version":"0.1.0","apps":1})", "health " + health.body);
                check(health.header("cache-control") == "no-store, no-cache, must-revalidate" && health.header("pragma") == "no-cache", "cache headers");
            }
            const auto missing = request(r.port, "GET", "/nope");
            check(missing.status == 404 && missing.body == "404 Not Found", "404");
            check(request(r.port, "GET", "/api/apps/demo/build").status == 404, "wrong method falls through to 404");
        }},
        {"static > embedded UI, gaze asset allowlist, no traversal", [] {
            Runner r({});
            const auto index = request(r.port, "GET", "/");
            check(index.status == 200 && index.header("content-type") == "text/html; charset=utf-8" && index.body.find("<html") != std::string::npos, "index");
            check(request(r.port, "GET", "/index.html").body == index.body, "index.html");
            check(request(r.port, "GET", "/js/app.js").header("content-type") == "text/javascript; charset=utf-8", "js");
            check(request(r.port, "GET", "/gaze-assets/NOTICE.txt").body == "notice", "allowlisted gaze file");
            check(request(r.port, "GET", "/gaze-assets/secret.txt").status == 404, "other files unreachable");
            check(request(r.port, "GET", "/gaze-assets/../gaze/NOTICE.txt").status == 404 && request(r.port, "GET", "/gaze-assets/%2e%2e/secret.txt").status == 404, "traversal");
            check(request(r.port, "GET", "/gaze-assets/vision_bundle.mjs").status == 404, "allowlisted but absent");
            check(request(r.port, "POST", "/js/app.js").status == 404, "static is GET only");
        }},
        {"apps-routes > list, detail, status, build/run conflicts, kill and unknown apps", [] {
            Runner r({});
            const auto list = json(request(r.port, "GET", "/api/apps"));
            check(list["apps"].size() == 1 && list["apps"][0]["config"]["id"] == "demo" && !list["apps"][0]["config"].contains("run"), "redacted list");
            check(json(request(r.port, "GET", "/api/apps/demo"))["status"]["lifecycle"] == "idle", "detail");
            check(request(r.port, "GET", "/api/apps/nope/status").body == R"({"error":"unknown app"})", "unknown");
            const auto build = json(request(r.port, "POST", "/api/apps/demo/build"));
            check(build["ok"] == true && build["buildId"].is_string(), "build started");
            const auto again = request(r.port, "POST", "/api/apps/demo/build");
            check(again.status == 409 && json(again)["error"] == "build already running for demo", "409");
            check(request(r.port, "POST", "/api/apps/demo/test").status == 409 && json(request(r.port, "POST", "/api/apps/demo/test"))["error"] == "app demo has no test command", "no test");
            check(json(request(r.port, "POST", "/api/apps/demo/kill"))["ok"] == false, "no run to kill");
            check(request(r.port, "POST", "/api/apps/demo/screenshot").status == 400, "no capture config");
            check(request(r.port, "POST", "/api/apps/demo/rtc/offer", R"({"sdp":"v=0"})").status == 400, "offer needs capture or bridge");
            check(request(r.port, "POST", "/api/apps/_/rtc/close", R"({})").body == R"({"error":"sessionId required"})", "close validation");
            check(json(request(r.port, "GET", "/api/apps/_/rtc/ice-servers"))["iceServers"][0]["urls"][0] == "stun:stun.cloudflare.com:3478", "ice servers");
            check(json(request(r.port, "GET", "/api/apps/_/rtc/diagnostic"))["sessions"].is_array(), "diagnostic");
        }},
        {"settings-routes > GET / PUT with validation", [] {
            Runner r({});
            check(request(r.port, "GET", "/api/settings/stream").body == R"({"ok":true,"prefs":{}})", "empty");
            check(request(r.port, "PUT", "/api/settings/stream", R"({"maxWidth":640,"intervalSec":2})").body == R"({"ok":true,"prefs":{"intervalSec":2,"maxWidth":640}})", "set");
            check(request(r.port, "PUT", "/api/settings/stream", "nope").body == R"({"ok":false,"error":"invalid JSON"})", "invalid JSON");
            check(request(r.port, "PUT", "/api/settings/stream", R"({"intervalSec":61})").status == 400, "range");
            check(request(r.port, "PUT", "/api/settings/stream", R"({"intervalSec":null})").body == R"({"ok":true,"prefs":{"maxWidth":640}})", "clear");
        }},
        {"playlog-routes > serves the list and NDJSON bodies", [] {
            Runner r({});
            const auto list = json(request(r.port, "GET", "/api/playlogs?appId=demo"));
            check(list["logs"].size() == 1 && list["logs"][0]["run"] == "20261011-000000-abcdef", "list");
            const auto body = request(r.port, "GET", "/api/playlogs/demo/20261011-000000-abcdef");
            check(body.header("content-type") == "application/x-ndjson; charset=utf-8" && body.body == "{\"seq\":0}\n", "body");
            check(request(r.port, "GET", "/api/playlogs/demo/nope").status == 404 && request(r.port, "GET", "/api/playlogs/demo/..%2f..%2fsecret").status == 404, "invalid ids");
        }},
        {"auth > token-required mode protects every API prefix, not health or the UI", [] {
            Runner r({AuthSettings{false, "", true}});
            for (const auto* path : {"/api/apps", "/api/apps/demo", "/api/apps/x/unknown", "/api/settings/stream", "/api/unity/demo/status", "/api/playlogs", "/api/playlogs/demo/x"}) {
                check(request(r.port, "GET", path).body == R"({"error":"unauthorized"})", std::string("401 for ") + path);
            }
            check(request(r.port, "GET", "/api/apps", {}, {{"authorization", "Bearer t"}}).status == 200, "token accepted");
            check(request(r.port, "GET", "/api/health").status == 200 && request(r.port, "GET", "/").status == 200, "open paths");
            WsClient anonymous(r.port);
            check(anonymous.next()["closed"] == 4401, "ws without token closes with 4401");
            WsClient with_token(r.port, "/ws?token=t");
            check(with_token.next()["type"] == "welcome", "ws with token");
        }},
        {"boundary > request body 256 KiB accepted, + 1 is 413; headers beyond 16 KiB are 431", [] {
            Runner r({});
            std::string at_limit = R"({"intervalSec":1,"pad":")" + std::string(256 * 1024 - 26, 'a') + "\"}";
            check(at_limit.size() == 256 * 1024, "fixture size");
            check(request(r.port, "PUT", "/api/settings/stream", at_limit).status == 200, "exactly 256 KiB");
            const auto over = request(r.port, "PUT", "/api/settings/stream", at_limit + " ");
            check(over.status == 413 && over.body == "Payload Too Large", "limit + 1");
            check(request(r.port, "GET", "/api/health", {}, {{"x-filler", std::string(17 * 1024, 'h')}}).status == 431, "header limit");
            std::string deep(40, '[');
            check(request(r.port, "PUT", "/api/settings/stream", deep + std::string(40, ']')).status == 400, "nesting beyond the budget is rejected");
        }},
        {"security > cross-origin writes and WebSockets refused; foreign Host refused on loopback", [] {
            Runner r({});
            const auto origin = "http://127.0.0.1:" + std::to_string(r.port);
            check(request(r.port, "POST", "/api/apps/demo/kill", "", {{"origin", "http://evil.test"}}).status == 403, "cross-origin POST");
            check(request(r.port, "POST", "/api/apps/demo/kill", "", {{"origin", origin}}).status == 200, "same-origin POST");
            check(request(r.port, "GET", "/api/apps", "", {{"origin", "http://evil.test"}}).status == 200, "cross-origin reads still answer (CORS blocks reading)");
            check(request(r.port, "GET", "/api/health", "", {{"host", "evil.test"}}).status == 403, "DNS rebinding Host");
            bool refused = false;
            try { WsClient foreign(r.port, "/ws", "http://evil.test"); } catch (const std::exception&) { refused = true; }
            check(refused, "cross-origin WebSocket refused at the handshake");
            WsClient same(r.port, "/ws", origin);
            check(same.next()["type"] == "welcome", "same-origin WebSocket");
        }},
        {"ws > subscribe, status / log / exit fan-out, input gating and message checks", [] {
            Runner r({});
            WsClient player(r.port), spectator(r.port);
            const auto welcome = player.next();
            check(welcome["type"] == "welcome" && welcome["clientId"].get<std::string>().size() == 36, "welcome");
            spectator.next();
            player.send(R"({"type":"subscribe","appId":"demo"})");
            check(player.next()["type"] == "audience-history" && player.next()["type"] == "hello", "history then hello");
            check(player.next() == Json{{"type", "control"}, {"appId", "demo"}, {"role", "free"}}, "free");
            spectator.send(R"({"type":"subscribe","appId":"demo"})");
            spectator.until("control");
            player.send(R"({"type":"key","appId":"demo","key":"W","down":true})");
            check(player.until("control")["role"] == "player", "first input takes the lease");
            check(spectator.until("control")["role"] == "spectator", "others become spectators");
            spectator.send(R"({"type":"control","appId":"demo","want":true})");
            check(spectator.until("control")["role"] == "spectator", "lease is not stolen");
            request(r.port, "POST", "/api/apps/demo/run", "", {});
            check(player.until("log")["text"] == "hi", "log line");
            check(player.until("exit")["exitCode"] == 0, "exit");
            player.send("not json");
            check(player.until("error")["message"] == "invalid JSON", "invalid JSON");
            player.send(R"({"type":"subscribe","appId":"nope"})");
            check(player.until("error")["message"] == "unknown appId: nope", "unknown app");
            player.send(R"({"type":"button","appId":"demo","id":"quit"})");
            check(player.until("error")["message"] == "use POST /api/apps/:id/kill for kill button", "kill button");
            player.send(R"({"type":"button","appId":"demo","id":"nope"})");
            check(player.until("error")["message"] == "unknown button: nope", "unknown button");
            player.send(R"({"type":"pointer","appId":"demo","x":2,"y":0,"button":"left","phase":"down"})");
            check(player.until("error")["message"] == "invalid pointer message", "pointer range");
            spectator.send(R"({"type":"comment","appId":"demo","text":"  nice\n run "})");
            const auto comment = player.until("comment");
            check(comment["text"] == "nice run", "comment normalised and fanned out");
            spectator.send(R"({"type":"comment","appId":"demo","text":"   "})");
            check(spectator.until("error")["message"] == "comment must be 1-200 characters", "empty comment");
        }},
        {"boundary > WebSocket message size and rate limits close the connection", [] {
            Runner r({});
            WsClient big(r.port);
            big.next();
            big.send(R"({"type":"comment","pad":")" + std::string(64 * 1024, 'x') + "\"}");
            check(big.next().contains("closed"), "message beyond 64 KiB closes");
            WsClient flood(r.port);
            flood.next();
            for (int i = 0; i < 60; ++i) flood.send(R"({"type":"unsubscribe","appId":"demo"})");
            check(flood.next()["closed"] == 1008, "rate limit closes with 1008");
        }},
        {"farm-routes > both exact device-list path and lease subpaths require user authentication", [] {
            Runner r({AuthSettings{false, "", false}, true});
            check(request(r.port, "GET", "/api/farm/devices").status == 401, "list needs auth");
            check(request(r.port, "GET", "/api/farm/devices", {}, {{"authorization", "Bearer alice"}}).status == 200, "user");
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease", R"({"ttlSec":60})").status == 401, "lease needs auth");
        }},
        {"farm-routes > host credentials cannot be used as user credentials and vice versa; no impersonation", [] {
            Runner r({AuthSettings{false, "", false}, true});
            const std::string report = R"({"devices":[{"id":"pixel","connected":true,"readyGeneration":1}]})";
            check(request(r.port, "POST", "/api/farm/hosts/host/report", report, {{"authorization", "Bearer alice"}}).status == 401, "user as host");
            check(request(r.port, "POST", "/api/farm/hosts/other/report", report, {{"authorization", "Bearer " + r.farm_token}}).status == 401, "other host");
            check(request(r.port, "POST", "/api/farm/hosts/host/report", report, {{"authorization", "Bearer " + r.farm_token}}).status == 200, "host");
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease", R"({"ttlSec":60})", {{"authorization", "Bearer " + r.farm_token}}).status == 401, "host as user");
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease", R"({"ttlSec":60,"ownerId":"bob"})", {{"authorization", "Bearer alice"}}).status == 400, "owner impersonation");
            const auto acquired = request(r.port, "POST", "/api/farm/devices/pixel/lease", R"({"ttlSec":60})", {{"authorization", "Bearer alice"}});
            check(acquired.status == 201, "acquired");
            const auto lease = json(acquired)["lease"]["id"].get<std::string>();
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease", R"({"ttlSec":60})", {{"authorization", "Bearer bob"}}).status == 409, "taken");
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease/release", R"({"leaseId":")" + lease + "\"}", {{"authorization", "Bearer bob"}}).status == 403, "not the owner");
            const auto released = request(r.port, "POST", "/api/farm/devices/pixel/lease/release", R"({"leaseId":")" + lease + "\"}", {{"authorization", "Bearer alice"}});
            check(json(released)["availability"] == "cleanup", "released");
        }},
        {"farm-routes > bad client JSON is 400; corrupt persisted JSON is 503 without exposing its contents; pending write is 503", [] {
            Runner r({AuthSettings{false, "", false}, true});
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease", "{", {{"authorization", "Bearer alice"}}).status == 400, "bad JSON");
            request(r.port, "POST", "/api/farm/hosts/host/report", R"({"devices":[{"id":"pixel","connected":true,"readyGeneration":1}]})", {{"authorization", "Bearer " + r.farm_token}});
            std::ofstream(r.dir / "state.json.pending") << "unresolved-write";
            check(request(r.port, "POST", "/api/farm/devices/pixel/lease", R"({"ttlSec":60})", {{"authorization", "Bearer alice"}}).status == 503, "uncommitted acquisition");
            fs::remove(r.dir / "state.json.pending");
            std::ofstream(r.dir / "state.json") << "private-corrupt-state";
            const auto failure = request(r.port, "GET", "/api/farm/devices", {}, {{"authorization", "Bearer alice"}});
            check(failure.status == 503 && failure.body == R"({"error":"farm_storage_unavailable"})", "corrupt state hidden");
        }},
    });
}
