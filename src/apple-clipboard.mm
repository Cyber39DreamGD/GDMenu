// Apple-only clipboard (UIPasteboard on iOS, NSPasteboard on macOS).
// Lives in a .mm file because ObjC message sending needs to be compiled as
// Objective-C++ (the same way Geode's own iOS util code does it).
//
// Deliberately does NOT include state.hpp / Geode headers: on macOS, AppKit
// pulls in CarbonCore's Script.h, whose `CommentType` typedef clashes with
// GD's own `CommentType` enum from the Geode bindings.
#import <Foundation/Foundation.h>
#import <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif
#include <string>

namespace video {
bool copyApple(std::string const& text) {
	NSString* str = [NSString stringWithUTF8String:text.c_str()];
	if (str.length == 0) return false;
#if TARGET_OS_IPHONE
	[UIPasteboard generalPasteboard].string = str;
	return YES;
#else
	NSPasteboard* pb = [NSPasteboard generalPasteboard];
	[pb clearContents];
	return [pb setString:str forType:NSPasteboardTypeString];
#endif
}
}
