#pragma once

#include <algorithm>
#include <cmath>

struct NdsInitialWindowSize {
    int top_width, bottom_width, height;
};

inline NdsInitialWindowSize nds_initial_window_size(
    int top_width, int bottom_width, bool separate, int scale,
    float coordinate_scale, int usable_width, int usable_height) {
    double factor = std::clamp(scale, 1, 8) *
        (std::isfinite(coordinate_scale) && coordinate_scale > 0
            ? coordinate_scale : 1.0);
    const int height = separate ? 192 : 384;
    const int width = separate ? top_width + bottom_width
                               : std::max(top_width, bottom_width);
    // Fit both windows together, reserving space for borders/taskbar/gap.
    if (usable_width > 96)
        factor = std::min(factor, (usable_width - 96.0) / width);
    if (usable_height > 80)
        factor = std::min(factor, (usable_height - 80.0) / height);
    return {std::max(1, static_cast<int>(std::lround(top_width * factor))),
            std::max(1, static_cast<int>(std::lround(bottom_width * factor))),
            std::max(1, static_cast<int>(std::lround(height * factor)))};
}
