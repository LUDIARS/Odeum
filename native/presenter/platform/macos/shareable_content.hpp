#pragma once
// Objective-C++ only: shared by the ScreenCaptureKit video and audio sources.
#import <ScreenCaptureKit/ScreenCaptureKit.h>

namespace odeum::presenter::macos {
// The displays, windows and applications ScreenCaptureKit may capture, fetched synchronously.
// Throws std::runtime_error (screen recording not permitted, or the query failed).
SCShareableContent* shareable_content();
// This process as ScreenCaptureKit lists it, so filters can leave it out; nil if not listed.
SCRunningApplication* current_application(SCShareableContent* content);
// A filter for the display, without this application's windows.
SCContentFilter* display_filter_without_self(SCShareableContent* content, SCDisplay* display);
// Waits for an asynchronous ScreenCaptureKit call that reports through a completion handler.
void wait_for(void (^start)(void (^done)(NSError*)), const char* what);
}
