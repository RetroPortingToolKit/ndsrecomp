#pragma once
#include <algorithm>
#include <cstdint>

enum class NdsScreenLayout : uint8_t { Stacked, Separate, PictureInPicture };
enum class NdsPipCorner : uint8_t { TopLeft, TopRight, BottomLeft, BottomRight };
enum class NdsPipMainScreen : uint8_t { Top, Bottom };

// Logical coordinates shared by presentation and touchscreen hit testing.
struct NdsScreenRect { int x{}, y{}, w{}, h{}; };
struct NdsScreenPlacement {
    NdsScreenRect screens[2];
    int width{}, height{};
    unsigned main_screen{};
    bool pip{};
};

// Presentation-only alpha, rounded to the nearest portable 8-bit value.
// Physical LCD images and the main screen always retain their opaque pixels.
inline constexpr uint8_t kNdsPipOpacity = (255u * 90u + 50u) / 100u;
inline uint8_t nds_screen_opacity(const NdsScreenPlacement& p, unsigned screen) {
    return p.pip && screen != p.main_screen ? kNdsPipOpacity : 255;
}
inline uint32_t nds_screen_blend_pixel(uint32_t background, uint32_t foreground, uint8_t opacity) {
    uint32_t result = 0xFF000000u;
    for (unsigned shift : {0u, 8u, 16u}) {
        const unsigned value = (((foreground >> shift) & 255u) * opacity +
            ((background >> shift) & 255u) * (255u - opacity) + 127u) / 255u;
        result |= value << shift;
    }
    return result;
}

inline NdsScreenPlacement nds_screen_placement(
    NdsScreenLayout layout, int top_width, int bottom_width,
    NdsPipCorner corner = NdsPipCorner::BottomRight,
    NdsPipMainScreen main = NdsPipMainScreen::Top) {
    top_width = std::max(top_width, 256);
    bottom_width = std::max(bottom_width, 256);
    NdsScreenPlacement p{};
    p.pip = layout == NdsScreenLayout::PictureInPicture;
    p.main_screen = p.pip && main == NdsPipMainScreen::Bottom ? 1u : 0u;
    if (p.pip) {
        const int widths[2]{top_width, bottom_width};
        const unsigned inset = p.main_screen ^ 1u;
        p.width = widths[p.main_screen]; p.height = 192;
        p.screens[p.main_screen] = {0, 0, p.width, p.height};
        constexpr int margin = 6;
        const int inset_width = std::min(widths[inset] * 72 / 192, p.width - margin*2);
        const int inset_height = std::max(1, inset_width * 192 / widths[inset]);
        const bool right = corner == NdsPipCorner::TopRight || corner == NdsPipCorner::BottomRight;
        const bool bottom = corner == NdsPipCorner::BottomLeft || corner == NdsPipCorner::BottomRight;
        p.screens[inset] = {right ? p.width - margin - inset_width : margin,
                            bottom ? p.height - margin - inset_height : margin,
                            inset_width, inset_height};
    } else {
        p.width = std::max(top_width, bottom_width);
        p.height = layout == NdsScreenLayout::Separate ? 192 : 384;
        p.screens[0] = {(p.width-top_width)/2, 0, top_width, 192};
        p.screens[1] = {(p.width-bottom_width)/2,
                        layout == NdsScreenLayout::Separate ? 0 : 192,
                        bottom_width, 192};
    }
    return p;
}

inline bool nds_screen_rect_contains(const NdsScreenRect& r, float x, float y) {
    return x >= r.x && y >= r.y && x < r.x+r.w && y < r.y+r.h;
}

inline NdsScreenRect nds_bottom_touch_rect(const NdsScreenPlacement& p, int bottom_width) {
    const auto& r = p.screens[1];
    const int native_width = r.w * 256 / std::max(bottom_width, 256);
    return {r.x + (r.w-native_width)/2, r.y, native_width, r.h};
}

inline bool nds_screen_touch_position(const NdsScreenPlacement& p, int bottom_width,
                                       float x, float y, float* touch_x, float* touch_y) {
    // A top-screen inset covers the bottom LCD when the generic layout swaps.
    if (p.pip && p.main_screen == 1 && nds_screen_rect_contains(p.screens[0], x, y))
        return false;
    const auto r = nds_bottom_touch_rect(p, bottom_width);
    if (!r.w || !r.h || !nds_screen_rect_contains(r, x, y)) return false;
    if (touch_x) *touch_x = (x-r.x)*256.0f/r.w;
    if (touch_y) *touch_y = (y-r.y)*192.0f/r.h;
    return true;
}
