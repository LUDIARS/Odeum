// odeum-unityctl: the Unity bridge CLI (custos/src/cli/unityctl.ts). Talks to the bridge directly,
// so compiles work while odeum-runner is down. Exit codes: 0 ok, 1 compile errors, 2 unreachable.
#include "../adapters/bridge_client.hpp"
#include "../adapters/paths.hpp"
#include "../adapters/playlog_files.hpp"
#include "../core/unityctl.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace odeum::runner;

namespace {
class Failure : public std::runtime_error {
public:
    Failure(std::string message, bool unreachable) : std::runtime_error(std::move(message)), unreachable_(unreachable) {}
    bool unreachable() const { return unreachable_; }
private:
    bool unreachable_;
};

// <CUSTOS_UNITY_PROJECT or cwd>/custos/bridge-port, else 17778.
int resolve_bridge_port() {
    std::string root;
    if (const char* project = std::getenv("CUSTOS_UNITY_PROJECT")) root = project;
    else { std::error_code e; root = from_path(std::filesystem::current_path(e)); }
    const auto file = from_path(to_path(root) / "custos" / "bridge-port");
    if (auto text = read_file_limited(file, 64)) if (auto port = parse_bridge_port_file(*text)) return *port;
    return default_unity_bridge_port;
}

struct Cli {
    boost::asio::io_context io;
    std::unique_ptr<UnityBridgeClient> client;

    UnityResult wait(const std::function<void(std::function<void(UnityResult)>)>& start) {
        std::optional<UnityResult> result;
        start([&result](UnityResult r) { result = std::move(r); });
        io.restart();
        while (!result && io.run_one() > 0) {}
        if (!result) throw Failure("internal: no response", true);
        return std::move(*result);
    }
    Json value(const std::function<void(std::function<void(UnityResult)>)>& start) {
        auto result = wait(start);
        if (auto* json = std::get_if<Json>(&result)) return std::move(*json);
        const auto& error = std::get<UnityBridgeError>(result);
        throw Failure(error.message, error.kind == UnityBridgeError::Kind::unreachable);
    }
    bool reachable() {
        auto result = wait([this](auto done) { client->health(done); });
        if (std::holds_alternative<Json>(result)) return true;
        return std::get<UnityBridgeError>(result).kind == UnityBridgeError::Kind::http;  // a listener answered
    }
    Json compile_and_wait() {
        constexpr long long poll_ms = 1000, total_budget_ms = 10 * 60'000;
        CompileWaitState state;
        state.ever_reachable = reachable();
        if (!state.ever_reachable) throw Failure("Unity bridge unreachable at " + client->endpoint().base_url() + ": bridge not reachable before compile", true);
        value([this](auto done) { client->request_compile(done); });
        long long elapsed = 0;
        // Reading before the reload starts would see "not compiling"; wait one interval first.
        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
        elapsed += poll_ms;
        for (;;) {
            bool reachable = false, compiling = true;
            Json snapshot;
            auto result = wait([this](auto done) { client->compile_status(done); });
            if (auto* json = std::get_if<Json>(&result)) {
                snapshot = *json;
                reachable = true;
                compiling = snapshot.value("isCompiling", false);
            } else if (std::get<UnityBridgeError>(result).kind != UnityBridgeError::Kind::unreachable) {
                throw Failure(std::get<UnityBridgeError>(result).message, false);
            }
            if (reachable) { state.ever_reachable = true; state.unreachable_ms = 0; }
            else state.unreachable_ms += poll_ms;
            const auto phase = next_compile_wait_phase(state, reachable, compiling);
            if (phase == CompileWaitPhase::done && reachable) return snapshot;
            if (phase == CompileWaitPhase::unreachable) throw Failure("Unity bridge unreachable at " + client->endpoint().base_url() + ": bridge did not come back after reload", true);
            if (elapsed >= total_budget_ms) throw Failure("compile did not finish within " + std::to_string(total_budget_ms / 1000) + "s", false);
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
            elapsed += poll_ms;
        }
    }
};

int run(const UnityctlArgs& args) {
    Cli cli;
    cli.client = std::make_unique<UnityBridgeClient>(cli.io, BridgeEndpoint{args.host, args.port ? *args.port : resolve_bridge_port()}, args.token);
    auto& c = *cli.client;
    const auto& cmd = args.command;
    if (cmd == "health") {
        const auto health = cli.value([&c](auto done) { c.health(done); });
        std::cout << (args.json ? render_json(health) : std::string("bridge ok (isPlaying=") + (health.contains("isPlaying") ? health["isPlaying"].dump() : "undefined") + ")");
    } else if (cmd == "status") {
        const auto status = cli.value([&c](auto done) { c.status(done); });
        std::cout << (args.json ? render_json(status) : render_status(status));
    } else if (cmd == "compile") {
        Json status;
        if (args.wait) status = cli.compile_and_wait();
        else {
            cli.value([&c](auto done) { c.request_compile(done); });
            status = cli.value([&c](auto done) { c.compile_status(done); });
        }
        std::cout << (args.json ? render_json(status) : render_compile_status(status)) << "\n";
        return args.wait ? compile_exit_code(status) : exit_ok;
    } else if (cmd == "errors") {
        const auto status = cli.value([&c](auto done) { c.compile_status(done); });
        std::cout << (args.json ? render_json(status) : render_compile_status(status)) << "\n";
        return compile_exit_code(status);
    } else if (cmd == "log") {
        const auto page = cli.value([&c, &args](auto done) { c.log(args.since, args.level, args.limit, done); });
        std::cout << (args.json ? render_json(page) : render_log(page));
    } else if (cmd == "play") {
        std::cout << render_json(cli.value([&c](auto done) { c.play(done); }));
    } else if (cmd == "stop") {
        std::cout << render_json(cli.value([&c](auto done) { c.stop(done); }));
    } else if (cmd == "scene") {
        std::cout << render_json(cli.value([&c](auto done) { c.scene(done); }));
    } else if (cmd == "hierarchy") {
        std::cout << render_json(cli.value([&c, &args](auto done) { c.hierarchy(args.depth, args.root, done); }));
    } else if (cmd == "capture") {
        std::cout << render_json(cli.value([&c, &args](auto done) { c.publish_capture(args.caption, args.source, done); }));
    }
    std::cout << "\n";
    return exit_ok;
}
}

int main(int argc, char** argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);
    UnityctlArgs args;
    try {
        args = parse_unityctl_args(arguments);
    } catch (const UnityctlArgError& error) {
        std::cerr << error.what() << "\n\n" << unityctl_usage() << "\n";
        return exit_unreachable;
    }
    try {
        return run(args);
    } catch (const std::exception& error) {
        // Unity not running or reloading: the message is enough, no stack.
        std::cerr << error.what() << "\n";
        return exit_unreachable;
    }
}
