#include "check.hpp"
#include "hub.hpp"
using namespace odeum;
using namespace odeum::relay;

namespace {
Ticket sender(std::string sub, Role role, std::string slot = {}) {
    return {sub, "Name " + sub, "room", sub + "-ticket", role, 9999, "", "", std::move(slot)};
}
Send into(std::vector<Json>& box) { return [&box](const Json& j) { box.push_back(j); }; }
}

int main() { return run([] {
    boost::asio::io_context io;
    Config config;
    Hub hub(io, config);
    const auto input1 = sender("a", Role::presenter, "input1"), input2 = sender("b", Role::presenter, "input2");
    const auto producer = sender("x", Role::producer), program = sender("p", Role::presenter);
    const Ticket viewer{"v", "Viewer", "room", "v-ticket", Role::viewer, 9999};
    std::vector<Json> a_box, b_box, x_box, v_box;
    bool viewer_closed = false;

    // Per-slot presenters: one sender per slot, the welcome names the slot.
    hub.join(1, input1, into(a_box), [] {}, 0, 1);
    check(a_box.front().at("role") == "presenter" && a_box.front().at("slot") == "input1", "Welcome carries the slot");
    rejects([&] { hub.join(2, sender("a2", Role::presenter, "input1"), [](const Json&) {}, [] {}, 0, 1); }, "presenter_exists");
    hub.join(3, input2, into(b_box), [] {}, 0, 1);
    rejects([&] { hub.join(9, sender("e", Role::presenter, "input5"), [](const Json&) {}, [] {}, 0, 1); }, "slot_unavailable");

    // The producer holds program; one per room.
    hub.join(4, producer, into(x_box), [] {}, 0, 1);
    check(x_box.front().at("role") == "producer" && x_box.front().at("slot") == "program", "Producer welcome");
    rejects([&] { hub.join(5, sender("y", Role::producer), [](const Json&) {}, [] {}, 0, 1); }, "producer_exists");
    rejects([&] { hub.join(6, program, [](const Json&) {}, [] {}, 0, 1); }, "presenter_exists");

    // presence and sessions: slot flags, program flag; the producer is not a viewer.
    hub.join(7, viewer, into(v_box), [&] { viewer_closed = true; }, 0, 1);
    const auto presence = v_box.back();
    check(presence.at("type") == "presence" && presence.at("viewer_count") == 1, "Producer is not counted as a viewer");
    check(presence.at("program_connected") == true && presence.at("presenter_connected") == true, "Program flag");
    check(presence.at("slots") == Json({{"input1", true}, {"input2", true}, {"input3", false}, {"input4", false}, {"program", true}}), "Slot flags");
    auto session = hub.sessions().at(0);
    check(session.at("slots") == presence.at("slots") && session.at("program_connected") == true && session.at("viewer_count") == 1,
        "Sessions report slots");

    // The producer cannot post reactions or polls.
    for (auto wire : {R"({"type":"good","count":1})", R"({"type":"stamp","kind":"clap"})", R"({"type":"comment","text":"x"})",
             R"({"type":"telop","text":"x"})", R"({"type":"submission","category":"question","text":"x","show_on_screen":true})",
             R"({"type":"poll.open","poll_id":"p","question":"Q","choices":["a","b"],"multi":false})",
             R"({"type":"poll.close","poll_id":"p"})", R"({"type":"poll.answer","poll_id":"p","choices":[0]})"})
        rejects([&] { hub.message(4, producer, parse_message(wire), 0, 1); }, "forbidden");

    // It receives everything sent to the whole room, never private text or per-person notices.
    hub.message(4, producer, parse_message(R"({"type":"reaction.ready","version":1})"), 0, 1);
    check(v_box.back().at("reaction_version") == 1, "Producer may announce reaction support");
    a_box.clear(); b_box.clear(); x_box.clear();
    hub.message(7, viewer, parse_message(R"({"type":"telop","text":"hello"})"), 0, 2);
    check(x_box.back().at("type") == "telop" && x_box.back().at("from").at("sub") == "v", "Telop reaches the producer");
    x_box.clear();
    hub.message(7, viewer, parse_message(R"({"type":"submission","category":"question","text":"secret","show_on_screen":false})"), 3000, 3);
    check(x_box.empty() && a_box.back().at("type") == "submission" && b_box.back().at("type") == "submission", "Private text to presenters only");
    hub.message(7, viewer, parse_message(R"({"type":"stamp","kind":"clap"})"), 3000, 3);
    check(x_box.empty() && a_box.back().at("type") == "stamp", "Stamp notice to presenters only");
    hub.message(7, viewer, parse_message(R"({"type":"good","count":3})"), 3000, 3);
    hub.tick(3300);
    check(x_box.back().at("type") == "reaction.burst" && x_box.back().at("good") == 3, "Bursts reach the producer");

    // Room lifetime: program leaving keeps the room while inputs remain; a new program sender can join.
    hub.leave(4, "room");
    check(!viewer_closed && hub.sessions().size() == 1, "Program leaving keeps the room");
    check(hub.sessions().at(0).at("program_connected") == false && v_box.back().at("slots").at("program") == false, "Program released");
    hub.join(6, program, [](const Json&) {}, [] {}, 0, 1);
    hub.leave(6, "room");
    hub.leave(1, "room");
    check(!viewer_closed && v_box.back().at("slots").at("input1") == false, "An input leaving keeps the room");
    hub.leave(3, "room");
    check(viewer_closed && hub.sessions().empty(), "All slots empty closes the room");

    // Without slots the single-presenter layout is unchanged: the presenter leaving closes the room.
    viewer_closed = false;
    hub.join(10, program, [](const Json&) {}, [] {}, 0, 1);
    hub.join(11, viewer, [](const Json&) {}, [&] { viewer_closed = true; }, 0, 1);
    check(hub.sessions().at(0).at("slots").at("program") == true, "Default slot is program");
    hub.leave(10, "room");
    check(viewer_closed && hub.sessions().empty(), "Legacy presenter leaving closes the room");

    // ODEUM_RELAY_MAX_INPUTS narrows the accepted inputs and the presence view.
    Config narrow; narrow.max_inputs = 2;
    Hub small(io, narrow);
    rejects([&] { small.join(1, sender("c", Role::presenter, "input3"), [](const Json&) {}, [] {}, 0, 1); }, "slot_unavailable");
    std::vector<Json> c_box;
    small.join(2, sender("d", Role::presenter, "input2"), into(c_box), [] {}, 0, 1);
    check(c_box.back().at("slots").size() == 3 && c_box.back().at("slots").at("input2") == true, "Configured input range");
    check(small.sessions().size() == 1, "Rejected slot leaves no empty room");
}); }
