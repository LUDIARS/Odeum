#include "lesson_fixture.hpp"
#include "timeline_player.hpp"
#include <map>
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    const auto plot = sample_plot();
    const auto model = make_provisional_model();
    const auto timeline = build_timeline(plot, *model);
    check(timeline.duration == 90 * 60'000, "Lesson length comes from the plot");
    check(timeline.cues.back().kind == CueKind::lesson_end && timeline.cues.back().at == timeline.duration, "Ends with lesson.end");

    Millis previous = 0;
    std::map<std::string, Millis> opened, closed, revealed, started, ended;
    std::size_t narrations = 0, checks = 0;
    for (const auto& cue : timeline.cues) {
        check(cue.at >= previous && cue.at <= timeline.duration, "Cues are ordered and inside the lesson");
        previous = cue.at;
        if (cue.kind == CueKind::section_start) started[cue.section] = cue.at;
        if (cue.kind == CueKind::section_end) ended[cue.section] = cue.at;
        if (cue.kind == CueKind::activity_open) opened[cue.detail.at("id")] = cue.at;
        if (cue.kind == CueKind::activity_close) closed[cue.detail.at("id")] = cue.at;
        if (cue.kind == CueKind::activity_reveal) revealed[cue.detail.at("id")] = cue.at;
        narrations += cue.kind == CueKind::narration;
        checks += cue.kind == CueKind::assignment_check;
    }
    std::size_t points = 0;
    for (const auto& section : plot.sections) {
        points += section.points.size();
        check(started.contains(section.id) && ended.contains(section.id) && started[section.id] < ended[section.id], "Every section has a span");
        if (!section.activity) continue;
        const auto& id = section.activity->id;
        check(opened[id] > started[section.id], "Talk comes before the question");
        check(closed[id] > opened[id] && revealed[id] >= closed[id] && revealed[id] < ended[section.id], "open -> close -> reveal inside the section");
        check(closed[id] - opened[id] <= static_cast<Millis>(section.activity->seconds) * 1000, "Answer time never exceeds the plot");
    }
    check(narrations == points, "One narration cue per talking point");
    check(opened.size() == 6 && checks >= 5, "All activities scheduled; the assignment is checked every minute");
    // Sections are proportional: the 14-minute hit stop section starts after 6 + 10 + 12 minutes.
    check(started["hitstop"] == 28 * 60'000 && ended["hitstop"] == 42 * 60'000, "Section boundaries follow minutes");
    check(timeline_json(timeline).at("cues").size() == timeline.cues.size(), "JSON carries every cue");

    // A plot whose sections add up to more than the lesson is compressed to fit.
    auto squeezed = plot; squeezed.minutes = 45;
    const auto half = build_timeline(squeezed, *model);
    check(half.duration == 45 * 60'000 && half.cues.size() >= timeline.cues.size() - checks, "Scaled to the requested length");
    for (const auto& cue : half.cues) check(cue.at <= half.duration, "Scaled cues stay inside");

    TimelinePlayer player(timeline);
    check(player.state() == TimelinePlayer::State::idle && player.due(1000).empty(), "Idle player emits nothing");
    rejects([&] { player.pause(0); }, "invalid_state");
    player.start(10'000);
    rejects([&] { player.start(10'000); }, "invalid_state");
    auto first = player.due(10'000);
    check(first.size() >= 3 && first[0].kind == CueKind::section_start && first[0].section == "intro", "Start emits the first section");
    check(player.due(10'000).empty(), "Cues are emitted once");
    player.pause(11'000);
    check(player.due(500'000).empty() && player.position(500'000) == 1000, "Paused clock does not advance");
    player.resume(500'000);
    check(player.position(501'000) == 2000, "Resume continues from the pause position");
    check(player.skip_section(501'000), "Skip to the next section");
    const auto skipped = player.due(501'000);
    // The section being left still ends properly before the next one starts.
    check(skipped.size() >= 2 && skipped[0].kind == CueKind::section_end && skipped[0].section == "intro", "Skipping closes the current section");
    check(skipped[1].kind == CueKind::section_start && skipped[1].section == "missing", "Skip lands on the next section start");
    player.seek(timeline.duration, 600'000);
    const auto last = player.due(600'000);
    check(!last.empty() && last.back().kind == CueKind::lesson_end && player.state() == TimelinePlayer::State::finished, "Seeking to the end finishes");
    check(!player.skip_section(600'000), "No section after the end");
}); }
