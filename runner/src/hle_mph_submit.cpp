#include "hle_mph_submit.h"
#include "hle_vector_math.h"
#include "io.h"
#include "state.h"
#include "sha1.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace mph_submit {
namespace {
constexpr uint32_t start = 0x01FF9420u, length = 0xFCu;
constexpr uint32_t entry = 0x01FF9470u;
constexpr uint32_t wait_before = 0x01FF94ACu;
constexpr uint32_t next_chunk = 0x01FF94CCu;
constexpr uint32_t wait_after = 0x01FF94F8u;
constexpr uint32_t dma_base = 0x040000B0u;
std::array<uint8_t, length> proof{};
std::array<uint8_t, 44> irq_proof{};
std::array<uint8_t, 112> argument_proof{};
const NdsStaticValidationRange ranges[] = {
    {start, length, proof.data()},
    {0x020882E0u, 44, irq_proof.data()},
    {0x02088728u, 112, argument_proof.data()},
};
NdsStaticValidation validation{start, length, proof.data(), ranges, 3};
Statistics counts{};

void program(uint32_t channel, uint32_t source, uint32_t dest, uint32_t control) {
    const uint32_t regs = dma_base + channel * 12u;
    // A single native operation owns the IRQ-protected register transaction.
    // DMA starts through the normal device path and is still advanced by the
    // scheduler. No command data is consumed early and no completion is invented.
    nds_io_write(regs, source, 4);
    nds_io_write(regs + 4u, dest, 4);
    nds_io_write(regs + 8u, control, 4);
}

bool boundary(uint32_t pc, uint32_t cycles) {
    g_cpu.R[15] = pc;
    runtime_tick(cycles);
    return runtime_unwinding() || runtime_should_yield();
}

void completed() {
    const uint32_t sp = g_cpu.R[13];
    // The resumable service uses the original guest frame, so IRQs, context
    // switches and same-build saves need no host pointer or private coroutine.
    for (unsigned r = 4; r <= 11; ++r)
        g_cpu.R[r] = nds_hle::load(sp + 4u + (r - 4u) * 4u);
    g_cpu.R[14] = nds_hle::load(sp + 36u);
    g_cpu.R[13] = sp + 40u;
    g_cpu.R[0] = 0;
    g_cpu.cpsr = (g_cpu.cpsr & ~(CPSR_N_BIT | CPSR_Z_BIT | CPSR_C_BIT)) |
                 CPSR_Z_BIT | CPSR_C_BIT;
    ++counts.completions;
    nds_hle::finish(16);
}
} // namespace

bool initialize(const char* rom_sha1) {
    counts = {};
    counts.rom_supported = rom_sha1 &&
        std::strcmp(rom_sha1, "90164d1ac127ee5f9815ea4ae7de798c7b5fc629") == 0;
    return counts.rom_supported;
}
Statistics statistics() { return counts; }

bool is_entry(NdsCpu cpu, uint32_t pc, bool thumb) {
    return counts.rom_supported && cpu == NDS_ARM9 && !thumb &&
        (pc == start || pc == entry || pc == wait_before ||
         pc == next_chunk || pc == wait_after);
}

Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb) {
    if (!is_entry(cpu, pc, thumb)) return {};
    std::array<uint8_t, length> live{};
    if (!bus_range_has_write_provenance(start, length) ||
        !bus_debug_copy(0, start, live.data(), length) ||
        gba::sha1(live.data(), length).hex() !=
            "37571edeb9fb5166d791281d814b662d138b6ef4")
        return {nullptr, &validation, start, length};
    std::array<uint8_t, 44> irq{};
    std::array<uint8_t, 112> argument{};
    if (!bus_range_has_write_provenance(0x020882E0u, irq.size()) ||
        !bus_range_has_write_provenance(0x02088728u, argument.size()) ||
        !bus_debug_copy(0, 0x020882E0u, irq.data(), irq.size()) ||
        !bus_debug_copy(0, 0x02088728u, argument.data(), argument.size()) ||
        gba::sha1(irq.data(), irq.size()).hex() != "0cb9faca9c3d4a39fa8284c8db115abfa995de90" ||
        gba::sha1(argument.data(), argument.size()).hex() != "bae2b2b2179e8f36cadb4a41dfc2ad87eb1c22cb")
        return {nullptr, &validation, start, length};
    proof = live;
    irq_proof = irq;
    argument_proof = argument;
    return {pc == start ? setup : submit, &validation, start, length};
}

void setup() {
    const uint32_t channel = g_cpu.R[0];
    if (channel >= 4u) { nds_halt("MPH HLE: invalid DMA channel"); return; }
    ++counts.setup_calls;
    program(channel, g_cpu.R[1], g_cpu.R[2], g_cpu.R[3]);
    // Preserve the inexpensive final scratch values of the IRQ wrapper.
    g_cpu.R[0] = CPSR_I_BIT;
    g_cpu.R[1] = g_cpu.cpsr | CPSR_I_BIT;
    g_cpu.R[2] = g_cpu.cpsr;
    g_cpu.R[3] = channel * 12u;
    nds_hle::finish(80);
}

void submit() {
    if (g_cpu.R[15] == entry) {
        ++counts.submit_calls;
        const uint32_t channel = g_cpu.R[0], source = g_cpu.R[1], size = g_cpu.R[2];
        if (!size) {
            g_cpu.cpsr = (g_cpu.cpsr & ~(CPSR_N_BIT | CPSR_Z_BIT)) | CPSR_Z_BIT;
            nds_hle::finish(8);
            return;
        }
        // This interface submits aligned command buffers from ordinary RAM.
        // Non-RAM/overflowing ranges are not treated as successful transfers.
        if (channel >= 4u || ((source | size) & 3u) ||
            source < 0x02000000u || source >= 0x03000000u ||
            size > 0x03000000u - source) {
            nds_halt("MPH HLE: unsupported geometry buffer"); return;
        }
        const uint32_t sp = g_cpu.R[13] - 40u;
        for (unsigned r = 4; r <= 11; ++r)
            nds_hle::store(sp + 4u + (r - 4u) * 4u, g_cpu.R[r]);
        nds_hle::store(sp + 36u, g_cpu.R[14]);
        g_cpu.R[13] = sp;
        g_cpu.R[8] = size;
        g_cpu.R[9] = source;
        g_cpu.R[10] = channel;
        g_cpu.R[7] = dma_base + channel * 12u + 8u;
        g_cpu.R[6] = 0; // no owned batch awaiting final register publication
        if (boundary(wait_before, 48)) return;
    }

    for (;;) {
        // Both wait points observe the real DMA status. Resume PCs are validated
        // dispatch entries with this build's own guest-register continuation.
        if (g_cpu.R[15] == wait_before || g_cpu.R[15] == wait_after) {
            const uint32_t pc = g_cpu.R[15];
            if (nds_io_read(g_cpu.R[7], 4) & 0x80000000u) {
                ++counts.waits;
                // Advance only to the already published CPU rendezvous. The
                // scheduler still services ARM7, timers and Wi-Fi deadlines.
                // A zero/invalidated fast limit requires an ordinary small step.
                const uint64_t left = g_nds_fast_limit > g_runtime_cycles
                    ? g_nds_fast_limit - g_runtime_cycles : 16u;
                if (boundary(pc, static_cast<uint32_t>(std::min(left, uint64_t{128})))) return;
                continue;
            }
            if (pc == wait_after) {
                // Expose the last retail 118-word chunk's completed channel
                // registers, without replaying its data or arming another DMA.
                program(g_cpu.R[10], g_cpu.R[5], 0x04000400u, g_cpu.R[4]);
                completed(); return;
            }
            g_cpu.R[15] = next_chunk;
        }
        if (g_cpu.R[15] != next_chunk) {
            nds_halt("MPH HLE: invalid submission continuation"); return;
        }
        if (g_cpu.R[6]) {
            program(g_cpu.R[10], g_cpu.R[5], 0x04000400u, g_cpu.R[4]);
            g_cpu.R[6] = 0;
        }
        // One contiguous native DMA owns many retail chunk services. Bound
        // very large buffers to a multiple of 118 below the 16-bit burst
        // index wrap; this also keeps all DMA count fields representable.
        const uint32_t bytes = std::min(g_cpu.R[8], 472u * 128u);
        if (!bytes) { completed(); return; }
        const uint32_t last_bytes = ((bytes - 4u) % 472u) + 4u;
        g_cpu.R[4] = 0x04400000u | (last_bytes >> 2u);
        g_cpu.R[5] = g_cpu.R[9] + bytes - last_bytes;
        g_cpu.R[6] = bytes;
        g_cpu.R[11] = 0x04000400u;
        program(g_cpu.R[10], g_cpu.R[9], g_cpu.R[11], 0x84400000u | (bytes >> 2u));
        counts.chunks += (bytes + 471u) / 472u;
        ++counts.dma_batches;
        counts.bytes += bytes;
        g_cpu.R[8] -= bytes;
        g_cpu.R[9] += bytes;
        g_cpu.R[14] = 0x01FF94ECu;
        // Keep ABI scratch effects of the nested setup routine cheap, too.
        g_cpu.R[0] = CPSR_I_BIT;
        g_cpu.R[1] = g_cpu.cpsr | CPSR_I_BIT;
        g_cpu.R[2] = g_cpu.cpsr;
        g_cpu.R[3] = g_cpu.R[10] * 12u;
        // DMA owns the CPU until its real completion. Publish the post-submit
        // continuation before yielding; neither a replay nor a duplicate chunk
        // occurs when an IRQ/context switch interrupts the wait.
        if (boundary(g_cpu.R[8] ? next_chunk : wait_after, 80)) return;
    }
}
} // namespace mph_submit
