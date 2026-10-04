#include "hle_mkds.h"
#include "hle_vector_math.h"
#include "state.h"
#include "sha1.h"

#include <array>
#include <cstring>

namespace mkds_hle {
namespace {
Statistics counts;
struct Binding {
    uint32_t entry, start, size;
    const char* sha1;
    Function function;
    std::array<uint8_t, 180> bytes{};
    NdsStaticValidation validation{};
};
// ROM hashes only; the proof bytes are read from the verified resident image.
Binding bindings[] = {
    {0x02147FD8, 0x02147FD8, 96, "a62a720989cd408241a1acc35520b93d866bf7df", scale_add},
    {0x021483A0, 0x021483A0, 156, "8a97f6bdacd40175ea4f577788a82635a8057984", cross_product},
    {0x02147288, 0x02147288, 180, "fe182d57e45fd6086b43f0b98ac689d14b815365", transform_translate},
};
} // namespace

bool initialize(const char* rom_sha1) {
    counts = {};
    counts.group_mask = 1; // only the three vector kernels are integrated
    counts.rom_supported = rom_sha1 &&
        std::strcmp(rom_sha1, "691e00d9a5dd80b04f80cc7559503e8b06848785") == 0;
    return counts.rom_supported;
}
Statistics statistics() { return counts; }

Replacement resolve(NdsCpu cpu, uint32_t pc, bool thumb) {
    if (!counts.rom_supported || cpu != NDS_ARM9 || thumb) return {};
    for (auto& b : bindings) {
        if (!b.function || pc != b.entry) continue;
        std::array<uint8_t, 180> live{};
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

} // namespace mkds_hle
