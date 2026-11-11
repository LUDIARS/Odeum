#include "timeline.hpp"
#include "text.hpp"
#include <algorithm>
#include <array>
#include <numeric>

namespace odeum::lesson {
namespace {
constexpr std::array cue_names{"section.start", "slide", "narration", "demo", "activity.open", "activity.close", "activity.reveal",
    "assignment.assign", "assignment.check", "assignment.close", "questions.share", "section.end", "lesson.end"};
constexpr Millis reveal_talk_ms = 45'000;       // time kept after a reveal to discuss the results
constexpr Millis check_interval_ms = 60'000;    // how often the lecturer looks at assignment progress
// The talk always keeps at least this share of a section, so narration never collapses to one instant.
constexpr double min_talk_share = 0.2;

Json activity_json(const ActivityPlan& plan) {
    Json out{{"id", plan.id}, {"kind", activity_kind_name(plan.kind)}, {"prompt", plan.prompt}, {"seconds", plan.seconds}};
    if (!plan.code.empty()) out["code"] = plan.code;
    if (plan.kind == ActivityKind::choice) { out["choices"] = plan.choices; out["multi"] = plan.multi; }
    return out;
}
}

Timeline build_timeline(const Plot& plot, const LessonModel& model) {
    Timeline timeline;
    timeline.duration = static_cast<Millis>(plot.minutes) * 60'000;
    const auto total = std::accumulate(plot.sections.begin(), plot.sections.end(), 0, [](int sum, const Section& s) { return sum + s.minutes; });
    const auto add = [&](Millis at, CueKind kind, const Section& section, Json detail) {
        timeline.cues.push_back({at, kind, section.id, std::move(detail)});
    };
    int elapsed = 0;
    for (const auto& section : plot.sections) {
        const Millis begin = timeline.duration * elapsed / total;
        elapsed += section.minutes;
        const Millis end = timeline.duration * elapsed / total;
        const Millis span = end - begin;
        const auto min_talk = static_cast<Millis>(static_cast<double>(span) * min_talk_share);

        Millis activity_span = 0, answer_span = 0;
        if (section.activity) {
            activity_span = std::min<Millis>(static_cast<Millis>(section.activity->seconds) * 1000 + reveal_talk_ms, span - min_talk);
            answer_span = std::max<Millis>(activity_span - reveal_talk_ms, activity_span / 2);
        }
        Millis work_span = 0;
        if (section.assignment)
            work_span = std::min<Millis>(timeline.duration * section.assignment->minutes / total,
                span - min_talk - activity_span);
        const Millis talk = span - activity_span - work_span;

        add(begin, CueKind::section_start, section, {{"title", section.title}, {"minutes", static_cast<double>(span) / 60'000.0}});
        add(begin, CueKind::slide, section, {{"title", section.title}, {"points", section.points}});

        // Each line gets a share of the talk proportional to its length, so long explanations are not rushed.
        std::vector<std::size_t> lengths;
        for (const auto& point : section.points) lengths.push_back(std::max<std::size_t>(1, decode_utf8(point).size()));
        const auto characters = std::accumulate(lengths.begin(), lengths.end(), std::size_t{0});
        std::vector<Millis> starts;
        std::size_t spoken = 0;
        for (const auto length : lengths) {
            starts.push_back(begin + static_cast<Millis>(static_cast<double>(talk) * static_cast<double>(spoken) / static_cast<double>(characters)));
            spoken += length;
        }
        for (std::size_t i = 0; i <= section.points.size(); ++i) {
            // A demo with after = n is flipped once n lines have been spoken, right before line n + 1.
            for (const auto& demo : section.demos) {
                if (std::min(demo.after, section.points.size()) != i) continue;
                add(i < starts.size() ? starts[i] : begin + talk, CueKind::demo, section,
                    {{"feature", demo.feature}, {"enabled", demo.enabled}, {"note", demo.note}});
            }
            if (i < section.points.size()) {
                const auto seconds = static_cast<double>((i + 1 < starts.size() ? starts[i + 1] : begin + talk) - starts[i]) / 1000.0;
                add(starts[i], CueKind::narration, section, {{"text", model.narrate(plot, section, section.points[i], seconds)}, {"seconds", seconds}});
            }
        }
        if (section.activity) {
            const Millis open = begin + talk;
            add(open, CueKind::activity_open, section, activity_json(*section.activity));
            add(open + answer_span, CueKind::activity_close, section, {{"id", section.activity->id}});
            add(open + answer_span, CueKind::activity_reveal, section, {{"id", section.activity->id}});
        }
        if (section.assignment) {
            const Millis assign = begin + talk + activity_span;
            add(assign, CueKind::assignment_assign, section, {{"id", section.assignment->id}});
            for (Millis at = assign + check_interval_ms; at < end; at += check_interval_ms)
                add(at, CueKind::assignment_check, section, {{"id", section.assignment->id}});
            add(end, CueKind::assignment_close, section, {{"id", section.assignment->id}});
        }
        if (section.share_questions) add(end, CueKind::questions_share, section, Json::object());
        add(end, CueKind::section_end, section, Json::object());
    }
    timeline.cues.push_back({timeline.duration, CueKind::lesson_end, {}, Json::object()});
    std::stable_sort(timeline.cues.begin(), timeline.cues.end(), [](const Cue& a, const Cue& b) { return a.at < b.at; });
    return timeline;
}
std::string_view cue_name(CueKind kind) { return cue_names.at(static_cast<std::size_t>(kind)); }
Json timeline_json(const Timeline& timeline) {
    Json cues = Json::array();
    for (const auto& cue : timeline.cues)
        cues.push_back({{"at_ms", cue.at}, {"kind", cue_name(cue.kind)}, {"section", cue.section}, {"detail", cue.detail}});
    return {{"duration_ms", timeline.duration}, {"cues", std::move(cues)}};
}
}
