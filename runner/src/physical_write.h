#pragma once
#include <cstdint>

// A backing's generation cells are contiguous. Compare integer offsets rather
// than ordering unrelated C++ pointers. Missing identity is always conservative.
inline bool nds_physical_write_may_touch(const uint32_t* first, uint32_t count,
                                        const uint32_t* observed) {
    if (!first || !count || !observed) return true;
    const auto offset = reinterpret_cast<std::uintptr_t>(observed) -
                        reinterpret_cast<std::uintptr_t>(first);
    return offset % sizeof(uint32_t) == 0 && offset / sizeof(uint32_t) < count;
}
