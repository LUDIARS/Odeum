#import <AudioToolbox/AudioToolbox.h>
#include "audio_toolbox_aac_encoder.hpp"
#include "../../core/program_settings.hpp"
#include <stdexcept>
#include <string>

namespace odeum::program::macos {
namespace {
constexpr UInt32 aac_frame_samples = 1024;
constexpr OSStatus no_more_input = 'odnm';
}

struct AudioToolboxAacEncoder::State {
    AudioConverterRef converter = nullptr;
    std::vector<float> pending;   // interleaved stereo waiting to be encoded
    std::size_t consumed = 0;     // floats handed to the converter from `pending`
    std::int64_t origin_us = -1;  // timestamp of the first sample still owed a packet
    std::uint64_t samples_out = 0;

    ~State() { if (converter) AudioConverterDispose(converter); }

    static OSStatus supply(AudioConverterRef, UInt32* packets, AudioBufferList* data, AudioStreamPacketDescription**, void* refcon) {
        auto* self = static_cast<State*>(refcon);
        const auto available = (self->pending.size() - self->consumed) / aac_channels;
        if (available < aac_frame_samples) { *packets = 0; return no_more_input; }
        const auto take = std::min<std::size_t>(*packets, available);
        data->mBuffers[0].mData = self->pending.data() + self->consumed;
        data->mBuffers[0].mDataByteSize = static_cast<UInt32>(take * aac_channels * sizeof(float));
        data->mBuffers[0].mNumberChannels = aac_channels;
        self->consumed += take * aac_channels;
        *packets = static_cast<UInt32>(take);
        return noErr;
    }
};

AudioToolboxAacEncoder::AudioToolboxAacEncoder() = default;
AudioToolboxAacEncoder::~AudioToolboxAacEncoder() = default;

void AudioToolboxAacEncoder::configure(int bitrate_kbps) {
    auto state = std::make_unique<State>();
    AudioStreamBasicDescription input{};
    input.mSampleRate = aac_rate;
    input.mFormatID = kAudioFormatLinearPCM;
    input.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    input.mChannelsPerFrame = aac_channels;
    input.mBitsPerChannel = 32;
    input.mFramesPerPacket = 1;
    input.mBytesPerFrame = input.mBytesPerPacket = aac_channels * sizeof(float);
    AudioStreamBasicDescription output{};
    output.mSampleRate = aac_rate;
    output.mFormatID = kAudioFormatMPEG4AAC;
    output.mFormatFlags = kMPEG4Object_AAC_LC;
    output.mChannelsPerFrame = aac_channels;
    output.mFramesPerPacket = aac_frame_samples;
    if (const auto status = AudioConverterNew(&input, &output, &state->converter); status != noErr)
        throw std::runtime_error("AudioConverterNew failed (" + std::to_string(status) + ")");
    UInt32 bitrate = static_cast<UInt32>(bitrate_kbps) * 1000;
    AudioConverterSetProperty(state->converter, kAudioConverterEncodeBitRate, sizeof bitrate, &bitrate);
    state_ = std::move(state);
}

std::vector<AacPacket> AudioToolboxAacEncoder::encode(std::span<const float> frame, std::int64_t timestamp_us) {
    std::vector<AacPacket> packets;
    if (!state_) return packets;
    auto& s = *state_;
    if (s.origin_us < 0) s.origin_us = timestamp_us;
    s.pending.insert(s.pending.end(), frame.begin(), frame.end());
    std::vector<std::byte> buffer(8192);
    while ((s.pending.size() - s.consumed) / aac_channels >= aac_frame_samples) {
        AudioBufferList list{};
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = aac_channels;
        list.mBuffers[0].mDataByteSize = static_cast<UInt32>(buffer.size());
        list.mBuffers[0].mData = buffer.data();
        UInt32 count = 1;
        AudioStreamPacketDescription description{};
        const auto status = AudioConverterFillComplexBuffer(s.converter, &State::supply, &s, &count, &list, &description);
        if (status != noErr && status != no_more_input) throw std::runtime_error("AudioConverterFillComplexBuffer failed (" + std::to_string(status) + ")");
        if (count == 0) break;
        AacPacket packet;
        packet.data.assign(buffer.begin(), buffer.begin() + list.mBuffers[0].mDataByteSize);
        packet.timestamp_us = s.origin_us + static_cast<std::int64_t>(s.samples_out * 1000000 / aac_rate);
        s.samples_out += aac_frame_samples;
        packets.push_back(std::move(packet));
    }
    s.pending.erase(s.pending.begin(), s.pending.begin() + static_cast<std::ptrdiff_t>(s.consumed));
    s.consumed = 0;
    return packets;
}
}
