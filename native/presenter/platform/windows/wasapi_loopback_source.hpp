#pragma once
#include "../../core/audio_source.hpp"
#include <atomic>
#include <thread>

namespace odeum::presenter::windows {
// The default playback device's mix through WASAPI loopback (shared mode), polled every 10 ms
// on its own thread. Float and 16-bit PCM mix formats are converted to float.
class WasapiLoopbackSource final : public AudioSource {
public:
    ~WasapiLoopbackSource() override;
    void start(Sink, ErrorSink) override;
    void stop() override;
private:
    void run(Sink sink, ErrorSink errors);
    std::thread thread_;
    std::atomic<bool> running_{false};
};
}
