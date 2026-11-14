#pragma once

#if defined(NDS_HAVE_SDL3)
#include <SDL3/SDL_video.h>

// A normal, nonmodal owned window stays above its game's fullscreen window
// and follows its visibility without being globally always-on-top.
inline bool nds_parent_touch_window(SDL_Window* touch, SDL_Window* game) {
    return touch && game && touch != game && SDL_SetWindowParent(touch, game);
}
#endif
