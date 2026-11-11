#pragma once
#include "model.hpp"

namespace odeum::lesson {
enum class CueKind { section_start, slide, narration, demo, activity_open, activity_close, activity_reveal,
    assignment_assign, assignment_check, assignment_close, questions_share, section_end, lesson_end };
struct Cue {
    Millis at = 0;        // offset from the start of the lesson
    CueKind kind = CueKind::narration;
    std::string section;  // section id; empty for lesson_end
    Json detail;          // kind-specific payload, already in wire form
};
struct Timeline {
    Millis duration = 0;
    std::vector<Cue> cues;  // ordered by `at`; ties keep authoring order
};
// Lays the plot out on the clock. Section lengths are the plot's `minutes` scaled so the
// lesson ends at plot.minutes; inside a section the talk comes first, then the activity
// (open -> close -> reveal), then the assignment with periodic progress checks.
Timeline build_timeline(const Plot& plot, const LessonModel& model);
std::string_view cue_name(CueKind kind);
Json timeline_json(const Timeline& timeline);
}
