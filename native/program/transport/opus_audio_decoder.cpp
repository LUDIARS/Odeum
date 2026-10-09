#include "opus_audio_decoder.hpp"
#include "../core/audio_mixer.hpp"
#include <opus.h>
#include <stdexcept>
#include <string>

namespace odeum::program {
namespace {
// The longest Opus packet: 120 ms.
constexpr int max_samples = mix_rate / 1000 * 120;
}

OpusAudioDecoder::OpusAudioDecoder() {
    int error = OPUS_OK;
    decoder_ = opus_decoder_create(mix_rate, mix_channels, &error);
    if (error != OPUS_OK) throw std::runtime_error(std::string("Opus decoder: ") + opus_strerror(error));
}

OpusAudioDecoder::~OpusAudioDecoder() { opus_decoder_destroy(decoder_); }

std::vector<float> OpusAudioDecoder::decode(std::span<const std::byte> packet) {
    std::vector<float> pcm(static_cast<std::size_t>(max_samples * mix_channels));
    const auto samples = opus_decode_float(decoder_, reinterpret_cast<const unsigned char*>(packet.data()),
                                           static_cast<opus_int32>(packet.size()), pcm.data(), max_samples, 0);
    if (samples <= 0) return {};
    pcm.resize(static_cast<std::size_t>(samples * mix_channels));
    return pcm;
}
}
