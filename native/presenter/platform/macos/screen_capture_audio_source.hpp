#pragma once
#include "../../core/audio_source.hpp"
#include <memory>

namespace odeum::presenter::macos {
// System audio through a ScreenCaptureKit stream of its own (48 kHz stereo, this process's own
// sound excluded). Kept apart from the video stream so switching the capture target never
// interrupts the sound.
class ScreenCaptureAudioSource final : public AudioSource {
public:
    ScreenCaptureAudioSource();
    ~ScreenCaptureAudioSource() override;
    void start(Sink, ErrorSink) override;
    void stop() override;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
