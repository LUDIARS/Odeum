#pragma once
#include "model.hpp"
#include <map>
#include <optional>

namespace odeum::lesson {
// Tracks each student's progress on the assignment and decides who needs a word from the
// lecturer. Progress arrives from the student's checklist or from a tool reporting on the
// student's behalf (the Ergo Studio side); both use report().
class AssignmentBoard {
public:
    enum class Kind { start_nudge, hint, class_hint, praise, escalate };
    struct Intervention {
        Kind kind;
        std::string student;  // empty for class_hint
        std::size_t step = 0;
        std::size_t level = 0;
        std::string text;
    };
    static constexpr Millis start_grace_ms = 90'000;   // untouched for this long after assigning -> nudge
    static constexpr Millis help_cooldown_ms = 15'000;  // a second "help" inside this window is the same request
    static constexpr std::size_t class_stall_minimum = 3;
    static constexpr double class_stall_share = 0.4;

    AssignmentBoard(const AssignmentPlan& plan, const LessonModel& model, Millis now) : plan_(&plan), model_(&model), assigned_(now) {}
    void enroll(const std::string& student, Millis now);
    void withdraw(const std::string& student);
    void report(const std::string& student, std::size_t step, bool done, Millis now);
    void request_help(const std::string& student, Millis now);
    // Call on every tick. Each returned intervention is produced once; repeated calls stay quiet
    // until the student's situation changes or the next stall interval passes.
    std::vector<Intervention> monitor(Millis now);
    const AssignmentPlan& plan() const { return *plan_; }
    Json brief() const;                               // what students see
    Json progress(const std::string& student) const;  // one student's checklist
    Json overview(Millis now) const;                  // console: everyone
private:
    struct Entry {
        std::vector<bool> done;
        std::vector<std::size_t> hints;  // hints already given per step
        Millis touched = 0;              // last progress or last intervention
        Millis helped = -1;
        bool started = false, nudged = false, praised = false, wants_help = false, escalated = false;
    };
    const AssignmentPlan* plan_;
    const LessonModel* model_;
    Millis assigned_;
    std::map<std::string, Entry> entries_;
    std::vector<bool> class_hinted_ = std::vector<bool>(plan_->steps.size());
    static std::optional<std::size_t> current_step(const Entry& entry);
    bool stalled(const Entry& entry, Millis now) const;
    Intervention next_hint(const std::string& student, Entry& entry, std::size_t step, Millis now);
};
}
