#pragma once
#include "program_state.hpp"
#include <odeum/message.hpp>
#include <optional>
#include <string_view>

namespace odeum::program {
// The relay names the sections it offers the producer `<slot>-<n>` (input1-3, ...). Returns the
// 0-based input of such a mid, or nullopt for anything else (program, the producer's own mids).
std::optional<int> input_of_mid(std::string_view mid);

// The producer's WebSocket carries signalling for two PeerConnections: the relay offers the
// receiving one, the producer offers the sending one. An `sdp` offer belongs to the receiver,
// an answer to the sender; a candidate whose mid the relay assigned belongs to the receiver.
enum class PeerSide { receiver, sender };
PeerSide side_of(const Message& signal);

// slots.input1..input4 of a presence message, false where absent.
std::array<bool, input_count> presence_inputs(const Json& presence);
}
