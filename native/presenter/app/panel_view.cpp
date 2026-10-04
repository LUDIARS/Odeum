#include "panel_view.hpp"
#include "labels.hpp"

namespace odeum::presenter {
namespace {
tela::Layout text_box(unsigned lines = 1, float scale = 1) {
    tela::Layout layout;
    layout.lines = lines;
    layout.text_scale = scale;
    layout.padding = 4;
    return layout;
}
tela::Layout row_box() {
    tela::Layout layout;
    layout.flow = tela::Flow::row;
    layout.padding = 0;
    layout.gap = 6;
    return layout;
}
tela::Layout button_box(float width) {
    tela::Layout layout;
    layout.width = width;
    layout.padding = 6;
    return layout;
}

std::string target_text(const PresenterController& c) {
    if (c.targets().empty()) return "取り込み対象: (一覧を更新してください)";
    const auto& t = c.targets()[*c.selected()];
    return "取り込み対象: " + capture_kind_label(t.kind) + " " + t.name + " (" + std::to_string(t.width) + "x" +
           std::to_string(t.height) + ")  " + std::to_string(*c.selected() + 1) + "/" + std::to_string(c.targets().size());
}

std::string choices_text(const PollDraft& draft) {
    if (draft.choices.empty()) return "選択肢: (1 行に 1 件で貼り付け、2〜6 件)";
    std::string text = "選択肢:";
    for (std::size_t i = 0; i < draft.choices.size(); ++i) text += (i ? " / " : " ") + draft.choices[i];
    return text;
}

void connection(tela::Document& doc, PresenterController& c) {
    doc.text("link-status", link_status_label(c.monitor(), c.now()), text_box(2));
    doc.text("media-status", media_state_label(c.media_state()) + "    視聴 " + grouped(c.overlay().viewer_count()) + " 人" +
             (c.capturing() ? "    配信中" : c.streaming() ? "    配信待ち" : "    停止中"), text_box());
    if (!c.notice().text.empty()) doc.text("notice", (c.notice().error ? "! " : "") + c.notice().text, text_box(2, 0.9f));
    doc.panel("launch-row", [&] {
        doc.button("paste-link", "発表リンクを貼り付け", [&c] { c.paste_launch(); }, button_box(200));
        doc.button("start", "配信開始", [&c] { c.start_stream(); }, button_box(100));
        doc.button("stop", "配信停止", [&c] { c.stop_stream(); }, button_box(100));
    }, row_box());
}

void capture(tela::Document& doc, PresenterController& c) {
    doc.text("target", target_text(c), text_box(2, 0.9f));
    doc.panel("target-row", [&] {
        doc.button("target-previous", "前へ", [&c] {
            if (!c.targets().empty()) c.select_target((*c.selected() + c.targets().size() - 1) % c.targets().size());
        }, button_box(80));
        doc.button("target-next", "次へ", [&c] {
            if (!c.targets().empty()) c.select_target((*c.selected() + 1) % c.targets().size());
        }, button_box(80));
        doc.button("target-refresh", "一覧を更新", [&c] { c.refresh_targets(); }, button_box(120));
        if (c.permission() != CapturePermission::granted)
            doc.button("permission", "画面収録を許可", [&c] { c.request_permission(); }, button_box(140));
    }, row_box());
}

void poll(tela::Document& doc, PresenterController& c) {
    const auto& draft = c.draft();
    doc.text("poll-heading", c.open_poll_id() ? "投票 (受付中)" : "投票の作成", text_box());
    doc.text("poll-question", "質問: " + (draft.question.empty() ? std::string("(クリップボードから貼り付け)") : draft.question), text_box(2, 0.9f));
    doc.text("poll-choices", choices_text(draft), text_box(2, 0.9f));
    doc.panel("poll-edit-row", [&] {
        doc.button("poll-question-paste", "質問を貼り付け", [&c] { c.paste_question(); }, button_box(140));
        doc.button("poll-choices-paste", "選択肢を貼り付け", [&c] { c.paste_choices(); }, button_box(150));
        doc.button("poll-multi", draft.multi ? "複数選択: 可" : "複数選択: 不可", [&c] { c.toggle_multi(); }, button_box(130));
    }, row_box());
    doc.panel("poll-run-row", [&] {
        doc.button("poll-open", "投票を開始", [&c] { c.open_poll(); }, button_box(140));
        doc.button("poll-close", "投票を終了", [&c] { c.close_poll(); }, button_box(140));
    }, row_box());
}

void overlay(tela::Document& doc, PresenterController& c) {
    doc.text("overlay-heading", "重ね表示 (位置はつまみ「:::」でも移動できます)", text_box(1, 0.9f));
    doc.panel("overlay-row", [&] {
        doc.button("comments", c.settings().comments_visible ? "コメント表示: ON" : "コメント表示: OFF", [&c] { c.toggle_comments(); }, button_box(170));
        doc.button("quit", "終了", [&c] { c.quit(); }, button_box(80));
    }, row_box());
    doc.panel("corner-row", [&] {
        doc.button("corner-top-left", "左上", [&c] { c.choose_corner("top-left"); }, button_box(80));
        doc.button("corner-top-right", "右上", [&c] { c.choose_corner("top-right"); }, button_box(80));
        doc.button("corner-bottom-left", "左下", [&c] { c.choose_corner("bottom-left"); }, button_box(80));
        doc.button("corner-bottom-right", "右下", [&c] { c.choose_corner("bottom-right"); }, button_box(80));
    }, row_box());
}
}

tela::Theme panel_theme() {
    tela::Theme theme;
    theme.panel = {24, 28, 40, 245};
    theme.font_size = 15;
    theme.line_height = 22;
    return theme;
}

tela::Document panel_document(PresenterController& c, bool with_grip) {
    tela::Document doc;
    tela::Layout root;
    root.width = panel_width;
    root.height = panel_height;
    root.padding = 12;
    root.gap = 6;
    doc.panel("panel", [&] {
        if (with_grip) doc.text(panel_grip, "::: Odeum 発表者パネル", text_box(1, 1.1f), tela::InputPolicy::exclusive);
        else doc.text("title", "Odeum 発表者パネル", text_box(1, 1.1f));
        connection(doc, c);
        capture(doc, c);
        poll(doc, c);
        overlay(doc, c);
    }, root);
    return doc;
}
}
