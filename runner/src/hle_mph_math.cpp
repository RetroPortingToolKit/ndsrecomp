#include "hle_mph_math.h"
#include "hle_vector_math.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "sha1.h"

namespace mph_hle {
namespace {
bool supported_rom = false;
Statistics counters{};

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

} // namespace

bool initialize(const char* rom_sha1) {
    supported_rom = rom_sha1 &&
        std::strcmp(rom_sha1, "90164d1ac127ee5f9815ea4ae7de798c7b5fc629") == 0;
    counters = {supported_rom, 0, 0, 0};
    return supported_rom;
}

Statistics statistics() { return counters; }

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

void scale_add() { ++counters.scale_add_calls; nds_hle::scale_add(); }
void cross_product() { ++counters.cross_product_calls; nds_hle::cross_product(); }
void transform_translate() { ++counters.transform_translate_calls; nds_hle::transform_translate(); }
} // namespace mph_hle
