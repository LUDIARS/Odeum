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
    struct Submitted { std::string id; bool duplicate = false; QuestionReview review; };

    explicit QuestionBoard(const LessonModel& model) : model_(&model) {}
    // A near-identical earlier question absorbs this one as a "me too" instead of creating a new entry.
    Submitted submit(const std::string& student, const std::string& name, bool show_name, std::string_view text,
        const Context& context, Millis now);
    void vote(const std::string& student, const std::string& id);
    // Marks up to max_per_share of the best pending questions as shared and returns their ids, best first.
    std::vector<std::string> share_best();
    void share(const std::string& id);
    void dismiss(const std::string& id);
    // Removes a student's pending questions (used when the student is ejected).
    void withdraw(const std::string& student);
    Json shared_view(const std::string& id) const;  // what the class sees
    Json shared_list() const;
    Json console_list() const;                      // console: all, with asker ids and scores
    bool knows(const std::string& id) const { return questions_.contains(id); }
    const std::string& asker(const std::string& id) const { return questions_.at(id).student; }
private:
    struct Question {
        std::string id, student, name, text, answer;
        std::u32string folded;
        std::string section;
        std::set<std::string> votes;
        int score = 0;
        Millis at = 0;
        Status status = Status::pending;
        bool show_name = false, needs_teacher = false;
    };
    const LessonModel* model_;
    std::map<std::string, Question> questions_;
    std::vector<std::string> order_;
    std::size_t next_ = 0;
    int rank(const Question& question) const;
    Question& find(const std::string& id);
};
}
