#pragma once
#include <cstdint>
#include <limits>

// Settle the service's no-event interval without entering command processing.
// Due work is left entirely untouched for the maintained device path. No cache
// survives register writes, frame transitions or state loads: eligibility is
// derived from live device state on every caller boundary.
template<class Geometry>
inline bool nds_gpu3d_settle_poll(Geometry& gpu, uint64_t arm9_time,
                                uint32_t clock_shift) noexcept {
    const uint64_t now = arm9_time >> clock_shift;
    if (!gpu.GeometryEnabled || gpu.FlushRequest ||
        (gpu.CmdPIPE.IsEmpty() && !(gpu.GXStat & (1u << 27)))) {
        gpu.Timestamp = now;
        return true;
    }
    // Timestamp discontinuities and signed-counter overflow stay on the
    // maintained path instead of turning an exceptional interval into idle.
    if (now < gpu.Timestamp || gpu.CycleCount <= 0) return false;
    const uint64_t elapsed = now - gpu.Timestamp;
    if (elapsed >= static_cast<uint64_t>(gpu.CycleCount) ||
        elapsed > static_cast<uint64_t>(std::numeric_limits<int32_t>::max()))
        return false;
    gpu.CycleCount -= static_cast<int32_t>(elapsed);
    gpu.Timestamp = now;
    return true;
}
