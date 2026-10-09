#include "media_sdp.hpp"
#include <sstream>

namespace odeum::relay {
namespace {
unsigned byte(const rtc::binary& p, std::size_t i) { return std::to_integer<unsigned>(p.at(i)); }
bool is_rtcp(const rtc::binary& p) { return p.size() >= 4 && (byte(p, 0) >> 6) == 2 && byte(p, 1) >= 192 && byte(p, 1) <= 223; }
const rtc::Description::Media& section(const rtc::Description& description, int index) {
    auto entry = description.media(index);
    auto media = std::get_if<const rtc::Description::Media*>(&entry);
    if (!media) throw ProtocolError("invalid_sdp", "Invalid media section");
    return **media;
}
void require_codec(const rtc::Description::Media& m) {
    if (m.type() != "video" && m.type() != "audio") throw ProtocolError("invalid_sdp", "Unsupported media");
    bool supported = false;
    for (auto pt : m.payloadTypes()) supported |= supported_codec(m, pt);
    if (!supported) throw ProtocolError("invalid_sdp", "Unsupported codec profile");
}
}
bool supported_codec(const rtc::Description::Media& media, int pt) {
    const auto* codec = media.rtpMap(pt);
    if (media.type() == "audio") return (codec->format == "opus" || codec->format == "OPUS") && codec->clockRate == 48000;
    if (codec->format != "H264" || codec->clockRate != 90000) return false;
    bool mode = false, profile = false;
    for (const auto& fmt : codec->fmtps) {
        mode |= fmt.find("packetization-mode=1") != std::string::npos;
        profile |= fmt.find("profile-level-id=42e0") != std::string::npos || fmt.find("profile-level-id=42c0") != std::string::npos;
    }
    return mode && profile;
}
void validate_offer(const rtc::Description& description) {
    if (description.hasApplication() || description.mediaCount() < 1 || description.mediaCount() > 2)
        throw ProtocolError("invalid_sdp", "One H264 video and optional Opus audio are required");
    int videos = 0, audios = 0;
    for (int i = 0; i < description.mediaCount(); ++i) {
        const auto& m = section(description, i);
        if (m.isRemoved()) throw ProtocolError("invalid_sdp", "Invalid media section");
        if (m.direction() != rtc::Description::Direction::SendOnly) throw ProtocolError("invalid_sdp", "Invalid media direction");
        require_codec(m);
        if (m.getSSRCs().size() != 1) throw ProtocolError("invalid_sdp", "Each source must declare one SSRC");
        if (m.type() == "video") ++videos; else ++audios;
    }
    if (videos != 1 || audios > 1) throw ProtocolError("invalid_sdp", "Invalid media count");
}
void validate_answer(const rtc::Description& description, const std::set<std::string>& assigned,
    const std::set<std::string>& live, std::size_t max_active) {
    if (description.hasApplication() || description.mediaCount() < 1)
        throw ProtocolError("invalid_sdp", "The answer must match the relay offer");
    std::size_t active = 0;
    for (int i = 0; i < description.mediaCount(); ++i) {
        const auto& m = section(description, i);
        if (!assigned.contains(m.mid())) throw ProtocolError("invalid_sdp", "The answer must match the relay offer");
        if (m.isRemoved() || !live.contains(m.mid())) continue;
        if (m.direction() != rtc::Description::Direction::RecvOnly) throw ProtocolError("invalid_sdp", "Invalid media direction");
        require_codec(m);
        ++active;
    }
    if (active > max_active) throw ProtocolError("invalid_sdp", "Invalid media count");
}
rtc::Description::Media downstream_media(const rtc::Description::Media& source, const std::string& mid) {
    auto media = source;
    media.parseSdpLine("a=mid:" + mid);
    media.setDirection(rtc::Description::Direction::SendOnly);
    for (auto id : media.extIds()) {
        const auto* ext = media.extMap(id);
        if (ext && (ext->uri == "urn:ietf:params:rtp-hdrext:sdes:mid" || ext->uri == "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id" ||
            ext->uri == "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id")) media.removeExtMap(id);
    }
    return media;
}
rtc::binary picture_loss_indication(std::uint32_t ssrc) {
    rtc::binary pli(12, std::byte{0}); pli[0] = std::byte{0x81}; pli[1] = std::byte{206}; pli[3] = std::byte{2};
    for (unsigned i = 0; i < 4; ++i) pli[8 + i] = static_cast<std::byte>((ssrc >> (24 - 8 * i)) & 255);
    return pli;
}
bool contains_keyframe_request(const rtc::binary& packet) {
    if (!is_rtcp(packet)) return false;
    for (std::size_t offset = 0; offset + 4 <= packet.size();) {
        auto size = ((byte(packet, offset + 2) << 8) | byte(packet, offset + 3)) * 4 + 4;
        if (size < 4 || offset + size > packet.size()) return false;
        auto fmt = byte(packet, offset) & 31;
        if (byte(packet, offset + 1) == 206 && ((fmt == 1 && size >= 12) || (fmt == 4 && size >= 20))) return true;
        offset += size;
    }
    return false;
}
std::string public_candidate(std::string candidate, const std::string& address) {
    if (address.empty()) return candidate;
    std::istringstream input(candidate); std::vector<std::string> parts; std::string part;
    while (input >> part) parts.push_back(part);
    if (parts.size() < 8 || parts[6] != "typ" || parts[7] != "host" || (parts[2] != "UDP" && parts[2] != "udp")) return candidate;
    parts[4] = address;
    std::ostringstream result; for (std::size_t i = 0; i < parts.size(); ++i) { if (i) result << ' '; result << parts[i]; }
    return result.str();
}
}
