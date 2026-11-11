#include "prep_sheet.hpp"
#include <algorithm>
#include <cmath>

namespace odeum::lesson {
namespace {
// Rows that fit on one slide at a readable size; longer lists continue on the next slide.
constexpr std::size_t bullets_per_slide = 6, terms_per_slide = 5, schedule_per_slide = 9;

template<class Item, class Emit> void paginate(const std::vector<Item>& items, std::size_t per_slide, Emit emit) {
    const auto pages = (items.size() + per_slide - 1) / per_slide;
    for (std::size_t page = 0; page < pages; ++page) {
        const auto first = items.begin() + static_cast<std::ptrdiff_t>(page * per_slide);
        const auto last = items.begin() + static_cast<std::ptrdiff_t>(std::min(items.size(), (page + 1) * per_slide));
        emit(std::vector<Item>(first, last), pages > 1 ? " (" + std::to_string(page + 1) + "/" + std::to_string(pages) + ")" : std::string());
    }
}
void bullets(PrepSheet& sheet, const std::string& title, const std::vector<std::string>& lines) {
    paginate(lines, bullets_per_slide, [&](std::vector<std::string> page, const std::string& suffix) {
        sheet.slides.push_back({SheetSlide::Kind::bullets, title + suffix, {}, std::move(page), {}});
    });
}
}
PrepSheet build_prep_sheet(const Plot& plot, const Timeline& timeline) {
    PrepSheet sheet{plot.title, {}};
    SheetSlide cover{SheetSlide::Kind::cover, plot.title, plot.subtitle, {}, {}};
    if (!plot.engine.empty()) cover.lines.push_back("使うもの: " + plot.engine);
    cover.lines.push_back("授業時間: " + std::to_string(plot.minutes) + " 分");
    if (!plot.audience.empty()) cover.lines.push_back("対象: " + plot.audience);
    sheet.slides.push_back(std::move(cover));

    bullets(sheet, "今日のゴール", plot.goals);
    auto preparation = plot.preparation;
    if (!plot.reference.empty()) preparation.push_back("参考にするサンプル: " + plot.reference);
    bullets(sheet, "授業までに準備すること", preparation);

    std::vector<std::pair<std::string, std::string>> terms;
    for (const auto& term : plot.vocabulary) terms.emplace_back(term.term, term.meaning);
    paginate(terms, terms_per_slide, [&](std::vector<std::pair<std::string, std::string>> page, const std::string& suffix) {
        sheet.slides.push_back({SheetSlide::Kind::terms, "キーワード" + suffix, {}, {}, std::move(page)});
    });

    std::size_t number = 0;
    for (const auto& section : plot.sections) {
        if (!section.prep) continue;
        sheet.slides.push_back({SheetSlide::Kind::question, section.title, "予習の問い " + std::to_string(++number),
            {section.prep->question, section.prep->hint}, {}});
    }

    // Minutes come from the generated timeline, so the sheet shows what the class will actually run.
    std::vector<std::pair<std::string, std::string>> schedule;
    for (const auto& cue : timeline.cues) {
        if (cue.kind != CueKind::section_start) continue;
        const auto minutes = static_cast<long>(std::lround(cue.detail.at("minutes").get<double>()));
        schedule.emplace_back(std::to_string(minutes) + " 分", cue.detail.at("title").get<std::string>());
    }
    paginate(schedule, schedule_per_slide, [&](std::vector<std::pair<std::string, std::string>> page, const std::string& suffix) {
        sheet.slides.push_back({SheetSlide::Kind::schedule, "授業の流れ" + suffix, {}, {}, std::move(page)});
    });
    return sheet;
}
}
