#include "execution_region.h"
#include "execution_region_stats.h"
#include "state.h"
#include "tier3.h"
#include "io.h"
#include <cstring>

extern "C" void runtime_region_tail(uint32_t target_pc);
namespace {
uint64_t blocked_observer, blocked_debt, blocked_boundary, blocked_deadline;
uint64_t unsupported_exits, io_exits;
}
std::string nds_region_diagnostic_fields() {
    return ",\"blocked_observer\":" + std::to_string(blocked_observer) +
        ",\"blocked_debt\":" + std::to_string(blocked_debt) +
        ",\"blocked_boundary\":" + std::to_string(blocked_boundary) +
        ",\"blocked_deadline\":" + std::to_string(blocked_deadline) +
        ",\"unsupported_exits\":" + std::to_string(unsupported_exits) +
        ",\"io_exits\":" + std::to_string(io_exits);
}
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
    if (g_nds_active != NDS_ARM9 || g_insn_hook_armed || g_runtime_deep_trace) {
        ++blocked_observer; return false;
    }
    if (runtime_deferred_cycles() != 0) { ++blocked_debt; return false; }
    runtime_tick(0);
    if (runtime_unwinding() || runtime_should_yield()) {
        ++blocked_boundary; return false;
    }
    if (g_nds_fast_limit <= g_runtime_cycles) { ++blocked_deadline; return false; }
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
    // Publish the completed state and unwind obsolete host continuations.
    // Hardware helpers already shorten the live cap when a real rendezvous is
    // needed. An ordinary reference instruction does not require an extra
    // ARM7 catch-up round.
    g_nds_unwinding = 1;
}

extern "C" void nds_region_finish(uint32_t single_step, uint32_t io) {
    // The generated inline exit has already published registers and cycles.
    if (single_step) {
        ++unsupported_exits;
        runtime_region_single_step();
        return;
    }
    if (io) ++io_exits;
    runtime_tick(0);
    if (runtime_unwinding() || runtime_should_yield()) return;
    // I/O exits publish all effects before redispatch. Preserve the hardware
    // model's revised cap, instead of forcing a new CPU round after every
    // access even when the device scheduled nothing.
    runtime_region_tail(g_cpu.R[15] | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u));
}
