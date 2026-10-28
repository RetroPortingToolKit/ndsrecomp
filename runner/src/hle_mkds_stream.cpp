#include "hle_mkds_stream.h"
#include "streaming_lz.h"
#include "execution_model.h"
#include "state.h"
#include "sha1.h"
#include "tier3.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace mkds_stream {
namespace {
constexpr uint32_t entry = 0x0214d464u, length = 256u;
constexpr std::array<uint32_t, 6> pcs{0x0214d480u, 0x0214d490u,
    0x0214d4fcu, 0x0214d514u, 0x0214d528u, 0x0214d540u};
std::array<uint8_t, length> proof{};
NdsStaticValidation validation{entry, length, proof.data(), nullptr, 0};
Statistics counts;
struct Range {
    const NdsBusFastWin* window = nullptr;
    uint32_t offset = 0;
    uint8_t* data() const { return window->data + offset; }
};
bool range(uint32_t address, uint32_t size, Range& result) {
    if (!size || size - 1u > UINT32_MAX - address) return false;
    uint32_t first = 0, last = 0;
    const auto* a = nds_busf_classify(address, &first);
    const auto* b = nds_busf_classify(address + size - 1u, &last);
    if (!a || a != b || !a->data || !a->written || !a->gen ||
        uint64_t{first} + size > uint64_t{a->mask} + 1u ||
        last != first + size - 1u) return false;
    // Equal endpoint mappings do not prove that a smaller DTCM overlay is
    // absent in the middle of a large shared-RAM input range.
    if (g_nds_active == NDS_ARM9 && a != &g_busf_itcm && a != &g_busf_dtcm &&
        g_busf_dtcm_limit && uint64_t{address} < uint64_t{g_busf_dtcm_base} + g_busf_dtcm_limit &&
        uint64_t{address} + size > g_busf_dtcm_base) return false;
    result = {a, first}; return true;
}
bool overlaps(const Range& a, uint32_t size, const Range& b, uint32_t other) {
    const auto first = reinterpret_cast<uintptr_t>(a.data());
    const auto second = reinterpret_cast<uintptr_t>(b.data());
    return first < second + other && second < first + size;
}
void fallback() {
    ++counts.declines;
    // Use the existing reference floor for exactly the entry instruction,
    // then ordinary dispatch owns the remaining native/reference execution.
    // No duplicate decompressor or partial emulation lives in this service.
    tier3_step_one(g_cpu.R[15]);
    if (!runtime_unwinding() && !runtime_should_yield())
        runtime_dispatch(g_cpu.R[15] | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u));
}
struct Memory {
    uint32_t input_base, output_base;
    const uint8_t* source;
    uint8_t* destination;
    uint8_t input(uint32_t at) const { return source[at - input_base]; }
    uint8_t output(uint32_t at) const { return destination[at - output_base]; }
    void write(uint32_t at, uint8_t value) { destination[at - output_base] = value; }
};
} // namespace
void reset() { counts = {}; }
Statistics statistics() { return counts; }
bool is_entry(uint32_t pc) {
    return pc == entry || std::find(pcs.begin(), pcs.end(), pc) != pcs.end();
}
mkds_hle::Replacement resolve(uint32_t pc) {
    if (!is_entry(pc)) return {};
    std::array<uint8_t, length> live{};
    if (!bus_range_has_write_provenance(entry, length) ||
        !bus_debug_copy(0, entry, live.data(), length) ||
        gba::sha1(live.data(), length).hex() != "d81a4ee7ee3663ae7868653aa94f5d8d350b81c5")
        return {nullptr, nullptr, entry, length};
    proof = live;
    return {decode, &validation, entry, length};
}
void decode() {
    const bool entering = g_cpu.R[15] == entry;
    if (!nds_performance_execution() || g_runtime_deep_trace ||
        g_runtime_cycles >= g_nds_fast_limit) { fallback(); return; }
    const uint32_t budget = static_cast<uint32_t>(
        std::min<uint64_t>(512u, g_nds_fast_limit - g_runtime_cycles));
    const uint32_t context = g_cpu.R[0];
    if (g_cpu.R[15] == pcs[2] && (!g_cpu.R[7] || g_cpu.R[7] > 18u ||
        !g_cpu.R[9] || g_cpu.R[9] > 4096u)) { fallback(); return; }
    const uint32_t sp = g_cpu.R[13] - (entering ? 28u : 0u);
    Range ctx, stack, code;
    if ((context & 3u) || (sp & 3u) || !range(context, 15, ctx) ||
        !range(sp, 28, stack) || !range(entry, length, code) ||
        overlaps(ctx, 15, code, length) || overlaps(stack, 28, code, length) ||
        (entering && budget < 14u)) { fallback(); return; }
    const auto load = [](uint32_t address) { return bus_read_u32(address); };
    const uint32_t output = entering ? load(context) : g_cpu.R[3];
    const uint32_t remaining = entering ? load(context + 4u) : g_cpu.R[4];
    const uint32_t maximum = budget / 2u + 18u;
    Range input, dest;
    // The whole publication span and all possible 12-bit backreferences must
    // be ordinary, stable RAM in this slice. Mirrors/TCM use physical windows;
    // wrap/remap/device/diagnostic cases retain the original execution path.
    if (!range(g_cpu.R[1], std::max(1u, g_cpu.R[2]), input) ||
        output < 4096u || !range(output - 4096u, 4096u + maximum, dest)) {
        fallback(); return;
    }
    Range writes{dest.window, dest.offset + 4096u};
    if (overlaps(writes, maximum, code, length)) { fallback(); return; }
    uint32_t used = 0;
    if (entering) {
        for (unsigned r = 4; r <= 10; ++r) bus_write_u32(sp + (r - 4u) * 4u, g_cpu.R[r]);
        g_cpu.R[13] = sp;
        g_cpu.R[3] = load(context); g_cpu.R[4] = load(context + 4u);
        for (unsigned r = 5; r <= 8; ++r) g_cpu.R[r] = bus_read_u8(context + r + 6u);
        g_cpu.R[15] = pcs[0]; used = 14u; ++counts.calls;
        // PUSH may alias the caller's context/input. Re-read exactly as the
        // guest does; if it changed output mapping, resume original semantics.
        if (g_cpu.R[3] != output || g_cpu.R[4] != remaining) {
            runtime_tick(used);
            if (!runtime_unwinding() && !runtime_should_yield()) runtime_dispatch(pcs[0]);
            return;
        }
    }
    auto at = std::find(pcs.begin(), pcs.end(), g_cpu.R[15]);
    if (at == pcs.end()) { fallback(); return; }
    nds_lz::State state{g_cpu.R[1],g_cpu.R[2],g_cpu.R[3],g_cpu.R[4],
        g_cpu.R[5],g_cpu.R[6],g_cpu.R[7],g_cpu.R[8],g_cpu.R[9],g_cpu.R[10],
        g_cpu.cpsr,static_cast<nds_lz::Phase>(at - pcs.begin())};
    Memory memory{g_cpu.R[1],output - 4096u,input.data(),dest.data()};
    used += nds_lz::run(state, memory, budget - used);
    const uint32_t produced = state.output - output;
    if (produced) {
        // One physical publication before any event, IRQ, ARM7 observation or
        // code-cache lookup can see the completed span. No unknown-write scan.
        nds_busf_note_write(writes.window, writes.offset, produced);
        counts.bytes += produced;
    }
    g_cpu.R[1] = state.input; g_cpu.R[2] = state.available;
    g_cpu.R[3] = state.output; g_cpu.R[4] = state.remaining;
    g_cpu.R[5] = state.flags; g_cpu.R[6] = state.bits;
    g_cpu.R[7] = state.token; g_cpu.R[8] = state.pending;
    g_cpu.R[9] = state.scratch9; g_cpu.R[10] = state.scratch10;
    g_cpu.cpsr = state.cpsr; g_cpu.R[15] = pcs[static_cast<unsigned>(state.phase)];
    if (state.phase == nds_lz::Phase::Finish && budget - used >= 18u) {
        bus_write_u32(context, g_cpu.R[3]); bus_write_u32(context + 4u, g_cpu.R[4]);
        for (unsigned r = 5; r <= 8; ++r) bus_write_u8(context + r + 6u, g_cpu.R[r]);
        g_cpu.R[0] = g_cpu.R[4];
        for (unsigned r = 4; r <= 10; ++r) g_cpu.R[r] = load(sp + (r - 4u) * 4u);
        g_cpu.R[13] = sp + 28u;
        const uint32_t target = g_cpu.R[14];
        g_cpu.R[15] = target & ((target & 1u) ? ~1u : ~3u);
        g_cpu.cpsr = (g_cpu.cpsr & ~CPSR_T_BIT) | ((target & 1u) ? CPSR_T_BIT : 0u);
        runtime_tick(used + 18u); ++counts.batches;
        if (runtime_unwinding() || runtime_should_yield() || runtime_call_should_return(g_cpu.R[15])) return;
        runtime_dispatch_with_exchange(target); return;
    }
    if (!used) { fallback(); return; }
    ++counts.batches; runtime_tick(used);
    if (!runtime_unwinding() && !runtime_should_yield()) runtime_dispatch(g_cpu.R[15]);
}
} // namespace mkds_stream
