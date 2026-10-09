#pragma once
#include <odeum/message.hpp>
#include <rtc/rtc.hpp>
#include <set>

namespace odeum::relay {
// SDP and RTCP helpers of the media relay. They never re-encode or rewrite RTP payloads.
bool supported_codec(const rtc::Description::Media& media, int pt);
// A sender's offer: one H.264 constrained-baseline video and optional Opus audio, sendonly, one SSRC each.
void validate_offer(const rtc::Description& description);
// A receiver's answer to a relay offer. Only sections the relay still sends (live) are checked and counted:
// recvonly, supported codec, at most max_active. Sections the relay retired (assigned but not live) are
// ignored even if the answerer keeps them active, as libdatachannel does; unknown mids are rejected.
void validate_answer(const rtc::Description& description, const std::set<std::string>& assigned,
    const std::set<std::string>& live, std::size_t max_active);
// The relay-side copy of a source section under a relay-assigned mid. The source's MID/RID header
// extensions are dropped so receivers demultiplex by the signalled SSRC rather than the sender's mid.
rtc::Description::Media downstream_media(const rtc::Description::Media& source, const std::string& mid);
rtc::binary picture_loss_indication(std::uint32_t ssrc);
bool contains_keyframe_request(const rtc::binary& packet);
std::string public_candidate(std::string candidate, const std::string& address);
}
