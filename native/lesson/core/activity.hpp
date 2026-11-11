#pragma once
#include "model.hpp"
#include <map>

namespace odeum::lesson {
// One in-class question, answered from each student's own device. Answers stay hidden from
// the class until the reveal, so nobody copies the majority.
class Activity {
public:
    enum class State { open, closed, revealed };
    static constexpr std::size_t max_answer_scalars = 400, max_highlights = 3;
    Activity(const ActivityPlan& plan, const LessonModel& model) : plan_(&plan), model_(&model) {}
    // body: {"choices":[...]} for choice activities, {"text":"..."} otherwise. A later answer replaces the earlier one.
    void answer(const std::string& student, const Json& body);
    void close();
    void reveal();
    State state() const { return state_; }
    const ActivityPlan& plan() const { return *plan_; }
    bool takes_text() const { return plan_->kind != ActivityKind::choice; }
    bool answered(const std::string& student) const { return answers_.contains(student); }
    std::size_t answered_count() const { return answers_.size(); }
    Json prompt() const;                               // what students see while it is open
    Json tally() const;                                // counts only; safe to show before the reveal
    Json results() const;                              // tally + answer key + highlighted answers (anonymous)
    Json personal(const std::string& student) const;   // one student's own result and feedback
private:
    struct Answer { std::vector<int> choices; std::string text; Grade grade; };
    const ActivityPlan* plan_;
    const LessonModel* model_;
    State state_ = State::open;
    std::map<std::string, Answer> answers_;
    bool choice_correct(const Answer& answer) const;
};
std::string_view activity_state_name(Activity::State state);
}
