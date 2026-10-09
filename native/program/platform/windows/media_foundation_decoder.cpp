#include "media_foundation_decoder.hpp"
#include "mf_check.hpp"
#include <windows.h>
#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wmcodecdsp.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>

namespace odeum::program::windows {
using Microsoft::WRL::ComPtr;

struct MediaFoundationDecoder::State {
    ComPtr<IMFTransform> mft;
    MFT_OUTPUT_STREAM_INFO output{};
    UINT32 width = 0, height = 0;       // padded frame size
    UINT32 visible_x = 0, visible_y = 0, visible_width = 0, visible_height = 0;
    LONG stride = 0;
    bool typed = false;

    void create() {
        mft.Reset();
        typed = false;
        check(CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&mft)), "H.264 decoder MFT");
        ComPtr<ICodecAPI> codec;
        if (SUCCEEDED(mft.As(&codec))) {
            VARIANT low_latency{};
            low_latency.vt = VT_BOOL;
            low_latency.boolVal = VARIANT_TRUE;
            codec->SetValue(&CODECAPI_AVLowLatencyMode, &low_latency);
        }
        ComPtr<IMFMediaType> input;
        check(MFCreateMediaType(&input), "MFCreateMediaType");
        check(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "major type");
        check(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "subtype");
        check(input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive), "interlace mode");
        check(mft->SetInputType(0, input.Get(), 0), "SetInputType");
        choose_output();
        check(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "begin streaming");
        check(mft->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "start of stream");
    }

    // NV12 output; size and aperture are read again after every stream change.
    void choose_output() {
        for (DWORD i = 0;; ++i) {
            ComPtr<IMFMediaType> type;
            if (FAILED(mft->GetOutputAvailableType(0, i, &type))) return;
            GUID subtype{};
            if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_NV12) continue;
            check(mft->SetOutputType(0, type.Get(), 0), "SetOutputType");
            check(mft->GetOutputStreamInfo(0, &output), "GetOutputStreamInfo");
            typed = SUCCEEDED(MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height)) && width > 0 && height > 0;
            UINT32 pitch = 0;
            stride = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &pitch)) ? static_cast<LONG>(pitch) : static_cast<LONG>(width);
            MFVideoArea area{};
            if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof area, nullptr))) {
                visible_x = static_cast<UINT32>(area.OffsetX.value);
                visible_y = static_cast<UINT32>(area.OffsetY.value);
                visible_width = static_cast<UINT32>(area.Area.cx);
                visible_height = static_cast<UINT32>(area.Area.cy);
            } else {
                visible_x = visible_y = 0;
                visible_width = width;
                visible_height = height;
            }
            visible_width &= ~1u;
            visible_height &= ~1u;
            return;
        }
    }

    void drain(std::vector<Nv12Picture>& pictures, std::int64_t timestamp_us) {
        for (;;) {
            ComPtr<IMFSample> sample;
            ComPtr<IMFMediaBuffer> memory;
            check(MFCreateSample(&sample), "MFCreateSample");
            check(MFCreateMemoryBuffer(std::max<DWORD>(output.cbSize, width * height * 3 / 2 + 4096), &memory), "MFCreateMemoryBuffer");
            check(sample->AddBuffer(memory.Get()), "AddBuffer");
            MFT_OUTPUT_DATA_BUFFER buffer{};
            buffer.pSample = sample.Get();
            DWORD status = 0;
            const auto result = mft->ProcessOutput(0, 1, &buffer, &status);
            if (buffer.pEvents) buffer.pEvents->Release();
            if (result == MF_E_TRANSFORM_NEED_MORE_INPUT) return;
            if (result == MF_E_TRANSFORM_STREAM_CHANGE) { choose_output(); continue; }
            check(result, "ProcessOutput");
            if (!typed) continue;
            ComPtr<IMFMediaBuffer> contiguous;
            check(sample->ConvertToContiguousBuffer(&contiguous), "ConvertToContiguousBuffer");
            BYTE* bytes = nullptr;
            DWORD length = 0;
            check(contiguous->Lock(&bytes, nullptr, &length), "Lock");
            const auto pitch = static_cast<std::size_t>(std::max<LONG>(stride, static_cast<LONG>(width)));
            // Rows of the padded luma plane; the chroma plane follows them.
            const auto rows = std::max<std::size_t>(height, length / pitch * 2 / 3);
            auto pixels = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(visible_width) * visible_height * 3 / 2);
            if (pitch * rows * 3 / 2 <= length) {
                auto* out = pixels->data();
                for (UINT32 y = 0; y < visible_height; ++y, out += visible_width)
                    std::memcpy(out, bytes + (visible_y + y) * pitch + visible_x, visible_width);
                const auto* uv = bytes + pitch * rows;
                for (UINT32 y = 0; y < visible_height / 2; ++y, out += visible_width)
                    std::memcpy(out, uv + (visible_y / 2 + y) * pitch + visible_x, visible_width);
                pictures.push_back({static_cast<int>(visible_width), static_cast<int>(visible_height), std::move(pixels), timestamp_us});
            }
            contiguous->Unlock();
        }
    }
};

MediaFoundationDecoder::MediaFoundationDecoder() : state_(std::make_unique<State>()) {
    check(MFStartup(MF_VERSION, MFSTARTUP_LITE), "MFStartup");
    state_->create();
}

MediaFoundationDecoder::~MediaFoundationDecoder() {
    state_.reset();
    MFShutdown();
}

std::vector<Nv12Picture> MediaFoundationDecoder::decode(std::span<const std::byte> access_unit, std::int64_t timestamp_us) {
    std::vector<Nv12Picture> pictures;
    auto& s = *state_;
    ComPtr<IMFMediaBuffer> buffer;
    check(MFCreateMemoryBuffer(static_cast<DWORD>(access_unit.size()), &buffer), "MFCreateMemoryBuffer");
    BYTE* bytes = nullptr;
    check(buffer->Lock(&bytes, nullptr, nullptr), "Lock");
    std::memcpy(bytes, access_unit.data(), access_unit.size());
    buffer->Unlock();
    check(buffer->SetCurrentLength(static_cast<DWORD>(access_unit.size())), "SetCurrentLength");
    ComPtr<IMFSample> sample;
    check(MFCreateSample(&sample), "MFCreateSample");
    check(sample->AddBuffer(buffer.Get()), "AddBuffer");
    check(sample->SetSampleTime(timestamp_us * 10), "SetSampleTime");
    auto result = s.mft->ProcessInput(0, sample.Get(), 0);
    if (result == MF_E_NOTACCEPTING) {
        s.drain(pictures, timestamp_us);
        result = s.mft->ProcessInput(0, sample.Get(), 0);
    }
    check(result, "ProcessInput");
    s.drain(pictures, timestamp_us);
    return pictures;
}

void MediaFoundationDecoder::reset() {
    state_->mft->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
}
}
