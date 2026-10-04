#include "check.hpp"
#include "core/poll_draft.hpp"
using namespace odeum;
using namespace odeum::presenter;

namespace {
template<class F> void refuses(F action, const char* code) {
    try { action(); } catch (const PollDraftError& error) { check(error.code == code, code); return; }
    throw std::runtime_error(std::string("Expected rejection: ") + code);
}
}

int main() { return run([] {
    check(PollDraft::split_choices("  はい \r\n\nいいえ\n  \nどちらでもない  ") == std::vector<std::string>({"はい", "いいえ", "どちらでもない"}), "Split choices");

    PollDraft draft{"  今日の発表はどうでしたか？ ", {"良い", "普通", "悪い"}, false};
    auto message = draft.build("p1");
    check(message["type"] == "poll.open" && message["poll_id"] == "p1" && message["question"] == "今日の発表はどうでしたか？", "Built poll");
    check(message["choices"].size() == 3 && message["multi"] == false, "Choices and multi");
    check(parse_message(message.dump()).type == MessageType::poll_open, "Relay schema accepts it");
    draft.multi = true;
    check(draft.build("p2")["multi"] == true, "Multiple selection");

    refuses([] { PollDraft{"   ", {"A", "B"}, false}.build("p"); }, "question_missing");
    refuses([] { PollDraft{std::string(1001, 'q'), {"A", "B"}, false}.build("p"); }, "question_too_long");
    refuses([] { PollDraft{"Q", {"A"}, false}.build("p"); }, "too_few_choices");
    refuses([] { PollDraft{"Q", {"1", "2", "3", "4", "5", "6", "7"}, false}.build("p"); }, "too_many_choices");
    refuses([] { PollDraft{"Q", {"A", "  "}, false}.build("p"); }, "too_few_choices");
    // 60 characters are counted as Unicode scalars, not bytes.
    std::string sixty;
    for (int i = 0; i < 60; ++i) sixty += "あ";
    check(PollDraft{"Q", {sixty, "B"}, false}.build("p")["choices"][0] == sixty, "60 characters allowed");
    refuses([&] { PollDraft{"Q", {sixty + "あ", "B"}, false}.build("p"); }, "choice_too_long");
    refuses([] { PollDraft{"Q", {"A", " A "}, false}.build("p"); }, "duplicate_choice");
    refuses([] { PollDraft{"Q", {std::string("\xff"), "B"}, false}.build("p"); }, "invalid_text");
    check(PollDraft{"Q", {"1", "2", "3", "4", "5", "6"}, false}.build("p")["choices"].size() == 6, "Six choices allowed");

    PollControl control;
    auto opened = control.open(draft, "p3");
    check(opened["poll_id"] == "p3" && control.open_poll() == "p3", "Open");
    refuses([&] { control.open(draft, "p4"); }, "poll_already_open");
    auto closed = control.close();
    check(closed == Json({{"type", "poll.close"}, {"poll_id", "p3"}}) && !control.open_poll(), "Close");
    check(parse_message(closed.dump()).type == MessageType::poll_close, "Close message valid");
    refuses([&] { control.close(); }, "no_open_poll");
    refuses([&] { control.open(PollDraft{"", {}, false}, "p5"); }, "question_missing");
    check(!control.open_poll(), "Rejected draft leaves no open poll");
}); }
