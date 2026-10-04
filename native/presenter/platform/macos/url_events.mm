#import <Foundation/Foundation.h>
#import <CoreServices/CoreServices.h>
#include "url_events.hpp"

@interface OdeumUrlHandler : NSObject
- (instancetype)initWithCallback:(std::function<void(std::string)>)callback;
- (void)handle:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply;
@end

@implementation OdeumUrlHandler {
    std::function<void(std::string)> _callback;
}
- (instancetype)initWithCallback:(std::function<void(std::string)>)callback {
    if ((self = [super init])) _callback = std::move(callback);
    return self;
}
- (void)handle:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)reply {
    NSString* url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
    if (url) _callback(url.UTF8String);
}
@end

namespace odeum::presenter::macos {
struct UrlEvents::Impl {
    OdeumUrlHandler* handler = nil;
};

UrlEvents::UrlEvents(std::function<void(std::string)> received) : impl_(std::make_unique<Impl>()) {
    impl_->handler = [[OdeumUrlHandler alloc] initWithCallback:std::move(received)];
    [[NSAppleEventManager sharedAppleEventManager] setEventHandler:impl_->handler andSelector:@selector(handle:withReplyEvent:)
                                                     forEventClass:kInternetEventClass andEventID:kAEGetURL];
}

UrlEvents::~UrlEvents() {
    [[NSAppleEventManager sharedAppleEventManager] removeEventHandlerForEventClass:kInternetEventClass andEventID:kAEGetURL];
}
}
