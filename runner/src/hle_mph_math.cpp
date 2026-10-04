#include "hle_mph_math.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "sha1.h"

namespace mph_hle {
namespace {
bool supported_rom = false;

struct Binding {
    uint32_t pc;
    uint32_t size;
    const char* sha1;
    Function function;
};
// Fingerprints, not ROM payloads. Only full routine entries are replaced.
constexpr Binding bindings[] = {
    {0x02080BD8u, 96u, "a62a720989cd408241a1acc35520b93d866bf7df", scale_add},
    {0x02080DE0u, 156u, "8a97f6bdacd40175ea4f577788a82635a8057984", cross_product},
    {0x0207FD38u, 180u, "fe182d57e45fd6086b43f0b98ac689d14b815365", transform_translate},
};

uint32_t load(uint32_t address) {
    return std::rotr(bus_read_u32(address & ~3u), (address & 3u) * 8u);
}

void store(uint32_t address, uint32_t value) {
    bus_write_u32(address & ~3u, value);
}

uint64_t product(uint32_t a, uint32_t b) {
    // Products fit signed 64 bits; unsigned accumulation deliberately wraps.
    return static_cast<uint64_t>(int64_t{std::bit_cast<int32_t>(a)} *
                                 int64_t{std::bit_cast<int32_t>(b)});
}

uint32_t q12(uint64_t value) {
    return static_cast<uint32_t>(value >> 12u);
}

void finish(uint32_t cycles) {
    const uint32_t target = g_cpu.R[14];
    g_cpu.R[15] = target & ((target & 1u) ? ~1u : ~3u);
    g_cpu.cpsr = (g_cpu.cpsr & ~CPSR_T_BIT) |
                 ((target & 1u) ? CPSR_T_BIT : 0u);
    // The operation is atomic. Publish its completed return state before a
    // single approximate timing charge/IRQ boundary. No guest instruction loop
    // or live handoff to an LLE body is involved.
    runtime_tick(cycles);
    if (runtime_unwinding()) return;
    if (runtime_call_should_return(g_cpu.R[15])) return;
    runtime_dispatch_with_exchange(target);
}
} // namespace

bool initialize(const char* rom_sha1) {
    supported_rom = rom_sha1 &&
        std::strcmp(rom_sha1, "90164d1ac127ee5f9815ea4ae7de798c7b5fc629") == 0;
    return supported_rom;
}

Function resolve(NdsCpu cpu, uint32_t pc, bool thumb,
                 const NdsStaticValidation* validation) {
    if (!supported_rom || cpu != NDS_ARM9 || thumb || !validation ||
        !validation->expected || pc < validation->addr) return nullptr;
    for (const auto& binding : bindings) {
        if (binding.pc != pc) continue;
        const uint32_t offset = pc - validation->addr;
        if (offset > validation->size ||
            binding.size > validation->size - offset) return nullptr;
        if (gba::sha1(validation->expected + offset, binding.size).hex() !=
            binding.sha1) return nullptr;
        return binding.function;
    }
    return nullptr;
}

void scale_add() {
    const uint32_t scale = g_cpu.R[0];
    const uint32_t vector = g_cpu.R[1];
    const uint32_t addend = g_cpu.R[2];
    const uint32_t out = g_cpu.R[3];
    uint64_t last_product = 0;
    uint32_t last_addend = 0, result = 0;
    // Keep per-component load/store ordering for overlapping vector buffers.
    for (uint32_t offset = 0; offset != 12u; offset += 4u) {
        const uint32_t value = load(vector + offset);
        last_addend = load(addend + offset);
        last_product = product(scale, value);
        result = q12(last_product) + last_addend;
        store(out + offset, result);
    }
    g_cpu.R[0] = result;
    g_cpu.R[1] = static_cast<uint32_t>(last_product >> 32u);
    g_cpu.R[2] = static_cast<uint32_t>(last_product);
    g_cpu.R[12] = last_addend;
    finish(96u);
}

void cross_product() {
    const uint32_t a = g_cpu.R[0], b = g_cpu.R[1], out = g_cpu.R[2];
    const uint32_t ax = load(a), ay = load(a + 4u), az = load(a + 8u);
    const uint32_t bx = load(b), by = load(b + 4u), bz = load(b + 8u);
    const uint64_t ax_bz = product(ax, bz), ay_bx = product(ay, bx);
    const uint64_t x = product(ay, bz) - product(az, by) + 0x800u;
    const uint64_t y = product(az, bx) - ax_bz + 0x800u;
    const uint64_t z_before_round = product(ax, by) - ay_bx;
    const uint64_t z = z_before_round + 0x800u;
    store(out, q12(x));
    store(out + 4u, q12(y));
    store(out + 8u, q12(z));
    g_cpu.R[0] = static_cast<uint32_t>(z >> 32u);
    g_cpu.R[1] = q12(z);
    g_cpu.R[3] = static_cast<uint32_t>(ay_bx >> 32u);
    g_cpu.R[12] = static_cast<uint32_t>(ax_bz >> 32u);
    // Retaining the final flags is cheap; intermediate ALU states disappear.
    const uint32_t low = static_cast<uint32_t>(z_before_round);
    const uint32_t rounded = low + 0x800u;
    uint32_t flags = rounded & CPSR_N_BIT;
    if (rounded == 0u) flags |= CPSR_Z_BIT;
    if (rounded < low) flags |= CPSR_C_BIT;
    if ((~(low ^ 0x800u) & (low ^ rounded)) & CPSR_N_BIT) flags |= CPSR_V_BIT;
    g_cpu.cpsr = (g_cpu.cpsr & ~(CPSR_N_BIT | CPSR_Z_BIT | CPSR_C_BIT | CPSR_V_BIT)) | flags;
    finish(160u);
}

void transform_translate() {
    const uint32_t vector = g_cpu.R[0], matrix = g_cpu.R[1], out = g_cpu.R[2];
    const uint32_t x = load(vector), y = load(vector + 4u), z = load(vector + 8u);
    uint32_t before_translation = 0, result = 0;
    for (uint32_t offset = 0; offset != 12u; offset += 4u) {
        const uint64_t sum = product(x, load(matrix + offset)) +
                             product(y, load(matrix + 12u + offset)) +
                             product(z, load(matrix + 24u + offset));
        before_translation = q12(sum);
        // This intermediate store is observable when out aliases the matrix.
        store(out + offset, before_translation);
        // The original reload rotates an unaligned output word. Ordinary
        // writable RAM lets us derive that value without reading it again.
        before_translation = std::rotr(before_translation, (out & 3u) * 8u);
        result = before_translation + load(matrix + 36u + offset);
        store(out + offset, result);
    }
    g_cpu.R[0] = result;
    g_cpu.R[3] = before_translation;
    g_cpu.R[12] = y;
    finish(160u);
}
} // namespace mph_hle
