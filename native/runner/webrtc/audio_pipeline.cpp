#include "audio_pipeline.hpp"
#include "../adapters/bridge_client.hpp"
#include "../adapters/log.hpp"
#include "../adapters/paths.hpp"
#include "../adapters/playlog_files.hpp"
#include "../adapters/process.hpp"
#include "../core/farm_host_auth.hpp"
#include <boost/asio/post.hpp>
#include <filesystem>

namespace odeum::runner {
namespace {
constexpr std::size_t pcm_backlog_limit = 2u * 1024 * 1024;  // ~10 s of 48 kHz stereo s16le; beyond: dropped
#ifdef _WIN32
constexpr auto build_timeout = std::chrono::milliseconds(60'000);

std::string hex(const std::array<unsigned char, 32>& digest, std::size_t bytes) {
    static constexpr char d[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < bytes; ++i) { out.push_back(d[digest[i] >> 4]); out.push_back(d[digest[i] & 15]); }
    return out;
}
#endif
}

// Builds ProcessLoopback.exe once per source hash; concurrent requests share one build.
class LoopbackHelper {
public:
    LoopbackHelper(boost::asio::io_context& io, AudioSettings settings) : io_(io), settings_(std::move(settings)) { (void)settings_; }
    void ensure(std::function<void(std::variant<std::string, std::string>)> done) {  // index 0 exe path, 1 error
#ifndef _WIN32
        boost::asio::post(io_, [done] { done(std::variant<std::string, std::string>(std::in_place_index<1>, "音声 (process-loopback) は Windows でだけ使えます")); });
#else
        waiting_.push_back(std::move(done));
        if (waiting_.size() > 1) return;
        auto finish = [this](std::variant<std::string, std::string> result) {
            auto waiting = std::move(waiting_);
            waiting_.clear();
            for (auto& callback : waiting) callback(result);
        };
        const auto source = read_file_limited(settings_.loopback_source, 1024 * 1024);
        if (!source) return finish(std::variant<std::string, std::string>(std::in_place_index<1>, "ProcessLoopback.cs が読めません"));
        const auto exe = from_path(to_path(settings_.temp_dir) / ("custos-process-loopback-" + hex(sha256(*source), 6) + ".exe"));
        std::error_code e;
        if (std::filesystem::exists(to_path(exe), e)) return finish(std::variant<std::string, std::string>(std::in_place_index<0>, exe));
        if (settings_.windir.empty()) return finish(std::variant<std::string, std::string>(std::in_place_index<1>, "WINDIR が未設定のため csc.exe を探せません"));
        const auto csc = from_path(to_path(settings_.windir) / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "csc.exe");
        if (!std::filesystem::exists(to_path(csc), e)) {
            return finish(std::variant<std::string, std::string>(std::in_place_index<1>, "csc.exe が見つかりません (" + csc + ")。.NET Framework 4 が必要です"));
        }
        log::info("process-loopback", "building process loopback helper");
        ProcessSpec spec{csc, {"-nologo", "-optimize+", "-platform:x64", "-out:" + exe, settings_.loopback_source}, {}, {}, false, true, true, true};
        run_collect(io_, spec, build_timeout, 1024 * 1024, 4096, std::nullopt, [finish, exe](CollectResult result) {
            if (auto* error = std::get_if<SpawnError>(&result)) return finish(std::variant<std::string, std::string>(std::in_place_index<1>, std::string("ProcessLoopback.cs のビルドに失敗しました: ") + error->what()));
            auto& out = std::get<CollectedOutput>(result);
            if (out.status.code != 0 || out.timed_out) {
                auto text = sanitize_utf8(out.stdout_bytes);
                const auto begin = text.find_first_not_of(" \t\r\n");
                text = begin == std::string::npos ? std::string("exit ") + (out.status.code ? std::to_string(*out.status.code) : "null") : text.substr(begin, 1000);
                return finish(std::variant<std::string, std::string>(std::in_place_index<1>, "ProcessLoopback.cs のビルドに失敗しました: " + text));
            }
            finish(std::variant<std::string, std::string>(std::in_place_index<0>, exe));
        });
#endif
    }
private:
    boost::asio::io_context& io_;
    AudioSettings settings_;
    std::vector<std::function<void(std::variant<std::string, std::string>)>> waiting_;
};

namespace {
class AudioPipeline final : public RtpPipeline {
public:
    AudioPipeline(boost::asio::io_context& io, MediaTools& media, std::shared_ptr<LoopbackHelper> helper, std::function<std::optional<long long>(const std::string&)> run_pid)
        : io_(io), media_(media), helper_(std::move(helper)), run_pid_(std::move(run_pid)) {}
    void start(const AppConfig& app, int rtp_port, Events events, std::function<void(std::optional<StartError>)> started) override {
        if (!app.audio) return started(StartError{"app " + app.id + " has no audio config"});
        events_ = std::move(events);
        auto alive = alive_;
        resolve_pid(app, [this, alive, rtp_port, started](std::variant<long long, std::string> pid) {
            if (!*alive) return started(StartError{"stopped"});
            if (auto* error = std::get_if<std::string>(&pid)) return started(StartError{*error});
            helper_->ensure([this, alive, rtp_port, started, pid = std::get<long long>(pid)](std::variant<std::string, std::string> exe) {
                if (!*alive) return started(StartError{"stopped"});
                if (exe.index() == 1) return started(StartError{std::get<1>(exe)});
                launch(std::get<0>(exe), pid, rtp_port, started);
            });
        });
    }
    void stop() override {
        *alive_ = false;
        if (capture_) capture_->kill_tree();
        if (encoder_) encoder_->kill_tree();
        capture_.reset();
        encoder_.reset();
    }
private:
    boost::asio::io_context& io_;
    MediaTools& media_;
    std::shared_ptr<LoopbackHelper> helper_;
    std::function<std::optional<long long>(const std::string&)> run_pid_;
    Events events_;
    std::shared_ptr<ChildProcess> capture_, encoder_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    bool ended_ = false;

    void resolve_pid(const AppConfig& app, std::function<void(std::variant<long long, std::string>)> done) {
        if (!app.in_app_bridge) {
            const auto pid = run_pid_(app.id);
            if (!pid || *pid <= 0) return boost::asio::post(io_, [done, id = app.id] { done("app " + id + ": 音声を取る対象のプロセスが動いていません (Run してから接続してください)"); });
            return boost::asio::post(io_, [done, pid = *pid] { done(pid); });
        }
        auto client = std::make_shared<UnityBridgeClient>(io_, BridgeEndpoint{app.in_app_bridge->host, app.in_app_bridge->port}, "", std::chrono::milliseconds(3000));
        client->health([client, done, id = app.id](UnityResult result) {
            if (auto* error = std::get_if<UnityBridgeError>(&result)) return done(error->message);
            const auto& health = std::get<Json>(result);
            const auto pid = health.find("pid");
            if (pid == health.end() || !pid->is_number_integer() || pid->get<long long>() <= 0) {
                return done("app " + id + ": ブリッジの /health が pid を返しません (ブリッジが古い可能性があります)");
            }
            done(pid->get<long long>());
        });
    }

    void ended(const char* name, const ExitStatus& status) {
        if (ended_ || !*alive_) return;
        ended_ = true;
        log::info("audio-pipeline", "audio pipeline process exited", {{"process", name}, {"code", status.code ? Json(*status.code) : Json(nullptr)}});
        stop();
        if (events_.exit) events_.exit();
    }

    std::function<void(std::string_view)> stderr_of(const char* name) {
        return [this, name](std::string_view chunk) {
            auto text = sanitize_utf8(chunk);
            const auto begin = text.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos) return;
            text = text.substr(begin, std::min<std::size_t>(2000, text.find_last_not_of(" \t\r\n") - begin + 1));
            log::warn("audio-pipeline", "audio pipeline stderr", {{"process", name}, {"msg", text}});
            if (events_.stderr_line) events_.stderr_line(std::string(name) + ": " + text);
        };
    }

    void launch(const std::string& exe, long long pid, int rtp_port, std::function<void(std::optional<StartError>)> started) {
        ChildProcess::Handlers encoder_handlers;
        encoder_handlers.on_stderr = stderr_of("ffmpeg");
        encoder_handlers.on_exit = [this](const ExitStatus& s) { ended("ffmpeg", s); };
        try {
            encoder_ = ChildProcess::spawn(io_, ProcessSpec{media_.ffmpeg(), audio_args(rtp_port), {}, {}, true, false, true, true}, std::move(encoder_handlers));
        } catch (const SpawnError& error) {
            return started(StartError{"audio のパイプラインを起動できません (ffmpeg: " + std::string(error.what()) + ")。ffmpeg は PATH に入れるか CUSTOS_FFMPEG で実行ファイルを指定してください"});
        }
        ChildProcess::Handlers capture_handlers;
        capture_handlers.on_stdout = [this](std::string_view pcm) {
            // Audio is not worth stalling for: a backlog beyond ~10 s is dropped.
            if (encoder_ && encoder_->stdin_queued_bytes() + pcm.size() <= pcm_backlog_limit) encoder_->write_stdin(std::make_shared<const std::string>(pcm), nullptr);
        };
        capture_handlers.on_stderr = stderr_of("process loopback");
        capture_handlers.on_exit = [this](const ExitStatus& s) { ended("process loopback", s); };
        try {
            capture_ = ChildProcess::spawn(io_, ProcessSpec{exe, {std::to_string(pid)}, {}, {}, false, true, true, true}, std::move(capture_handlers));
        } catch (const SpawnError& error) {
            encoder_->kill_tree();
            encoder_.reset();
            return started(StartError{"audio のパイプラインを起動できません (process loopback: " + std::string(error.what()) + ")"});
        }
        log::info("audio-pipeline", "audio pipeline starting", {{"pid", pid}, {"rtpPort", rtp_port}});
        started(std::nullopt);
    }
};
}

AudioPipelineFactory::AudioPipelineFactory(boost::asio::io_context& io, MediaTools& media, AudioSettings settings,
    std::function<std::optional<long long>(const std::string&)> run_pid)
    : io_(io), media_(media), helper_(std::make_shared<LoopbackHelper>(io, std::move(settings))), run_pid_(std::move(run_pid)) {}

AudioPipelineFactory::~AudioPipelineFactory() = default;

std::unique_ptr<RtpPipeline> AudioPipelineFactory::make() { return std::make_unique<AudioPipeline>(io_, media_, helper_, run_pid_); }
}
