#include "shareable_content.hpp"
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace odeum::presenter::macos {
namespace {
constexpr int64_t timeout_ns = 10LL * NSEC_PER_SEC;
}

SCShareableContent* shareable_content() {
    __block SCShareableContent* result = nil;
    __block NSError* failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:YES
        completionHandler:^(SCShareableContent* content, NSError* error) {
            result = content;
            failure = error;
            dispatch_semaphore_signal(done);
        }];
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, timeout_ns)) != 0)
        throw std::runtime_error("ScreenCaptureKit did not answer");
    if (!result) throw std::runtime_error(failure ? std::string(failure.localizedDescription.UTF8String) : "Screen recording is not permitted");
    return result;
}

SCRunningApplication* current_application(SCShareableContent* content) {
    const pid_t self = getpid();
    for (SCRunningApplication* application in content.applications)
        if (application.processID == self) return application;
    return nil;
}

SCContentFilter* display_filter_without_self(SCShareableContent* content, SCDisplay* display) {
    SCRunningApplication* self = current_application(content);
    NSArray<SCRunningApplication*>* excluded = self ? @[self] : @[];
    return [[SCContentFilter alloc] initWithDisplay:display excludingApplications:excluded exceptingWindows:@[]];
}

void wait_for(void (^start)(void (^done)(NSError*)), const char* what) {
    __block NSError* failure = nil;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    start(^(NSError* error) {
        failure = error;
        dispatch_semaphore_signal(done);
    });
    if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, timeout_ns)) != 0)
        throw std::runtime_error(std::string(what) + " timed out");
    if (failure) throw std::runtime_error(std::string(what) + ": " + failure.localizedDescription.UTF8String);
}
}
