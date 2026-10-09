#include "program_labels.hpp"
#include "app/labels.hpp"
#include <algorithm>
#include <cstdio>

namespace odeum::program {
std::string scene_label(const Scene& scene) {
    switch (scene.mode) {
    case SceneMode::full: return "全画面: 入力 " + std::to_string(scene.focus + 1);
    case SceneMode::quad: return "4 分割";
    case SceneMode::standby: return "待機 (まもなく始まります)";
    case SceneMode::ended: return "終了 (ご視聴ありがとうございました)";
    }
    return {};
}

std::string publish_status_label(const PublishSupervisor& supervisor, presenter::Millis now) {
    switch (supervisor.status()) {
    case PublishStatus::stopped: return "YouTube: 停止中";
    case PublishStatus::connecting: return "YouTube: 接続中…";
    case PublishStatus::live: return "YouTube: 送出中";
    case PublishStatus::waiting: {
        const auto seconds = (std::max<presenter::Millis>(0, supervisor.retry_at() - now) + 999) / 1000;
        return "YouTube: 再接続まで " + std::to_string(seconds) + " 秒 (再接続 " + std::to_string(supervisor.reconnects()) + " 回)";
    }
    }
    return {};
}

std::string publish_error_label(std::string_view code) {
    if (code == "network") return "YouTube に接続できませんでした (ネットワークまたは TLS)";
    if (code == "connect_rejected") return "YouTube が接続を拒否しました";
    if (code == "publish_rejected") return "YouTube がストリームを拒否しました。ストリームキーとライブ配信の設定を確認してください";
    if (code == "timeout") return "YouTube の応答がありませんでした";
    if (code == "unsupported_version" || code == "invalid_chunk") return "YouTube との RTMP のやり取りに失敗しました";
    return "YouTube への送出が止まりました";
}

std::string publish_stats_label(const PublishStats& stats) {
    char rate[32];
    std::snprintf(rate, sizeof rate, "%.0f", stats.bitrate_kbps);
    return std::string("送出 ") + rate + " kbps   送信 " + presenter::grouped(static_cast<std::int64_t>(stats.bytes_sent / 1000000)) +
           " MB   ドロップ " + presenter::grouped(static_cast<std::int64_t>(stats.dropped_frames));
}

std::string producer_launch_error_label(std::string_view code) {
    if (code == "unsupported_scheme" || code == "unsupported_action" || code == "invalid_url")
        return "odeum://produce で始まる番組リンクではありません";
    return presenter::launch_error_label(code);
}

std::string producer_relay_error_label(std::string_view code) {
    if (code == "producer_exists") return "この発表には別の番組制作アプリが接続しています";
    if (code == "presenter_exists") return "program スロットを発表者アプリが使っています";
    return presenter::relay_error_label(code);
}
}
