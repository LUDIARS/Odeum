#include "labels.hpp"
#include <algorithm>

namespace odeum::presenter {
std::string stamp_label(std::string_view kind) {
    if (kind == "clap") return "拍手";
    if (kind == "laugh") return "笑い";
    if (kind == "wow") return "驚き";
    if (kind == "question") return "質問";
    if (kind == "agree") return "同意";
    return std::string(kind);
}

std::string link_status_label(const ConnectionMonitor& monitor, Millis now) {
    switch (monitor.status()) {
    case LinkStatus::idle: return "未接続 — GLab の発表リンクを開くか、貼り付けてください";
    case LinkStatus::connecting: return "中継に接続中…";
    case LinkStatus::connected: return "中継に接続済み";
    case LinkStatus::waiting: {
        const auto seconds = (std::max<Millis>(0, monitor.retry_at() - now) + 999) / 1000;
        auto text = "再接続まで " + std::to_string(seconds) + " 秒";
        if (monitor.ticket_suspect()) text += "（続けて失敗する場合は GLab でリンクを再発行してください）";
        return text;
    }
    case LinkStatus::ticket_expired: return "チケットの期限 (5 分) が切れました。GLab で発表リンクを再発行してください";
    }
    return {};
}

std::string media_state_label(MediaState state) {
    switch (state) {
    case MediaState::idle: return "映像経路: なし";
    case MediaState::negotiating: return "映像経路: 準備中";
    case MediaState::connected: return "映像経路: 接続";
    case MediaState::failed: return "映像経路: 切断";
    }
    return {};
}

std::string relay_error_label(std::string_view code) {
    if (code == "presenter_exists") return "この発表には別の発表者アプリが接続しています";
    if (code == "capacity") return "中継の同時接続数の上限に達しています";
    if (code == "session_closed") return "発表セッションが終了しました";
    if (code == "forbidden") return "この操作は許可されていません";
    if (code == "invalid_sdp") return "映像の取り決めを中継が受け付けませんでした";
    if (code == "poll_not_open" || code == "poll_active") return "投票を中継が受け付けませんでした";
    if (code == "invalid_message" || code == "unknown_type" || code == "message_too_large") return "中継とのメッセージ形式が合いません";
    return "中継エラー: " + std::string(code);
}

std::string launch_error_label(std::string_view code) {
    if (code == "unsupported_scheme" || code == "unsupported_action" || code == "invalid_url")
        return "odeum://present で始まる発表リンクではありません";
    if (code == "relay_missing" || code == "invalid_relay") return "リンクに中継の URL がありません";
    if (code == "insecure_relay") return "中継の URL は wss:// である必要があります";
    if (code == "ticket_missing") return "リンクにチケットがありません。GLab で発表リンクを発行し直してください";
    if (code == "invalid_ticket") return "リンクのチケットが読めません。GLab で発表リンクを発行し直してください";
    return "発表リンクを読めませんでした";
}

std::string poll_error_label(std::string_view code) {
    if (code == "question_missing") return "質問を貼り付けてください";
    if (code == "question_too_long") return "質問は 1000 文字までです";
    if (code == "too_few_choices") return "選択肢は 2 件以上必要です (1 行に 1 件)";
    if (code == "too_many_choices") return "選択肢は 6 件までです";
    if (code == "choice_too_long") return "選択肢は 1 件 60 文字までです";
    if (code == "duplicate_choice") return "同じ選択肢が 2 回あります";
    if (code == "poll_already_open") return "いまの投票を終了してから開始してください";
    if (code == "no_open_poll") return "開いている投票はありません";
    return "投票の内容を確認してください";
}

std::string permission_label(CapturePermission permission) {
    switch (permission) {
    case CapturePermission::granted: return "画面収録: 許可済み";
    case CapturePermission::denied:
        return "画面収録が許可されていません。システム設定 > プライバシーとセキュリティ > 画面収録 で odeum-presenter を許可し、アプリを再起動してください";
    case CapturePermission::undetermined: return "画面収録の許可を求めます";
    }
    return {};
}

std::string capture_kind_label(CaptureKind kind) { return kind == CaptureKind::display ? "画面" : "ウインドウ"; }

std::string grouped(std::int64_t value) {
    auto digits = std::to_string(value < 0 ? -value : value);
    for (auto i = static_cast<std::ptrdiff_t>(digits.size()) - 3; i > 0; i -= 3) digits.insert(static_cast<std::size_t>(i), ",");
    return value < 0 ? "-" + digits : digits;
}
}
