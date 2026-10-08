#include "check.hpp"
#include "admission.hpp"
#include "hub.hpp"
using namespace odeum;
using namespace odeum::relay;

int main() { return run([] {
    boost::asio::io_context io;
    Config config;
    Hub hub(io, config);
    const std::string key = std::string(42, 'k') + "A";
    Ticket presenter{"p", "Presenter", "room", "p-ticket", Role::presenter, 9999,
        sha256_base64url("ABCDEFGH12"), sha256_base64url(key)};
    std::vector<Json> inbox, guest_box, overlay_box;
    rejects([&] { hub.invitations().resolve_overlay(key, 0); }, "session_not_live");
    hub.join(1, presenter, [&](const Json& j) { inbox.push_back(j); }, [] {}, 0, 1);
    hub.message(1, presenter, parse_message(R"({"type":"reaction.ready","version":1})"), 0, 1);

    // Overlay admission: receives broadcasts, is not a viewer, cannot send.
    auto overlay = overlay_ticket(hub.invitations().resolve_overlay(key, 0));
    hub.join(2, overlay, [&](const Json& j) { overlay_box.push_back(j); }, [] {}, 0, 1, Admission::overlay);
    check(overlay_box.front().at("role") == "overlay" && overlay_box.front().at("ice_servers").empty(), "Overlay welcome without media");
    check(inbox.back().at("viewer_count") == 0, "Overlay is not counted as a viewer");
    rejects([&] { hub.message(2, overlay, parse_message(R"({"type":"good","count":1})"), 0, 1); }, "forbidden");
    rejects([&] { hub.message(2, overlay, parse_message(R"({"type":"telop","text":"x"})"), 0, 1); }, "forbidden");
    rejects([&] { hub.join(9, overlay, [](const Json&) {}, [] {}, 0, 1, Admission::ticket); }, "forbidden");

    // Guest admission: a viewer without media.
    auto guest = guest_ticket(hub.invitations().resolve_join("abcd-efgh-12", 0), "Guest");
    hub.join(3, guest, [&](const Json& j) { guest_box.push_back(j); }, [] {}, 0, 1, Admission::guest);
    check(inbox.back().at("viewer_count") == 1, "Guest counts as a viewer");
    rejects([&] { hub.message(3, guest, parse_message(R"({"type":"sdp","sdp":{"type":"answer","sdp":"v=0"}})"), 0, 1); }, "forbidden");

    // Private text never reaches the overlay; public events do.
    overlay_box.clear(); inbox.clear();
    hub.message(3, guest, parse_message(R"({"type":"submission","category":"question","text":"secret","show_on_screen":false})"), 0, 2);
    check(overlay_box.empty() && inbox.size() == 1, "Private submission only to presenter");
    hub.message(3, guest, parse_message(R"({"type":"stamp","kind":"clap"})"), 3000, 3);
    check(overlay_box.empty(), "Per-person stamp notice stays with the presenter");
    hub.message(3, guest, parse_message(R"({"type":"telop","text":"hello"})"), 6000, 4);
    check(overlay_box.back().at("type") == "telop" && overlay_box.back().at("from").at("name") == "Guest", "Telop reaches overlay");
    hub.tick(6300);
    check(overlay_box.back().at("type") == "reaction.burst", "Bursts reach overlay");

    // Overlay connection limit.
    for (std::uint64_t id = 10; id < 10 + Hub::max_overlays - 1; ++id)
        hub.join(id, overlay_ticket("room"), [](const Json&) {}, [] {}, 0, 1, Admission::overlay);
    rejects([&] { hub.join(20, overlay_ticket("room"), [](const Json&) {}, [] {}, 0, 1, Admission::overlay); }, "capacity");

    // Another live session cannot claim the same invitation.
    Ticket rival{"r", "Rival", "other", "r-ticket", Role::presenter, 9999, presenter.invite_join, sha256_base64url("other")};
    rejects([&] { hub.join(30, rival, [](const Json&) {}, [] {}, 0, 1); }, "invite_conflict");
    check(hub.sessions().size() == 1, "Rejected presenter leaves no empty room");

    // Presenter leaving releases the invitation.
    hub.leave(1, "room");
    rejects([&] { hub.invitations().resolve_join("ABCDEFGH12", 7000); }, "session_not_live");
}); }
