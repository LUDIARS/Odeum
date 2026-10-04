#import <AppKit/AppKit.h>
#include "macos_platform.hpp"
#include "screen_capture_audio_source.hpp"
#include "screen_capture_source.hpp"
#include "video_toolbox_encoder.hpp"

namespace odeum::presenter::macos {
namespace {
std::filesystem::path application_support() {
    NSArray<NSURL*>* folders = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
    if (folders.count == 0) throw std::runtime_error("Cannot locate Application Support");
    return std::filesystem::path(folders.firstObject.fileSystemRepresentation) / "Odeum";
}
}

std::unique_ptr<CaptureSource> MacOSPlatform::capture() { return std::make_unique<ScreenCaptureSource>(); }
std::unique_ptr<VideoEncoder> MacOSPlatform::video_encoder() { return std::make_unique<VideoToolboxEncoder>(); }
std::unique_ptr<AudioSource> MacOSPlatform::audio_source() { return std::make_unique<ScreenCaptureAudioSource>(); }

std::string MacOSPlatform::clipboard_text() {
    @autoreleasepool {
        NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        return text ? std::string(text.UTF8String) : std::string();
    }
}

std::filesystem::path MacOSPlatform::settings_path() { return application_support() / "presenter.json"; }

std::vector<std::filesystem::path> MacOSPlatform::font_candidates() {
    // Arial Unicode is a single .ttf with Japanese glyphs on every supported macOS; the Hiragino
    // faces ship as .ttc collections, which Pictor cannot read.
    return {application_support() / "presenter.ttf", "/Library/Fonts/Arial Unicode.ttf",
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"};
}
}
