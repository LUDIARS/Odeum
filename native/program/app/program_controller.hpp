#pragma once
#include "input_pipeline.hpp"
#include "platform.hpp"
#include "program_engine.hpp"
#include "../core/publish_supervisor.hpp"
#include "../transport/input_receiver.hpp"
#include "../transport/youtube_publisher.hpp"
#include "core/connection_monitor.hpp"
#include "core/launch_url.hpp"
#include "core/overlay_state.hpp"
#include "transport/media_sender.hpp"
#include "transport/relay_link.hpp"
#include <tela/drawing.hpp>
#include <filesystem>

namespace odeum::program {
struct Notice {
    std::string text;
    bool error{};
};

// Everything the operator controls, owned by the UI thread: the producer link to the relay, the
// receiving and return-feed PeerConnections, the input pipelines, the programme clock, the
// YouTube publish and its reconnection, and the settings. Buttons call straight into it.
class ProgramController {
public:
    struct Hooks { std::function<void()> quit; };
    ProgramController(presenter::EventQueue& queue, ProgramPlatform& platform, ProgramSettings settings,
                      std::filesystem::path settings_path, Hooks hooks);
    ~ProgramController();
    ProgramController(const ProgramController&) = delete;
    ProgramController& operator=(const ProgramController&) = delete;

    // The clock first, then queued work: due reconnections run here.
    void tick(presenter::Millis now, std::int64_t epoch_s);
    void accept_launch(std::string_view text);
    void paste_launch();

    void show_full(int input);
    void show_quad();
    void standby();
    void end();
    void toggle_mute(int input);
    void change_volume(double delta);

    void start_youtube();
    void stop_youtube();
    void paste_stream_key();
    void erase_stream_key();
    void start_return();
    void stop_return();
    void quit();

    // Called by the app every frame with the freshly drawn text layers.
    void publish_layers(std::vector<Layer> layers);
    std::shared_ptr<const tela::Image> thumbnail(int input) const { return thumbnails_[input_at(input)]; }

    const ProgramState& state() const noexcept { return state_; }
    const ProgramSettings& settings() const noexcept { return settings_; }
    const presenter::OverlayState& overlay() const noexcept { return overlay_; }
    const presenter::ConnectionMonitor& monitor() const noexcept { return monitor_; }
    const PublishSupervisor& youtube() const noexcept { return supervisor_; }
    PublishStats youtube_stats() const { return publisher_.stats(); }
    EngineStats engine_stats() const { return engine_ ? engine_->stats() : EngineStats{}; }
    presenter::MediaState return_state() const noexcept { return sender_.state(); }
    bool returning() const noexcept { return returning_; }
    bool stream_key_set() const noexcept { return key_set_; }
    const Notice& notice() const noexcept { return notice_; }
    presenter::Millis now() const noexcept { return now_; }
private:
    void handle(const Message& message);
    void route(const Message& signal);
    void closed();
    void input_live(int input, bool live);
    void begin_publish();
    void refresh_thumbnails();
    void save();
    void tell(std::string text, bool error = false);
    presenter::EventQueue& queue_;
    ProgramPlatform& platform_;
    ProgramSettings settings_;
    std::filesystem::path settings_path_;
    Hooks hooks_;
    std::unique_ptr<SecretStore> secrets_;
    ProgramState state_;
    presenter::OverlayState overlay_;
    presenter::ConnectionMonitor monitor_;
    PublishSupervisor supervisor_;
    AudioMixer mixer_;
    std::array<std::unique_ptr<InputPipeline>, input_count> pipelines_;
    YouTubePublisher publisher_;
    presenter::MediaSender sender_;
    InputReceiver receiver_;
    std::unique_ptr<ProgramEngine> engine_;
    presenter::RelayLink link_;
    std::optional<presenter::LaunchRequest> request_;
    std::optional<Json> ice_servers_;
    std::vector<Layer> layers_;
    std::array<std::shared_ptr<const tela::Image>, input_count> thumbnails_;
    presenter::Millis now_ = 0, thumbnails_at_ = 0;
    std::int64_t epoch_s_ = 0;
    bool returning_ = false, key_set_ = false;
    Notice notice_;
};
}
