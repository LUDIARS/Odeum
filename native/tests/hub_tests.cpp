#include "check.hpp"
#include "hub.hpp"
using namespace odeum;
using namespace odeum::relay;

int main() { return run([] {
    boost::asio::io_context io;
    Config config;
    Hub hub(io, config);
    Ticket presenter{"p", "Presenter", "room", "p-ticket", Role::presenter, 9999};
    Ticket viewer{"v", "Viewer", "room", "v-ticket", Role::viewer, 9999};
    Ticket other{"other", "Other", "room", "o-ticket", Role::viewer, 9999};
    std::vector<Json> inbox, own, audience;
    hub.join(1, presenter, [&](const Json& j) { inbox.push_back(j); }, [] {}, 0, 1);
    hub.join(2, viewer, [&](const Json& j) { own.push_back(j); }, [] {}, 0, 1);
    hub.join(3, other, [&](const Json& j) { audience.push_back(j); }, [] {}, 0, 1);
    const auto ready = parse_message(R"({"type":"reaction.ready","version":1})");
    auto post = parse_message(R"({"type":"submission","category":"question","text":"private","show_on_screen":false,"from":{"sub":"forged","name":"Forged"},"at":0})");
    rejects([&] { hub.message(2, viewer, post, 0, 1); }, "reaction_unavailable");
    rejects([&] { hub.message(2, viewer, ready, 0, 1); }, "forbidden");
    hub.message(1, presenter, ready, 0, 1);
    check(audience.back().at("reaction_version") == 1, "Presenter capability announced");
    inbox.clear(); own.clear(); audience.clear();
    hub.message(2, viewer, post, 0, 2);
    check(inbox.size() == 1 && own.empty() && audience.empty(), "Private text goes only to presenter");
    check(inbox.back().at("from").at("sub") == "v" && inbox.back().at("at") == 2, "Identity and time are authoritative");
    post.body["show_on_screen"] = true;
    rejects([&] { hub.message(2, viewer, post, 100, 3); }, "rate_limited");
    hub.message(2, viewer, post, 3000, 4);
    check(inbox.size() == 2 && own.size() == 1 && audience.size() == 1, "Public post reaches participants");
    auto telop = parse_message(R"({"type":"telop","text":"hello"})");
    hub.message(2, viewer, telop, 6000, 7);
    check(audience.back().at("type") == "telop", "Telops reach viewers");
    rejects([&] { hub.message(1, presenter, telop, 9000, 10); }, "forbidden");
    hub.leave(1, "room");
    inbox.clear(); own.clear(); audience.clear();
    hub.join(1, presenter, [&](const Json& j) { inbox.push_back(j); }, [] {}, 10000, 11);
    hub.join(2, viewer, [&](const Json& j) { own.push_back(j); }, [] {}, 10000, 11);
    check(own.back().at("reaction_version") == 0, "New room needs a fresh capability announcement");
    rejects([&] { hub.message(2, viewer, post, 10000, 11); }, "reaction_unavailable");
}); }
