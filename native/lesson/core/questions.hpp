#pragma once
#include "model.hpp"
#include <map>
#include <set>

namespace odeum::lesson {
// Collects students' questions, answers each asker privately, and picks the ones worth the
// whole class's time. A shared question carries no name unless its asker chose to show it.
class QuestionBoard {
public:
    enum class Status { pending, shared, dismissed };
    static constexpr std::size_t max_scalars = 280, max_questions = 500, max_per_share = 2;
    static constexpr int share_score = 70;          // model score at which a question is shared without the operator
    static constexpr double duplicate_similarity = 0.6;
    // own: a duplicate of the same student's earlier question (not counted as a "me too").
    struct Submitted { std::string id; bool duplicate = false, own = false; QuestionReview review; };

    explicit QuestionBoard(const LessonModel& model) : model_(&model) {}
    // A near-identical earlier question absorbs this one as a "me too" instead of creating a new entry.
    Submitted submit(const std::string& student, const std::string& name, bool show_name, std::string_view text,
        const Context& context, Millis now);
    // A "me too" from someone other than the asker; the asker's own vote is refused (own_question).
    // Returns the question's vote count afterwards.
    std::size_t vote(const std::string& student, const std::string& id);
    // Marks up to max_per_share of the best pending questions as shared and returns their ids, best first.
    std::vector<std::string> share_best();
    void share(const std::string& id);
    // Returns who should be told: the asker first, then everyone who said "me too".
    std::vector<std::string> dismiss(const std::string& id);
    // The asker says the answer they received did not settle it (「まだ分からない」). The question goes
    // back to waiting for the human teacher and is no longer shared automatically.
    // Throws unknown_question / not_your_question / no_answer_yet.
    void unresolved(const std::string& student, const std::string& id);
    // Who hears about a change to this question: the asker first, then the me-toos.
    std::vector<std::string> audience(const std::string& id) const;
    // One question as `student` sees it in their own list: {id, text, state, answer, shared, relation}.
    // state: answered (a prepared answer) / waiting (先生の回答待ち) / teacher (先生から回答) / dismissed (見送り).
    Json status_for(const std::string& student, const std::string& id) const;
    Json mine(const std::string& student) const;   // the questions `student` asked or said "me too" to
    // Questions still waiting for the teacher that `student` asked or joined and has not yet been told
    // about; they are marked as told.
    std::vector<std::string> take_unanswered(const std::string& student);
    std::size_t waiting() const;                    // questions waiting for the human teacher
    // The human teacher answers a question. It stops needing the teacher and may now be shared.
    // Returns who should hear the answer: the asker first, then everyone who said "me too".
    std::vector<std::string> answer(const std::string& id, const std::string& text);
    // Removes a student's pending questions (used when the student is ejected).
    void withdraw(const std::string& student);
    Json shared_view(const std::string& id) const;  // what the class sees
    // `viewer` marks the viewer's own questions (`mine`) and the ones they already said "me too" to (`voted`).
    Json shared_list(const std::string& viewer) const;
    bool shared(const std::string& id) const { return questions_.at(id).status == Status::shared; }
    std::size_t votes(const std::string& id) const { return questions_.at(id).votes.size(); }
    Json console_list() const;                      // console: all, with asker ids and scores
    bool knows(const std::string& id) const { return questions_.contains(id); }
    const std::string& asker(const std::string& id) const { return questions_.at(id).student; }
    const std::string& text(const std::string& id) const { return questions_.at(id).text; }
private:
    struct Question {
        std::string id, student, name, text, answer;
        std::u32string folded;
        std::string section;
        std::set<std::string> votes;
        int score = 0;
        Millis at = 0;
        Status status = Status::pending;
        bool show_name = false, needs_teacher = false, answered_by_teacher = false, unresolved = false;
        std::set<std::string> told_unanswered;   // students already told it was not answered in class
    };
    const LessonModel* model_;
    std::map<std::string, Question> questions_;
    std::vector<std::string> order_;
    std::size_t next_ = 0;
    int rank(const Question& question) const;
    Question& find(const std::string& id);
};
}
