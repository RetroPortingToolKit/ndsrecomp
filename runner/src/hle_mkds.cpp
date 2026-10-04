#include "hle_mkds.h"
#include "hle_vector_math.h"
#include "state.h"
#include "sha1.h"

#include <array>
#include <cstring>
#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
#include "gpu3d.h"
#include "io.h"
#include <algorithm>
#endif

namespace mkds_hle {
namespace {
Statistics counts;
struct Binding {
    uint32_t entry, start, size;
    const char* sha1;
    Function function;
    std::array<uint8_t, 252> bytes{};
    NdsStaticValidation validation{};
    std::array<NdsStaticValidationRange, 3> dependencies{};
};
#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
std::array<uint8_t, 156> packet_helpers;
std::array<uint8_t, 152> packet_flush;
#endif
// ROM hashes only; the proof bytes are read from the verified resident image.
Binding bindings[] = {
    {0x02147FD8, 0x02147FD8, 96, "a62a720989cd408241a1acc35520b93d866bf7df", scale_add},
    {0x021483A0, 0x021483A0, 156, "8a97f6bdacd40175ea4f577788a82635a8057984", cross_product},
    {0x02147288, 0x02147288, 180, "fe182d57e45fd6086b43f0b98ac689d14b815365", transform_translate},
#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
    {0x01FF9048, 0x01FF9048, 252, "c377c97f7c4a956e4c6f8396b94871d21437edac", packet},
    // HLE continuation, with an explicitly owned register/stack ABI. The
    // full function and its literals must still match at every admission.
    {0x01FF9130, 0x01FF9048, 252, "c377c97f7c4a956e4c6f8396b94871d21437edac", packet_resume},
#endif
};
} // namespace

bool initialize(const char* rom_sha1) {
    counts = {};
    counts.group_mask = 1; // only the three vector kernels are integrated
#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
    counts.group_mask |= 8;
#endif
    counts.rom_supported = rom_sha1 &&
        std::strcmp(rom_sha1, "691e00d9a5dd80b04f80cc7559503e8b06848785") == 0;
    return counts.rom_supported;
}
Statistics statistics() { return counts; }

Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb) {
    if (!counts.rom_supported || cpu != NDS_ARM9 || thumb) return {};
    for (auto& b : bindings) {
        if (!b.function || pc != b.entry) continue;
        Replacement rejected{nullptr, nullptr, b.start, b.size};
#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
        const bool is_packet = b.start == 0x01FF9048u;
        if (is_packet) {
            rejected.helper_guard_start = 0x0214D044u;
            rejected.helper_guard_size = packet_helpers.size();
            auto proof = [](uint32_t addr, auto& bytes, const char* hash) {
                return bus_range_has_write_provenance(addr, bytes.size()) &&
                    bus_debug_copy(0, addr, bytes.data(), bytes.size()) &&
                    gba::sha1(bytes.data(), bytes.size()).hex() == hash;
            };
            // Stage into temporaries: a rejected proof must not overwrite the
            // immutable expected bytes held by an already active guard.
            decltype(packet_helpers) helpers;
            decltype(packet_flush) flush;
            if (!proof(0x0214D044u, helpers, "44e97ad8b7c1e5b048f04927acd1f21cb6fa803f") ||
                !proof(0x01FF921Cu, flush, "b1ad249c3c1f9dddedaeeeea9fff8aabc11dc50e"))
                return rejected;
            packet_helpers = helpers; packet_flush = flush;
        }
#endif
        std::array<uint8_t, 252> live{};
        if (!bus_range_has_write_provenance(b.start, b.size) ||
            !bus_debug_copy(0, b.start, live.data(), b.size) ||
            gba::sha1(live.data(), b.size).hex() != b.sha1)
            return rejected;
        // Successful revalidation always copies the same fingerprinted bytes.
        // Their stable address may be retained by dispatch/link/active guards.
        b.bytes = live;
        b.validation = {b.start, b.size, b.bytes.data(), nullptr, 0};
#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
        if (is_packet) {
            b.dependencies = {{{b.start, b.size, b.bytes.data()},
                {0x0214D044u, uint32_t(packet_helpers.size()), packet_helpers.data()},
                {0x01FF921Cu, uint32_t(packet_flush.size()), packet_flush.data()}}};
            b.validation.dependencies = b.dependencies.data();
            b.validation.dependency_count = b.dependencies.size();
        }
#endif
        rejected.function = b.function;
        rejected.validation = &b.validation;
        return rejected;
    }
    return {};
}

void scale_add() { ++counts.scale_add; nds_hle::scale_add(); }
void cross_product() { ++counts.cross_product; nds_hle::cross_product(); }
void transform_translate() { ++counts.transform_translate; nds_hle::transform_translate(); }

#ifdef NDS_EXPERIMENT_MKDS_PACKET_HLE
namespace {
constexpr uint32_t kBuffer = 0x021731E4, kBusy = 0x021731E8;
constexpr uint32_t kResume = 0x01FF9130;

void packet_pause(uint32_t cycles) {
    ++counts.packet_waits;
    runtime_tick(cycles);
    if (runtime_unwinding()) return; // preserve an IRQ's actual continuation
    nds_reschedule_slice(g_runtime_cycles >> 1u);
    g_nds_unwinding = 1;
}
}

void packet() {
    const uint32_t command = g_cpu.R[0], source = g_cpu.R[1], size = g_cpu.R[2];
    const uint32_t buffer = nds_hle::load(kBuffer);
    const uint32_t busy = nds_hle::load(kBusy);
    const uint32_t used = buffer ? nds_hle::load(buffer) : 0;
    if (buffer && busy && uint64_t{used} + 1u + size <= 192u) {
        ++counts.packet_calls;
        ++counts.packet_buffered;
        nds_hle::store(buffer, used + 1u);
        nds_hle::store(buffer + 4u + used * 4u, command);
        // The SDK copy snapshots eight words before storing each full block;
        // keep that observable overlap behavior without executing its loop.
        uint32_t i = 0;
        for (; size - i >= 8u; i += 8u) {
            uint32_t block[8];
            for (uint32_t j = 0; j < 8u; ++j)
                block[j] = bus_read_u32((source + (i + j) * 4u) & ~3u);
            for (uint32_t j = 0; j < 8u; ++j)
                nds_hle::store(buffer + 8u + (used + i + j) * 4u, block[j]);
        }
        for (; i < size; ++i)
            nds_hle::store(buffer + 8u + (used + i) * 4u,
                           bus_read_u32((source + i * 4u) & ~3u));
        if (size) nds_hle::store(buffer, nds_hle::load(buffer) + size);
        nds_hle::finish(32u + size * 4u);
        return;
    }
    // A prior asynchronous transfer owns the stream. Retry the operation
    // without side effects until its genuine completion clears the flag.
    if (busy) { packet_pause(128); return; }
    ++counts.packet_calls;
    ++counts.packet_direct;
    const uint32_t sp = g_cpu.R[13] - 16u;
    nds_hle::store(sp, g_cpu.R[4]); nds_hle::store(sp + 4u, g_cpu.R[5]);
    nds_hle::store(sp + 8u, g_cpu.R[6]); nds_hle::store(sp + 12u, g_cpu.R[14]);
    g_cpu.R[13] = sp;
    g_cpu.R[4] = size; g_cpu.R[5] = source; g_cpu.R[6] = command;
    // HLE state: r0=cursor, r1=words left, r2=flush/command/parameters,
    // r3=buffer header. r4-r6 and the 16-byte saved caller frame survive IRQs
    // and savestates. There is no host-only queue or HLE-to-LLE handoff.
    g_cpu.R[0] = buffer + 4u; g_cpu.R[1] = used;
    g_cpu.R[2] = used ? 0u : 1u; g_cpu.R[3] = buffer;
    g_cpu.R[15] = kResume;
    packet_resume();
}

void packet_resume() {
    std::array<uint32_t, 64> words;
    for (;;) {
        uint32_t consumed = 0;
        if (g_cpu.R[2] == 1u) {
            consumed = nds_gpu3d_submit_words(0x04000400u, &g_cpu.R[6], 1);
            if (consumed) {
                g_cpu.R[0] = g_cpu.R[5]; g_cpu.R[1] = g_cpu.R[4];
                g_cpu.R[2] = 2u;
            }
        } else if (g_cpu.R[1]) {
            const uint32_t count = std::min<uint32_t>(words.size(), g_cpu.R[1]);
            for (uint32_t i = 0; i < count; ++i)
                words[i] = bus_read_u32((g_cpu.R[0] + i * 4u) & ~3u);
            consumed = nds_gpu3d_submit_words(0x04000400u, words.data(), count);
            g_cpu.R[0] += consumed * 4u;
            g_cpu.R[1] -= consumed;
        }
        counts.packet_words += consumed;
        ++counts.packet_batches;
        if (g_cpu.R[2] == 0u && !g_cpu.R[1]) {
            nds_hle::store(g_cpu.R[3], 0);
            g_cpu.R[2] = 1u;
        } else if (g_cpu.R[2] == 2u && !g_cpu.R[1]) {
            const uint32_t sp = g_cpu.R[13];
            g_cpu.R[4] = nds_hle::load(sp); g_cpu.R[5] = nds_hle::load(sp + 4u);
            g_cpu.R[6] = nds_hle::load(sp + 8u); g_cpu.R[14] = nds_hle::load(sp + 12u);
            g_cpu.R[13] += 16u;
            nds_hle::finish(8u + consumed * 4u);
            return;
        }
        g_cpu.R[15] = kResume;
        runtime_tick(8u + consumed * 4u);
        if (runtime_unwinding()) { ++counts.packet_waits; return; }
        if (nds_gxfifo_stalled()) {
            ++counts.packet_waits;
            nds_reschedule_slice(g_runtime_cycles >> 1u);
            g_nds_unwinding = 1;
            return;
        }
        if (runtime_should_yield() || runtime_slice_yield()) {
            ++counts.packet_waits;
            return;
        }
    }
}
#endif

} // namespace mkds_hle
