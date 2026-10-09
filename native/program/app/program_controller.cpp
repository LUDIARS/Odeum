#include "program_controller.hpp"
#include "program_labels.hpp"
#include "program_settings_file.hpp"
#include "../core/producer_routing.hpp"
#include "app/labels.hpp"
#include "core/keyframe_request.hpp"
#include "../render/nv12_canvas.hpp"
#include <algorithm>
#include <cctype>

namespace odeum::program {
namespace {
constexpr int thumbnail_width = 160, thumbnail_height = 90;
constexpr presenter::Millis thumbnail_interval = 500;

StreamMetadata metadata(const ProgramSettings& s) {
    return {program_width, program_height, program_fps, s.video_bitrate_kbps, s.audio_bitrate_kbps, aac_rate, aac_channels};
}

std::string trimmed(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    std::size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) ++start;
    return text.substr(start);
}
}

ProgramController::ProgramController(presenter::EventQueue& queue, ProgramPlatform& platform, ProgramSettings settings,
                                     std::filesystem::path settings_path, Hooks hooks)
    : queue_(queue), platform_(platform), settings_(std::move(settings)), settings_path_(std::move(settings_path)),
      hooks_(std::move(hooks)), secrets_(platform.secret_store()),
      publisher_(queue, metadata(settings_), YouTubePublisher::Callbacks{
          [this] { supervisor_.live(); supervisor_.headers_sent(); tell("YouTube への送出を始めました"); },
          [this](const std::string& code) {
              if (supervisor_.status() == PublishStatus::stopped) return;
              const auto delay = supervisor_.failed(now_);
              tell(publish_error_label(code) + " (" + std::to_string(delay / 1000) + " 秒後に再接続)", true);
          },
          [this] { if (engine_) engine_->request_program_keyframe(); }}),
      sender_(queue, presenter::MediaSender::Callbacks{
          [this](const Json& message) {
              try { link_.send(message); }
              catch (const ProtocolError&) { tell(producer_relay_error_label("invalid_sdp"), true); }
          },
          [this](presenter::MediaState state) {
              if (state == presenter::MediaState::connected && engine_) engine_->request_return_keyframe();
          },
          [this](std::span<const std::byte> packet) {
              if (presenter::is_keyframe_request(packet) && engine_) engine_->request_return_keyframe();
          }}),
      receiver_(queue, InputReceiver::Callbacks{
          [this](const Json& message) {
              try { link_.send(message); }
              catch (const ProtocolError&) { tell(producer_relay_error_label("invalid_sdp"), true); }
          },
          [this](int input, std::span<const std::byte> unit) { pipelines_[static_cast<std::size_t>(input)]->video(unit); },
          [this](int input, std::span<const std::byte> packet) { pipelines_[static_cast<std::size_t>(input)]->audio(packet); },
          [this](int input, bool live) { input_live(input, live); }}),
      link_(queue, presenter::RelayLink::Callbacks{
          [] {},
          [this](const Message& message) { handle(message); },
          [this](const std::string& code) { tell(producer_relay_error_label(code), true); },
          [this] { closed(); }}) {
    for (int i = 0; i < input_count; ++i) {
        // Posted, not called: the pipeline thread must not reach into the receiver directly.
        pipelines_[static_cast<std::size_t>(i)] = std::make_unique<InputPipeline>(i, platform.video_decoder(), mixer_,
            [this, i] { queue_.post([this, i] { receiver_.request_keyframe(i); }); });
        state_.mute(i, settings_.muted[static_cast<std::size_t>(i)]);
    }
    state_.volume(settings_.volume);
    try { key_set_ = secrets_->load().has_value(); }
    catch (const std::exception&) { key_set_ = false; }
    std::array<InputPipeline*, input_count> inputs{};
    for (int i = 0; i < input_count; ++i) inputs[static_cast<std::size_t>(i)] = pipelines_[static_cast<std::size_t>(i)].get();
    engine_ = std::make_unique<ProgramEngine>(platform, settings_, inputs, mixer_, ProgramEngine::Outputs{
        [this](const presenter::EncodedFrame& frame) { publisher_.video(frame); },
        [this](const AacPacket& packet) { publisher_.audio(packet); },
        [this](const presenter::EncodedFrame& frame) { sender_.send_video(frame); },
        [this](std::span<const std::byte> packet, std::int64_t timestamp_us) { sender_.send_audio(packet, timestamp_us); }});
}

ProgramController::~ProgramController() {
    // Threads that call into the others stop first: network, then the publish, then the clock.
    link_.close();
    receiver_.stop();
    publisher_.stop();
    sender_.stop();
    engine_.reset();
}

void ProgramController::tell(std::string text, bool error) { notice_ = {std::move(text), error}; }

void ProgramController::save() {
    try { save_settings(settings_path_, settings_); }
    catch (const std::exception& error) { tell(std::string("設定を保存できません: ") + error.what(), true); }
}

void ProgramController::tick(presenter::Millis now, std::int64_t epoch_s) {
    now_ = now;
    epoch_s_ = epoch_s;
    overlay_.expire(now);
    if (monitor_.due(now) && request_) {
        monitor_.retrying();
        link_.connect(presenter::relay_socket_url(*request_));
    }
    if (supervisor_.due(now)) {
        supervisor_.retrying();
        begin_publish();
    }
    if (now - thumbnails_at_ >= thumbnail_interval) refresh_thumbnails();
    if (engine_) engine_->update({state_.scene(), state_.connections(), state_.mutes(), state_.volume(), layers_, returning_});
}

void ProgramController::publish_layers(std::vector<Layer> layers) { layers_ = std::move(layers); }

void ProgramController::refresh_thumbnails() {
    thumbnails_at_ = now_;
    const auto now_us = steady_us();
    for (int i = 0; i < input_count; ++i) {
        auto picture = state_.connected(i) ? pipelines_[static_cast<std::size_t>(i)]->latest(now_us, 1000) : std::nullopt;
        if (!picture) { thumbnails_[static_cast<std::size_t>(i)].reset(); continue; }
        auto image = std::make_shared<tela::Image>();
        image->width = thumbnail_width;
        image->height = thumbnail_height;
        image->pixels = thumbnail_bgra(*picture, thumbnail_width, thumbnail_height);
        thumbnails_[static_cast<std::size_t>(i)] = std::move(image);
    }
}

void ProgramController::accept_launch(std::string_view text) {
    try {
        auto request = presenter::parse_launch_url(text, "produce");
        const auto expiry = presenter::ticket_expiry(request.ticket);
        if (expiry <= epoch_s_) { tell("このリンクのチケットは期限切れです。GLab で番組リンクを再発行してください", true); return; }
        link_.close();
        receiver_.stop();
        sender_.stop();
        for (int i = 0; i < input_count; ++i) input_live(i, false);
        request_ = std::move(request);
        overlay_.reset();
        monitor_.start(expiry);
        link_.connect(presenter::relay_socket_url(*request_));
        tell("番組リンクを受け付けました。中継に接続しています");
    } catch (const presenter::LaunchUrlError& error) {
        tell(producer_launch_error_label(error.code), true);
    }
}

void ProgramController::paste_launch() {
    const auto text = platform_.clipboard_text();
    if (text.empty()) { tell("クリップボードに番組リンクがありません", true); return; }
    accept_launch(text);
}

void ProgramController::handle(const Message& message) {
    switch (message.type) {
    case MessageType::welcome:
        if (message.body.at("role").get<std::string>() != "producer") {
            monitor_.stop();
            link_.close();
            tell("このリンクは番組制作用 (producer) ではありません", true);
            return;
        }
        monitor_.welcomed();
        // The program draws public telops and submissions, so it may announce readiness too.
        link_.send({{"type", "reaction.ready"}, {"version", 1}});
        overlay_.reset();
        overlay_.apply(message, now_);
        ice_servers_ = message.body.at("ice_servers");
        receiver_.start(*ice_servers_);
        if (returning_) sender_.start(*ice_servers_, true);
        tell("中継に接続しました");
        return;
    case MessageType::sdp:
    case MessageType::candidate:
        route(message);
        return;
    case MessageType::track_closed:
        receiver_.closed(message.body.at("mids"));
        return;
    case MessageType::presence: {
        const auto live = presence_inputs(message.body);
        for (int i = 0; i < input_count; ++i) if (!live[static_cast<std::size_t>(i)] && state_.connected(i)) input_live(i, false);
        for (int i = 0; i < input_count; ++i) state_.input_connected(i, live[static_cast<std::size_t>(i)]);
        overlay_.apply(message, now_);
        return;
    }
    case MessageType::error: {
        const auto code = message.body.at("code").get<std::string>();
        monitor_.rejected(code);
        tell(producer_relay_error_label(code), true);
        return;
    }
    default:
        overlay_.apply(message, now_);
        return;
    }
}

void ProgramController::route(const Message& signal) {
    try {
        if (side_of(signal) == PeerSide::receiver) receiver_.remote(signal);
        else sender_.remote(signal);
    } catch (const std::exception&) {
        tell(producer_relay_error_label("invalid_sdp"), true);
    }
}

void ProgramController::input_live(int input, bool live) {
    if (live) {
        receiver_.request_keyframe(input);
        return;
    }
    // A dropped input never leaves its last picture on the programme.
    pipelines_[static_cast<std::size_t>(input)]->reset();
    thumbnails_[static_cast<std::size_t>(input)].reset();
}

void ProgramController::closed() {
    receiver_.stop();
    sender_.stop();
    for (int i = 0; i < input_count; ++i) {
        input_live(i, false);
        state_.input_connected(i, false);
    }
    const auto decision = monitor_.closed(epoch_s_, now_);
    if (decision.ticket_expired) tell("チケットの期限が切れました。GLab で番組リンクを再発行してください", true);
    else if (decision.retry) tell("中継との接続が切れました。再接続します", true);
}

void ProgramController::show_full(int input) { state_.show_full(input); }
void ProgramController::show_quad() { state_.show_quad(); }
void ProgramController::standby() { state_.standby(); }
void ProgramController::end() { state_.end(); }

void ProgramController::toggle_mute(int input) {
    state_.mute(input, !state_.muted(input));
    settings_.muted[input_at(input)] = state_.muted(input);
    save();
}

void ProgramController::change_volume(double delta) {
    const auto volume = std::clamp(state_.volume() + delta, 0.0, 2.0);
    state_.volume(volume);
    settings_.volume = volume;
    save();
}

void ProgramController::begin_publish() {
    std::optional<std::string> key;
    try { key = secrets_->load(); }
    catch (const std::exception&) { key.reset(); }
    if (!key) {
        supervisor_.stop();
        tell("YouTube のストリームキーが設定されていません。「キーを貼り付け」で設定してください", true);
        return;
    }
    publisher_.start(parse_rtmps_url(settings_.youtube_url), std::move(*key));
}

void ProgramController::start_youtube() {
    if (supervisor_.status() != PublishStatus::stopped) return;
    supervisor_.start();
    begin_publish();
    if (supervisor_.status() == PublishStatus::connecting) tell("YouTube に接続しています");
}

void ProgramController::stop_youtube() {
    supervisor_.stop();
    publisher_.stop();
    tell("YouTube への送出を止めました");
}

void ProgramController::paste_stream_key() {
    const auto key = trimmed(platform_.clipboard_text());
    try {
        validate_stream_key(key);
        secrets_->save(key);
        key_set_ = true;
        tell("ストリームキーを保存しました (表示はしません)");
    } catch (const std::invalid_argument&) {
        tell("クリップボードの内容は YouTube のストリームキーの形式ではありません", true);
    } catch (const std::exception&) {
        tell("ストリームキーを保存できませんでした", true);
    }
}

void ProgramController::erase_stream_key() {
    try {
        secrets_->erase();
        key_set_ = false;
        tell("ストリームキーを削除しました");
    } catch (const std::exception&) {
        tell("ストリームキーを削除できませんでした", true);
    }
}

void ProgramController::start_return() {
    if (returning_) return;
    returning_ = true;
    if (ice_servers_ && monitor_.status() == presenter::LinkStatus::connected) sender_.start(*ice_servers_, true);
    tell("番組の返送を始めました");
}

void ProgramController::stop_return() {
    returning_ = false;
    sender_.stop();
    tell("番組の返送を止めました");
}

void ProgramController::quit() {
    if (hooks_.quit) hooks_.quit();
}
}
