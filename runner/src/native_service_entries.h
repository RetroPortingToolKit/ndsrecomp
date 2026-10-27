#pragma once
#if defined(NDS_MKDS_HLE)
#include "hle_mkds.h"
#endif
#if defined(NDS_MPH_MATH_HLE)
#include "hle_mph_math.h"
#endif
#if defined(NDS_MPH_SUBMIT_HLE)
#include "hle_mph_submit.h"
#endif
#include "runtime_arm.h"

// Stop translation at every supported service entry/continuation, even if its
// current bytes decline HLE. The ordinary dispatcher owns positive AND negative
// code proofs and dependency invalidation; no JIT copy can bypass those guards.
inline bool nds_native_service_entry(NdsCpu cpu, uint32_t pc, bool thumb) {
#if defined(NDS_MKDS_HLE)
    if (mkds_hle::is_entry(cpu, pc, thumb)) return true;
#endif
#if defined(NDS_MPH_MATH_HLE)
    if (mph_hle::is_entry(cpu, pc, thumb)) return true;
#endif
#if defined(NDS_MPH_SUBMIT_HLE)
    if (mph_submit::is_entry(cpu, pc, thumb)) return true;
#endif
    return false;
}
