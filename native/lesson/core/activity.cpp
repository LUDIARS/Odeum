#include "activity.hpp"
#include "text.hpp"
#include <algorithm>
#include <array>
#include <set>

namespace odeum::lesson {
void Activity::answer(const std::string& student, const Json& body) {
    if (state_ != State::open) throw ProtocolError("activity_closed", "This question is no longer taking answers");
    Answer answer;
    if (plan_->kind == ActivityKind::choice) {
        const auto found = body.find("choices");
        if (found == body.end() || !found->is_array() || found->empty() || found->size() > plan_->choices.size() || (!plan_->multi && found->size() != 1))
            throw ProtocolError("invalid_answer", "Invalid choices");
        std::set<int> unique;
        for (const auto& item : *found) {
            if (!item.is_number_integer()) throw ProtocolError("invalid_answer", "Invalid choices");
            const auto index = item.get<std::int64_t>();
            if (index < 0 || index >= static_cast<std::int64_t>(plan_->choices.size()) || !unique.insert(static_cast<int>(index)).second)
                throw ProtocolError("invalid_answer", "Invalid choices");
        }
        answer.choices.assign(unique.begin(), unique.end());
    } else {
        const auto found = body.find("text");
        if (found == body.end() || !found->is_string()) throw ProtocolError("invalid_answer", "Answer text required");
        answer.text = trim(found->get_ref<const std::string&>());
        const auto length = utf8_length(answer.text);
        if (length == 0 || length > max_answer_scalars) throw ProtocolError("invalid_answer", "Answer must be 1-400 characters");
        answer.grade = model_->grade(*plan_, answer.text);
    }
    answers_[student] = std::move(answer);
}
void Activity::close() { if (state_ == State::open) state_ = State::closed; }
void Activity::reveal() { state_ = State::revealed; }
bool Activity::choice_correct(const Answer& answer) const {
    auto expected = plan_->correct; std::sort(expected.begin(), expected.end());
    return answer.choices == expected;
}
Json Activity::prompt() const {
    Json out{{"id", plan_->id}, {"kind", activity_kind_name(plan_->kind)}, {"prompt", plan_->prompt}, {"seconds", plan_->seconds},
        {"state", activity_state_name(state_)}};
    if (!plan_->code.empty()) out["code"] = plan_->code;
    if (plan_->kind == ActivityKind::choice) { out["choices"] = plan_->choices; out["multi"] = plan_->multi; }
    return out;
}
Json Activity::tally() const {
    Json out{{"id", plan_->id}, {"answered", answers_.size()}};
    if (plan_->kind == ActivityKind::choice) {
        std::vector<int> counts(plan_->choices.size());
        for (const auto& [student, answer] : answers_) for (const int choice : answer.choices) ++counts.at(static_cast<std::size_t>(choice));
        out["counts"] = counts;
        return out;
    }
    Json points = Json::array();
    for (const auto& point : plan_->points) {
        const auto count = std::count_if(answers_.begin(), answers_.end(), [&](const auto& entry) {
            const auto& matched = entry.second.grade.matched;
            return std::find(matched.begin(), matched.end(), point.id) != matched.end(); });
        points.push_back({{"id", point.id}, {"label", point.label}, {"count", count}});
    }
    out["points"] = std::move(points);
    return out;
}
Json Activity::results() const {
    auto out = tally();
    out["kind"] = activity_kind_name(plan_->kind);
    out["prompt"] = plan_->prompt;
    if (plan_->kind == ActivityKind::choice) {
        out["choices"] = plan_->choices;
        out["correct"] = plan_->correct;
        out["has_answer"] = !plan_->correct.empty();
        if (!plan_->explanation.empty()) out["explanation"] = plan_->explanation;
        return out;
    }
    out["has_answer"] = plan_->kind != ActivityKind::open;
    if (!plan_->model_answer.empty()) out["model_answer"] = plan_->model_answer;
    if (!plan_->explanation.empty()) out["explanation"] = plan_->explanation;
    out["correct_count"] = std::count_if(answers_.begin(), answers_.end(), [](const auto& entry) { return entry.second.grade.correct; });
    // Show the answers that cover the most points, without names: the point is the idea, not who said it.
    std::vector<const Answer*> ranked;
    for (const auto& [student, answer] : answers_) if (!answer.grade.matched.empty()) ranked.push_back(&answer);
    std::stable_sort(ranked.begin(), ranked.end(), [](const Answer* a, const Answer* b) { return a->grade.matched.size() > b->grade.matched.size(); });
    Json highlights = Json::array();
    for (std::size_t i = 0; i < std::min(ranked.size(), max_highlights); ++i) highlights.push_back(ranked[i]->text);
    out["highlights"] = std::move(highlights);
    return out;
}
Json Activity::personal(const std::string& student) const {
    Json out{{"id", plan_->id}};
    const auto found = answers_.find(student);
    if (found == answers_.end()) {
        out["answered"] = false;
        out["feedback"] = "今回は回答がありませんでした。結果を見て、自分ならどう答えるか考えてみましょう。";
        return out;
    }
    out["answered"] = true;
    const auto& answer = found->second;
    if (plan_->kind == ActivityKind::choice) {
        out["choices"] = answer.choices;
        if (plan_->correct.empty()) out["feedback"] = "これは正解のない質問です。みんなの選び方と比べてみましょう。";
        else {
            out["correct"] = choice_correct(answer);
            const std::string why = plan_->explanation.empty() ? "" : "理由はクラスの結果の解説を見てください。";
            out["feedback"] = (choice_correct(answer) ? "正解です。" : "正解とは違いました。") + why;
        }
        return out;
    }
    out["text"] = answer.text;
    out["matched"] = answer.grade.matched;
    if (answer.grade.has_verdict) out["correct"] = answer.grade.correct;
    // Only perspectives some other student really gave may be mentioned to this one.
    std::vector<std::string> others;
    for (const auto& point : plan_->points)
        for (const auto& [id, theirs] : answers_)
            if (id != student && std::find(theirs.grade.matched.begin(), theirs.grade.matched.end(), point.id) != theirs.grade.matched.end()) {
                others.push_back(point.id);
                break;
            }
    out["feedback"] = model_->feedback(*plan_, answer.grade, others);
    return out;
}
Json Activity::submitted(const std::string& student) const {
    const auto found = answers_.find(student);
    if (found == answers_.end()) return nullptr;
    if (plan_->kind == ActivityKind::choice) return {{"choices", found->second.choices}};
    return {{"text", found->second.text}};
}
std::string_view activity_state_name(Activity::State state) {
    constexpr std::array names{"open", "closed", "revealed"};
    return names.at(static_cast<std::size_t>(state));
}
}
