#include "media_foundation_encoder.hpp"
#include "../../core/h264_bitstream.hpp"
#include "../../core/nv12_scaler.hpp"
#include <windows.h>
#include <d3d11.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <codecapi.h>
#include <strmif.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace odeum::presenter::windows {
using Microsoft::WRL::ComPtr;

namespace {
void check(HRESULT result, const char* what) {
    if (SUCCEEDED(result)) return;
    std::ostringstream message;
    message << what << " failed (0x" << std::hex << std::setw(8) << std::setfill('0') << static_cast<unsigned long>(result) << ")";
    throw std::runtime_error(message.str());
}

bool set_codec(ICodecAPI* codec, const GUID& key, ULONG value) {
    VARIANT variant{};
    variant.vt = VT_UI4;
    variant.ulVal = value;
    return SUCCEEDED(codec->SetValue(&key, &variant));
}

UINT32 mf_profile(H264Profile profile) {
    switch (profile) {
    case H264Profile::main: return eAVEncH264VProfile_Main;
    case H264Profile::high: return eAVEncH264VProfile_High;
    default: return eAVEncH264VProfile_ConstrainedBase;
    }
}

ComPtr<IMFMediaType> video_type(const GUID& subtype, const StreamSettings& s) {
    ComPtr<IMFMediaType> type;
    check(MFCreateMediaType(&type), "MFCreateMediaType");
    check(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "major type");
    check(type->SetGUID(MF_MT_SUBTYPE, subtype), "subtype");
    check(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, static_cast<UINT32>(s.width), static_cast<UINT32>(s.height)), "frame size");
    check(MFSetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, static_cast<UINT32>(s.fps), 1), "frame rate");
    check(MFSetAttributeRatio(type.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1), "aspect ratio");
    check(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "interlace mode");
    return type;
}
}

struct MediaFoundationEncoder::State {
    StreamSettings settings;
    Sink sink;
    ComPtr<IMFTransform> mft;
    ComPtr<ICodecAPI> codec;
    MFT_OUTPUT_STREAM_INFO output{};
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11Texture2D> staging;
    UINT staging_width = 0, staging_height = 0;
    DXGI_FORMAT staging_format = DXGI_FORMAT_UNKNOWN;
    std::vector<std::uint8_t> nv12;
    std::vector<std::byte> parameter_sets;

    void read_parameter_sets() {
        ComPtr<IMFMediaType> type;
        if (FAILED(mft->GetOutputCurrentType(0, &type))) return;
        UINT32 size = 0;
        if (FAILED(type->GetBlobSize(MF_MT_MPEG_SEQUENCE_HEADER, &size)) || size == 0) return;
        parameter_sets.resize(size);
        if (FAILED(type->GetBlob(MF_MT_MPEG_SEQUENCE_HEADER, reinterpret_cast<UINT8*>(parameter_sets.data()), size, nullptr))) parameter_sets.clear();
    }

    void renegotiate() {
        ComPtr<IMFMediaType> type;
        check(mft->GetOutputAvailableType(0, 0, &type), "GetOutputAvailableType");
        check(mft->SetOutputType(0, type.Get(), 0), "SetOutputType");
        check(mft->GetOutputStreamInfo(0, &output), "GetOutputStreamInfo");
        parameter_sets.clear();
    }

    void drain() {
        const bool provides = (output.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
        for (;;) {
            ComPtr<IMFSample> sample;
            MFT_OUTPUT_DATA_BUFFER buffer{};
            if (!provides) {
                ComPtr<IMFMediaBuffer> memory;
                check(MFCreateSample(&sample), "MFCreateSample");
                check(MFCreateMemoryBuffer(std::max<DWORD>(output.cbSize, 1 << 20), &memory), "MFCreateMemoryBuffer");
                check(sample->AddBuffer(memory.Get()), "AddBuffer");
                buffer.pSample = sample.Get();
            }
            DWORD status = 0;
            const auto result = mft->ProcessOutput(0, 1, &buffer, &status);
            if (buffer.pEvents) buffer.pEvents->Release();
            if (provides && buffer.pSample) sample.Attach(buffer.pSample);
            if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            if (result == MF_E_TRANSFORM_STREAM_CHANGE) { renegotiate(); continue; }
            check(result, "ProcessOutput");
            if (!sample) continue;
            ComPtr<IMFMediaBuffer> contiguous;
            check(sample->ConvertToContiguousBuffer(&contiguous), "ConvertToContiguousBuffer");
            BYTE* bytes = nullptr;
            DWORD length = 0;
            check(contiguous->Lock(&bytes, nullptr, &length), "Lock");
            EncodedFrame frame;
            frame.data.resize(length);
            std::memcpy(frame.data.data(), bytes, length);
            contiguous->Unlock();
            LONGLONG time = 0;
            sample->GetSampleTime(&time);
            frame.timestamp_us = time / 10;
            // With B-frames the MFT stamps the decode time separately; without them it equals the sample time.
            UINT64 decode_time = 0;
            frame.decode_timestamp_us = SUCCEEDED(sample->GetUINT64(MFSampleExtension_DecodeTimestamp, &decode_time))
                ? static_cast<std::int64_t>(decode_time) / 10 : frame.timestamp_us;
            UINT32 clean = 0;
            frame.keyframe = SUCCEEDED(sample->GetUINT32(MFSampleExtension_CleanPoint, &clean)) ? clean != 0 : contains_nal(frame.data, nal_idr);
            if (parameter_sets.empty()) read_parameter_sets();
            ensure_parameter_sets(frame.data, frame.keyframe, parameter_sets);
            sink(std::move(frame));
        }
    }

    void stage(const VideoFrame& frame, ID3D11Texture2D* texture) {
        ComPtr<ID3D11Device> source_device;
        texture->GetDevice(&source_device);
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        if (source_device != device || desc.Width != staging_width || desc.Height != staging_height || desc.Format != staging_format) {
            D3D11_TEXTURE2D_DESC copy{};
            copy.Width = desc.Width;
            copy.Height = desc.Height;
            copy.MipLevels = 1;
            copy.ArraySize = 1;
            copy.Format = desc.Format;
            copy.SampleDesc.Count = 1;
            copy.Usage = D3D11_USAGE_STAGING;
            copy.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            staging.Reset();
            check(source_device->CreateTexture2D(&copy, nullptr, &staging), "CreateTexture2D (staging)");
            device = source_device;
            staging_width = desc.Width;
            staging_height = desc.Height;
            staging_format = desc.Format;
        }
        ComPtr<ID3D11DeviceContext> context;
        device->GetImmediateContext(&context);
        const auto width = std::min<UINT>(static_cast<UINT>(frame.width), desc.Width);
        const auto height = std::min<UINT>(static_cast<UINT>(frame.height), desc.Height);
        const D3D11_BOX box{0, 0, 0, width, height, 1};
        context->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, texture, 0, &box);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "Map");
        try {
            bgra_to_nv12({static_cast<const std::uint8_t*>(mapped.pData), static_cast<std::size_t>(mapped.RowPitch) * height},
                         static_cast<int>(width), static_cast<int>(height), static_cast<int>(mapped.RowPitch), nv12, settings.width, settings.height);
        } catch (...) {
            context->Unmap(staging.Get(), 0);
            throw;
        }
        context->Unmap(staging.Get(), 0);
    }
};

MediaFoundationEncoder::MediaFoundationEncoder() { check(MFStartup(MF_VERSION, MFSTARTUP_LITE), "MFStartup"); }

MediaFoundationEncoder::~MediaFoundationEncoder() {
    stop();
    MFShutdown();
}

void MediaFoundationEncoder::configure(const StreamSettings& settings, Sink sink) {
    stop();
    auto state = std::make_unique<State>();
    state->settings = settings;
    state->sink = std::move(sink);
    check(CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&state->mft)), "H.264 encoder MFT");
    check(state->mft.As(&state->codec), "ICodecAPI");
    auto* codec = state->codec.Get();
    const ULONG peak = static_cast<ULONG>(settings.max_bitrate_kbps) * 1000;
    const bool baseline = h264_.profile == H264Profile::constrained_baseline;
    // Rate control has to be chosen before the media types are set. Main/High is the program
    // feed for YouTube, which asks for constant bitrate.
    if (!baseline) {
        set_codec(codec, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        set_codec(codec, CODECAPI_AVEncCommonMeanBitRate, peak);
    } else if (set_codec(codec, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_PeakConstrainedVBR)) {
        set_codec(codec, CODECAPI_AVEncCommonMeanBitRate, peak / 10 * 7);
        set_codec(codec, CODECAPI_AVEncCommonMaxBitRate, peak);
    } else {
        set_codec(codec, CODECAPI_AVEncCommonRateControlMode, eAVEncCommonRateControlMode_CBR);
        set_codec(codec, CODECAPI_AVEncCommonMeanBitRate, peak);
    }
    set_codec(codec, CODECAPI_AVEncMPVGOPSize, static_cast<ULONG>(settings.fps * settings.keyframe_interval_s));
    // B-frames only outside Baseline; an encoder that refuses the count simply emits none.
    const auto b_frames = baseline ? 0 : std::clamp(h264_.b_frames, 0, 2);
    if (!set_codec(codec, CODECAPI_AVEncMPVDefaultBPictureCount, static_cast<ULONG>(b_frames))) set_codec(codec, CODECAPI_AVEncMPVDefaultBPictureCount, 0);
    VARIANT low_latency{};
    low_latency.vt = VT_BOOL;
    // Low-latency mode turns reordering off, so it stays on only when no B-frames are wanted.
    low_latency.boolVal = b_frames == 0 ? VARIANT_TRUE : VARIANT_FALSE;
    codec->SetValue(&CODECAPI_AVLowLatencyMode, &low_latency);

    auto output = video_type(MFVideoFormat_H264, settings);
    check(output->SetUINT32(MF_MT_AVG_BITRATE, peak), "average bitrate");
    check(output->SetUINT32(MF_MT_MPEG2_PROFILE, mf_profile(h264_.profile)), "profile");
    if (FAILED(state->mft->SetOutputType(0, output.Get(), 0))) {
        // Older encoders only know plain Baseline; without B-frames and FMO it decodes the same.
        // Main/High has no such fallback: the profile was asked for, so a refusal is an error.
        if (!baseline) throw std::runtime_error("The H.264 encoder refused the requested profile");
        check(output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_Base), "profile");
        check(state->mft->SetOutputType(0, output.Get(), 0), "SetOutputType");
    }
    auto input = video_type(MFVideoFormat_NV12, settings);
    input->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT709);
    input->SetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, MFNominalRange_16_235);
    check(state->mft->SetInputType(0, input.Get(), 0), "SetInputType");
    check(state->mft->GetOutputStreamInfo(0, &state->output), "GetOutputStreamInfo");
    state->nv12.resize(static_cast<std::size_t>(settings.width) * static_cast<std::size_t>(settings.height) * 3 / 2);
    check(state->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "begin streaming");
    check(state->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "start of stream");
    state_ = std::move(state);
}

void MediaFoundationEncoder::encode(const VideoFrame& frame, bool force_keyframe) {
    if (!state_ || (!frame.native && !frame.nv12)) return;
    auto& s = *state_;
    if (frame.nv12) {
        // Composed in memory at the stream size already (odeum-program).
        if (frame.width != s.settings.width || frame.height != s.settings.height || frame.nv12->size() < s.nv12.size())
            throw std::invalid_argument("NV12 frame does not match the stream size");
        std::memcpy(s.nv12.data(), frame.nv12->data(), s.nv12.size());
    } else {
        s.stage(frame, static_cast<ID3D11Texture2D*>(frame.native.get()));
    }
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateMemoryBuffer(static_cast<DWORD>(s.nv12.size()), &buffer), "MFCreateMemoryBuffer");
    BYTE* bytes = nullptr;
    check(buffer->Lock(&bytes, nullptr, nullptr), "Lock");
    std::memcpy(bytes, s.nv12.data(), s.nv12.size());
    buffer->Unlock();
    check(buffer->SetCurrentLength(static_cast<DWORD>(s.nv12.size())), "SetCurrentLength");
    ComPtr<IMFSample> sample;
    check(MFCreateSample(&sample), "MFCreateSample");
    check(sample->AddBuffer(buffer.Get()), "AddBuffer");
    check(sample->SetSampleTime(frame.timestamp_us * 10), "SetSampleTime");
    check(sample->SetSampleDuration(10000000LL / s.settings.fps), "SetSampleDuration");
    if (force_keyframe) set_codec(s.codec.Get(), CODECAPI_AVEncVideoForceKeyFrame, 1);
    auto result = s.mft->ProcessInput(0, sample.Get(), 0);
    if (result == MF_E_NOTACCEPTING) {
        s.drain();
        result = s.mft->ProcessInput(0, sample.Get(), 0);
    }
    check(result, "ProcessInput");
    s.drain();
}

void MediaFoundationEncoder::stop() {
    if (!state_) return;
    state_->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
    state_->mft->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
    state_.reset();
}
}
