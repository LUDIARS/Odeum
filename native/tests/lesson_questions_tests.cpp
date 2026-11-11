#include "lesson_fixture.hpp"
#include "questions.hpp"
using namespace odeum;
using namespace odeum::lesson;

int main() { return run([] {
    const auto plot = sample_plot();
    const auto model = make_provisional_model();
    const Context context{&plot, find_section(plot, "hitstop"), false, false};
    QuestionBoard board(*model);

    // A deep, on-topic question the plot can answer.
    const auto good = board.submit("s1", "あおい", false, "ヒットストップは何秒くらいにするのがいいですか？なぜですか", context, 1000);
    check(!good.duplicate && good.review.score >= QuestionBoard::share_score && !good.review.needs_teacher, "Good question scores high");
    check(good.review.answer.find("威力 ÷ 24") != std::string::npos, "Answer is grounded in the plot's FAQ");

    // The same question from someone else becomes a "me too", not a second entry.
    const auto again = board.submit("s2", "はると", false, "ヒットストップは何秒くらいにするのが良いですか？なぜ？", context, 2000);
    check(again.duplicate && again.id == good.id, "Near-duplicate is absorbed");
    const auto self = board.submit("s1", "あおい", false, "ヒットストップは何秒くらいにするのがいいですか？なぜですか", context, 2500);
    check(self.duplicate && board.console_list().size() == 1 && board.console_list()[0].at("votes") == 1, "Asking twice does not vote for yourself");

    // Off-topic and tiny questions score low; unanswerable ones need the teacher.
    const auto lunch = board.submit("s3", "りく", true, "今日の給食は何ですか", context, 3000);
    check(lunch.review.score < QuestionBoard::share_score && lunch.review.needs_teacher, "Off-topic question is not shared");
    const auto unknown = board.submit("s4", "みお", true, "ヒットストップ中に入力を受け付けるかどうかの違いはどう決めますか", context, 4000);
    check(unknown.review.needs_teacher || !unknown.review.answer.empty(), "Every asker gets some answer");

    rejects([&] { board.submit("s5", "x", false, "   ", context, 0); }, "invalid_question");
    rejects([&] { board.submit("s5", "x", false, std::string(281, 'a'), context, 0); }, "invalid_question");
    rejects([&] { board.vote("s5", "q999"); }, "unknown_question");

    board.vote("s5", good.id); board.vote("s5", good.id); board.vote("s1", good.id);
    check(board.console_list()[0].at("votes") == 2, "One vote per student; askers cannot vote for themselves");

    // Sharing: best first, anonymous unless the asker opted in, never one that lacks an answer.
    const auto shared = board.share_best();
    check(shared.size() == 1 && shared[0] == good.id, "Only answered, high-scoring questions are shared automatically");
    const auto view = board.shared_view(good.id);
    check(!view.contains("name") && !view.contains("student") && view.at("votes") == 2 && view.at("section") == "hitstop", "Shared anonymously");
    check(board.share_best().empty(), "A question is shared once");
    board.share(lunch.id);
    check(board.shared_view(lunch.id).at("name") == "りく" && board.shared_list().size() == 2, "Operator can share any question; opted-in name shows");
    check(board.asker(good.id) == "s1" && board.knows(good.id) && !board.knows("q0"), "Asker lookup for the private thank-you");

    board.dismiss(unknown.id);
    rejects([&] { board.vote("s1", unknown.id); }, "unknown_question");
    const auto fresh = board.submit("s6", "そら", false, "ヒットストップ中に入力を受け付けるかどうかの違いはどう決めますか", context, 5000);
    check(!fresh.duplicate, "A dismissed question does not absorb new ones");
    board.withdraw("s6");
    check(board.console_list().back().at("status") == "dismissed", "Removed students' pending questions are withdrawn");
    board.withdraw("s1");
    check(board.shared_list().size() == 2, "Already shared questions stay");

    // At most two per share round.
    QuestionBoard many(*model);
    many.submit("a", "a", false, "ノックバックの向きはなぜ相手の後ろ向きなのですか", {&plot, find_section(plot, "knockback"), false, false}, 0);
    many.submit("b", "b", false, "ヒットストップで全体を止めるのと二人だけ止めるのはどう違いますか、理由も知りたいです", context, 0);
    many.submit("c", "c", false, "カメラの揺れで酔う場合はどうすればいいですか、なぜ酔うのですか", {&plot, find_section(plot, "camera"), false, false}, 0);
    check(many.share_best().size() == QuestionBoard::max_per_share && many.share_best().size() == 1, "Two per round, the rest next time");
}); }
