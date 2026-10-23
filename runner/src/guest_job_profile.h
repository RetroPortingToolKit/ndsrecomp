#pragma once

#include <cstdint>

// Optional companion to hostprof. Sources must remain mapped for the entire
// capture; capture is called ONLY while the owning emulation thread is suspended.
// Configure before starting the sampler, and finish only after joining it.
struct NdsGuestJobSources {
    const uint32_t* pc;
    const uint32_t* cpsr;
    const void* cpu;  // a 32-bit selector; copied without type aliasing
    const uint32_t* returns;
    const uint32_t* depth;
    uint32_t return_capacity;
};

constexpr uint32_t kNdsGuestJobFrames = 64;
struct NdsGuestJobSample {
    uint64_t qpc;  // same timestamp as the corresponding hostprof sample
    uint32_t pc, cpsr, cpu, full_depth;
    uint32_t returns[kNdsGuestJobFrames];  // innermost first, retaining mode bit
};
static_assert(sizeof(NdsGuestJobSample) == 280);

bool nds_guest_jobs_start(const NdsGuestJobSources& sources, const char* path,
                          uint32_t capacity = 32768);
// No allocation, locks, I/O, clock reads or guest execution.
void nds_guest_jobs_capture(uint64_t qpc);
// Writes chronological records after the sampler has stopped. False on I/O error.
bool nds_guest_jobs_finish();
