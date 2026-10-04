#include "presenter_controller.hpp"
#include "labels.hpp"
#include "settings_file.hpp"
#include "../transport/opus_audio_encoder.hpp"
#include <iomanip>
#include <random>
#include <sstream>

namespace odeum::presenter {
namespace {
std::string random_poll_id() {
    static std::mt19937_64 generator(std::random_device{}());
    std::ostringstream id;
    id << "poll-" << std::hex << std::setw(16) << std::setfill('0') << generator();
    return id.str();
}
}

PresenterController::PresenterController(EventQueue& queue, Platform& platform, PresenterSettings settings,
                                         std::filesystem::path settings_path, Hooks hooks)
    : queue_(queue), platform_(platform), settings_(std::move(settings)), settings_path_(std::move(settings_path)),
      hooks_(std::move(hooks)), capture_(platform.capture()), encoder_(platform.video_encoder()),
      audio_(platform.audio_source()), pipeline_(*encoder_),
      media_(queue, MediaSender::Callbacks{
          [this](const Json& message) {
              try { link_.send(message); }
              catch (const ProtocolError&) { tell(relay_error_label("invalid_sdp"), true); }
          },
          [this](MediaState state) {
              // Viewers joining mid-stream are covered by the relay's PLI; this covers the first one.
              if (state == MediaState::connected) pipeline_.request_keyframe();
              if (state == MediaState::failed && monitor_.status() == LinkStatus::connected) tell(media_state_label(state), true);
          },
          [this](std::span<const std::byte> packet) { pipeline_.on_rtcp(packet); }}),
      link_(queue, RelayLink::Callbacks{
          [] {},
          [this](const Message& message) { handle(message); },
          [this](const std::string& code) { tell(relay_error_label(code), true); },
          [this] { closed(); }}) {
    permission_ = capture_->permission();
}

PresenterController::~PresenterController() {
    // Sources first: their threads call into the pipeline and the media sender.
    stop_capture();
    link_.close();
    media_.stop();
}

void PresenterController::tell(std::string text, bool error) { notice_ = {std::move(text), error}; }

void PresenterController::accept_launch(std::string_view text) {
    try {
        auto request = parse_launch_url(text);
        const auto expiry = ticket_expiry(request.ticket);
        if (expiry <= epoch_s_) { tell("このリンクのチケットは期限切れです。GLab で発表リンクを再発行してください", true); return; }
        stop_capture();
        link_.close();
        media_.stop();
        request_ = std::move(request);
        overlay_.reset();
        polls_.reset();
        monitor_.start(expiry);
        link_.connect(relay_socket_url(*request_));
        tell("発表リンクを受け付けました。中継に接続しています");
    } catch (const LaunchUrlError& error) {
        tell(launch_error_label(error.code), true);
    }
}

void PresenterController::paste_launch() {
    const auto text = platform_.clipboard_text();
    if (text.empty()) { tell("クリップボードに発表リンクがありません", true); return; }
    accept_launch(text);
}

void PresenterController::handle(const Message& message) {
    switch (message.type) {
    case MessageType::welcome:
        if (message.body.at("role").get<std::string>() != "presenter") {
            monitor_.stop();
            link_.close();
            tell("このリンクは発表者用ではありません。GLab の「発表を始める」から開いてください", true);
            return;
        }
        monitor_.welcomed();
        // The relay rebuilds the room whenever the presenter (re)joins, so its counts restart too.
        overlay_.reset();
        overlay_.apply(message, now_);
        polls_.reset();
        media_.start(message.body.at("ice_servers"), settings_.stream.audio);
        tell("中継に接続しました");
        update_capture();
        return;
    case MessageType::sdp:
    case MessageType::candidate:
        try { media_.remote(message); }
        catch (const std::exception&) { tell(relay_error_label("invalid_sdp"), true); }
        return;
    case MessageType::error: {
        const auto code = message.body.at("code").get<std::string>();
        monitor_.rejected(code);
        overlay_.apply(message, now_);
        tell(relay_error_label(code), true);
        return;
    }
    default:
        overlay_.apply(message, now_);
        return;
    }
}

void PresenterController::closed() {
    media_.stop();
    polls_.reset();
    update_capture();
    const auto decision = monitor_.closed(epoch_s_, now_);
    if (decision.ticket_expired) tell(link_status_label(monitor_, now_), true);
    else if (decision.retry) tell("中継との接続が切れました。再接続します", true);
}

void PresenterController::start_stream() {
    permission_ = capture_->permission();
    if (permission_ != CapturePermission::granted) {
        capture_->request_permission();
        permission_ = capture_->permission();
        if (permission_ != CapturePermission::granted) { tell(permission_label(permission_), true); return; }
    }
    if (targets_.empty()) refresh_targets();
    if (!selected_) { tell("取り込む画面かウインドウを選んでください", true); return; }
    streaming_ = true;
    update_capture();
    if (!capturing_) tell(request_ ? "中継に接続すると配信を始めます" : "GLab の発表リンクを開くと配信を始めます");
}

void PresenterController::stop_stream() {
    streaming_ = false;
    update_capture();
    tell("配信を止めました (中継との接続は保っています)");
}

void PresenterController::refresh_targets() {
    try {
        const auto previous = selected_ ? std::optional<std::string>(targets_[*selected_].id) : std::nullopt;
        targets_ = capture_->targets();
        selected_.reset();
        for (std::size_t i = 0; i < targets_.size(); ++i) if (previous && targets_[i].id == *previous) selected_ = i;
        // The main display is listed first by every adapter, so it is the natural default.
        if (!selected_ && !targets_.empty()) selected_ = 0;
    } catch (const std::exception& error) {
        tell(std::string("取り込み対象を取得できません: ") + error.what(), true);
    }
}

void PresenterController::select_target(std::size_t index) {
    if (index >= targets_.size()) return;
    const bool changed = selected_ != index;
    selected_ = index;
    if (changed && capturing_) { stop_capture(); update_capture(); }
}

void PresenterController::request_permission() {
    capture_->request_permission();
    permission_ = capture_->permission();
    tell(permission_label(permission_), permission_ != CapturePermission::granted);
}

void PresenterController::update_capture() {
    const bool wanted = streaming_ && selected_ && monitor_.status() == LinkStatus::connected;
    if (wanted && !capturing_) start_capture();
    else if (!wanted && capturing_) stop_capture();
}

void PresenterController::start_capture() {
    try {
        pipeline_.start(settings_.stream, [this](const EncodedFrame& frame) { media_.send_video(frame); });
        capture_->start(targets_.at(*selected_), settings_.stream,
            [this](const VideoFrame& frame) { pipeline_.on_frame(frame); },
            [this](const std::string& message) {
                queue_.post([this, message] { streaming_ = false; stop_capture(); tell("画面の取り込みが止まりました: " + message, true); });
            });
        if (settings_.stream.audio && audio_) {
            audio_encoder_ = std::make_unique<OpusAudioEncoder>();
            audio_encoder_->configure(settings_.stream.audio_bitrate_kbps);
            framer_.reset();
            audio_->start(
                [this](const AudioBlock& block) {
                    framer_.push(block, [this](std::span<const float> frame, std::int64_t timestamp_us) {
                        media_.send_audio(audio_encoder_->encode(frame), timestamp_us);
                    });
                },
                [this](const std::string& message) {
                    queue_.post([this, message] { if (audio_) audio_->stop(); tell("音声の取り込みが止まりました: " + message, true); });
                });
        }
        capturing_ = true;
        tell("配信中");
    } catch (const std::exception& error) {
        capture_->stop();
        if (audio_) audio_->stop();
        pipeline_.stop();
        streaming_ = false;
        tell(std::string("配信を始められません: ") + error.what(), true);
    }
}

void PresenterController::stop_capture() {
    if (!capturing_) return;
    capture_->stop();
    if (audio_) audio_->stop();
    pipeline_.stop();
    audio_encoder_.reset();
    capturing_ = false;
}

void PresenterController::paste_question() {
    draft_.question = platform_.clipboard_text();
    tell(draft_.question.empty() ? "クリップボードに質問がありません" : "質問を貼り付けました", draft_.question.empty());
}

void PresenterController::paste_choices() {
    draft_.choices = PollDraft::split_choices(platform_.clipboard_text());
    tell("選択肢を " + std::to_string(draft_.choices.size()) + " 件貼り付けました", draft_.choices.size() < poll_min_choices);
}

void PresenterController::toggle_multi() { draft_.multi = !draft_.multi; }

void PresenterController::open_poll() {
    if (monitor_.status() != LinkStatus::connected) { tell("中継に接続してから投票を始めてください", true); return; }
    try {
        const auto message = polls_.open(draft_, random_poll_id());
        if (!link_.send(message)) { polls_.reset(); tell("中継に送れませんでした", true); return; }
        tell("投票を始めました");
    } catch (const PollDraftError& error) {
        tell(poll_error_label(error.code), true);
    }
}

void PresenterController::close_poll() {
    try {
        const auto message = polls_.close();
        if (!link_.send(message)) { tell("中継に送れませんでした", true); return; }
        tell("投票を締め切りました");
    } catch (const PollDraftError& error) {
        tell(poll_error_label(error.code), true);
    }
}

void PresenterController::toggle_comments() {
    settings_.comments_visible = !settings_.comments_visible;
    persist();
}

void PresenterController::choose_corner(const std::string& corner) {
    if (!is_corner_name(corner)) return;
    settings_.overlay.corner = corner;
    settings_.overlay.absolute = false;
    if (hooks_.place_overlay) hooks_.place_overlay(settings_.overlay);
    persist();
}

void PresenterController::overlay_moved(const OverlayPlacement& placement) {
    settings_.overlay = placement;
    persist();
}

void PresenterController::persist() {
    try { save_settings(settings_path_, settings_); }
    catch (const std::exception& error) { tell(std::string("設定を保存できません: ") + error.what(), true); }
}

void PresenterController::tick(Millis now, std::int64_t epoch_s) {
    now_ = now;
    epoch_s_ = epoch_s;
    if (monitor_.due(now) && request_) {
        monitor_.retrying();
        link_.connect(relay_socket_url(*request_));
    }
    overlay_.expire(now);
}

void PresenterController::quit() {
    stop_capture();
    link_.close();
    media_.stop();
    if (hooks_.quit) hooks_.quit();
}
}
