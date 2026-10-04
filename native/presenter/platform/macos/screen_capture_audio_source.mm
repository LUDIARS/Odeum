#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include "screen_capture_audio_source.hpp"
#include "shareable_content.hpp"
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

using odeum::presenter::AudioBlock;
using odeum::presenter::AudioSource;

// Turns ScreenCaptureKit's audio sample buffers (planar or interleaved float) into interleaved
// AudioBlocks on the source's serial queue.
@interface OdeumAudioOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithSink:(AudioSource::Sink)sink errors:(AudioSource::ErrorSink)errors;
- (void)halt;
@end

@implementation OdeumAudioOutput {
    AudioSource::Sink _sink;
    AudioSource::ErrorSink _errors;
    std::atomic<bool> _running;
    std::vector<float> _samples;
}
- (instancetype)initWithSink:(AudioSource::Sink)sink errors:(AudioSource::ErrorSink)errors {
    if ((self = [super init])) {
        _sink = std::move(sink);
        _errors = std::move(errors);
        _running = true;
    }
    return self;
}
- (void)halt { _running = false; }
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeAudio || !_running || !CMSampleBufferIsValid(sample)) return;
    const auto* format = CMAudioFormatDescriptionGetStreamBasicDescription(CMSampleBufferGetFormatDescription(sample));
    if (!format || format->mFormatID != kAudioFormatLinearPCM || !(format->mFormatFlags & kAudioFormatFlagIsFloat) || format->mBitsPerChannel != 32) return;
    const auto frames = static_cast<std::size_t>(CMSampleBufferGetNumSamples(sample));
    const auto channels = static_cast<std::size_t>(format->mChannelsPerFrame);
    if (frames == 0 || channels == 0) return;
    // Room for up to 8 buffers (one per channel when planar).
    struct { AudioBufferList list; AudioBuffer extra[7]; } storage{};
    CMBlockBufferRef retained = nullptr;
    if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sample, nullptr, &storage.list, sizeof(storage), nullptr, nullptr,
            kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &retained) != noErr) return;
    _samples.assign(frames * channels, 0.0f);
    const auto& list = storage.list;
    if (format->mFormatFlags & kAudioFormatFlagIsNonInterleaved) {
        for (std::size_t c = 0; c < std::min<std::size_t>(channels, list.mNumberBuffers); ++c) {
            const auto* plane = static_cast<const float*>(list.mBuffers[c].mData);
            const auto count = std::min<std::size_t>(frames, list.mBuffers[c].mDataByteSize / sizeof(float));
            for (std::size_t i = 0; i < count; ++i) _samples[i * channels + c] = plane[i];
        }
    } else if (list.mNumberBuffers > 0) {
        const auto count = std::min<std::size_t>(_samples.size(), list.mBuffers[0].mDataByteSize / sizeof(float));
        std::copy_n(static_cast<const float*>(list.mBuffers[0].mData), count, _samples.begin());
    }
    CFRelease(retained);
    const CMTime time = CMSampleBufferGetPresentationTimeStamp(sample);
    try {
        _sink(AudioBlock{_samples, static_cast<int>(channels), static_cast<int>(format->mSampleRate),
                         static_cast<std::int64_t>(CMTimeGetSeconds(time) * 1e6)});
    } catch (const std::exception& error) {
        _running = false;
        _errors(error.what());
    }
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    if (!_running.exchange(false)) return;
    _errors(error ? std::string(error.localizedDescription.UTF8String) : std::string("audio capture stopped"));
}
@end

namespace odeum::presenter::macos {
struct ScreenCaptureAudioSource::State {
    SCStream* stream = nil;
    OdeumAudioOutput* output = nil;
    dispatch_queue_t queue = nil;
};

ScreenCaptureAudioSource::ScreenCaptureAudioSource() = default;
ScreenCaptureAudioSource::~ScreenCaptureAudioSource() { stop(); }

void ScreenCaptureAudioSource::start(Sink sink, ErrorSink errors) {
    stop();
    @autoreleasepool {
        SCShareableContent* content = shareable_content();
        if (content.displays.count == 0) throw std::runtime_error("No display to attach the audio capture to");
        SCContentFilter* filter = display_filter_without_self(content, content.displays.firstObject);
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.capturesAudio = YES;
        config.excludesCurrentProcessAudio = YES;
        config.sampleRate = 48000;
        config.channelCount = 2;
        // The video of this stream is never used; keep it as small and slow as allowed.
        config.width = 2;
        config.height = 2;
        config.minimumFrameInterval = CMTimeMake(1, 1);
        auto state = std::make_unique<State>();
        state->queue = dispatch_queue_create("odeum.presenter.audio", DISPATCH_QUEUE_SERIAL);
        state->output = [[OdeumAudioOutput alloc] initWithSink:std::move(sink) errors:std::move(errors)];
        state->stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:state->output];
        NSError* error = nil;
        if (![state->stream addStreamOutput:state->output type:SCStreamOutputTypeAudio sampleHandlerQueue:state->queue error:&error])
            throw std::runtime_error(error ? error.localizedDescription.UTF8String : "Cannot attach the audio output");
        SCStream* stream = state->stream;
        wait_for(^(void (^done)(NSError*)) { [stream startCaptureWithCompletionHandler:done]; }, "Starting audio capture");
        state_ = std::move(state);
    }
}

void ScreenCaptureAudioSource::stop() {
    if (!state_) return;
    [state_->output halt];
    SCStream* stream = state_->stream;
    try { wait_for(^(void (^done)(NSError*)) { [stream stopCaptureWithCompletionHandler:done]; }, "Stopping audio capture"); }
    catch (const std::exception&) { /* Already stopped by the system. */ }
    dispatch_sync(state_->queue, ^{});
    state_.reset();
}
}
