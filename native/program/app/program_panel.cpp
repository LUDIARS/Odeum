#include "program_panel.hpp"
#include "program_labels.hpp"
#include "app/labels.hpp"
#include <cmath>

namespace odeum::program {
namespace {
constexpr float thumb_width = 160, thumb_height = 90;

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
tela::Layout column_box(float width) {
    tela::Layout layout;
    layout.width = width;
    layout.padding = 0;
    layout.gap = 4;
    return layout;
}

void inputs(tela::Document& doc, ProgramController& c) {
    doc.panel("inputs", [&] {
        for (int i = 0; i < input_count; ++i) {
            const auto id = std::to_string(i + 1);
            doc.panel("input-" + id, [&] {
                tela::Drawing thumb;
                if (auto image = c.thumbnail(i)) thumb.image({0, 0, thumb_width, thumb_height}, image);
                else thumb.rectangle({0, 0, thumb_width, thumb_height}, {40, 40, 46, 255}, {{90, 90, 100, 255}, 1});
                tela::Layout canvas;
                canvas.width = thumb_width;
                canvas.height = thumb_height;
                canvas.padding = 0;
                doc.canvas("thumb-" + id, std::move(thumb), canvas);
                doc.text("input-state-" + id, "入力 " + id + (c.state().connected(i) ? "  接続" : "  未接続"), text_box(1, 0.9f));
                doc.button("full-" + id, "全画面 " + id, [&c, i] { c.show_full(i); }, button_box(thumb_width));
                doc.button("mute-" + id, c.state().muted(i) ? "ミュート中" : "音声 ON", [&c, i] { c.toggle_mute(i); }, button_box(thumb_width));
            }, column_box(thumb_width));
        }
    }, row_box());
}

void scenes(tela::Document& doc, ProgramController& c) {
    doc.text("scene", "番組: " + scene_label(c.state().scene()), text_box(1, 1.05f));
    doc.panel("scene-row", [&] {
        doc.button("quad", "4 分割", [&c] { c.show_quad(); }, button_box(110));
        doc.button("standby", "待機", [&c] { c.standby(); }, button_box(110));
        doc.button("end", "終了", [&c] { c.end(); }, button_box(110));
        doc.button("volume-down", "音量 -", [&c] { c.change_volume(-0.1); }, button_box(90));
        doc.button("volume-up", "音量 +", [&c] { c.change_volume(0.1); }, button_box(90));
        doc.text("volume", "音量 " + std::to_string(static_cast<int>(std::lround(c.state().volume() * 100))) + "%", text_box());
    }, row_box());
}

void youtube(tela::Document& doc, ProgramController& c) {
    doc.text("youtube-status", publish_status_label(c.youtube(), c.now()) + "    " + publish_stats_label(c.youtube_stats()), text_box(1, 0.9f));
    doc.panel("youtube-row", [&] {
        doc.button("youtube-start", "YouTube 送出開始", [&c] { c.start_youtube(); }, button_box(170));
        doc.button("youtube-stop", "送出停止", [&c] { c.stop_youtube(); }, button_box(110));
        doc.button("key-paste", c.stream_key_set() ? "キーを貼り替え" : "キーを貼り付け", [&c] { c.paste_stream_key(); }, button_box(150));
        doc.button("key-erase", "キーを削除", [&c] { c.erase_stream_key(); }, button_box(120));
    }, row_box());
    doc.text("key-state", c.stream_key_set() ? "ストリームキー: 設定済み (OS の保管庫に保存、表示しません)" : "ストリームキー: 未設定", text_box(1, 0.85f));
}

void return_feed(tela::Document& doc, ProgramController& c) {
    doc.text("return-status", std::string("返送 (640x360): ") + (c.returning() ? "ON  " : "OFF  ") + presenter::media_state_label(c.return_state()),
             text_box(1, 0.9f));
    doc.panel("return-row", [&] {
        doc.button("return-start", "返送開始", [&c] { c.start_return(); }, button_box(120));
        doc.button("return-stop", "返送停止", [&c] { c.stop_return(); }, button_box(120));
        doc.button("paste-link", "番組リンクを貼り付け", [&c] { c.paste_launch(); }, button_box(200));
        doc.button("quit", "終了", [&c] { c.quit(); }, button_box(80));
    }, row_box());
}
}

tela::Theme program_panel_theme() {
    tela::Theme theme;
    theme.panel = {24, 28, 40, 245};
    theme.font_size = 15;
    theme.line_height = 22;
    return theme;
}

tela::Document program_panel_document(ProgramController& c, bool with_grip) {
    tela::Document doc;
    tela::Layout root;
    root.width = program_panel_width;
    root.height = program_panel_height;
    root.padding = 12;
    root.gap = 6;
    doc.panel("panel", [&] {
        if (with_grip) doc.text(program_panel_grip, "::: Odeum 番組制作", text_box(1, 1.1f), tela::InputPolicy::exclusive);
        else doc.text("title", "Odeum 番組制作", text_box(1, 1.1f));
        doc.text("link-status", presenter::link_status_label(c.monitor(), c.now()) + "    視聴 " +
                 presenter::grouped(c.overlay().viewer_count()) + " 人", text_box(1, 0.9f));
        if (!c.notice().text.empty()) doc.text("notice", (c.notice().error ? "! " : "") + c.notice().text, text_box(2, 0.9f));
        inputs(doc, c);
        scenes(doc, c);
        youtube(doc, c);
        return_feed(doc, c);
        const auto engine = c.engine_stats();
        doc.text("engine", "合成 " + presenter::grouped(static_cast<std::int64_t>(engine.frames)) + " フレーム   遅れ " +
                 presenter::grouped(static_cast<std::int64_t>(engine.late_frames)), text_box(1, 0.8f));
    }, root);
    return doc;
}
}
