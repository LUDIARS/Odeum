#include "model.hpp"
#include "text.hpp"
#include <algorithm>

namespace odeum::lesson {
namespace {
// Stand-in for the lecturer model. It never invents content: everything it says comes from the
// plot (points, rubric labels, hints, FAQ), so swapping in the real model changes wording and
// judgement, not the classroom's control flow.
const std::vector<std::string> abuse{"死ね", "しね", "殺す", "ころす", "うざい", "きもい", "くそ", "ばか", "あほ", "うんこ", "fuck", "shit"};
const std::vector<std::string> help_words{"わからない", "分からない", "わかりません", "分かりません", "教えて", "助けて", "詰まった", "つまった",
    "動かない", "うごかない", "できない", "できません", "エラー", "ヒント"};
const std::vector<std::string> question_words{"なぜ", "なんで", "どうして", "どうやって", "どうすれば", "ですか", "ますか", "とは", "違い", "ちがい",
    "どっち", "どちら", "何が", "なにが", "どう"};
const std::vector<std::string> depth_words{"なぜ", "なんで", "どうして", "どうやって", "違い", "ちがい", "もし", "場合", "逆に", "トレードオフ",
    "どちらが", "代わり", "理由", "比べ"};

constexpr std::size_t noise_run = 6;     // "wwwwww", "ああああああ"
// One shared word ("ヒットストップ") is not enough to call a prepared answer relevant.
constexpr std::size_t faq_min_hits = 2;
constexpr int share_bonus_depth = 25, share_bonus_topic = 25, share_bonus_length = 10, share_base = 30;

bool asks(std::string_view raw, std::u32string_view folded) {
    const auto text = decode_utf8(raw);
    const bool mark = !text.empty() && (text.back() == U'?' || text.back() == U'？');
    return mark || mentions_any(folded, question_words);
}
// On topic means "about this lesson", not "about the section running right now": a camera question
// asked during the knockback section is early, not off-topic.
bool on_topic(std::u32string_view folded, const Context& context) {
    if (!context.plot) return false;
    for (const auto& section : context.plot->sections) if (mentions_any(folded, section.keywords)) return true;
    return false;
}

class ProvisionalModel final : public LessonModel {
public:
    std::string name() const override { return "provisional-rules"; }
    std::string narrate(const Plot&, const Section&, const std::string& point, double) const override { return point; }

    Grade grade(const ActivityPlan& activity, std::string_view answer) const override {
        Grade grade;
        const auto folded = fold(answer);
        for (const auto& point : activity.points) if (mentions_any(folded, point.keywords)) grade.matched.push_back(point.id);
        grade.has_verdict = activity.kind == ActivityKind::text || activity.kind == ActivityKind::missing;
        grade.correct = grade.has_verdict && grade.matched.size() >= activity.min_points;
        return grade;
    }
    std::string feedback(const ActivityPlan& activity, const Grade& grade) const override {
        const auto labels = [&](bool matched) {
            std::string out;
            for (const auto& point : activity.points) {
                const bool hit = std::find(grade.matched.begin(), grade.matched.end(), point.id) != grade.matched.end();
                if (hit == matched) out += (out.empty() ? "" : "、") + point.label;
            }
            return out;
        };
        const auto hit = labels(true), missed = labels(false);
        if (activity.kind == ActivityKind::open) {
            if (hit.empty()) return "答えは一つではありません。ほかの人の観点と比べて、自分ならどうするかを一言で言えるようにしておきましょう。";
            return "あなたの観点: " + hit + "。" + (missed.empty() ? "" : "ほかに「" + missed + "」という見方も出ています。") + "正解はありません。理由を言えることが大事です。";
        }
        if (hit.empty()) return "今回は当たりませんでした。見落としやすいのは「" + missed + "」です。動かして確かめてみましょう。";
        if (missed.empty()) return "全部押さえています: " + hit + "。";
        return "押さえた点: " + hit + "。あと一歩: " + missed + "。";
    }
    Reading read(std::string_view utterance, const Context& context) const override {
        const auto folded = fold(utterance);
        Reading reading;
        reading.on_topic = on_topic(folded, context);
        if (mentions_any(folded, abuse)) { reading.intent = Intent::disruption; reading.severity = 2; return reading; }
        // Punctuation-only or one repeated character carries no content for the class.
        if (folded.empty() || longest_run(folded) >= noise_run) { reading.intent = Intent::disruption; reading.severity = 1; return reading; }
        if (mentions_any(folded, help_words)) { reading.intent = Intent::help; return reading; }
        if (asks(utterance, folded)) { reading.intent = Intent::question; return reading; }
        if (context.text_activity_open) { reading.intent = Intent::answer; return reading; }
        reading.intent = Intent::chatter;
        return reading;
    }
    QuestionReview review_question(std::string_view question, const Context& context) const override {
        QuestionReview review;
        const auto folded = fold(question);
        const auto length = folded.size();
        const bool topical = on_topic(folded, context);
        review.score = share_base + (mentions_any(folded, depth_words) ? share_bonus_depth : 0) + (topical ? share_bonus_topic : 0)
            + (length >= 12 && length <= 120 ? share_bonus_length : 0) - (length < 6 ? 30 : 0);
        review.score = std::clamp(review.score, 0, 100);
        std::size_t best = faq_min_hits - 1;
        if (context.plot) for (const auto& faq : context.plot->faq) {
            const auto hits = static_cast<std::size_t>(std::count_if(faq.keywords.begin(), faq.keywords.end(),
                [&](const std::string& keyword) { return mentions(folded, keyword); }));
            if (hits > best) { best = hits; review.answer = faq.answer; }
        }
        if (review.answer.empty()) {
            review.needs_teacher = true;
            review.answer = "いい質問です。いまは手元に確かな答えがないので、授業の中で取り上げます。";
        }
        return review;
    }
    std::string hint(const AssignmentPlan& assignment, std::size_t step, std::size_t level) const override {
        const auto& plan = assignment.steps.at(step);
        if (plan.hints.empty()) return "「" + plan.label + "」で止まっているようです。どこまでできたかを一言で教えてください。";
        return plan.hints[std::min(level, plan.hints.size() - 1)];
    }
};
}
std::unique_ptr<LessonModel> make_provisional_model() { return std::make_unique<ProvisionalModel>(); }
}
