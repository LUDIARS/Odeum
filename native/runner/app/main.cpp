// odeum-runner: the remote test runner (build / run / test of registered apps, logs, virtual keys,
// Unity control, play logs, device farm and WebRTC screen streaming). Replaces the Node runner of
// the imported Custos copy; same ports (4649 and 7676 serve the same content), routes and JSON.
#include "../adapters/clock.hpp"
#include "../adapters/farm_store.hpp"
#include "../adapters/log.hpp"
#include "../adapters/paths.hpp"
#include "../adapters/playlog_files.hpp"
#include "api_router.hpp"
#include "http_server.hpp"
#include "settings.hpp"
#include "ws_broker.hpp"
#include <boost/asio/signal_set.hpp>
#include <algorithm>
#include <csignal>
#include <filesystem>
#include <iostream>
#ifdef ODEUM_RUNNER_HAS_WEBRTC
#include "../webrtc/webrtc_broker.hpp"
#endif

using namespace odeum::runner;
namespace fs = std::filesystem;

namespace {
AppsConfig load_apps(const std::string& file) {
    std::error_code error;
    if (!fs::exists(to_path(file), error)) return {};  // no file: start with no apps, as before
    const auto text = read_file_limited(file, config_file_limits.bytes);
    if (!text) throw ConfigError("apps file unreadable or larger than 4 MiB");
    return parse_apps_config(parse_json(*text, config_file_limits));
}

std::unique_ptr<FarmRuntime> load_farm(const RunnerSettings& s, const EnvReader& env) {
    if (!s.farm_config) return nullptr;
    auto absolute = [](const std::string& p) { return !p.empty() && to_path(p).is_absolute(); };
    if (!absolute(*s.farm_config)) throw FarmContractError("CUSTOS_FARM_CONFIG must be an absolute file path");
    if (!s.farm_state || !absolute(*s.farm_state)) throw FarmContractError("CUSTOS_FARM_STATE_FILE must be an absolute file path");
    auto lower = [](std::string p) { std::transform(p.begin(), p.end(), p.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return p; };
    if (lower(from_path(to_path(*s.farm_config).lexically_normal())) == lower(from_path(to_path(*s.farm_state).lexically_normal()))) {
        throw FarmContractError("farm configuration and state must use separate files");
    }
    if (s.auth.open || s.auth.cernere_url.find_first_not_of(" \t") == std::string::npos) {
        throw FarmContractError("device farm requires CERNERE_URL and verified user identities; open/stub mode is unsupported");
    }
    const auto text = read_file_limited(*s.farm_config, 1024 * 1024);
    if (!text) throw FarmContractError("farm configuration unreadable");
    auto config = parse_farm_config(parse_json(*text, config_file_limits));
    auto runtime = std::make_unique<FarmRuntime>();
    runtime->host_auth = std::make_unique<FarmHostAuth>(config.hosts, env);
    runtime->store = std::make_unique<FileFarmStore>(*s.farm_state);
    runtime->pool = std::make_unique<DevicePool>(std::move(config), *runtime->store, epoch_ms, random_uuid);
    return runtime;
}

log::Level level_of(const std::string& name) {
    if (name == "debug") return log::Level::debug;
    if (name == "warn") return log::Level::warn;
    if (name == "error") return log::Level::error;
    return log::Level::info;
}
}

int serve() {
#ifdef ODEUM_RUNNER_HAS_WEBRTC
    init_webrtc_logging();
#endif
    try {
        const auto env = process_environment();
        std::error_code error;
        const auto cwd = from_path(fs::current_path(error));
        const auto settings = load_settings(env, cwd);
        log::set_level(level_of(settings.log_level));
        const auto apps = load_apps(settings.apps_file);
        auto farm = load_farm(settings, env);
        log::info("main", "apps config loaded", {{"apps", apps.apps.size()}, {"authMode", describe(settings.auth)}});
        // Custos runs commands, injects input and captures the screen: never anonymous beyond loopback.
        if (refuses_bind(settings.host, settings.auth)) {
            log::error("main", "refusing to bind a non-loopback host without authentication — set CERNERE_URL (or CUSTOS_AUTH_REQUIRED=1), "
                "or keep CUSTOS_HOST on loopback. Custos can run commands / inject input / capture the screen.", {{"host", settings.host}});
            return 1;
        }

        boost::asio::io_context io(1);
        AppsRegistry registry(apps.apps, epoch_ms);
        AppsRunner runner(io, registry, [&io](const ProcessSpec& spec, ChildProcess::Handlers handlers) { return ChildProcess::spawn(io, spec, std::move(handlers)); }, random_uuid);
        CernereAuth auth(io, settings.auth, epoch_ms);
        MediaTools media(io, settings.ffmpeg, CaptureHost{current_platform(), settings.display, settings.avfoundation_input}, settings.video_encoder);
        auto screenshots = make_screenshot_source(io, media);
        RuntimeStreamPrefs prefs;
        ScreenshotStreamer streamer(io, *screenshots, prefs, settings.screenshot_interval_sec, epoch_ms);
        ControlLease control;
        AudienceRoom audience(epoch_ms, random_uuid);
        InputForwarder input(io, make_desktop_input(), settings.adb);
        // The salt is needed only when a play log is written.
        std::optional<std::string> salt;
        PlayLogRecorder play_log(io, PlayLogDeps{
            settings.playlog_dir,
            [&registry](const std::string& id) { return registry.config(id); },
            [&salt, &settings](const std::string& user) {
                if (!salt) salt = resolve_player_salt(settings.playlog_dir, settings.playlog_salt);
                return hash_player(*salt, user);
            },
            epoch_ms,
            [](const std::string& path) { return open_play_log_file(path); },
            random_below,
            [&io](const BridgeEndpoint& ep, auto done) { bridge_fetch_state(io, ep, std::move(done)); }});
        IceServerProvider ice(io, settings.stun_urls, settings.turn_key_id, settings.turn_key_token, epoch_ms);
        std::unique_ptr<RtcService> rtc;
#ifdef ODEUM_RUNNER_HAS_WEBRTC
        rtc = make_webrtc_broker(io, ice, media, settings, [&runner](const std::string& id) { return runner.run_pid(id); });
#endif
        UnityProxy unity(registry, control, [&io](const BridgeTarget& t) { return std::make_shared<UnityBridgeClient>(io, BridgeEndpoint{t.host, t.port}); });
        ApiRouter router(RouterDeps{registry, runner, prefs, auth, unity, media, *screenshots, ice, rtc.get(), farm.get(), settings.playlog_dir,
            settings.gaze_assets_dir, env("CUSTOS_VIDEO_ENCODER").value_or("") != ""});
        WsBroker broker(WsBrokerDeps{registry, runner, streamer, &play_log, control, audience, input, auth, epoch_ms, random_uuid});
        runner.on_exit([&](const std::string& app_id, RunnerKind kind, const ExitStatus&) {
            if (kind != RunnerKind::run) return;
            if (rtc) rtc->close_all_for_app(app_id);
            play_log.close_app(app_id, "exit");
            streamer.stop(app_id);  // restarted by the next subscribe after a new run
        });

        HttpServer server(io, router, broker, settings.access);
        std::vector<std::pair<std::uint16_t, const char*>> ports;
        if (settings.frontend_port > 0) ports.emplace_back(settings.frontend_port, "frontend");
        if (settings.backend_port > 0 && settings.backend_port != settings.frontend_port) ports.emplace_back(settings.backend_port, "backend");
        for (const auto& [port, label] : ports) server.listen(settings.host, port, label);
        std::cout << "\n  ▶ Open odeum-runner: http://localhost:" << settings.frontend_port << "/   (or http://localhost:" << settings.backend_port << "/)\n\n" << std::flush;

        boost::asio::signal_set signals(io, SIGINT, SIGTERM);
        boost::asio::steady_timer exit_timer(io);
        signals.async_wait([&](const boost::system::error_code& e, int signal) {
            if (e) return;
            log::info("main", "shutting down", {{"sig", signal == SIGINT ? "SIGINT" : "SIGTERM"}});
            if (rtc) rtc->shutdown();
            streamer.shutdown();
            play_log.shutdown();
            runner.shutdown();
            server.stop();
            exit_timer.expires_after(std::chrono::milliseconds(200));
            exit_timer.async_wait([&io](const boost::system::error_code&) { io.stop(); });
        });
        for (;;) {
            try {
                io.run();
                break;
            } catch (const std::exception& failure) {
                // As before: an unexpected error in one handler does not stop the server.
                log::error("main", "uncaught exception — server continues", {{"err", failure.what()}});
            }
        }
        return 0;
    } catch (const std::exception& failure) {
        log::error("main", "fatal during startup", {{"err", failure.what()}});
        return 1;
    }
}

int main() {
    const int status = serve();
#ifdef ODEUM_RUNNER_HAS_WEBRTC
    // Every broker and peer connection is gone; join libdatachannel's workers before exit().
    if (!finish_webrtc(std::chrono::seconds(10))) {
        log::error("main", "WebRTC cleanup did not finish", {});
        return 1;
    }
#endif
    return status;
}
