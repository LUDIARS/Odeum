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
// While an assignment runs, these mark a report of being stuck on the work ("手順 3 で進まない") rather
// than a question for the class. Outside an assignment they stay ordinary words.
const std::vector<std::string> stuck_words{"進まない", "進めない", "うまくいかない", "うまく行かない", "動きません", "できなく", "詰まって", "つまって",
    "詰まり", "はまって", "ハマって", "手が止まっ", "手順", "ステップ", "反映されない", "変わらない", "チェックが入らない", "つながらない", "繋がらない"};
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
    // Says only what keyword matching establishes: which prepared points the answer names, which ones
    // other students named, and whether that reaches min_points. It cannot tell whether a reason given
    // is good, so it never says so.
    std::string feedback(const ActivityPlan& activity, const Grade& grade, const std::vector<std::string>& others) const override {
        const auto has = [](const std::vector<std::string>& ids, const std::string& id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
        const auto labels = [&](auto keep) {
            std::string out;
            for (const auto& point : activity.points) if (keep(point.id)) out += (out.empty() ? "「" : "」「") + point.label;
            return out.empty() ? out : out + "」";
        };
        const auto mine = labels([&](const std::string& id) { return has(grade.matched, id); });
        if (activity.kind == ActivityKind::open) {
            const auto theirs = labels([&](const std::string& id) { return has(others, id) && !has(grade.matched, id); });
            std::string out = mine.empty() ? "あなたの回答は、用意した観点のどれにも分類されませんでした (キーワードで分類しています)。"
                                           : "あなたの回答に出てきた観点: " + mine + "。";
            if (!theirs.empty()) out += "ほかの人からは " + theirs + " という観点も出ています。";
            return out + "この問いに正解はありません。";
        }
        const auto rest = labels([&](const std::string& id) { return !has(grade.matched, id); });
        const auto needed = std::to_string(activity.min_points);
        if (mine.empty()) return "用意した観点のどれにも当てはまる言葉が見つかりませんでした (キーワードで判定しています)。用意した観点は " + rest + " です。";
        std::string out = "あなたの回答に出てきた観点: " + mine + "。";
        out += grade.correct ? "観点が " + needed + " つ以上あるので、正解の扱いです。" : "正解の扱いには観点が " + needed + " つ要ります。";
        if (!rest.empty()) out += "ほかに用意した観点: " + rest + "。";
        return out;
    }
    Reading read(std::string_view utterance, const Context& context) const override {
        const auto folded = fold(utterance);
        Reading reading;
        reading.on_topic = on_topic(folded, context);
        if (mentions_any(folded, abuse)) { reading.intent = Intent::disruption; reading.severity = 2; return reading; }
        // Punctuation-only or one repeated character carries no content for the class.
        if (folded.empty() || longest_run(folded) >= noise_run) { reading.intent = Intent::disruption; reading.severity = 1; return reading; }
        if (mentions_any(folded, help_words) || (context.assignment_active && mentions_any(folded, stuck_words))) { reading.intent = Intent::help; return reading; }
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
            review.answer = "この質問には、用意した答えがありません。人間の先生に回しました。先生から直接答えが届くことがあり、届いたらここに出ます。";
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
