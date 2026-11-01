#include "hle_mkds_card_pio.h"
#include "hle_vector_math.h"
#include "arm9_native.h"
#include "execution_model.h"
#include "io.h"
#include "sha1.h"
#include "state.h"
#include "tier3.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

namespace mkds_card_pio {
namespace {
constexpr uint32_t Entry = 0x021463a4u;
constexpr uint32_t Length = 256u;
constexpr uint32_t Request = 0x0217fa20u;
constexpr uint32_t Cache = 0x02180060u;
constexpr uint32_t Control = 0x040001a4u;
constexpr uint32_t Data = 0x04100010u;
constexpr uint32_t Busy = 0x80000000u;
constexpr uint32_t Ready = 0x00800000u;
constexpr uint32_t Block = 512u;
constexpr uint32_t Frame = 40u;
constexpr std::array<uint32_t, 11> Pcs{
    Entry, 0x021463ccu, 0x0214640cu, 0x02146414u, 0x02146418u, 0x02146424u,
    0x02146428u, 0x02146434u, 0x0214643cu, 0x0214646cu, 0x02146484u};
bool supported = false;
Statistics counts;
std::array<uint8_t, Length> proof{};
NdsStaticValidation validation{Entry, Length, proof.data(), nullptr, 0};

struct Range {
    const NdsBusFastWin* window = nullptr;
    uint32_t offset = 0;
    const uint8_t* data() const { return window->data + offset; }
};

bool range(uint32_t address, uint32_t size, Range& out) {
    if (!size || size - 1u > UINT32_MAX - address) return false;
    uint32_t first = 0, last = 0;
    const auto* a = nds_busf_classify(address, &first);
    const auto* b = nds_busf_classify(address + size - 1u, &last);
    if (!a || a != b || !a->data || !a->written || !a->gen ||
        uint64_t{first} + size > uint64_t{a->mask} + 1u ||
        last != first + size - 1u) return false;
    // Whole-slice proof, including a possible imported interior TCM overlay.
    if (a != &g_busf_itcm && a != &g_busf_dtcm && g_busf_dtcm_limit &&
        uint64_t{address} < uint64_t{g_busf_dtcm_base} + g_busf_dtcm_limit &&
        uint64_t{address} + size > g_busf_dtcm_base) return false;
    out = {a, first};
    return true;
}

bool overlaps(const Range& a, uint32_t size, const Range& b, uint32_t other) {
    const auto first = reinterpret_cast<uintptr_t>(a.data());
    const auto second = reinterpret_cast<uintptr_t>(b.data());
    return first < second + other && second < first + size;
}

// These are ARM CMP/SUBS and the two rotated-immediate ANDS flag results,
// not a second instruction decoder. Low control bits and V survive ANDS.
void sub_flags(uint32_t a, uint32_t b) {
    const uint32_t result = a - b;
    uint32_t flags = result & CPSR_N_BIT;
    if (!result) flags |= CPSR_Z_BIT;
    if (a >= b) flags |= CPSR_C_BIT;
    if (((a ^ b) & (a ^ result)) >> 31u) flags |= CPSR_V_BIT;
    g_cpu.cpsr = (g_cpu.cpsr & ~(CPSR_N_BIT | CPSR_Z_BIT | CPSR_C_BIT | CPSR_V_BIT)) | flags;
}

void and_flags(uint32_t result, bool carry) {
    uint32_t flags = result & CPSR_N_BIT;
    if (!result) flags |= CPSR_Z_BIT;
    if (carry) flags |= CPSR_C_BIT;
    g_cpu.cpsr = (g_cpu.cpsr & ~(CPSR_N_BIT | CPSR_Z_BIT | CPSR_C_BIT)) | flags;
}

uint32_t load(uint32_t address) { return bus_read_u32(address); }
void store(uint32_t address, uint32_t value) { bus_write_u32(address, value); }

void fallback() {
    ++counts.declines;
    // Decline without a partial hidden job. The current PC/registers/frame
    // are always an original executable state, including after a restore.
    tier3_step_one(g_cpu.R[15]);
    if (!runtime_unwinding() && !runtime_should_yield())
        runtime_dispatch(g_cpu.R[15] | ((g_cpu.cpsr & CPSR_T_BIT) ? 1u : 0u));
}

bool admit(uint32_t pc) {
    const bool entering = pc == Entry;
    if (g_nds_active != NDS_ARM9 || (g_cpu.cpsr & CPSR_T_BIT) ||
        !nds_performance_execution() || !nds_arm9_native_supported() ||
        g_runtime_deep_trace || g_insn_hook_armed)
        return false;
    if ((entering && (g_cpu.R[0] != Cache || g_cpu.R[13] < Frame)) ||
        (!entering && (g_cpu.R[4] != 0xfffffe00u || g_cpu.R[5] != Data ||
            g_cpu.R[6] != Control || g_cpu.R[7] != Cache + 32u ||
            g_cpu.R[9] != Request || g_cpu.R[10] != Cache || g_cpu.R[11] != 0)))
        return false;
    const uint32_t sp = g_cpu.R[13] - (entering ? Frame : 0u);
    Range stack, code, request, cache;
    if ((sp & 3u) || !range(sp, Frame, stack) || !range(Entry, Length, code) ||
        !range(Request, 0x118u, request) || !range(Cache, 32u + Block, cache) ||
        overlaps(stack, Frame, code, Length) || overlaps(stack, Frame, request, 0x118u) ||
        overlaps(stack, Frame, cache, 32u + Block)) return false;
    // Original command/control preparation and edge-copy helpers remain
    // guest calls. Admit only their ordinary512B read-command contract here.
    const uint32_t control = load(Cache + 4u);
    if ((control & 0xc7000000u) != 0x81000000u) return false;
    if (entering || pc == 0x021463ccu) {
        if (!load(Request + 0x24u)) return false;
        return true;
    }
    Range target;
    if ((g_cpu.R[8] & 3u) || !range(g_cpu.R[8], Block, target) ||
        overlaps(target, Block, code, Length) || overlaps(target, Block, stack, Frame) ||
        overlaps(target, Block, request, 0x118u)) return false;
    // A staging block may write the cache payload, never its control/header.
    if (g_cpu.R[8] != Cache + 32u && overlaps(target, Block, cache, 32u + Block)) return false;
    if ((pc == 0x02146418u || pc == 0x02146424u || pc == 0x02146428u ||
         pc == 0x02146434u || pc == 0x0214643cu) && g_cpu.R[0] > Block / 4u)
        return false;
    if ((pc == 0x02146424u || pc == 0x02146428u) &&
        (!(g_cpu.R[2] & Ready) || (g_cpu.R[2] & 0x47000000u) != 0x01000000u))
        return false;
    return true;
}

bool boundary(uint32_t pc, uint32_t cycles) {
    g_cpu.R[15] = pc;
    runtime_tick(cycles);
    return runtime_unwinding() || runtime_should_yield();
}

bool store_fenced(uint32_t next_pc, uint32_t used, uint32_t whole_phase) {
    // Typed publication can invalidate an arbitrary executing caller even
    // when the output does not alias this service's own proof. Finish exactly
    // the original store instruction; do not execute later grouped work after
    // its newly shortened fence. `used` includes that completed instruction.
    if (!runtime_unwinding() && g_nds_fast_limit >= g_runtime_cycles + whole_phase)
        return false;
    if (!boundary(next_pc, used)) runtime_dispatch(next_pc);
    return true;
}

void completed(uint32_t cycles) {
    const uint32_t sp = g_cpu.R[13];
    for (uint32_t reg = 4; reg <= 11; ++reg)
        g_cpu.R[reg] = load(sp + 4u + (reg - 4u) * 4u);
    g_cpu.R[14] = load(sp + 36u);
    g_cpu.R[13] = sp + Frame;
    nds_hle::finish(cycles);
}
} // namespace

Statistics statistics() { return counts; }

bool initialize(const char* rom_sha1) {
    counts = {};
    supported = rom_sha1 && std::strcmp(rom_sha1,
        "691e00d9a5dd80b04f80cc7559503e8b06848785") == 0;
    return supported;
}

bool is_entry(uint32_t pc) {
    return supported && std::find(Pcs.begin(), Pcs.end(), pc) != Pcs.end();
}

mkds_hle::Replacement resolve(uint32_t pc) {
    if (!is_entry(pc)) return {};
    std::array<uint8_t, Length> live{};
    if (!bus_range_has_write_provenance(Entry, Length) ||
        !bus_debug_copy(0, Entry, live.data(), Length) ||
        gba::sha1(live.data(), live.size()).hex() != "142145b87ee0a970275fe7eca19106a7756acbc0")
        return {nullptr, nullptr, Entry, Length};
    proof = live;
    return {run, &validation, Entry, Length};
}

void run() {
    ++counts.calls;
    if (runtime_unwinding() || runtime_should_yield()) return;
    // Qualification assumption is the retained performance timing2 profile.
    // At most512 ARM9 cycles of this native service before redispatch. Actual
    // device deadlines/IRQ/other-CPU rendezvous may shorten it at any boundary.
    const uint64_t start = g_runtime_cycles;
    const uint64_t end = std::min<uint64_t>(g_nds_fast_limit,
        start > UINT64_MAX - 512u ? UINT64_MAX : start + 512u);
    for (;;) {
        const uint32_t pc = g_cpu.R[15];
        if (!admit(pc)) { fallback(); return; }
        const uint64_t left = end > g_runtime_cycles ? end - g_runtime_cycles : 0u;
        const auto need = [&](uint32_t cycles) { return left >= cycles; };
        if (pc == Entry) {
            if (!need(20)) { fallback(); return; }
            const uint32_t sp = g_cpu.R[13] - Frame;
            for (uint32_t reg = 4; reg <= 11; ++reg)
                store(sp + 4u + (reg - 4u) * 4u, g_cpu.R[reg]);
            store(sp + 36u, g_cpu.R[14]);
            // All nine stores are the single original STM/PUSH instruction.
            // Its next instruction is SUB sp,#4, not the grouped setup tail.
            g_cpu.R[13] = sp + 4u;
            if (store_fenced(0x021463a8u, 2, 20)) return;
            g_cpu.R[13] = sp;
            g_cpu.R[10] = Cache;g_cpu.R[9] = Request;g_cpu.R[7] = Cache + 32u;
            g_cpu.R[5] = Data;g_cpu.R[6] = Control;g_cpu.R[11] = 0;
            g_cpu.R[0] = Block;g_cpu.R[4] = 0u - Block;
            if (boundary(0x021463ccu, 20)) return;
        } else if (pc == 0x021463ccu) {
            // Derive the next original512B transfer, not a whole-ROM memcpy.
            if (!need(30)) { fallback(); return; }
            const uint32_t source = load(Request + 0x1cu);
            const uint32_t page = source & ~(Block - 1u);
            g_cpu.R[0] = source;g_cpu.R[1] = page;sub_flags(page, source);
            uint32_t cycles = 18;
            bool direct = page == source;
            if (direct) {
                g_cpu.R[8] = load(Request + 0x20u);
                g_cpu.R[0] = g_cpu.R[8] & 3u;
                and_flags(g_cpu.R[0], (g_cpu.cpsr & CPSR_C_BIT) != 0);
                direct = g_cpu.R[0] == 0;cycles = 24;
                if (direct) {
                    g_cpu.R[0] = load(Request + 0x24u);sub_flags(g_cpu.R[0], Block);
                    direct = g_cpu.R[0] >= Block;cycles = direct ? 26u : 30u;
                }
            }
            if (!direct) {
                g_cpu.R[8] = Cache + 32u;g_cpu.R[15] = 0x021463f8u;
                store(Cache + 8u, page);
                if (store_fenced(0x021463fcu, cycles - 6u, cycles)) return;
            }
            g_cpu.R[0] = (page >> 8u) | 0xb7000000u;
            g_cpu.R[1] = page << 24u;
            // Execute the actual original BL instruction/helper. It owns the
            // prior-busy wait, AUX enable and all eight bytewise command writes.
            if (boundary(0x02146408u, cycles)) return;
            runtime_dispatch(0x02146408u);return;
        } else if (pc == 0x0214640cu) {
            if (!need(4)) { fallback(); return; }
            g_cpu.R[1] = load(Cache + 4u);g_cpu.R[0] = g_cpu.R[11];
            // Publish elapsed LDR/MOV time before the actual MMIO start write.
            // Device events must never be scheduled from a stale CPU timestamp.
            if (boundary(0x02146414u, 4)) return;
        } else if (pc == 0x02146414u) {
            if (!need(2)) { fallback(); return; }
            store(Control, g_cpu.R[1]);
            if (boundary(0x02146418u, 2)) return;
        } else if (pc == 0x02146418u) {
            if (!need(10)) { fallback(); return; }
            uint64_t event_bound = left;
            const uint64_t event = nds_next_system_event_time();
            if (event != UINT64_MAX && event <= UINT64_MAX / 2u) {
                const uint64_t when = event * 2u;
                if (when <= g_runtime_cycles || when - g_runtime_cycles < 10u) {
                    fallback();return;
                }
                event_bound = std::min(event_bound, when - g_runtime_cycles);
            }
            g_cpu.R[2] = load(Control);
            // An actual read can tighten a performance-window fence. In that
            // case only this LDR has executed: hand off at its original next PC.
            if (g_nds_fast_limit <= g_runtime_cycles ||
                g_nds_fast_limit - g_runtime_cycles < 10u) {
                if (boundary(0x0214641cu, 2)) return;
                runtime_dispatch(0x0214641cu);return;
            }
            g_cpu.R[1] = g_cpu.R[2] & Ready;and_flags(g_cpu.R[1], false);
            if (g_cpu.R[1]) {
                if (boundary(0x02146424u, 6)) return;
            } else {
                g_cpu.R[1] = g_cpu.R[2] & Busy;and_flags(g_cpu.R[1], true);
                if (!g_cpu.R[1]) {
                    if (boundary(0x0214643cu, 10)) return;
                } else {
                    const uint64_t wait = std::min(event_bound,
                        uint64_t(g_nds_fast_limit - g_runtime_cycles));
                    // A busy/not-ready loop has five timing2 instructions.
                    // Elide only repeated observational polls before the first
                    // real event/window boundary; never consume a data word early.
                    const uint32_t cycles = uint32_t(wait / 10u * 10u);
                    if (boundary(0x02146418u, cycles)) return;
                }
            }
        } else if (pc == 0x02146424u) {
            if (!need(2) || g_cpu.R[0] >= Block / 4u) { fallback(); return; }
            g_cpu.R[1] = load(Data);
            ++counts.words;
            // Final card data read may raise IRQ. The fetched word and its
            // not-yet-written destination are canonical before that boundary.
            if (boundary(0x02146428u, 2)) return;
        } else if (pc == 0x02146428u) {
            if (!need(6) || g_cpu.R[0] >= Block / 4u) { fallback(); return; }
            sub_flags(g_cpu.R[0], Block);
            g_cpu.R[15] = 0x0214642cu;
            store(g_cpu.R[8] + g_cpu.R[0] * 4u, g_cpu.R[1]);
            if (store_fenced(0x02146430u, 4, 6)) return;
            ++g_cpu.R[0];
            if (boundary(0x02146434u, 6)) return;
        } else if (pc == 0x02146434u) {
            if (!need(4)) { fallback(); return; }
            g_cpu.R[1] = g_cpu.R[2] & Busy;and_flags(g_cpu.R[1], true);
            if (boundary(g_cpu.R[1] ? 0x02146418u : 0x0214643cu, 4)) return;
        } else if (pc == 0x0214643cu) {
            if (!need(24) || g_cpu.R[0] != Block / 4u) { fallback(); return; }
            const uint32_t destination = load(Request + 0x20u);
            const uint32_t remaining = load(Request + 0x24u);
            if (g_cpu.R[8] == destination && remaining < Block) { fallback(); return; }
            g_cpu.R[0] = destination;sub_flags(g_cpu.R[8], destination);
            if (g_cpu.R[8] != destination) {
                g_cpu.R[0] = Cache;
                // Original helper performs byte/halfword-sensitive head/tail
                // copies and updates only the requested prefix. Do not replace
                // it with memcpy or reproduce its partial-word behavior here.
                if (boundary(0x02146480u, 8)) return;
                runtime_dispatch(0x02146480u);return;
            }
            g_cpu.R[2] = load(Request + 0x1cu) + Block;
            g_cpu.R[1] = load(Request + 0x20u) + Block;
            g_cpu.R[0] = remaining - Block;sub_flags(remaining, Block);
            g_cpu.R[15] = 0x02146460u;
            store(Request + 0x1cu, g_cpu.R[2]);
            if (store_fenced(0x02146464u, 20, 24)) return;
            g_cpu.R[15] = 0x02146464u;
            store(Request + 0x20u, g_cpu.R[1]);
            if (store_fenced(0x02146468u, 22, 24)) return;
            g_cpu.R[15] = 0x02146468u;
            store(Request + 0x24u, g_cpu.R[0]);
            if (boundary(0x0214646cu, 24)) return;
        } else if (pc == 0x0214646cu) {
            if (!need(8)) { fallback(); return; }
            if (!(g_cpu.cpsr & CPSR_Z_BIT)) {
                if (boundary(0x021463ccu, 2)) return;
            } else { completed(8);return; }
        } else if (pc == 0x02146484u) {
            if (!need(10)) { fallback(); return; }
            sub_flags(g_cpu.R[0], 0);
            if (g_cpu.R[0]) {
                if (boundary(0x021463ccu, 4)) return;
            } else { completed(10);return; }
        } else { fallback();return; }
        if (g_runtime_cycles >= end) {
            if (!runtime_unwinding() && !runtime_should_yield()) runtime_dispatch(g_cpu.R[15]);
            return;
        }
    }
}
} // namespace mkds_card_pio
