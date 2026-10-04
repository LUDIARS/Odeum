#include "opus_audio_encoder.hpp"
#include "../core/pcm_framer.hpp"
#include <opus.h>
#include <stdexcept>
#include <string>

namespace odeum::presenter {
namespace {
void check(int result, const char* what) {
    if (result != OPUS_OK) throw std::runtime_error(std::string(what) + ": " + opus_strerror(result));
}
}

OpusAudioEncoder::OpusAudioEncoder() {
    int error = OPUS_OK;
    encoder_ = opus_encoder_create(pcm_rate, pcm_channels, OPUS_APPLICATION_AUDIO, &error);
    check(error, "Opus encoder");
}

OpusAudioEncoder::~OpusAudioEncoder() { opus_encoder_destroy(encoder_); }

void OpusAudioEncoder::configure(int bitrate_kbps) {
    check(opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_kbps * 1000)), "Opus bitrate");
    // In-band FEC matches the useinbandfec=1 the SDP advertises.
    check(opus_encoder_ctl(encoder_, OPUS_SET_INBAND_FEC(1)), "Opus FEC");
    check(opus_encoder_ctl(encoder_, OPUS_SET_PACKET_LOSS_PERC(5)), "Opus loss");
}

std::vector<std::byte> OpusAudioEncoder::encode(std::span<const float> frame) {
    if (frame.size() != static_cast<std::size_t>(pcm_frame_samples * pcm_channels)) throw std::invalid_argument("Opus needs one 20 ms stereo frame");
    std::vector<std::byte> packet(4000);
    const auto size = opus_encode_float(encoder_, frame.data(), pcm_frame_samples,
                                        reinterpret_cast<unsigned char*>(packet.data()), static_cast<opus_int32>(packet.size()));
    if (size < 0) throw std::runtime_error(std::string("Opus encode: ") + opus_strerror(size));
    packet.resize(static_cast<std::size_t>(size));
    return packet;
}
}
