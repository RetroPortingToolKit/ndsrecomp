#pragma once

#include "runtime_arm.h"

namespace mph_hle {
using Function = void (*)(void);

// Called once at startup, before dispatch caches are populated. This scopes
// the compiled implementation to its supported ROM; it is not a mode setter.
bool initialize(const char* rom_sha1);

// Only called on a cold, already content-validated native lookup. The caller
// retains its existing validation/page-generation guards around the result.
Function resolve(NdsCpu cpu, uint32_t pc, bool thumb,
                 const NdsStaticValidation* validation);

void scale_add();
void cross_product();
void transform_translate();
} // namespace mph_hle
