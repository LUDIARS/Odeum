#pragma once
#include "types.hpp"
#include <map>
#include <optional>

namespace odeum::lesson {
// The plot is what a lesson author writes: sections with talking points, demos, in-class
// activities and an assignment. Everything else (timeline, prep sheet, monitoring) derives from it.

// One thing a good answer mentions. `keywords` are what the provisional model looks for;
// a delivered model may read `label` instead.
struct RubricPoint { std::string id, label; std::vector<std::string> keywords; };

// choice: pick from options. text: short answer with expected points.
// missing: "what is missing from this implementation?" (expected points are the removed parts).
// open: no right answer; points are perspectives used only to group what the class said.
enum class ActivityKind { choice, text, missing, open };
struct ActivityPlan {
    std::string id, prompt, code, model_answer;
    ActivityKind kind = ActivityKind::text;
    std::vector<std::string> choices;
    std::vector<int> correct;      // choice only; empty means a survey without a right answer
    bool multi = false;            // choice only
    std::vector<RubricPoint> points;
    std::size_t min_points = 1;    // text / missing: points needed to count as "got it"
    int seconds = 120;
};
// Lets a tool on the student's machine (Ergo Studio) tick the step: it is done once the variable
// the running game publishes satisfies the comparison. Steps without a check are ticked by hand.
struct StepCheck { std::string variable, op; Json value; };
struct StepPlan { std::string id, label; std::vector<std::string> hints; std::optional<StepCheck> check; };
struct AssignmentPlan {
    std::string id, title, brief, extra;  // extra: a stretch goal offered to students who finish
    std::vector<StepPlan> steps;
    int minutes = 10;
    int stall_seconds = 180;              // no progress for this long counts as stalled
};
// A switch the presenter flips in the running game (for example "hit_stop" off, then on).
struct DemoPlan { std::string feature, note; bool enabled = true; std::size_t after = 0; };
struct PrepQuestion { std::string question, hint; };
struct Faq { std::vector<std::string> keywords; std::string answer; };
struct Term { std::string term, meaning; };
struct Section {
    std::string id, title;
    int minutes = 5;
    std::vector<std::string> points;    // narration lines, in order
    std::vector<std::string> keywords;  // what counts as on-topic while this section runs
    std::vector<DemoPlan> demos;
    std::optional<ActivityPlan> activity;
    std::optional<AssignmentPlan> assignment;
    std::optional<PrepQuestion> prep;
    bool share_questions = false;
};
struct Plot {
    std::string id, title, subtitle, engine, reference, audience;
    int minutes = 60;
    std::vector<std::string> goals, preparation;
    std::vector<Term> vocabulary;
    std::vector<Section> sections;
    std::vector<Faq> faq;
    // Demo feature -> the bool variable that switches it in the running game. With this map a
    // tool on the presenter's machine can flip demos itself; without it demos are only announced.
    std::map<std::string, std::string> tool_features;
};
// Throws ProtocolError("invalid_plot") naming the offending field.
Plot parse_plot(const Json& document);
std::string_view activity_kind_name(ActivityKind kind);
const Section* find_section(const Plot& plot, std::string_view id);
const ActivityPlan* find_activity(const Plot& plot, std::string_view id);
const AssignmentPlan* find_assignment(const Plot& plot, std::string_view id);
// True when some assignment step can be ticked by a tool watching the student's game.
bool has_checked_steps(const Plot& plot);
}
