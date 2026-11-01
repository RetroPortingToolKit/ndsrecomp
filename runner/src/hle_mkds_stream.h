#pragma once
#include "hle_mkds.h"
namespace mkds_stream {
struct Statistics { uint64_t calls = 0, batches = 0, bytes = 0, declines = 0; };
void reset();
Statistics statistics();
bool is_entry(uint32_t pc);
mkds_hle::Replacement resolve(uint32_t pc);
void decode();
}
