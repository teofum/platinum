#include "cocoa_utils.h"

#import <Cocoa/Cocoa.h>

NSWindow* getCocoaWindow(SDL_Window* sdlWindow) {
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  SDL_GetWindowWMInfo(sdlWindow, &info);
  return info.info.cocoa.window;
};

void setupWindowStyle(SDL_Window* sdlWindow) {
  NSWindow* window = getCocoaWindow(sdlWindow);
  
  window.titleVisibility = NSWindowTitleHidden;
  window.titlebarAppearsTransparent = YES;
  window.styleMask |= NSWindowStyleMaskFullSizeContentView;
}

bool isFullscreenEnabled(SDL_Window* sdlWindow) {
  NSWindow* window = getCocoaWindow(sdlWindow);
  return (window.styleMask & NSWindowStyleMaskFullScreen) == NSWindowStyleMaskFullScreen;
}

bool isSystemDarkModeEnabled() {
  NSString* appleInterfaceStyle = [[NSUserDefaults standardUserDefaults] stringForKey:@"AppleInterfaceStyle"];
  
  if (appleInterfaceStyle && [appleInterfaceStyle length] > 0) {
    return [[appleInterfaceStyle lowercaseString] containsString:@"dark"];
  } else {
    return false;
  }
}
