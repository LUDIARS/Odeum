#include "check.hpp"
#include "core/overlay_state.hpp"
using namespace odeum;
using namespace odeum::presenter;

namespace {
Message comment(int n) {
    return parse_message(R"({"type":"comment","text":"c)" + std::to_string(n) + R"(","from":{"sub":"u)" + std::to_string(n) +
                         R"(","name":"N"},"at":1})");
}
}

int main() { return run([] {
    OverlayLimits limits;
    limits.comment_limit = 3;
    OverlayState state(limits);
    check(state.apply(parse_message(R"({"type":"welcome","sid":"s1","role":"presenter","self":{"sub":"p","name":"発表者"},"ice_servers":[]})"), 0), "Welcome");
    check(state.self_name() == "発表者" && state.session_id() == "s1", "Self");

    // Good: totals accumulate burst by burst; a burst of zero good adds nothing to animate.
    state.apply(parse_message(R"({"type":"reaction.burst","good":30,"stamps":{"clap":2}})"), 1000);
    state.apply(parse_message(R"({"type":"reaction.burst","good":12,"stamps":{"clap":1,"wow":4}})"), 1250);
    state.apply(parse_message(R"({"type":"reaction.burst","good":0,"stamps":{"agree":1}})"), 1500);
    check(state.good_total() == 42 && state.bursts().size() == 2, "Burst total");
    check(state.stamp_totals().at("clap") == 3 && state.stamp_totals().at("wow") == 4 && state.stamp_totals().at("agree") == 1, "Stamp totals");
    check(state.heat(1500) > 0 && state.heat(1500) <= 1, "Heat in range");
    check(state.heat(1500) > state.heat(4100), "Heat cools down");
    state.expire(1000 + limits.burst_lifetime);
    check(state.bursts().size() == 1 && state.good_total() == 42, "Burst animation expires, total stays");

    // Stamps: the presenter's copy carries the name; it disappears after its lifetime.
    check(state.apply(parse_message(R"({"type":"stamp","kind":"clap","from":{"sub":"v","name":"山田"},"at":5})"), 2000), "Named stamp");
    check(!state.apply(parse_message(R"({"type":"stamp","kind":"clap"})"), 2000), "Nameless stamp ignored");
    check(state.stamps().size() == 1 && state.stamps().front().name == "山田" && state.stamps().front().kind_or_text == "clap", "Stamp shown");
    state.expire(2000 + limits.stamp_lifetime - 1);
    check(state.stamps().size() == 1, "Stamp still shown");
    state.expire(2000 + limits.stamp_lifetime);
    check(state.stamps().empty(), "Stamp lifetime");
    for (std::size_t i = 0; i < limits.stamp_limit + 5; ++i)
        state.apply(parse_message(R"({"type":"stamp","kind":"wow","from":{"sub":"v","name":"N"},"at":5})"), 10000);
    check(state.stamps().size() == limits.stamp_limit, "Stamp limit");

    // Comments: only the newest comment_limit remain, oldest dropped first.
    for (int i = 0; i < 5; ++i) state.apply(comment(i), 20000 + i);
    check(state.comments().size() == 3 && state.comments().front().kind_or_text == "c2" && state.comments().back().kind_or_text == "c4", "Comment limit");
    state.expire(20004 + limits.comment_lifetime);
    check(state.comments().empty(), "Comment lifetime");

    // Polls: tally for another poll is ignored; counts follow the open poll; close keeps the result.
    state.apply(parse_message(R"({"type":"poll.open","poll_id":"p1","question":"Q","choices":["A","B","C"],"multi":false})"), 30000);
    check(state.poll() && state.poll()->open && state.poll()->counts == std::vector<std::int64_t>({0, 0, 0}), "Poll open");
    check(!state.apply(parse_message(R"({"type":"tally","poll_id":"other","counts":[9,9],"answered":9})"), 30100), "Foreign tally");
    check(state.apply(parse_message(R"({"type":"tally","poll_id":"p1","counts":[2,5,1],"answered":8})"), 31000), "Tally");
    check(state.poll()->counts == std::vector<std::int64_t>({2, 5, 1}) && state.poll()->answered == 8, "Tally counts");
    state.apply(parse_message(R"({"type":"poll.closed","poll_id":"p1","tally":{"counts":[2,6,1],"answered":9}})"), 32000);
    check(!state.poll()->open && state.poll()->counts[1] == 6 && state.poll()->answered == 9, "Final tally");
    state.expire(32000 + limits.closed_poll_lifetime - 1);
    check(state.poll().has_value(), "Result still shown");
    state.expire(32000 + limits.closed_poll_lifetime);
    check(!state.poll(), "Result lifetime");

    state.apply(parse_message(R"({"type":"presence","presenter_connected":true,"viewer_count":42})"), 40000);
    check(state.viewer_count() == 42 && state.presenter_connected(), "Presence");
    state.apply(parse_message(R"({"type":"error","code":"rate_limited","message":"x"})"), 40000);
    check(state.last_error() == "rate_limited", "Error kept");
    check(!state.apply(parse_message(R"({"type":"candidate","candidate":"","mid":"0"})"), 40000), "Signalling is not overlay");
    state.reset();
    check(state.good_total() == 0 && state.viewer_count() == 0 && !state.poll() && state.stamps().empty(), "Reset");
}); }
