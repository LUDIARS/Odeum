#include "overlay_view.hpp"
#include "good_fountain.hpp"
#include "labels.hpp"
#include <algorithm>
#include <cmath>

namespace odeum::presenter {
namespace {
constexpr float pad = 12, row = 24, fountain_height = 170;

tela::Layout at(float x, float y, float width, float height = 0, unsigned lines = 1, float scale = 1) {
    tela::Layout layout;
    layout.positioned = true;
    layout.x = x;
    layout.y = y;
    layout.width = width;
    layout.height = height;
    layout.lines = lines;
    layout.text_scale = scale;
    layout.padding = 2;
    return layout;
}

tela::Color mix(tela::Color a, tela::Color b, float t, float alpha) {
    const auto lerp = [t](unsigned char x, unsigned char y) { return static_cast<unsigned char>(std::lround(x + (y - x) * t)); };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), static_cast<unsigned char>(std::lround(255 * std::clamp(alpha, 0.f, 1.f)))};
}

tela::Color stamp_color(const std::string& kind) {
    if (kind == "clap") return {255, 196, 77, 255};
    if (kind == "laugh") return {255, 140, 66, 255};
    if (kind == "wow") return {173, 120, 255, 255};
    if (kind == "question") return {80, 180, 255, 255};
    return {96, 214, 140, 255}; // agree
}

void fountain(tela::Document& doc, const OverlayViewInput& in) {
    tela::Drawing drawing;
    for (const auto& p : fountain_particles(in.state, in.now, overlay_width, fountain_height)) {
        const auto color = mix({255, 205, 80, 255}, {255, 84, 140, 255}, p.warmth, p.alpha);
        tela::Stroke glow{{0, 0, 0, 0}, 0};
        glow.glow = {mix({255, 230, 160, 255}, {255, 140, 190, 255}, p.warmth, p.alpha * 0.6f), p.radius * 0.8f};
        drawing.ellipse({p.x - p.radius, p.y - p.radius, p.radius * 2, p.radius * 2}, color, glow);
    }
    doc.canvas("fountain", std::move(drawing), at(0, 32, overlay_width, fountain_height));
}

float stamps(tela::Document& doc, const OverlayViewInput& in, float y) {
    const auto& list = in.state.stamps();
    // Newest at the top; four rows keep the overlay compact during an applause.
    const auto shown = std::min<std::size_t>(list.size(), 4);
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& stamp = list[list.size() - 1 - i];
        const auto remaining = static_cast<float>(stamp.expires_at - in.now) / static_cast<float>(in.state.limits().stamp_lifetime);
        tela::Drawing chip;
        auto color = stamp_color(stamp.kind_or_text);
        color.a = static_cast<unsigned char>(std::lround(255 * std::clamp(remaining * 2, 0.f, 1.f)));
        chip.rectangle({0, 4, 52, row - 6}, color, {{0, 0, 0, 0}, 0}, 8);
        const auto id = std::to_string(i);
        doc.canvas("stamp-chip-" + id, std::move(chip), at(pad, y, 52, row));
        doc.text("stamp-kind-" + id, stamp_label(stamp.kind_or_text), at(pad + 6, y, 48, row, 1, 0.8f));
        doc.text("stamp-name-" + id, stamp.name.empty() ? "(名前なし)" : stamp.name, at(pad + 60, y, overlay_width - pad * 2 - 60, row));
        y += row;
    }
    return y;
}

float poll(tela::Document& doc, const PollTally& poll, float y) {
    const float width = overlay_width - pad * 2;
    doc.text("poll-question", (poll.open ? "投票: " : "投票結果: ") + poll.question, at(pad, y, width, row * 2, 2));
    y += row * 2 + 2;
    std::int64_t top = 1;
    for (auto n : poll.counts) top = std::max(top, n);
    for (std::size_t i = 0; i < poll.choices.size(); ++i) {
        const auto count = i < poll.counts.size() ? poll.counts[i] : 0;
        tela::Drawing bar;
        bar.rectangle({0, 3, width, row - 6}, {255, 255, 255, 36}, {{0, 0, 0, 0}, 0}, 6);
        const float fill = width * static_cast<float>(count) / static_cast<float>(top);
        if (count > 0) bar.rectangle({0, 3, fill, row - 6}, poll.open ? tela::Color{90, 160, 255, 200} : tela::Color{120, 210, 150, 220}, {{0, 0, 0, 0}, 0}, 6);
        const auto id = std::to_string(i);
        doc.canvas("poll-bar-" + id, std::move(bar), at(pad, y, width, row));
        doc.text("poll-choice-" + id, poll.choices[i] + "  " + grouped(count), at(pad + 4, y, width - 8, row, 1, 0.85f));
        y += row;
    }
    doc.text("poll-answered", "回答 " + grouped(poll.answered) + " 人" + (poll.multi ? "（複数選択）" : ""), at(pad, y, width, row, 1, 0.85f));
    return y + row + 4;
}

void comments(tela::Document& doc, const OverlayViewInput& in, float y) {
    const auto& list = in.state.comments();
    const float width = overlay_width - pad * 2;
    const auto room = static_cast<std::size_t>(std::max(0.f, (overlay_height - pad - y) / (row * 2)));
    const auto shown = std::min(list.size(), room);
    // Oldest of the shown ones first so new comments flow in at the bottom.
    for (std::size_t i = 0; i < shown; ++i) {
        const auto& comment = list[list.size() - shown + i];
        doc.text("comment-" + std::to_string(i), (comment.name.empty() ? "" : comment.name + ": ") + comment.kind_or_text,
                 at(pad, y, width, row * 2, 2, 0.9f));
        y += row * 2;
    }
}
}

tela::Theme overlay_theme() {
    tela::Theme theme;
    theme.panel = {16, 20, 30, 0};
    theme.text = {248, 248, 252, 255};
    theme.font_size = 15;
    theme.line_height = 22;
    return theme;
}

tela::Document overlay_document(const OverlayViewInput& in) {
    tela::Document doc;
    tela::Drawing backdrop;
    backdrop.rectangle({0, 0, overlay_width, overlay_height}, {16, 20, 30, 168}, {{255, 255, 255, 40}, 1}, 14);
    doc.canvas("backdrop", std::move(backdrop), at(0, 0, overlay_width, overlay_height));
    const auto& state = in.state;
    doc.text(overlay_grip, "::: Odeum   視聴 " + grouped(state.viewer_count()) + " 人", at(0, 0, overlay_width, 32),
             tela::InputPolicy::exclusive);
    fountain(doc, in);
    doc.text("good-total", "グッド " + grouped(state.good_total()), at(pad, 32 + fountain_height - 44, overlay_width - pad * 2, 40, 1, 1.6f));
    float y = 32 + fountain_height + 6;
    if (!state.stamp_totals().empty()) {
        std::string totals = "スタンプ";
        for (const auto& [kind, n] : state.stamp_totals()) totals += "  " + stamp_label(kind) + " " + grouped(n);
        doc.text("stamp-totals", totals, at(pad, y, overlay_width - pad * 2, row, 1, 0.8f));
        y += row;
    }
    y = stamps(doc, in, y);
    if (state.poll()) y = poll(doc, *state.poll(), y + 4);
    if (in.comments_visible) comments(doc, in, y + 4);
    return doc;
}
}
