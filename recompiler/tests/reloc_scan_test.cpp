#include "reloc_scan.h"

#include <cstdint>
#include <cstdio>
#include <new>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#endif

// Address/thread/memory sanitizers reserve large virtual address ranges.
// Keep semantic coverage there without imposing the normal-build AS limit.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define NDS_RELOC_TEST_VIRTUAL_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
    __has_feature(memory_sanitizer)
#define NDS_RELOC_TEST_VIRTUAL_SANITIZER 1
#endif
#endif

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

bool sparse_writes() {
#if defined(__linux__) && !defined(NDS_RELOC_TEST_VIRTUAL_SANITIZER)
    // One byte per page must not allocate hundreds of MiB. This catches
    // eager dense-page allocation; other hosts still check scan semantics.
    rlimit limit{};
    if (!expect(getrlimit(RLIMIT_AS, &limit) == 0, "read memory limit"))
        return false;
    constexpr rlim_t budget = 256u * 1024u * 1024u;
    if (limit.rlim_cur == RLIM_INFINITY || limit.rlim_cur > budget) {
        limit.rlim_cur = budget;
        if (!expect(setrlimit(RLIMIT_AS, &limit) == 0, "set memory limit"))
            return false;
    }
#endif
    // 65,536 STRB writes, one per page, with no relocatable content.
    const uint32_t program[] = {
        0xE59F001C, 0xE59F101C, 0xE3A02000, 0xE5C02000,
        0xE2800A01, 0xE2511001, 0x1AFFFFFB, 0xE12FFF1E,
        0, 0x10000000, 65536,
    };
    std::vector<uint8_t> image(sizeof(program));
    for (unsigned i = 0; i < sizeof(program) / sizeof(program[0]); ++i)
        word(image, i * 4, program[i]);
    ndsrecomp::RelocScanStats stats;
    try {
        const auto copies = ndsrecomp::scan_relocations(
            image.data(), image.size(), base, base, &stats);
        return expect(copies.empty() && stats.shadow_bytes == 65536 &&
                      stats.regions_total == 65536 &&
                      stats.stop_reason == "pc-left-module",
                      "all sparse writes retained within memory budget");
    } catch (const std::bad_alloc&) {
        return expect(false, "sparse scan exceeded memory budget");
    }
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
    // First make the code's own page dense: later instruction fetches must
    // still fall back to the image for bytes the scan never wrote.
    copy(0xC0, 0xD0);
    // Write in reverse address order, then overwrite an existing region.
    copy(0xC0, 0xC8);
    copy(0xC8, 0xC4);                     // read back the shadow copy
    copy(0xC0, 0xC8);
    literal(1, 0xCC);
    emit(0xE3A03000);                     // mov r3, #0
    emit(0xE5C13000);                     // strb r3, [r1] (a sparse zero)
    literal(1, 0xD4);
    emit(0xE3A03011);                     // mov r3, #0x11
    emit(0xE5613001);                     // strb r3, [r1, #-1]! (descending)
    emit(0xE5613001);                     // strb r3, [r1, #-1]!
    emit(0xE3A03022);                     // mov r3, #0x22
    emit(0xE5C13000);                     // strb r3, [r1] (sparse overwrite)
    emit(0xE5D12001);                     // ldrb r2, [r1, #1]
    emit(0xE3520011);                     // cmp r2, #0x11
    emit(0x1F000000);                     // swine #0 (lost sparse byte)
    literal(0, 0xC0);
    emit(0xE3A030AB);                     // mov r3, #0xAB
    emit(0xE5C03000);                     // strb r3, [r0] (inside image)
    emit(0xE5D03000);                     // ldrb r3, [r0] (shadow wins)
    emit(0xE35300AB);                     // cmp r3, #0xAB
    emit(0x1F000000);                     // swine #0 (wrong read stops here)
    emit(0xE12FFF1E);                     // bx lr (leave image)
    word(image, 0xC0, base + 0x100);
    word(image, 0xC4, low);
    word(image, 0xC8, high);
    word(image, 0xCC, high + 0x1000);
    word(image, 0xD0, base + 0x200);
    word(image, 0xD4, high + 0x2002);

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
    ok &= expect(pc <= 0xC0, "fixture code stays below its literal pool");
    ok &= expect(stats.shadow_bytes == 388, "overwrites count once; zero is written");
    ok &= expect(stats.regions_total == 4 && stats.regions_matched == 2 &&
                 stats.regions_unmatched == 2 && stats.regions_ambiguous == 0,
                 "sparse writes stay separate; in-image writes are excluded");
    ok &= expect(stats.unmapped_reads == 0 && stats.stop_reason == "pc-left-module",
                 "shadow reads override image without unmapped reads");
    ok &= sparse_writes();
    return ok ? 0 : 1;
}
