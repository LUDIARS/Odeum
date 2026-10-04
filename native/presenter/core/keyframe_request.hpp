#pragma once
#include <cstddef>
#include <span>

namespace odeum::presenter {
// True when an RTCP (compound) packet carries a Picture Loss Indication (PT 206, FMT 1) or a
// Full Intra Request (PT 206, FMT 4). The relay folds every viewer's request into a PLI at most
// once a second; FIR is accepted too so a different SFU or a direct peer works the same way.
bool is_keyframe_request(std::span<const std::byte> packet);
}
