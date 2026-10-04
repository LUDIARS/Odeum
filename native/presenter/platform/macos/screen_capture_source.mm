#import <AppKit/AppKit.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include "screen_capture_source.hpp"
#include "shareable_content.hpp"
#include <atomic>
#include <stdexcept>
#include <string>

using odeum::presenter::CaptureSource;
using odeum::presenter::VideoFrame;

// Receives ScreenCaptureKit frames on the source's serial queue and hands complete ones on.
@interface OdeumScreenOutput : NSObject <SCStreamOutput, SCStreamDelegate>
- (instancetype)initWithFrames:(CaptureSource::FrameSink)frames errors:(CaptureSource::ErrorSink)errors;
- (void)halt;
@end

@implementation OdeumScreenOutput {
    CaptureSource::FrameSink _frames;
    CaptureSource::ErrorSink _errors;
    std::atomic<bool> _running;
}
- (instancetype)initWithFrames:(CaptureSource::FrameSink)frames errors:(CaptureSource::ErrorSink)errors {
    if ((self = [super init])) {
        _frames = std::move(frames);
        _errors = std::move(errors);
        _running = true;
    }
    return self;
}
- (void)halt { _running = false; }
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sample ofType:(SCStreamOutputType)type {
    if (type != SCStreamOutputTypeScreen || !_running || !CMSampleBufferIsValid(sample)) return;
    CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(sample, false);
    if (!attachments || CFArrayGetCount(attachments) == 0) return;
    NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(attachments, 0);
    // Idle and blank frames carry no picture; only complete ones are encoded.
    NSNumber* status = info[SCStreamFrameInfoStatus];
    if (!status || status.integerValue != SCFrameStatusComplete) return;
    CVPixelBufferRef pixels = CMSampleBufferGetImageBuffer(sample);
    if (!pixels) return;
    const CMTime time = CMSampleBufferGetPresentationTimeStamp(sample);
    VideoFrame frame{static_cast<int>(CVPixelBufferGetWidth(pixels)), static_cast<int>(CVPixelBufferGetHeight(pixels)),
                     static_cast<std::int64_t>(CMTimeGetSeconds(time) * 1e6),
                     std::shared_ptr<void>(CVPixelBufferRetain(pixels), [](void* p) { CVPixelBufferRelease(static_cast<CVPixelBufferRef>(p)); })};
    try { _frames(frame); }
    catch (const std::exception& error) { _running = false; _errors(error.what()); }
}
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error {
    if (!_running.exchange(false)) return;
    _errors(error ? std::string(error.localizedDescription.UTF8String) : std::string("capture stopped"));
}
@end

namespace odeum::presenter::macos {
namespace {
constexpr std::string_view display_prefix = "display:", window_prefix = "window:";
}

struct ScreenCaptureSource::State {
    SCStream* stream = nil;
    OdeumScreenOutput* output = nil;
    dispatch_queue_t queue = nil;
};

ScreenCaptureSource::ScreenCaptureSource() = default;
ScreenCaptureSource::~ScreenCaptureSource() { stop(); }

std::vector<CaptureTarget> ScreenCaptureSource::targets() {
    @autoreleasepool {
        SCShareableContent* content = shareable_content();
        std::vector<CaptureTarget> result;
        const auto main_display = CGMainDisplayID();
        int number = 0;
        for (SCDisplay* display in content.displays) {
            CaptureTarget target{std::string(display_prefix) + std::to_string(display.displayID), CaptureKind::display,
                                 std::to_string(++number), static_cast<int>(display.width), static_cast<int>(display.height)};
            if (display.displayID == main_display) {
                target.name += " (メイン)";
                result.insert(result.begin(), target);
            } else result.push_back(target);
        }
        SCRunningApplication* self = current_application(content);
        for (SCWindow* window in content.windows) {
            if (!window.onScreen || window.windowLayer != 0 || window.frame.size.width < 64 || window.frame.size.height < 64) continue;
            if (self && window.owningApplication.processID == self.processID) continue;
            NSString* owner = window.owningApplication.applicationName ?: @"";
            NSString* title = window.title.length ? [NSString stringWithFormat:@"%@ — %@", owner, window.title] : owner;
            if (title.length == 0) continue;
            result.push_back({std::string(window_prefix) + std::to_string(window.windowID), CaptureKind::window, title.UTF8String,
                              static_cast<int>(window.frame.size.width), static_cast<int>(window.frame.size.height)});
        }
        return result;
    }
}

void ScreenCaptureSource::start(const CaptureTarget& target, const StreamSettings& settings, FrameSink frames, ErrorSink errors) {
    stop();
    @autoreleasepool {
        SCShareableContent* content = shareable_content();
        SCContentFilter* filter = nil;
        if (target.id.starts_with(display_prefix)) {
            const auto id = static_cast<CGDirectDisplayID>(std::stoul(target.id.substr(display_prefix.size())));
            for (SCDisplay* display in content.displays) if (display.displayID == id) filter = display_filter_without_self(content, display);
        } else if (target.id.starts_with(window_prefix)) {
            const auto id = static_cast<CGWindowID>(std::stoul(target.id.substr(window_prefix.size())));
            for (SCWindow* window in content.windows) if (window.windowID == id) filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:window];
        }
        if (!filter) throw std::runtime_error("The chosen display or window is no longer available");
        SCStreamConfiguration* config = [[SCStreamConfiguration alloc] init];
        config.width = static_cast<size_t>(settings.width);
        config.height = static_cast<size_t>(settings.height);
        config.minimumFrameInterval = CMTimeMake(1, settings.fps);
        config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        config.colorMatrix = kCGDisplayStreamYCbCrMatrix_ITU_R_709_2;
        config.showsCursor = YES;
        config.queueDepth = 6;
        if ([config respondsToSelector:@selector(setPreservesAspectRatio:)]) [config setValue:@YES forKey:@"preservesAspectRatio"];
        auto state = std::make_unique<State>();
        state->queue = dispatch_queue_create("odeum.presenter.capture", DISPATCH_QUEUE_SERIAL);
        state->output = [[OdeumScreenOutput alloc] initWithFrames:std::move(frames) errors:std::move(errors)];
        state->stream = [[SCStream alloc] initWithFilter:filter configuration:config delegate:state->output];
        NSError* error = nil;
        if (![state->stream addStreamOutput:state->output type:SCStreamOutputTypeScreen sampleHandlerQueue:state->queue error:&error])
            throw std::runtime_error(error ? error.localizedDescription.UTF8String : "Cannot attach the capture output");
        SCStream* stream = state->stream;
        wait_for(^(void (^done)(NSError*)) { [stream startCaptureWithCompletionHandler:done]; }, "Starting screen capture");
        state_ = std::move(state);
    }
}

void ScreenCaptureSource::stop() {
    if (!state_) return;
    [state_->output halt];
    SCStream* stream = state_->stream;
    try { wait_for(^(void (^done)(NSError*)) { [stream stopCaptureWithCompletionHandler:done]; }, "Stopping screen capture"); }
    catch (const std::exception&) { /* Already stopped by the system; nothing more to release. */ }
    // Lets a frame already on the queue finish before the sinks are released.
    dispatch_sync(state_->queue, ^{});
    state_.reset();
}

CapturePermission ScreenCaptureSource::permission() {
    return CGPreflightScreenCaptureAccess() ? CapturePermission::granted : CapturePermission::denied;
}

void ScreenCaptureSource::request_permission() {
    // The system prompt appears only once per installation; afterwards the setting has to be
    // changed in System Settings, so that pane is opened as well.
    if (CGRequestScreenCaptureAccess()) return;
    [[NSWorkspace sharedWorkspace] openURL:[NSURL URLWithString:@"x-apple.systempreferences:com.apple.preference.security?Privacy_ScreenCapture"]];
}
}
