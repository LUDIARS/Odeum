#import <AppKit/AppKit.h>
#include "macos_program_platform.hpp"
#include "audio_toolbox_aac_encoder.hpp"
#include "keychain_secret_store.hpp"
#include "video_toolbox_decoder.hpp"
#include "platform/macos/video_toolbox_encoder.hpp"

namespace odeum::program::macos {
namespace {
std::filesystem::path application_support() {
    NSArray<NSURL*>* folders = [[NSFileManager defaultManager] URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask];
    if (folders.count == 0) throw std::runtime_error("Cannot locate Application Support");
    return std::filesystem::path(folders.firstObject.fileSystemRepresentation) / "Odeum";
}
}

std::unique_ptr<VideoDecoder> MacOSProgramPlatform::video_decoder() { return std::make_unique<VideoToolboxDecoder>(); }
std::unique_ptr<AacEncoder> MacOSProgramPlatform::aac_encoder() { return std::make_unique<AudioToolboxAacEncoder>(); }
std::unique_ptr<presenter::VideoEncoder> MacOSProgramPlatform::video_encoder() { return std::make_unique<presenter::macos::VideoToolboxEncoder>(); }
std::unique_ptr<SecretStore> MacOSProgramPlatform::secret_store() { return std::make_unique<KeychainSecretStore>(); }

std::string MacOSProgramPlatform::clipboard_text() {
    @autoreleasepool {
        NSString* text = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        return text ? std::string(text.UTF8String) : std::string();
    }
}

std::filesystem::path MacOSProgramPlatform::settings_path() { return application_support() / "program.json"; }

std::vector<std::filesystem::path> MacOSProgramPlatform::font_candidates() {
    // The Hiragino faces ship as .ttc collections, which Pictor cannot read.
    return {application_support() / "program.ttf", "/Library/Fonts/Arial Unicode.ttf",
            "/System/Library/Fonts/Supplemental/Arial Unicode.ttf"};
}
}
