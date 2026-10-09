#include "media_foundation_aac_encoder.hpp"
#include "mf_check.hpp"
#include "../../core/program_settings.hpp"
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace odeum::program::windows {
using Microsoft::WRL::ComPtr;

namespace {
UINT32 bytes_per_second(int kbps) {
    constexpr int offered[] = {96, 128, 160, 192};
    int best = offered[0];
    for (int rate : offered) if (std::abs(rate - kbps) < std::abs(best - kbps)) best = rate;
    return static_cast<UINT32>(best * 1000 / 8);
}

ComPtr<IMFMediaType> audio_type(const GUID& subtype) {
    ComPtr<IMFMediaType> type;
    check(MFCreateMediaType(&type), "MFCreateMediaType");
    check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio), "major type");
    check(type->SetGUID(MF_MT_SUBTYPE, subtype), "subtype");
    check(type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16), "bits per sample");
    check(type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, aac_rate), "sample rate");
    check(type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, aac_channels), "channels");
    return type;
}
}

struct MediaFoundationAacEncoder::State {
    ComPtr<IMFTransform> mft;
    MFT_OUTPUT_STREAM_INFO output{};

    void drain(std::vector<AacPacket>& packets) {
        const bool provides = (output.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        for (;;) {
            ComPtr<IMFSample> sample;
            MFT_OUTPUT_DATA_BUFFER buffer{};
            if (!provides) {
                ComPtr<IMFMediaBuffer> memory;
                check(MFCreateSample(&sample), "MFCreateSample");
                check(MFCreateMemoryBuffer(std::max<DWORD>(output.cbSize, 8192), &memory), "MFCreateMemoryBuffer");
                check(sample->AddBuffer(memory.Get()), "AddBuffer");
                buffer.pSample = sample.Get();
            }
            DWORD status = 0;
            const auto result = mft->ProcessOutput(0, 1, &buffer, &status);
            if (buffer.pEvents) buffer.pEvents->Release();
            if (provides && buffer.pSample) sample.Attach(buffer.pSample);
            if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            check(result, "ProcessOutput (AAC)");
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> contiguous;
            check(sample->ConvertToContiguousBuffer(&contiguous), "ConvertToContiguousBuffer");
            BYTE* bytes = nullptr;
            DWORD length = 0;
            check(contiguous->Lock(&bytes, nullptr, &length), "Lock");
            AacPacket packet;
            packet.data.resize(length);
            std::memcpy(packet.data.data(), bytes, length);
            contiguous->Unlock();
            LONGLONG time = 0;
            sample->GetSampleTime(&time);
            packet.timestamp_us = time / 10;
            if (!packet.data.empty()) packets.push_back(std::move(packet));
        }
    }
};

MediaFoundationAacEncoder::MediaFoundationAacEncoder() { check(MFStartup(MF_VERSION, MFSTARTUP_LITE), "MFStartup"); }

MediaFoundationAacEncoder::~MediaFoundationAacEncoder() {
    state_.reset();
    MFShutdown();
}

void MediaFoundationAacEncoder::configure(int bitrate_kbps) {
    auto state = std::make_unique<State>();
    check(CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&state->mft)), "AAC encoder MFT");
    auto output = audio_type(MFAudioFormat_AAC);
    check(output->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes_per_second(bitrate_kbps)), "AAC bitrate");
    check(output->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0), "raw AAC payload");
    check(output->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29), "AAC-LC profile");
    check(state->mft->SetOutputType(0, output.Get(), 0), "SetOutputType (AAC)");
    auto input = audio_type(MFAudioFormat_PCM);
    check(input->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, aac_channels * 2), "block alignment");
    check(input->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, aac_rate * aac_channels * 2), "PCM bytes per second");
    check(state->mft->SetInputType(0, input.Get(), 0), "SetInputType (PCM)");
    check(state->mft->GetOutputStreamInfo(0, &state->output), "GetOutputStreamInfo");
    check(state->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "begin streaming");
    check(state->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "start of stream");
    state_ = std::move(state);
}

std::vector<AacPacket> MediaFoundationAacEncoder::encode(std::span<const float> frame, std::int64_t timestamp_us) {
    std::vector<AacPacket> packets;
    if (!state_) return packets;
    const auto bytes = static_cast<DWORD>(frame.size() * sizeof(std::int16_t));
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateMemoryBuffer(bytes, &buffer), "MFCreateMemoryBuffer");
    BYTE* data = nullptr;
    check(buffer->Lock(&data, nullptr, nullptr), "Lock");
    auto* pcm = reinterpret_cast<std::int16_t*>(data);
    for (std::size_t i = 0; i < frame.size(); ++i)
        pcm[i] = static_cast<std::int16_t>(std::lround(std::clamp(frame[i], -1.f, 1.f) * 32767.f));
    buffer->Unlock();
    check(buffer->SetCurrentLength(bytes), "SetCurrentLength");
    ComPtr<IMFSample> sample;
    check(MFCreateSample(&sample), "MFCreateSample");
    check(sample->AddBuffer(buffer.Get()), "AddBuffer");
    check(sample->SetSampleTime(timestamp_us * 10), "SetSampleTime");
    check(sample->SetSampleDuration(static_cast<LONGLONG>(frame.size() / aac_channels) * 10000000LL / aac_rate), "SetSampleDuration");
    auto result = state_->mft->ProcessInput(0, sample.Get(), 0);
    if (result == MF_E_NOTACCEPTING) {
        state_->drain(packets);
        result = state_->mft->ProcessInput(0, sample.Get(), 0);
    }
    check(result, "ProcessInput (AAC)");
    state_->drain(packets);
    return packets;
}
}
