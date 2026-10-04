#include "check.hpp"
#include "poll.hpp"
using namespace odeum;
using namespace odeum::relay;
int main() { return run([] {
    Reactions reactions(0);
    const auto good=parse_message(R"({"type":"good","count":50})");
    auto first=reactions.accept("u",good,0); check(first.accepted==30 && !first.limited,"Good clipped");
    check(reactions.accept("u",good,999).accepted==0,"Rolling window");
    check(!reactions.flush(249),"No early burst");
    auto burst=reactions.flush(250); check(burst && burst->at("good")==30,"250ms burst");
    check(!reactions.flush(500),"No empty burst");
    check(reactions.accept("u",good,1000).accepted==30,"Window expiry");
    check(reactions.accept("v",good,1000).accepted==30,"Independent user");
    auto stamp=parse_message(R"({"type":"stamp","kind":"clap"})");
    check(!reactions.accept("u",stamp,1000).limited,"First stamp");
    check(!reactions.accept("u",stamp,1001).limited,"Second stamp");
    check(reactions.accept("u",stamp,1999).limited,"Stamp limit");
    check(!reactions.accept("u",stamp,2000).limited,"Stamp boundary");
    auto comment=parse_message(R"({"type":"comment","text":"hi"})");
    check(!reactions.accept("u",comment,2000).limited,"First comment");
    check(reactions.accept("u",comment,4999).limited,"Comment limit");
    check(!reactions.accept("u",comment,5000).limited,"Comment boundary");
    Poll poll;
    auto definition=Json{{"type","poll.open"},{"poll_id","p"},{"question","Q"},{"choices",{"A","B","C"}},{"multi",false}};
    poll.open(definition,0);
    poll.answer("u","p",{0}); auto tally=poll.answer("u","p",{1});
    check(tally["answered"]==1 && tally["counts"]==Json({0,1,0}),"Answer overwrite");
    rejects([&] { poll.answer("u","p",{0,1}); },"invalid_answer");
    check(!poll.flush(999),"Tally interval"); check(poll.flush(1000).has_value(),"Tally boundary");
    check(!poll.flush(2000),"No unchanged tally");
    auto closed=poll.close("p"); check(closed["tally"]["answered"]==1,"Final tally");
    rejects([&] { poll.answer("u","p",{0}); },"poll_not_open");
    definition["multi"]=true; poll.open(definition,2000); poll.answer("u","p",{0,2});
    auto multi=poll.answer("v","p",{1,2}); check(multi["counts"]==Json({1,1,2}) && multi["answered"]==2,"Multi selection");
    rejects([&] { poll.answer("u","p",{0,0}); },"invalid_answer");
    rejects([&] { poll.answer("u","p",{3}); },"invalid_answer");
}); }
