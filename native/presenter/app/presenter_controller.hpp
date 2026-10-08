#pragma once
#include "platform.hpp"
#include "../core/connection_monitor.hpp"
#include "../core/launch_url.hpp"
#include "../core/overlay_state.hpp"
#include "../core/submission_inbox.hpp"
#include "../core/pcm_framer.hpp"
#include "../core/poll_draft.hpp"
#include "../core/presenter_settings.hpp"
#include "../core/video_pipeline.hpp"
#include "../transport/media_sender.hpp"
#include "../transport/relay_link.hpp"
#include <filesystem>
#include <optional>

namespace odeum::presenter {
struct PanelNotice {
    std::string text;
    bool error{};
};

// Owns the presenter's state on the UI thread: the launch link, the relay link and its
// reconnection, the media path, capture, polls and what the overlay shows. Every public
// method is called from the UI thread; work from other threads arrives through the queue.
class PresenterController {
public:
    struct Hooks {
        // The overlay must move to this placement (a corner was chosen).
        std::function<void(const OverlayPlacement&)> place_overlay;
        std::function<void()> quit;
    };
    PresenterController(EventQueue& queue, Platform& platform, PresenterSettings settings,
                        std::filesystem::path settings_path, Hooks hooks);
    ~PresenterController();
    PresenterController(const PresenterController&) = delete;
    PresenterController& operator=(const PresenterController&) = delete;

    // Launch link from the OS (URL scheme), the command line or the clipboard.
    void accept_launch(std::string_view text);
    void paste_launch();

    // Screen sending. The relay link stays up while stopped, so reactions keep arriving.
    void start_stream();
    void stop_stream();
    void refresh_targets();
    void select_target(std::size_t index);
    void request_permission();

    void paste_question();
    void paste_choices();
    void toggle_multi();
    void open_poll();
    void close_poll();

    void toggle_comments();
    SubmissionInbox& inbox() noexcept { return inbox_; }
    bool inbox_visible() const noexcept { return inbox_visible_; }
    void toggle_inbox() noexcept { inbox_visible_ = !inbox_visible_; }
    void choose_corner(const std::string& corner);
    // The overlay was dragged and settled here.
    void overlay_moved(const OverlayPlacement& placement);

    // Advances clocks: reconnection, expiry of reactions. Call every UI loop iteration.
    void tick(Millis now, std::int64_t epoch_s);
    void quit();

    const OverlayState& overlay() const noexcept { return overlay_; }
    const ConnectionMonitor& monitor() const noexcept { return monitor_; }
    MediaState media_state() const noexcept { return media_.state(); }
    const PresenterSettings& settings() const noexcept { return settings_; }
    const PanelNotice& notice() const noexcept { return notice_; }
    const std::vector<CaptureTarget>& targets() const noexcept { return targets_; }
    std::optional<std::size_t> selected() const noexcept { return selected_; }
    bool streaming() const noexcept { return streaming_; }
    bool capturing() const noexcept { return capturing_; }
    bool has_launch() const noexcept { return request_.has_value(); }
    const PollDraft& draft() const noexcept { return draft_; }
    const std::optional<std::string>& open_poll_id() const noexcept { return polls_.open_poll(); }
    CapturePermission permission() const noexcept { return permission_; }
    Millis now() const noexcept { return now_; }
private:
    void handle(const Message& message);
    void closed();
    void update_capture();
    void start_capture();
    void stop_capture();
    void persist();
    void tell(std::string text, bool error = false);

    EventQueue& queue_;
    Platform& platform_;
    PresenterSettings settings_;
    std::filesystem::path settings_path_;
    Hooks hooks_;
    Millis now_ = 0;
    std::int64_t epoch_s_ = 0;
    std::optional<LaunchRequest> request_;
    ConnectionMonitor monitor_;
    OverlayState overlay_;
    SubmissionInbox inbox_;
    bool inbox_visible_ = false;
    PollDraft draft_;
    PollControl polls_;
    PanelNotice notice_;
    std::unique_ptr<CaptureSource> capture_;
    std::unique_ptr<VideoEncoder> encoder_;
    std::unique_ptr<AudioSource> audio_;
    std::unique_ptr<AudioEncoder> audio_encoder_;
    PcmFramer framer_; // audio thread only, between start_capture and stop_capture
    // Declared before media_ and link_: their network callbacks reach the pipeline, so it must
    // outlive them.
    VideoPipeline pipeline_;
    MediaSender media_;
    RelayLink link_;
    std::vector<CaptureTarget> targets_;
    std::optional<std::size_t> selected_;
    CapturePermission permission_ = CapturePermission::undetermined;
    bool streaming_ = false, capturing_ = false;
};
}
