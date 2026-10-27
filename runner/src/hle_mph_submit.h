#pragma once

#include "runtime_arm.h"

namespace mph_submit {
using Function = void (*)();
struct Replacement {
    Function function;
    const NdsStaticValidation* validation;
    uint32_t guard_start, guard_size;
};
struct Statistics {
    bool rom_supported;
    uint64_t setup_calls, submit_calls, chunks, bytes, waits, completions, dma_batches;
};
bool initialize(const char* rom_sha1);
bool is_entry(NdsCpu cpu, uint32_t pc, bool thumb);
Statistics statistics();
Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb);
void setup();
void submit();
} // namespace mph_submit
