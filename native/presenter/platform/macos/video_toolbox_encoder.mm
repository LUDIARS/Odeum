#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#include "video_toolbox_encoder.hpp"
#include "../../core/h264_bitstream.hpp"
#include <cstring>
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
    int width = 0, height = 0;

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
        const auto decode_time = CMSampleBufferGetDecodeTimeStamp(sample);
        frame.decode_timestamp_us = CMTIME_IS_VALID(decode_time) ? static_cast<std::int64_t>(CMTimeGetSeconds(decode_time) * 1e6) : frame.timestamp_us;
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
        const bool baseline = h264_.profile == H264Profile::constrained_baseline;
        state->set(kVTCompressionPropertyKey_RealTime, kCFBooleanTrue);
        // Reordering (B-frames) only outside Baseline, and only when asked for.
        state->set(kVTCompressionPropertyKey_AllowFrameReordering, !baseline && h264_.b_frames > 0 ? kCFBooleanTrue : kCFBooleanFalse);
        if (baseline) {
            if (VTSessionSetProperty(state->session, kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_ConstrainedBaseline_AutoLevel) != noErr)
                state->set(kVTCompressionPropertyKey_ProfileLevel, kVTProfileLevel_H264_Baseline_AutoLevel);
            state->set(kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef)@(peak / 10 * 7));
        } else {
            check(VTSessionSetProperty(state->session, kVTCompressionPropertyKey_ProfileLevel, h264_.profile == H264Profile::high
                ? kVTProfileLevel_H264_High_AutoLevel : kVTProfileLevel_H264_Main_AutoLevel), "H.264 profile");
            // The program feed for YouTube: average at the configured rate, capped just above it.
            state->set(kVTCompressionPropertyKey_AverageBitRate, (__bridge CFTypeRef)@(peak));
        }
        // Bytes per one-second window: the hard ceiling the design asks for.
        state->set(kVTCompressionPropertyKey_DataRateLimits, (__bridge CFTypeRef)@[@(baseline ? peak / 8 : peak / 8 * 11 / 10), @1]);
        state->set(kVTCompressionPropertyKey_ExpectedFrameRate, (__bridge CFTypeRef)@(s.fps));
        state->set(kVTCompressionPropertyKey_MaxKeyFrameInterval, (__bridge CFTypeRef)@(s.fps * s.keyframe_interval_s));
        state->set(kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, (__bridge CFTypeRef)@(s.keyframe_interval_s));
    }
    check(VTCompressionSessionPrepareToEncodeFrames(state->session), "VTCompressionSessionPrepareToEncodeFrames");
    state->width = s.width;
    state->height = s.height;
    state_ = std::move(state);
}

void VideoToolboxEncoder::encode(const VideoFrame& frame, bool force_keyframe) {
    if (!state_ || (!frame.native && !frame.nv12)) return;
    CVPixelBufferRef pixels = frame.native ? static_cast<CVPixelBufferRef>(frame.native.get()) : nullptr;
    CVPixelBufferRef owned = nullptr;
    if (!pixels) {
        // Composed in memory (odeum-program): copy the NV12 planes into a pixel buffer.
        if (frame.width != state_->width || frame.height != state_->height ||
            frame.nv12->size() < static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 3 / 2)
            throw std::invalid_argument("NV12 frame does not match the stream size");
        check(CVPixelBufferCreate(kCFAllocatorDefault, frame.width, frame.height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,
                                  nullptr, &owned), "CVPixelBufferCreate");
        CVPixelBufferLockBaseAddress(owned, 0);
        const auto* source = frame.nv12->data();
        for (size_t plane = 0; plane < 2; ++plane) {
            auto* destination = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(owned, plane));
            const auto stride = CVPixelBufferGetBytesPerRowOfPlane(owned, plane);
            const auto rows = static_cast<size_t>(plane == 0 ? frame.height : frame.height / 2);
            for (size_t row = 0; row < rows; ++row)
                std::memcpy(destination + row * stride, source + row * static_cast<size_t>(frame.width), static_cast<size_t>(frame.width));
            source += rows * static_cast<size_t>(frame.width);
        }
        CVPixelBufferUnlockBaseAddress(owned, 0);
        pixels = owned;
    }
    CFDictionaryRef options = nullptr;
    if (force_keyframe) {
        const void* keys[] = {kVTEncodeFrameOptionKey_ForceKeyFrame};
        const void* values[] = {kCFBooleanTrue};
        options = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    }
    const auto status = VTCompressionSessionEncodeFrame(state_->session, pixels, CMTimeMake(frame.timestamp_us, 1000000), kCMTimeInvalid,
                                                        options, nullptr, nullptr);
    if (options) CFRelease(options);
    if (owned) CVPixelBufferRelease(owned);
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
