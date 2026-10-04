#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#include "video_toolbox_encoder.hpp"
#include "../../core/h264_bitstream.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace odeum::presenter::macos {
namespace {
void check(OSStatus status, const char* what) {
    if (status != noErr) throw std::runtime_error(std::string(what) + " failed (" + std::to_string(status) + ")");
}

bool is_keyframe(CMSampleBufferRef sample) {
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
    if (!attachments || CFArrayGetCount(attachments) == 0) return true;
    auto dictionary = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(attachments, 0));
    return !CFDictionaryContainsKey(dictionary, kCMSampleAttachmentKey_NotSync);
}

// SPS and PPS from the format description, as Annex-B, plus the AVCC length-field size.
std::vector<std::byte> parameter_sets(CMFormatDescriptionRef format, int& length_size) {
    std::vector<std::byte> result;
    size_t count = 0;
    int header = 4;
    if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, 0, nullptr, nullptr, &count, &header) != noErr) return result;
    length_size = header;
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format, i, &data, &size, nullptr, nullptr) != noErr) continue;
        append_nal(result, {reinterpret_cast<const std::byte*>(data), size});
    }
    return result;
}
}

struct VideoToolboxEncoder::State {
    VTCompressionSessionRef session = nullptr;
    Sink sink;

    static void output(void* refcon, void*, OSStatus status, VTEncodeInfoFlags flags, CMSampleBufferRef sample) {
        auto* self = static_cast<State*>(refcon);
        if (status != noErr || !sample || (flags & kVTEncodeInfo_FrameDropped) || !CMSampleBufferDataIsReady(sample)) return;
        CMBlockBufferRef block = CMSampleBufferGetDataBuffer(sample);
        if (!block) return;
        const auto size = CMBlockBufferGetDataLength(block);
        std::vector<std::byte> avcc(size);
        if (CMBlockBufferCopyDataBytes(block, 0, size, avcc.data()) != kCMBlockBufferNoErr) return;
        int length_size = 4;
        const bool keyframe = is_keyframe(sample);
        auto sets = parameter_sets(CMSampleBufferGetFormatDescription(sample), length_size);
        EncodedFrame frame;
        try { frame.data = avcc_to_annexb(avcc, length_size); }
        catch (const std::invalid_argument&) { return; }
        frame.keyframe = keyframe;
        frame.timestamp_us = static_cast<std::int64_t>(CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(sample)) * 1e6);
        ensure_parameter_sets(frame.data, keyframe, sets);
        self->sink(std::move(frame));
    }

    void set(CFStringRef key, CFTypeRef value) { VTSessionSetProperty(session, key, value); }
};

VideoToolboxEncoder::VideoToolboxEncoder() = default;
VideoToolboxEncoder::~VideoToolboxEncoder() { stop(); }

void VideoToolboxEncoder::configure(const StreamSettings& s, Sink sink) {
    stop();
    auto state = std::make_unique<State>();
    state->sink = std::move(sink);
    check(VTCompressionSessionCreate(kCFAllocatorDefault, s.width, s.height, kCMVideoCodecType_H264, nullptr, nullptr, nullptr,
                                     &State::output, state.get(), &state->session), "VTCompressionSessionCreate");
    @autoreleasepool {
        const auto peak = static_cast<long long>(s.max_bitrate_kbps) * 1000;
        state->set(kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        state->set(kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse);
        if (VTSessionSetProperty(state->session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_ConstrainedBaseline_AutoLevel) != noErr)
            state->set(kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Baseline_AutoLevel);
        state->set(kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef)@(peak / 10 * 7));
        // Bytes per one-second window: the hard ceiling the design asks for.
        state->set(kVTCompressionPropertyKey_DataRateLimits, (__bridge CFTypeRef)@[@(peak / 8), @1]);
        state->set(kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFTypeRef)@(s.fps));
        state->set(kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFTypeRef)@(s.fps * s.keyframe_interval_s));
        state->set(kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, (__bridge CFTypeRef)@(s.keyframe_interval_s));
    }
    check(VTCompressionSessionPrepareToEncodeFrames(state->session), "VTCompressionSessionPrepareToEncodeFrames");
    state_ = std::move(state);
}

void VideoToolboxEncoder::encode(const VideoFrame& frame, bool force_keyframe) {
    if (!state_ || !frame.native) return;
    auto pixels = static_cast<CVPixelBufferRef>(frame.native.get());
    CFDictionaryRef options = nullptr;
    if (force_keyframe) {
        const void* keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
        const void* values[] = {kCFBooleanTrue};
        options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }
    const auto status = VTCompressionSessionEncodeFrame(state_->session, pixels, CMTimeMake(frame.timestamp_us, 1000000), kCMTimeInvalid,
                                                        options, nullptr, nullptr);
    if (options) CFRelease(options);
    check(status, "VTCompressionSessionEncodeFrame");
}

void VideoToolboxEncoder::stop() {
    if (!state_) return;
    VTCompressionSessionCompleteFrames(state_->session, kCMTimeInvalid);
    VTCompressionSessionInvalidate(state_->session);
    CFRelease(state_->session);
    state_.reset();
}
}
