#pragma once
#include "runtime_arm.h"

namespace mkds_hle {
using Function = void (*)();
struct Replacement {
    Function function = nullptr;
    const NdsStaticValidation* validation = nullptr;
    uint32_t guard_start = 0, guard_size = 0;
};
struct Statistics {
    bool rom_supported = false;
    uint32_t group_mask = 0; // VECTOR=1; compiled choice
    uint64_t scale_add = 0, cross_product = 0, transform_translate = 0;
};
bool initialize(const char* rom_sha1);
Statistics statistics();
// Cold lookup only. Supplies a full-operation proof, including literals,
// even when the generated entry owns only a short prefix or a loop block.
Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb);
void scale_add();
void cross_product();
void transform_translate();
} // namespace mkds_hle
