#include "producer_routing.hpp"
#include <odeum/slot.hpp>

namespace odeum::program {
std::optional<int> input_of_mid(std::string_view mid) {
    const auto dash = mid.rfind('-');
    if (dash == std::string_view::npos || dash + 1 == mid.size()) return std::nullopt;
    if (mid.substr(dash + 1).find_first_not_of("0123456789") != std::string_view::npos) return std::nullopt;
    const auto index = input_index(mid.substr(0, dash));
    if (!index || *index > static_cast<std::size_t>(input_count)) return std::nullopt;
    return static_cast<int>(*index) - 1;
}

PeerSide side_of(const Message& signal) {
    if (signal.type == MessageType::sdp)
        return signal.body.at("sdp").at("type").get<std::string>() == "offer" ? PeerSide::receiver : PeerSide::sender;
    if (signal.type == MessageType::candidate && signal.body.contains("mid") && signal.body.at("mid").is_string())
        return input_of_mid(signal.body.at("mid").get<std::string>()) ? PeerSide::receiver : PeerSide::sender;
    return PeerSide::sender;
}

std::array<bool, input_count> presence_inputs(const Json& presence) {
    std::array<bool, input_count> live{};
    if (!presence.contains("slots") || !presence.at("slots").is_object()) return live;
    for (int i = 0; i < input_count; ++i) {
        const auto name = input_slot(static_cast<std::size_t>(i) + 1);
        const auto& slots = presence.at("slots");
        live[static_cast<std::size_t>(i)] = slots.contains(name) && slots.at(name).is_boolean() && slots.at(name).get<bool>();
    }
    return live;
}
}
