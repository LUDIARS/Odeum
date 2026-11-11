#pragma once
#include "plot.hpp"
#include <memory>

namespace odeum::lesson {
// What the lecturer model decides. The real model is delivered later; until then
// make_provisional_model() answers from the plot's own rubric, hints and FAQ by keyword rules.
// Implementations must be pure with respect to their arguments: the classroom calls them
// from its own thread and tests replay them.

struct Grade {
    std::vector<std::string> matched;  // rubric point ids the answer mentions
    bool has_verdict = false;          // false for open activities
    bool correct = false;
};
enum class Intent { question, answer, help, chatter, disruption };
struct Reading {
    Intent intent = Intent::chatter;
    int severity = 0;      // disruption only: 1 = noise, 2 = abuse
    bool on_topic = false;
};
struct QuestionReview {
    int score = 0;               // 0..100, how much the whole class gains from hearing it
    std::string answer;
    bool needs_teacher = false;  // the model has no grounded answer
};
struct Context {
    const Plot* plot = nullptr;
    const Section* section = nullptr;  // null before the lesson starts
    bool text_activity_open = false;
    bool assignment_active = false;
};
class LessonModel {
public:
    virtual ~LessonModel() = default;
    virtual std::string name() const = 0;
    // Turns one talking point into what the lecturer says. `seconds` is the time until the next
    // line, so a model can elaborate to fill it; the returned text is spoken and shown as a caption.
    virtual std::string narrate(const Plot& plot, const Section& section, const std::string& point, double seconds) const = 0;
    virtual Grade grade(const ActivityPlan& activity, std::string_view answer) const = 0;
    // What to tell one student once the activity is revealed.
    virtual std::string feedback(const ActivityPlan& activity, const Grade& grade) const = 0;
    virtual Reading read(std::string_view utterance, const Context& context) const = 0;
    virtual QuestionReview review_question(std::string_view question, const Context& context) const = 0;
    // level counts from 0; implementations clamp to the last available hint.
    virtual std::string hint(const AssignmentPlan& assignment, std::size_t step, std::size_t level) const = 0;
};
std::unique_ptr<LessonModel> make_provisional_model();
}
