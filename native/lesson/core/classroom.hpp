#pragma once
#include "activity.hpp"
#include "assignment.hpp"
#include "conduct.hpp"
#include "questions.hpp"
#include "timeline_player.hpp"
#include "voice.hpp"
#include <functional>
#include <map>
#include <memory>
#include <optional>

namespace odeum::lesson {
// One class in session: the roster and everything that happens between the lecturer model,
// the students and the operator console. It performs no I/O and reads no clock; the transport
// feeds it messages and ticks and delivers the Outbox it returns.
//
// Names and what students say live here only while the class runs. Nothing is written to disk.
class Classroom {
public:
    using TokenSource = std::function<std::string()>;
    struct Admission { std::string student, resume; };
    static constexpr std::size_t default_max_students = 60, max_utterance_scalars = 280, max_tell_scalars = 400;
    static constexpr Millis snapshot_interval_ms = 500;

    Classroom(Plot plot, std::shared_ptr<const LessonModel> model, TokenSource tokens, std::size_t max_students = default_max_students);
    Classroom(const Classroom&) = delete;
    Classroom& operator=(const Classroom&) = delete;

    // `name` must already be validated by the caller (1-32 scalars, no control characters).
    // A known resume token returns the same student; throws ejected / entry_locked / capacity.
    Admission admit(const std::string& name, std::string_view resume);
    Outbox connect(const std::string& student, Millis now);
    Outbox disconnect(const std::string& student, Millis now);
    Outbox from_student(const std::string& student, const Json& message, Millis now);
    Outbox from_console(const Json& message, Millis now);
    // A tool on a participant's machine (Ergo Studio) reads what it should act on and reports
    // assignment progress it observed. An empty `student` is the presenter's tool.
    Json tool_state(const std::string& student) const;
    Outbox from_tool(const std::string& student, const Json& message, Millis now);
    Outbox tick(Millis now);
    Json console_welcome(Millis now) const;   // timeline + first snapshot for a console that just connected
    Json console_snapshot(Millis now) const;
    const Plot& plot() const { return plot_; }
    bool has_student(const std::string& student) const { return students_.contains(student); }
private:
    struct Student { std::string id, name, resume; unsigned connections = 0; };
    Plot plot_;
    std::shared_ptr<const LessonModel> model_;
    TokenSource tokens_;
    std::size_t max_students_;
    TimelinePlayer player_;
    std::map<std::string, Student> students_;
    std::map<std::string, std::string, std::less<>> resumes_;  // resume token -> student id
    std::size_t next_student_ = 0;
    bool entry_locked_ = false;

    const Section* section_ = nullptr;
    Json slide_, narration_;                       // last shown, replayed to late joiners
    std::optional<Activity> activity_;
    std::optional<AssignmentBoard> assignment_;
    bool assignment_active_ = false;
    std::map<std::string, bool> demo_states_;      // what the demos have switched so far
    std::uint64_t demo_seq_ = 0;                   // grows with every demo cue, so a tool can tell old from new
    QuestionBoard questions_;
    Conduct conduct_;
    VoiceDesk voice_;
    SpeakerRegistry speakers_;
    bool dirty_ = true;                            // console snapshot is out of date
    Millis last_snapshot_ = -snapshot_interval_ms;

    Context context() const;
    Student& student(const std::string& id);

    // classroom.cpp: what students send
    void answer(Outbox& out, const Student& from, const Json& message, Millis now);
    void report_progress(Outbox& out, const std::string& student, const Json& message, Millis now);
    void ask(Outbox& out, const Student& from, const std::string& text, bool show_name, Millis now);
    void report_stuck(Outbox& out, const Student& from, const std::string& text, Millis now);
    void hear(Outbox& out, const Student& from, const std::string& text, Millis now);
    void ask_for_help(Outbox& out, const Student& from, Millis now);
    void apply(Outbox& out, const std::string& student, const Conduct::Verdict& verdict, Millis now);
    bool allowed_to_post(Outbox& out, const Student& from, Millis now);

    // classroom_cues.cpp: what the timeline and the operator drive
    void run(Outbox& out, const Cue& cue, Millis now);
    void open_activity(Outbox& out, const std::string& id);
    void close_activity(Outbox& out);
    void reveal_activity(Outbox& out);
    void assign(Outbox& out, const std::string& id, Millis now);
    void close_assignment(Outbox& out);
    void share_questions(Outbox& out, const std::vector<std::string>& ids);
    void answer_question(Outbox& out, const std::string& id, const std::string& text);
    void supervise(Outbox& out, Millis now);
    void switch_demo(const std::string& feature, bool enabled);
    void resync_demos(Millis now);

    // classroom_views.cpp
    Json welcome(const Student& to, Millis now) const;
    Json standing_json(const std::string& student, Millis now) const;
};
}
