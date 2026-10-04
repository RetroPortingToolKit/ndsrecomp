#include "hle_mkds.h"
#include "hle_vector_math.h"
#include "gpu3d.h"
#include "io.h"
#include "state.h"
#include "sha1.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#ifndef NDS_MKDS_HLE_GROUP_MASK
#define NDS_MKDS_HLE_GROUP_MASK 7
#endif

namespace mkds_hle {
namespace {
Statistics counts;
struct Binding {
    uint32_t entry, start, size;
    const char* sha1;
    Function function;
    std::array<uint8_t, 364> bytes{};
    NdsStaticValidation validation{};
};
// ROM hashes only; the proof bytes are read from the verified resident image.
Binding bindings[] = {
#if NDS_MKDS_HLE_GROUP_MASK & 1
    {0x02147FD8, 0x02147FD8, 96, "a62a720989cd408241a1acc35520b93d866bf7df", scale_add},
    {0x021483A0, 0x021483A0, 156, "8a97f6bdacd40175ea4f577788a82635a8057984", cross_product},
    {0x02147288, 0x02147288, 180, "fe182d57e45fd6086b43f0b98ac689d14b815365", transform_translate},
#endif
#if NDS_MKDS_HLE_GROUP_MASK & 2
    {0x01FFCF7C, 0x01FFCF7C, 364, "7c90aa0fcd0833960463a030b6838a86571e4851", normalize},
    {0x02147E28, 0x02147E28, 60, "d3b343e50f4ec02eccd3bdd78190bcf798d2caee", divide_result},
#endif
#if NDS_MKDS_HLE_GROUP_MASK & 4
    {0x0214D044, 0x0214D044, 24, "ae6f870471f3f8365211f530ed2cfb372d22a9b4", send_words},
    // Explicit resumable loop ABI: r0=current source, r12=end, r2=last word.
    {0x0214D048, 0x0214D044, 24, "ae6f870471f3f8365211f530ed2cfb372d22a9b4", send_words},
#endif
    {0, 0, 0, "", nullptr}, // keeps the empty build selection well-formed
};

void add_flags(uint32_t a, uint32_t b) {
    const uint32_t v = a + b;
    uint32_t f = v & CPSR_N_BIT;
    if (!v) f |= CPSR_Z_BIT;
    if (v < a) f |= CPSR_C_BIT;
    if ((~(a ^ b) & (a ^ v)) & CPSR_N_BIT) f |= CPSR_V_BIT;
    g_cpu.cpsr = (g_cpu.cpsr & 0x0FFFFFFFu) | f;
}

void compare_flags(uint32_t a, uint32_t b) {
    const uint32_t v = a - b;
    uint32_t f = v & CPSR_N_BIT;
    if (!v) f |= CPSR_Z_BIT;
    if (a >= b) f |= CPSR_C_BIT;
    if (((a ^ b) & (a ^ v)) & CPSR_N_BIT) f |= CPSR_V_BIT;
    g_cpu.cpsr = (g_cpu.cpsr & 0x0FFFFFFFu) | f;
}
} // namespace

bool initialize(const char* rom_sha1) {
    counts = {};
    counts.group_mask = NDS_MKDS_HLE_GROUP_MASK;
    counts.rom_supported = rom_sha1 &&
        std::strcmp(rom_sha1, "691e00d9a5dd80b04f80cc7559503e8b06848785") == 0;
    return counts.rom_supported;
}
Statistics statistics() { return counts; }

Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb) {
    if (!counts.rom_supported || cpu != NDS_ARM9 || thumb) return {};
    for (auto& b : bindings) {
        if (!b.function || pc != b.entry) continue;
        std::array<uint8_t, 364> live{};
        if (!bus_range_has_write_provenance(b.start, b.size) ||
            !bus_debug_copy(0, b.start, live.data(), b.size) ||
            gba::sha1(live.data(), b.size).hex() != b.sha1)
            return {nullptr, nullptr, b.start, b.size};
        // Successful revalidation always copies the same fingerprinted bytes.
        // Their stable address may be retained by dispatch/link/active guards.
        b.bytes = live;
        b.validation = {b.start, b.size, b.bytes.data(), nullptr, 0};
        return {b.function, &b.validation, b.start, b.size};
    }
    return {};
}

void scale_add() { ++counts.scale_add; nds_hle::scale_add(); }
void cross_product() { ++counts.cross_product; nds_hle::cross_product(); }
void transform_translate() { ++counts.transform_translate; nds_hle::transform_translate(); }

void normalize() {
    ++counts.normalize;
    const uint32_t src = g_cpu.R[0], dst = g_cpu.R[1];
    uint64_t sum = 0;
    for (uint32_t i = 0; i < 16; i += 4) {
        const uint32_t v = nds_hle::load(src + i);
        sum += nds_hle::product(v, v);
    }
    // One native calculation per device. Publish operands/results/status for
    // callers that inspect the math registers after the operation returns.
    const uint64_t quotient = nds_math_hle_divide(2, uint64_t{1} << 56, sum);
    const uint32_t root = nds_math_hle_sqrt(1, sum << 2);
    const uint64_t factor = quotient *
        static_cast<uint64_t>(int64_t{std::bit_cast<int32_t>(root)});
    uint64_t p = 0;
    uint32_t input = 0, output = 0;
    for (uint32_t i = 0; i < 16; i += 4) {
        // Reload in original store order, including partial overlap.
        input = nds_hle::load(src + i);
        p = factor * static_cast<uint64_t>(int64_t{std::bit_cast<int32_t>(input)});
        const uint32_t rounded = static_cast<uint32_t>(p >> 32) + 0x1000u;
        output = static_cast<uint32_t>(std::bit_cast<int32_t>(rounded) >> 13);
        nds_hle::store(dst + i, output);
    }
    g_cpu.R[0] = output;
    g_cpu.R[2] = static_cast<uint32_t>(factor >> 32);
    g_cpu.R[3] = static_cast<uint32_t>(factor);
    g_cpu.R[12] = input;
    add_flags(static_cast<uint32_t>(p), 0);
    nds_hle::finish(240);
}

void divide_result() {
    ++counts.divide_result;
    const uint64_t result = nds_math_hle_complete_division();
    const uint64_t rounded = result + 0x80000u;
    g_cpu.R[0] = static_cast<uint32_t>(rounded >> 20);
    g_cpu.R[1] = static_cast<uint32_t>(rounded >> 32);
    g_cpu.R[2] = static_cast<uint32_t>(result);
    add_flags(static_cast<uint32_t>(result), 0x80000u);
    nds_hle::finish(80);
}

void send_words() {
    uint32_t source = g_cpu.R[0];
    if (g_cpu.R[15] == 0x0214D044u) {
        ++counts.send_calls;
        g_cpu.R[12] = source + g_cpu.R[2];
    }
    const uint32_t end = g_cpu.R[12], destination = g_cpu.R[1] & ~3u;
    for (;;) {
        if (std::bit_cast<int32_t>(source) >= std::bit_cast<int32_t>(end)) {
            compare_flags(source, end);
            nds_hle::finish(8);
            return;
        }
        // A batch has no persistent host-only continuation. At a yield the
        // guest registers describe the original loop, so IRQs and saves work.
        const uint64_t left = int64_t{std::bit_cast<int32_t>(end)} -
                              int64_t{std::bit_cast<int32_t>(source)};
        uint32_t words = static_cast<uint32_t>(std::min<uint64_t>(32, (left + 3) / 4));
        uint32_t consumed = 0;
        std::array<uint32_t, 32> data{};
        // Prefetch only ordinary main RAM; MMIO or other sources retain their
        // read/write ordering through the native one-word adapter below.
        if (destination >= 0x04000400u && destination < 0x04000440u &&
            source >= 0x02000000u && uint64_t{source} + words * 4 <= 0x02400000u) {
            for (uint32_t i = 0; i < words; ++i)
                data[i] = bus_read_u32((source + 4 * i) & ~3u);
            consumed = nds_gpu3d_submit_words(destination, data.data(), words);
            if (consumed) g_cpu.R[2] = data[consumed - 1];
        } else if (!nds_gxfifo_stalled()) {
            g_cpu.R[2] = bus_read_u32(source & ~3u); // LDM aligns, without rotation
            bus_write_u32(destination, g_cpu.R[2]);
            consumed = 1;
        }
        source += consumed * 4;
        counts.send_words += consumed;
        ++counts.send_batches;
        g_cpu.R[0] = source;
        g_cpu.R[15] = 0x0214D048u;
        runtime_tick(8 + consumed * 4); // one estimate per batch, not per word
        // Unwinding alone only returns to the scheduler's inner dispatch
        // loop. End this slice so it can drain the device before retrying.
        if (nds_gxfifo_stalled()) nds_reschedule_slice(g_runtime_cycles >> 1u);
        if (runtime_unwinding()) { ++counts.send_yields; return; }
        if (nds_gxfifo_stalled()) {
            g_nds_unwinding = 1;
            ++counts.send_yields;
            return;
        }
        if (runtime_should_yield() || runtime_slice_yield()) {
            ++counts.send_yields;
            return;
        }
    }
}
} // namespace mkds_hle
