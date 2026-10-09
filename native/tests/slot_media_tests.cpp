#include "check.hpp"
#include "media.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
using namespace odeum;
using namespace odeum::relay;

namespace {
// A signalling endpoint: its own PeerConnection plus everything the relay sent to it.
struct Client {
    std::shared_ptr<rtc::PeerConnection> pc;
    std::vector<std::shared_ptr<rtc::Track>> tracks;
    std::vector<Json> inbox;
    Client() {
        rtc::Configuration configuration; configuration.disableAutoNegotiation = true;
        pc = std::make_shared<rtc::PeerConnection>(configuration);
    }
    Send sink() { return [this](const Json& j) { inbox.push_back(j); }; }
    std::vector<std::string> offers() const {
        std::vector<std::string> result;
        for (const auto& j : inbox) if (j.at("type") == "sdp" && j.at("sdp").at("type") == "offer") result.push_back(j["sdp"]["sdp"]);
        return result;
    }
    bool answered() const {
        for (const auto& j : inbox) if (j.at("type") == "sdp" && j.at("sdp").at("type") == "answer") return true;
        return false;
    }
};
Message sdp(const rtc::Description& description) {
    return parse_message(Json{{"type", "sdp"}, {"sdp", {{"type", description.typeString()}, {"sdp", std::string(description)}}}}.dump());
}
// A sender's offer: H.264 constrained baseline (libdatachannel default profile) and Opus, one SSRC each.
Message publish(Client& client, std::uint32_t ssrc) {
    rtc::Description::Video video("0", rtc::Description::Direction::SendOnly);
    video.addH264Codec(96); video.addSSRC(ssrc, "cname", "stream", "video");
    rtc::Description::Audio audio("1", rtc::Description::Direction::SendOnly);
    audio.addOpusCodec(111); audio.addSSRC(ssrc + 1, "cname", "stream", "audio");
    client.tracks.push_back(client.pc->addTrack(video)); client.tracks.push_back(client.pc->addTrack(audio));
    client.pc->setLocalDescription(rtc::Description::Type::Offer);
    return sdp(*client.pc->localDescription());
}
Message answer(Client& client, const std::string& offer) {
    client.pc->setRemoteDescription(rtc::Description(offer, "offer"));
    client.pc->setLocalDescription(rtc::Description::Type::Answer);
    return sdp(*client.pc->localDescription());
}
std::set<std::string> active_mids(const std::string& offer) {
    rtc::Description description(offer, "offer"); std::set<std::string> mids;
    for (int i = 0; i < description.mediaCount(); ++i) {
        auto entry = description.media(i);
        if (auto media = std::get_if<rtc::Description::Media*>(&entry); media && !(*media)->isRemoved()) mids.insert((*media)->mid());
    }
    return mids;
}
bool only(const std::set<std::string>& mids, std::initializer_list<std::string_view> slots, std::size_t count) {
    if (mids.size() != count) return false;
    for (const auto& mid : mids) {
        bool known = false;
        for (auto slot : slots) known |= mid.starts_with(std::string(slot) + "-");
        if (!known) return false;
    }
    return true;
}
void pump(boost::asio::io_context& io, const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done()) {
        check(std::chrono::steady_clock::now() < deadline, "Timed out waiting for relay signalling");
        io.restart(); io.run_for(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
void settle(boost::asio::io_context& io) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);
    while (std::chrono::steady_clock::now() < until) { io.restart(); io.run_for(std::chrono::milliseconds(10)); }
}
}

int main() { return run([] {
    rtc::InitLogger(rtc::LogLevel::None);
    boost::asio::io_context io;
    Config config; config.rtc.disableAutoNegotiation = true;
    auto room = std::make_shared<MediaRoom>(io, config);
    Client viewer, input1, input2, producer_down, producer_up, input1_again;

    // A viewer waits for a source; the first input becomes its source.
    room->join(10, Role::viewer, "", viewer.sink());
    room->join(2, Role::presenter, "input2", input2.sink());
    const auto first_offer = publish(input2, 2000);
    room->signal(2, first_offer);
    pump(io, [&] { return viewer.offers().size() == 1 && input2.answered(); });
    check(only(active_mids(viewer.offers()[0]), {"input2"}, 2), "Viewer receives the only input");
    room->signal(10, answer(viewer, viewer.offers()[0]));

    // A lower-numbered input takes over: the viewer is re-offered input1 and input2 is removed.
    room->join(1, Role::presenter, "input1", input1.sink());
    room->signal(1, publish(input1, 1000));
    pump(io, [&] { return viewer.offers().size() == 2; });
    check(only(active_mids(viewer.offers()[1]), {"input1"}, 2), "Viewer switches to input1");
    room->signal(10, answer(viewer, viewer.offers()[1]));

    // The producer receives every live input, each section under a "<slot>-<n>" mid.
    room->join(20, Role::producer, "program", producer_down.sink());
    pump(io, [&] { return producer_down.offers().size() == 1; });
    check(only(active_mids(producer_down.offers()[0]), {"input1", "input2"}, 4), "Producer receives all inputs");
    room->signal(20, answer(producer_down, producer_down.offers()[0]));

    // The producer's program becomes the viewer source; the producer is not offered its own program.
    room->signal(20, publish(producer_up, 3000));
    pump(io, [&] { return viewer.offers().size() == 3; });
    check(only(active_mids(viewer.offers()[2]), {"program"}, 2), "Program wins over inputs");
    room->signal(10, answer(viewer, viewer.offers()[2]));
    settle(io);
    check(producer_down.offers().size() == 1, "Program is not sent back to the producer");

    // An input leaving: the producer gets track.closed and a narrower offer; the viewer keeps program.
    room->leave(1);
    pump(io, [&] { return producer_down.offers().size() == 2; });
    const auto closed = std::find_if(producer_down.inbox.begin(), producer_down.inbox.end(), [](const Json& j) { return j.at("type") == "track.closed"; });
    check(closed != producer_down.inbox.end() && closed->at("mids").size() == 2, "Producer is told which sections ended");
    for (const auto& mid : closed->at("mids")) check(mid.get<std::string>().starts_with("input1-"), "Closed sections are input1");
    parse_message(closed->dump());
    check(only(active_mids(producer_down.offers()[1]), {"input2"}, 2), "Producer follows the remaining input");
    room->signal(20, answer(producer_down, producer_down.offers()[1]));
    settle(io);
    check(viewer.offers().size() == 3, "Viewer source is unchanged while program is live");

    // Program leaving falls back to the lowest live input.
    room->leave(20);
    pump(io, [&] { return viewer.offers().size() == 4; });
    check(only(active_mids(viewer.offers()[3]), {"input2"}, 2), "Viewer falls back to input2");

    // A change during an outstanding offer waits for the answer, then is re-offered.
    room->join(3, Role::presenter, "input1", input1_again.sink());
    room->signal(3, publish(input1_again, 4000));
    pump(io, [&] { return input1_again.answered(); });
    settle(io);
    check(viewer.offers().size() == 4, "No second offer before the answer");
    room->signal(10, answer(viewer, viewer.offers()[3]));
    pump(io, [&] { return viewer.offers().size() == 5; });
    check(only(active_mids(viewer.offers()[4]), {"input1"}, 2), "Deferred switch to the reconnected input1");

    // Signalling rules per role.
    rejects([&] { room->signal(10, first_offer); }, "invalid_sdp");
    rejects([&] { room->signal(2, first_offer); }, "invalid_sdp");
    room->leave(3); room->leave(2); room->leave(10);
}); }
