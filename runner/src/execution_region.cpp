#include "execution_region.h"
#include "state.h"
#include "tier3.h"
#include "io.h"
#include <cstring>

extern "C" void runtime_region_tail(uint32_t target_pc);
extern "C" {
uint64_t g_nds_region_entries = 0, g_nds_region_reference_steps = 0;
#if defined(NDS_MKDS_HLE)
uint32_t g_nds_region_hle_vectors = 1;
#else
uint32_t g_nds_region_hle_vectors = 0;
#endif
}

extern "C" bool nds_region_prepare(void) {
    // First prototype targets ARM9. The universal current backend continues to
    // own ARM7, unsupported instructions and exact instruction-level observers.
    if (g_nds_active != NDS_ARM9 || g_insn_hook_armed || g_runtime_deep_trace ||
        runtime_deferred_cycles() != 0)
        return false;
    runtime_tick_slow(0);
    if (runtime_unwinding() || runtime_should_yield_slow() ||
        g_nds_fast_limit <= g_runtime_cycles) return false;
    // Guest LR/stack now carry guest returns. Discard the reference backend's
    // implementation-only host continuation hints at this boundary.
    runtime_call_stack_restore(nullptr, 0);
    ++g_nds_region_entries;
    return true;
}

extern "C" void runtime_region_single_step() {
    if (runtime_should_yield()) return;
    ++g_nds_region_reference_steps;
    tier3_step_one(g_cpu.R[15]);
    // A single-step may have touched a device or changed mode/TCM. Rendezvous
    // before returning to compiled regions, retaining the actual completed PC.
    nds_reschedule_slice(g_runtime_cycles >> (g_nds_active == NDS_ARM9 ? 1 : 0));
    g_nds_unwinding = 1;
}

extern "C" void nds_region_end(NdsExecRegion* r) {
    nds_region_publish(r);
    if (r->single_step) {
        runtime_region_single_step();
        return;
    }
    runtime_tick_slow(0);
    if (runtime_unwinding() || runtime_should_yield_slow()) return;
    if (r->io) {
        nds_reschedule_slice(g_runtime_cycles >> 1);
        g_nds_unwinding = 1;
        return;
    }
    runtime_region_tail(g_cpu.R[15] | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u));
}
