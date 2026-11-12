#pragma once
// WebRTC sessions (custos/src/capture/webrtc-broker.ts) on libdatachannel instead of werift:
// browser offer -> answer with one send-only H.264 (constrained baseline, packetization-mode 1,
// the browser's payload type) and optional Opus track; RTP from the shared app source is re-stamped
// with the session's payload type and SSRC; PLI / FIR / REMB from the browser go back to the source.
// At most 32 sessions (beyond: refused with an error).
#include "../adapters/ice_provider.hpp"
#include "../adapters/media_tools.hpp"
#include "../app/rtc_service.hpp"
#include "../app/settings.hpp"
#include <boost/asio/io_context.hpp>
#include <chrono>

namespace odeum::runner {
std::unique_ptr<RtcService> make_webrtc_broker(boost::asio::io_context& io, IceServerProvider& ice, MediaTools& media, const RunnerSettings& settings,
    std::function<std::optional<long long>(const std::string&)> run_pid);
// Turns libdatachannel's diagnostics off (SDP and candidates must not reach the logs). Call once
// at startup, before any peer connection exists: changing the level later races with the
// library's workers reading it.
void init_webrtc_logging();
// Joins libdatachannel's worker threads once every broker and peer connection is gone; call it
// before main returns so OpenSSL's atexit cleanup cannot run under a worker. False on timeout.
bool finish_webrtc(std::chrono::milliseconds timeout);

// Offer inspection (pure; tested without a peer connection).
struct OfferCodec {
    std::string mid;
    int payload_type = -1;
};
// The browser's H.264 payload type for packetization-mode=1, preferring profile-level-id 42e01f, then
// any constrained baseline (42e0 / 42c0), then baseline (4200).
std::optional<OfferCodec> pick_h264(const std::string& offer_sdp);
std::optional<OfferCodec> pick_opus(const std::string& offer_sdp);
// m=, a=rtpmap (video codecs), a=fmtp, candidate count and a=setup of an SDP (diagnostics).
Json sdp_summary(const std::string& sdp);
}
