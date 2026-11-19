#pragma once

#include <cstdint>

constexpr uint32_t NDS_GPU2D_DISPCNT_WINDOW_ENABLE_MASK = 0xE000u;

// Unused RGB6 transport bits carry BG0 visibility in R[6] and ordinary
// effects permission in G[6], including pixels without a visible OBJ.
constexpr uint32_t nds_gpu2d_pack_direct_pixel(uint32_t color, uint8_t priority,
    uint8_t alpha, bool valid, uint8_t window_mask) {
    uint32_t value = ((window_mask & 1u) ? 0u : 0x40u) |
                     ((window_mask & 0x20u) ? 0u : 0x4000u);
    if (!valid || !(window_mask & 0x10u)) return value;
    const uint32_t code = alpha == 0u ? 17u : alpha == 0xFFu ? 255u :
                          alpha > 16u ? 16u : alpha;
    return value | (color & 0x003F3F3Fu) |
           (uint32_t(priority & 3u) << 22) | (code << 24);
}

// Title-declared source for the invented gap fill. source[side] is a native
// screen column inside the outer band that the gap continues (-1 keeps the
// two-column seam stretch). scroll_x[bg] is the horizontal scroll at which
// that column is sampled for each BG (-1 = the live BGxHOFS). Declaring the
// scroll the HUD was authored at keeps the flat filler fixed to one point of
// the frame art while the guest sways its HUD layers through HOFS, so the
// invented pixels cannot change colour as different art scrolls past the
// seam. Vertical scroll stays live so filler rows track the anchored bands.
struct NdsGpu2dHudGap {
    int16_t source[2] = {-1, -1};
    int16_t scroll_x[4] = {-1, -1, -1, -1};
};

// Which invented gap of an anchored adaptive HUD a wide column lies in:
// 0 = between the left band and the centered band, 1 = between the centered
// band and the right band, -1 = an authored band (or no anchoring/widening).
constexpr int nds_gpu2d_adaptive_hud_gap_side(int x, int width, bool anchor,
                                             int center_width) {
    const int extra = (width - 256) / 2;
    if (!anchor || extra <= 0) return -1;
    const int left = (256 - center_width) / 2;
    const int right = left + center_width;
    if (x >= left && x < extra + left) return 0;
    if (x >= extra + right && x < width - (256 - right)) return 1;
    return -1;
}

// True when a declared gap source column lies inside the anchored outer band
// it continues: left of the centered band for the left gap, right of it for
// the right gap. Only such columns are scroll-anchored authored frame art.
constexpr bool nds_gpu2d_adaptive_hud_gap_source_valid(int side, int source,
                                                       int center_width) {
    const int left = (256 - center_width) / 2;
    const int right = left + center_width;
    return side == 0 ? source >= 0 && source < left
                     : source >= right && source < 256;
}

// Native source column for one wide HUD column. Authored bands map 1:1.
// With fill_gaps, each invented gap column also gets a source: a title-
// declared single seam column per side (left_source / right_source, which
// must satisfy nds_gpu2d_adaptive_hud_gap_source_valid) gives each gap one
// flat colour per row that continues the outer band; with no declaration the
// two columns adjacent to each seam are stretched, half the gap each.
constexpr int nds_gpu2d_adaptive_hud_x(int x, int width, bool anchor,
                                      int center_width, bool fill_gaps = false,
                                      int left_source = -1,
                                      int right_source = -1) {
    const int extra = (width - 256) / 2;
    const int left = (256 - center_width) / 2;
    const int right = left + center_width;
    if (anchor) {
        if (x < left) return x;
        if (x >= extra + left && x < extra + right) return x - extra;
        if (x >= width - (256 - right)) return x - (width - 256);
        // A title may extend its decorative background seams while keeping
        // authored panels and all separately anchored OBJ instruments intact.
        if (fill_gaps && extra > 0) {
            if (x < extra + left) {
                if (nds_gpu2d_adaptive_hud_gap_source_valid(0, left_source,
                                                            center_width))
                    return left_source;
                return left ? left - 1 + (2 * (x - left)) / extra : 0;
            }
            if (nds_gpu2d_adaptive_hud_gap_source_valid(1, right_source,
                                                        center_width))
                return right_source;
            return right < 256
                ? right - 1 + (2 * (x - extra - right)) / extra : 255;
        }
    } else if (x >= extra && x < extra + 256) return x - extra;
    return -1;
}

// Shared, side-effect-free DS window selection. Keeping this separate from
// the scanline renderer lets the eligibility check use precisely the same
// precedence and register interpretation as native composition.
struct NdsGpu2dWindowState {
    uint32_t dispcnt;
    // Raw MMIO byte order: WINxH/V low byte is the end coordinate, high
    // byte is the start coordinate. WININ/WINOUT retain their byte order.
    const uint8_t* win;
};

constexpr bool nds_gpu2d_window_contains(uint8_t point, uint8_t start,
                                         uint8_t end) {
    // melonDS updates the active latch by clearing at end before setting at
    // start. Equal endpoints therefore describe an empty window; a reversed
    // interval wraps through coordinate zero.
    return start < end ? point >= start && point < end
                       : start > end ? point >= start || point < end
                                     : false;
}

constexpr bool nds_gpu2d_window_rect_contains(uint8_t x, uint8_t y,
                                              uint8_t left, uint8_t right,
                                              uint8_t top, uint8_t bottom) {
    return nds_gpu2d_window_contains(x, left, right) &&
           nds_gpu2d_window_contains(y, top, bottom);
}

constexpr uint8_t nds_gpu2d_window_mask(const NdsGpu2dWindowState& state,
                                         uint8_t x, uint8_t y,
                                         bool obj_window) {
    // WINOUT does not participate until at least one window mechanism is
    // enabled. This matches melonDS DrawScanline_BGOBJ's all-enabled fast
    // mask when DISPCNT[15:13] is clear.
    if (!(state.dispcnt & NDS_GPU2D_DISPCNT_WINDOW_ENABLE_MASK)) return 0x3Fu;
    const uint8_t* const win = state.win;
    if ((state.dispcnt & 0x2000u) &&
        nds_gpu2d_window_rect_contains(x, y, win[1], win[0],
                                       win[5], win[4]))
        return win[8];
    if ((state.dispcnt & 0x4000u) &&
        nds_gpu2d_window_rect_contains(x, y, win[3], win[2],
                                       win[7], win[6]))
        return win[9];
    if ((state.dispcnt & 0x8000u) && obj_window) return win[11];
    return win[10];
}

// Native window coordinate for one column of an adaptive (widened) frame.
// Columns that carry a 2D HUD source use that source's native x. The widened
// 3D margins have no native 2D source, so they take the window status of the
// nearest native edge column: a window that reaches the native edge extends
// across the margin, and a full-screen window covers the whole adaptive
// frame. (Casting the wide x to uint8_t would wrap columns >= 256 back onto
// the left of the native screen and sample an unrelated window region.)
constexpr uint8_t nds_gpu2d_adaptive_window_x(int x, int extra, int hud_x) {
    if (hud_x >= 0) return static_cast<uint8_t>(hud_x);
    const int native = x - extra;
    return static_cast<uint8_t>(native < 0 ? 0 : native > 255 ? 255 : native);
}

constexpr uint32_t nds_gpu2d_hd_window_metadata(uint8_t mask) {
    // Bit 6 is free between the BLDCNT target and effects-enable bit. The
    // presenter must not reinsert 3D in pixels where a DS window hides BG0.
    return ((mask & 0x01u) ? 0u : 0x40u) |
           ((mask & 0x20u) ? 0x80u : 0u);
}
