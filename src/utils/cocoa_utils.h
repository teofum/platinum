#ifndef PLATINUM_COCOA_UTILS_H
#define PLATINUM_COCOA_UTILS_H

#include <Foundation/Foundation.hpp>
#include <SDL_syswm.h>

NSWindow* getCocoaWindow(SDL_Window* sdlWindow);

void setupWindowStyle(SDL_Window* sdlWindow);

#endif // PLATINUM_COCOA_UTILS_H
