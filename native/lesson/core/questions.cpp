#include "questions.hpp"
#include "text.hpp"
#include <algorithm>

namespace odeum::lesson {
namespace {
constexpr int vote_weight = 8;  // each "me too" counts for this many score points when ranking
const char* status_name(QuestionBoard::Status status) {
    return status == QuestionBoard::Status::pending ? "pending" : status == QuestionBoard::Status::shared ? "shared" : "dismissed";
}
}
QuestionBoard::Question& QuestionBoard::find(const std::string& id) {
    const auto found = questions_.find(id);
    if (found == questions_.end()) throw ProtocolError("unknown_question", "Unknown question");
    return found->second;
}
int QuestionBoard::rank(const Question& question) const { return question.score + vote_weight * static_cast<int>(question.votes.size()); }

QuestionBoard::Submitted QuestionBoard::submit(const std::string& student, const std::string& name, bool show_name, std::string_view raw,
    const Context& context, Millis now) {
    const auto text = trim(raw);
    const auto length = utf8_length(text);
    if (length == 0 || length > max_scalars) throw ProtocolError("invalid_question", "Question must be 1-280 characters");
    const auto folded = fold(text);
    for (const auto& id : order_) {
        auto& existing = questions_.at(id);
        if (existing.status == Status::dismissed || similarity(existing.folded, folded) < duplicate_similarity) continue;
        const bool own = existing.student == student;
        if (!own) existing.votes.insert(student);
        return {existing.id, true, own, {existing.score, existing.answer, existing.needs_teacher}};
    }
    if (questions_.size() >= max_questions) throw ProtocolError("capacity", "Question limit reached");
    Question question;
    question.id = "q" + std::to_string(++next_);
    question.student = student; question.name = name; question.show_name = show_name;
    question.text = text; question.folded = folded; question.at = now;
    if (context.section) question.section = context.section->id;
    const auto review = model_->review_question(text, context);
    question.score = review.score; question.answer = review.answer; question.needs_teacher = review.needs_teacher;
    order_.push_back(question.id);
    const auto id = question.id;
    questions_.emplace(id, std::move(question));
    return {id, false, false, review};
}
std::size_t QuestionBoard::vote(const std::string& student, const std::string& id) {
    auto& question = find(id);
    if (question.status == Status::dismissed) throw ProtocolError("unknown_question", "Unknown question");
    if (question.student == student) throw ProtocolError("own_question", "Cannot vote for your own question");
    question.votes.insert(student);
    return question.votes.size();
}
std::vector<std::string> QuestionBoard::share_best() {
    std::vector<const Question*> ready;
    // An unanswered question is not shared automatically: the class would get the question without an answer.
    for (const auto& id : order_) {
        const auto& question = questions_.at(id);
        if (question.status == Status::pending && !question.needs_teacher && rank(question) >= share_score) ready.push_back(&question);
    }
    std::stable_sort(ready.begin(), ready.end(), [this](const Question* a, const Question* b) { return rank(*a) > rank(*b); });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < std::min(ready.size(), max_per_share); ++i) out.push_back(ready[i]->id);
    for (const auto& id : out) questions_.at(id).status = Status::shared;
    return out;
}
void QuestionBoard::share(const std::string& id) { find(id).status = Status::shared; }
std::vector<std::string> QuestionBoard::dismiss(const std::string& id) {
    find(id).status = Status::dismissed;
    return audience(id);
}
std::vector<std::string> QuestionBoard::audience(const std::string& id) const {
    const auto& question = questions_.at(id);
    std::vector<std::string> out{question.student};
    out.insert(out.end(), question.votes.begin(), question.votes.end());
    return out;
}
void QuestionBoard::unresolved(const std::string& student, const std::string& id) {
    auto& question = find(id);
    if (question.status == Status::dismissed) throw ProtocolError("unknown_question", "Unknown question");
    if (question.student != student) throw ProtocolError("not_your_question", "Only the asker can say the answer did not help");
    if (question.needs_teacher) throw ProtocolError("no_answer_yet", "This question is already waiting for the teacher");
    question.needs_teacher = true;
    question.unresolved = true;
}
namespace {
const char* state_name(bool dismissed, bool needs_teacher, bool by_teacher) {
    return dismissed ? "dismissed" : needs_teacher ? "waiting" : by_teacher ? "teacher" : "answered";
}
}
Json QuestionBoard::status_for(const std::string& student, const std::string& id) const {
    const auto& question = questions_.at(id);
    return {{"id", question.id}, {"text", question.text}, {"relation", question.student == student ? "asker" : "me_too"},
        {"state", state_name(question.status == Status::dismissed, question.needs_teacher, question.answered_by_teacher)},
        {"answer", question.needs_teacher || question.status == Status::dismissed ? "" : question.answer},
        {"shared", question.status == Status::shared}, {"unresolved", question.unresolved}};
}
Json QuestionBoard::mine(const std::string& student) const {
    Json out = Json::array();
    for (const auto& id : order_) {
        const auto& question = questions_.at(id);
        if (question.student == student || question.votes.contains(student)) out.push_back(status_for(student, id));
    }
    return out;
}
std::vector<std::string> QuestionBoard::take_unanswered(const std::string& student) {
    std::vector<std::string> out;
    for (const auto& id : order_) {
        auto& question = questions_.at(id);
        if (question.status == Status::dismissed || !question.needs_teacher || question.told_unanswered.contains(student)) continue;
        if (question.student != student && !question.votes.contains(student)) continue;
        question.told_unanswered.insert(student);
        out.push_back(id);
    }
    return out;
}
std::size_t QuestionBoard::waiting() const {
    return static_cast<std::size_t>(std::count_if(questions_.begin(), questions_.end(), [](const auto& entry) {
        return entry.second.status != Status::dismissed && entry.second.needs_teacher; }));
}
std::vector<std::string> QuestionBoard::answer(const std::string& id, const std::string& text) {
    auto& question = find(id);
    if (question.status == Status::dismissed) throw ProtocolError("unknown_question", "Unknown question");
    question.answer = text;
    question.needs_teacher = false;
    question.unresolved = false;
    question.answered_by_teacher = true;
    question.told_unanswered.clear();
    return audience(id);
}
void QuestionBoard::withdraw(const std::string& student) {
    for (auto& [id, question] : questions_) if (question.student == student && question.status == Status::pending) question.status = Status::dismissed;
}
Json QuestionBoard::shared_view(const std::string& id) const {
    const auto& question = questions_.at(id);
    // answer_from: where the answer shown to the class came from, so nobody mistakes the "no answer" note for one.
    Json out{{"id", question.id}, {"text", question.text}, {"answer", question.answer}, {"votes", question.votes.size()}, {"section", question.section},
        {"answer_from", question.answered_by_teacher ? "teacher" : question.needs_teacher && !question.unresolved ? "none" : "prepared"}};
    if (question.show_name) out["name"] = question.name;
    return out;
}
Json QuestionBoard::shared_list(const std::string& viewer) const {
    Json out = Json::array();
    for (const auto& id : order_) {
        const auto& question = questions_.at(id);
        if (question.status != Status::shared) continue;
        auto view = shared_view(id);
        if (question.student == viewer) view["mine"] = true;
        if (question.votes.contains(viewer)) view["voted"] = true;
        out.push_back(std::move(view));
    }
    return out;
}
Json QuestionBoard::console_list() const {
    Json out = Json::array();
    for (const auto& id : order_) {
        const auto& question = questions_.at(id);
        out.push_back({{"id", question.id}, {"student", question.student}, {"text", question.text}, {"answer", question.answer},
            {"score", question.score}, {"votes", question.votes.size()}, {"status", status_name(question.status)},
            {"needs_teacher", question.needs_teacher}, {"answered_by_teacher", question.answered_by_teacher}, {"unresolved", question.unresolved}, {"section", question.section}, {"at", question.at}});
    }
    return out;
}
}
