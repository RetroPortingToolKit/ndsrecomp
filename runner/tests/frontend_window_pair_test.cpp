#include <SDL3/SDL.h>
#include "frontend_window_pair.h"
#include <cstdio>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    auto* top = SDL_CreateWindow("owned-window test parent", 64, 64, SDL_WINDOW_HIDDEN);
    auto* bottom = SDL_CreateWindow("owned-window test touch", 64, 64, SDL_WINDOW_HIDDEN);
    int result = 1;
    if (top && bottom && SDL_GetWindowParent(bottom) == nullptr &&
        !nds_parent_touch_window(top, top) &&
        !nds_parent_touch_window(nullptr, top) && nds_parent_touch_window(bottom, top) &&
        SDL_GetWindowParent(bottom) == top && SDL_GetWindowParent(top) == nullptr &&
        !(SDL_GetWindowFlags(bottom) & (SDL_WINDOW_MODAL | SDL_WINDOW_ALWAYS_ON_TOP))) {
        result = 0;
#if defined(_WIN32)
        auto top_handle = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(top), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        auto bottom_handle = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(bottom), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        // Inspect the actual native ownership and focus contract. All windows
        // stay hidden; the test never changes desktop/fullscreen/focus state.
        if (!top_handle || !bottom_handle || GetWindow(bottom_handle, GW_OWNER) != top_handle ||
            !IsWindowEnabled(top_handle) || !IsWindowEnabled(bottom_handle) ||
            (GetWindowLongPtr(bottom_handle, GWL_EXSTYLE) & WS_EX_TOPMOST)) result = 1;
#endif
    }
    if (bottom) SDL_DestroyWindow(bottom);
    if (top) SDL_DestroyWindow(top);
    SDL_Quit();
    if (result) std::fprintf(stderr, "nonmodal touch-window ownership failed: %s\n", SDL_GetError());
    else std::puts("hidden native touch window has game owner, independent input, no global topmost");
    return result;
}
