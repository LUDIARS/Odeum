#include "wasapi_loopback_source.hpp"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace odeum::presenter::windows {
using Microsoft::WRL::ComPtr;

namespace {
void check(HRESULT result, const char* what) {
    if (FAILED(result)) throw std::runtime_error(std::string(what) + " failed");
}

enum class SampleKind { float32, int16, unsupported };

SampleKind kind_of(const WAVEFORMATEX* format) {
    auto tag = format->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
        if (extensible->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (extensible->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) tag = WAVE_FORMAT_PCM;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && format->wBitsPerSample == 32) return SampleKind::float32;
    if (tag == WAVE_FORMAT_PCM && format->wBitsPerSample == 16) return SampleKind::int16;
    return SampleKind::unsupported;
}
}

WasapiLoopbackSource::~WasapiLoopbackSource() { stop(); }

void WasapiLoopbackSource::start(Sink sink, ErrorSink errors) {
    stop();
    running_ = true;
    thread_ = std::thread([this, sink = std::move(sink), errors = std::move(errors)] { run(sink, errors); });
}

void WasapiLoopbackSource::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void WasapiLoopbackSource::run(Sink sink, ErrorSink errors) {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    try {
        ComPtr<IMMDeviceEnumerator> enumerator;
        check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)), "MMDeviceEnumerator");
        ComPtr<IMMDevice> device;
        check(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device), "GetDefaultAudioEndpoint");
        ComPtr<IAudioClient> client;
        check(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf())), "IAudioClient");
        WAVEFORMATEX* format = nullptr;
        check(client->GetMixFormat(&format), "GetMixFormat");
        const auto kind = kind_of(format);
        const int channels = format->nChannels;
        const int rate = static_cast<int>(format->nSamplesPerSec);
        const auto initialized = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000 /* 1 s buffer */, 0, format, nullptr);
        CoTaskMemFree(format);
        check(initialized, "IAudioClient::Initialize");
        if (kind == SampleKind::unsupported) throw std::runtime_error("Unsupported mix format");
        ComPtr<IAudioCaptureClient> capture;
        check(client->GetService(IID_PPV_ARGS(&capture)), "IAudioCaptureClient");
        check(client->Start(), "IAudioClient::Start");
        std::vector<float> samples;
        const auto origin = std::chrono::steady_clock::now();
        while (running_) {
            UINT32 packet = 0;
            check(capture->GetNextPacketSize(&packet), "GetNextPacketSize");
            if (packet == 0) { Sleep(10); continue; }
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            check(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "GetBuffer");
            samples.assign(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels), 0.0f);
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                if (kind == SampleKind::float32) std::memcpy(samples.data(), data, samples.size() * sizeof(float));
                else {
                    const auto* pcm = reinterpret_cast<const std::int16_t*>(data);
                    for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = static_cast<float>(pcm[i]) / 32768.0f;
                }
            }
            capture->ReleaseBuffer(frames);
            const auto now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - origin).count();
            sink({samples, channels, rate, now});
        }
        client->Stop();
    } catch (const std::exception& error) {
        if (running_) errors(error.what());
    }
    if (com) CoUninitialize();
}
}
