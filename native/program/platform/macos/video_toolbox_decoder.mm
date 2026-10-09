#import <VideoToolbox/VideoToolbox.h>
#include "video_toolbox_decoder.hpp"
#include "../../core/flv.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

namespace odeum::program::macos {
namespace {
// The AVCC body of an access unit without AUD/SPS/PPS: avc_frame()'s tag body after its
// five-byte FLV video header.
constexpr std::size_t flv_video_header = 5;
}

struct VideoToolboxDecoder::State {
    VTDecompressionSessionRef session = nullptr;
    CMVideoFormatDescriptionRef format = nullptr;
    AvcParameterSets sets;
    std::vector<Nv12Picture>* sink = nullptr;
    std::int64_t timestamp_us = 0;

    ~State() { close(); }

    void close() {
        if (session) {
            VTDecompressionSessionInvalidate(session);
            CFRelease(session);
            session = nullptr;
        }
        if (format) {
            CFRelease(format);
            format = nullptr;
        }
    }

    static void output(void* refcon, void*, OSStatus status, VTDecodeInfoFlags, CVImageBufferRef image, CMTime, CMTime) {
        auto* self = static_cast<State*>(refcon);
        if (status != noErr || !image || !self->sink) return;
        CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
        const int width = static_cast<int>(CVPixelBufferGetWidth(image)) & ~1, height = static_cast<int>(CVPixelBufferGetHeight(image)) & ~1;
        auto pixels = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(width) * height * 3 / 2);
        auto* out = pixels->data();
        for (size_t plane = 0; plane < 2; ++plane) {
            const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(image, plane));
            const auto stride = CVPixelBufferGetBytesPerRowOfPlane(image, plane);
            const int rows = plane == 0 ? height : height / 2;
            for (int row = 0; row < rows; ++row, out += width) std::memcpy(out, base + static_cast<size_t>(row) * stride, static_cast<size_t>(width));
        }
        CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
        self->sink->push_back({width, height, std::move(pixels), self->timestamp_us});
    }

    void open(const AvcParameterSets& next) {
        close();
        sets = next;
        const uint8_t* pointers[] = {reinterpret_cast<const uint8_t*>(sets.sps.data()), reinterpret_cast<const uint8_t*>(sets.pps.data())};
        const size_t sizes[] = {sets.sps.size(), sets.pps.size()};
        if (CMVideoFormatDescriptionCreateFromH264ParameterSets(kCFAllocatorDefault, 2, pointers, sizes, 4, &format) != noErr)
            throw std::runtime_error("Unreadable SPS/PPS");
        const int pixel_format = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &pixel_format);
        const void* keys[] = {kCVPixelBufferPixelFormatTypeKey};
        const void* values[] = {number};
        CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        VTDecompressionOutputCallbackRecord callback{&State::output, this};
        const auto status = VTDecompressionSessionCreate(kCFAllocatorDefault, format, nullptr, attributes, &callback, &session);
        CFRelease(attributes);
        CFRelease(number);
        if (status != noErr) throw std::runtime_error("VTDecompressionSessionCreate failed (" + std::to_string(status) + ")");
    }
};

VideoToolboxDecoder::VideoToolboxDecoder() : state_(std::make_unique<State>()) {}
VideoToolboxDecoder::~VideoToolboxDecoder() = default;

std::vector<Nv12Picture> VideoToolboxDecoder::decode(std::span<const std::byte> access_unit, std::int64_t timestamp_us) {
    std::vector<Nv12Picture> pictures;
    auto& s = *state_;
    if (auto sets = find_parameter_sets(access_unit); sets && (!s.session || !(*sets == s.sets))) s.open(*sets);
    if (!s.session) return pictures; // waiting for the first IDR with parameter sets
    const auto body = avc_frame(access_unit, false, 0, 0).body;
    if (body.size() <= flv_video_header) return pictures;
    const auto size = body.size() - flv_video_header;
    CMBlockBufferRef block = nullptr;
    if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, size, kCFAllocatorDefault, nullptr, 0, size, 0, &block) != kCMBlockBufferNoErr)
        throw std::runtime_error("CMBlockBufferCreateWithMemoryBlock failed");
    CMBlockBufferReplaceDataBytes(body.data() + flv_video_header, block, 0, size);
    CMSampleBufferRef sample = nullptr;
    const size_t sample_size = size;
    const auto status = CMSampleBufferCreateReady(kCFAllocatorDefault, block, s.format, 1, 0, nullptr, 1, &sample_size, &sample);
    CFRelease(block);
    if (status != noErr) throw std::runtime_error("CMSampleBufferCreateReady failed");
    s.sink = &pictures;
    s.timestamp_us = timestamp_us;
    const auto decoded = VTDecompressionSessionDecodeFrame(s.session, sample, 0, nullptr, nullptr);
    s.sink = nullptr;
    CFRelease(sample);
    if (decoded != noErr) throw std::runtime_error("VTDecompressionSessionDecodeFrame failed (" + std::to_string(decoded) + ")");
    return pictures;
}

void VideoToolboxDecoder::reset() { state_->close(); }
}
