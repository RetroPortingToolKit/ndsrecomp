#pragma once

#include <algorithm>
#include <cstdint>
#include "state.h"

// Complete one contiguous ordinary main-RAM service burst. No device or CPU
// observes inside the existing nds_dma_run call. Preserve its target overshoot
// (one final unit), continuation and clock; provenance is published once.
inline bool dma_ram_batch(int cpu, uint32_t width, uint64_t target,
                          uint32_t& src, uint32_t& dst, uint32_t& remaining,
                          uint16_t& burst_index, bool& burst_start,
                          uint64_t& clock) {
    if ((width != 2u && width != 4u) || !remaining || clock >= target ||
        src < 0x02000000u || src >= 0x03000000u ||
        dst < 0x02000000u || dst >= 0x03000000u) return false;
    const uint32_t unit = (width == 4u ? 18u : 16u) << (cpu == 0 ? 1u : 0u);
    const uint32_t units = static_cast<uint32_t>(std::min<uint64_t>(
        remaining, 1u + (target - clock - 1u) / unit));
    if (units < 4u || units > UINT32_MAX / width) return false;
    const uint32_t bytes = units * width;
    if (!bus_dma_copy_main_ram(src & ~(width - 1u), dst & ~(width - 1u), bytes)) return false;
    clock += uint64_t{units} * unit;
    src += bytes;
    dst += bytes;
    remaining -= units;
    burst_index = static_cast<uint16_t>(burst_index + units);
    burst_start = false;
    return true;
}
