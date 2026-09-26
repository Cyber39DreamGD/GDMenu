// Apple-only clipboard (UIPasteboard on iOS, NSPasteboard on macOS).
// Lives in a .mm file because ObjC message sending needs to be compiled as
// Objective-C++ (the same way Geode's own iOS util code does it).
#include "state.hpp"
#import <Foundation/Foundation.h>
#if defined(GEODE_IS_IOS)
#import <UIKit/UIKit.h>
#else
#import <AppKit/AppKit.h>
#endif

namespace video {
bool copyApple(std::string const& text) {
	NSString* str = [NSString stringWithUTF8String:text.c_str()];
	if (str.length == 0) return false;
#if defined(GEODE_IS_IOS)
	[UIPasteboard generalPasteboard].string = str;
	return YES;
#else
	NSPasteboard* pb = [NSPasteboard generalPasteboard];
	[pb clearContents];
	return [pb setString:str forType:NSPasteboardTypeString];
#endif
}
}
