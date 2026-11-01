#pragma once

#include "hle_mkds.h"

// Same resolver/native-barrier contract as the existing resumable MKDS service.
namespace mkds_card_pio {
// Passive counters, reset on ROM initialization; not canonical guest state.
// Calls include declined resumes; positive words prove actual native port reads.
struct Statistics { uint64_t calls = 0, words = 0, declines = 0; };
Statistics statistics();
bool initialize(const char* rom_sha1);
bool is_entry(uint32_t pc);
mkds_hle::Replacement resolve(uint32_t pc);
void run();
} // namespace mkds_card_pio
