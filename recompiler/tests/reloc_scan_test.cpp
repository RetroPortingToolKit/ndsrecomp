#include "reloc_scan.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr uint32_t base = 0x02000000;
constexpr uint32_t low = 0x01FF8FE0;
constexpr uint32_t high = 0x03800000;

void word(std::vector<uint8_t>& image, unsigned offset, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        image[offset + i] = static_cast<uint8_t>(value >> (8 * i));
}

bool expect(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}
} // namespace

int main() {
    std::vector<uint8_t> image(0x400, 0);
    for (unsigned i = 0; i < 128; ++i)
        image[0x100 + i] = static_cast<uint8_t>((i * 37) ^ (i >> 2));

    unsigned pc = 0;
    auto emit = [&](uint32_t instruction) {
        word(image, pc, instruction);
        pc += 4;
    };
    auto literal = [&](unsigned reg, unsigned offset) {
        emit(0xE59F0000u | (reg << 12) | (offset - pc - 8));
    };
    auto copy = [&](unsigned source, unsigned destination) {
        literal(0, source);
        literal(1, destination);
        emit(0xE3A02020);                 // mov r2, #32
        emit(0xE4903004);                 // ldr r3, [r0], #4
        emit(0xE4813004);                 // str r3, [r1], #4
        emit(0xE2522001);                 // subs r2, r2, #1
        emit(0x1AFFFFFB);                 // bne copy loop
    };
    // Write in reverse address order, then overwrite an existing region.
    copy(0x80, 0x88);
    copy(0x88, 0x84);                     // read back the shadow copy
    copy(0x80, 0x88);
    literal(1, 0x8C);
    emit(0xE3A03000);                     // mov r3, #0
    emit(0xE5C13000);                     // strb r3, [r1] (a sparse zero)
    literal(0, 0x80);
    emit(0xE3A030AB);                     // mov r3, #0xAB
    emit(0xE5C03000);                     // strb r3, [r0] (inside image)
    emit(0xE5D03000);                     // ldrb r3, [r0] (shadow wins)
    emit(0xE35300AB);                     // cmp r3, #0xAB
    emit(0x1F000000);                     // swine #0 (wrong read stops here)
    emit(0xE12FFF1E);                     // bx lr (leave image)
    word(image, 0x80, base + 0x100);
    word(image, 0x84, low);
    word(image, 0x88, high);
    word(image, 0x8C, high + 0x1000);

    ndsrecomp::RelocScanStats stats;
    const auto copies = ndsrecomp::scan_relocations(
        image.data(), image.size(), base, base, &stats);
    bool ok = expect(copies.size() == 2, "two unique copied regions");
    if (copies.size() == 2) {
        ok &= expect(copies[0].runtime_start == low &&
                     copies[1].runtime_start == high,
                     "copies stay ordered independently of write order");
        for (const auto& c : copies)
            ok &= expect(c.source_start == base + 0x100 && c.size == 128,
                         "exact source and extent");
    }
    ok &= expect(stats.shadow_bytes == 258, "overwrites count once; zero is written");
    ok &= expect(stats.regions_total == 3 && stats.regions_matched == 2 &&
                 stats.regions_unmatched == 1 && stats.regions_ambiguous == 0,
                 "sparse writes stay separate; in-image writes are excluded");
    ok &= expect(stats.unmapped_reads == 0 && stats.stop_reason == "pc-left-module",
                 "shadow reads override image without unmapped reads");
    return ok ? 0 : 1;
}
