#include "dma_ram_batch.h"
#include "runtime_arm.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

static int failures;
static void check(bool yes, const char* what) {
    if (!yes) { std::fprintf(stderr, "FAIL %s\n", what); ++failures; }
}
static void reference(int cpu, uint32_t width, uint64_t target, uint32_t& src,
                      uint32_t& dst, uint32_t& remaining, uint16_t& index,
                      bool& start, uint64_t& clock) {
    while (remaining && clock < target) {
        clock += (width == 4u ? 18u : 16u) << (cpu == 0 ? 1u : 0u);
        if (width == 4) bus_write_u32(dst & ~3u, bus_read_u32(src & ~3u));
        else bus_write_u16(dst & ~1u, bus_read_u16(src & ~1u));
        src += width; dst += width; --remaining; ++index; start = false;
    }
}
int main(int argc, char**) {
    bus_init(); cp15_reset();
    for (int cpu = 0; cpu < 2; ++cpu) for (uint32_t width : {2u, 4u}) {
        g_nds_active = static_cast<NdsCpu>(cpu);
        for (unsigned i = 0; i < 4096; ++i) bus_write_u8(0x02000000u + i, static_cast<uint8_t>(i * 37));
        for (uint64_t target : {uint64_t{251}, uint64_t{10000}, UINT64_MAX}) {
            uint32_t s = 0x02000001u, d = 0x02010001u, n = 512;
            uint16_t index = 65530; bool start = true; uint64_t clock = 7;
            uint32_t rs = s, rd = 0x02020001u, rn = n;
            uint16_t ri = index; bool rb = start; uint64_t rc = clock;
            check(dma_ram_batch(cpu, width, target, s, d, n, index, start, clock), "batch accepted");
            reference(cpu, width, target, rs, rd, rn, ri, rb, rc);
            check(s == rs && d - 0x02010000u == rd - 0x02020000u && n == rn &&
                  index == ri && start == rb && clock == rc, "continuation and time");
            for (unsigned i = 0; i < (512u - n) * width; ++i)
                check(bus_read_u8(0x02010000u + i) == bus_read_u8(0x02020000u + i), "copied bytes");
            check(bus_range_has_write_provenance(0x02010000u, (512u - n) * width), "DMA provenance");
            check(bus_exec_page_generation(0x02010000u) != 0, "code generation invalidation");
        }
    }
    g_nds_active = NDS_ARM9;
    check(!bus_dma_copy_main_ram(0x02000000u, 0x02400000u, 64), "physical mirror overlap rejected");
    check(!bus_dma_copy_main_ram(0x023FFFF0u, 0x02010000u, 64), "mirror boundary rejected");
    check(!bus_dma_copy_main_ram(0x04000000u, 0x02010000u, 64), "MMIO rejected");
    g_cp15.dtcm_enable = true; g_cp15.dtcm_base = 0x02010000; g_cp15.dtcm_size = 0x4000;
    check(!bus_dma_copy_main_ram(0x02000000u, 0x0200FFF0u, 64), "interior TCM overlap rejected");
    cp15_reset();
    if (argc > 1 && !failures) {
        for (unsigned i = 0; i < 65536; ++i)
            bus_write_u8(0x02000000u + i, static_cast<uint8_t>(i * 37));
        constexpr unsigned transfers = 2000000;
        const auto begin = std::chrono::steady_clock::now();
        for (unsigned bytes : {64u, 1024u, 65536u}) {
            const unsigned count = bytes == 64 ? 1500000 : (bytes == 1024 ? 437500 : 62500);
            const auto size_begin = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < count; ++i) {
            uint32_t s = 0x02000000u, d = 0x02010000u, n = bytes / 4;
            uint16_t index = 0; bool start = true; uint64_t clock = 0;
#if defined(NDS_DMA_RAM_HLE)
            if (!dma_ram_batch(0, 4, UINT64_MAX, s, d, n, index, start, clock)) return 2;
#else
            reference(0, 4, UINT64_MAX, s, d, n, index, start, clock);
#endif
            }
            std::printf("component_size bytes=%u transfers=%u seconds=%.6f\n", bytes, count,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - size_begin).count());
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        uint64_t digest = 1469598103934665603ULL;
        for (unsigned i = 0; i < 65536; ++i) {
            const uint8_t value = bus_read_u8(0x02010000u + i);
            check(value == bus_read_u8(0x02000000u + i), "probe output bytes");
            digest = (digest ^ value) * 1099511628211ULL;
        }
        std::printf("component_probe transfers=%u sizes=64,1024,65536 counts=1500000,437500,62500 seconds=%.6f digest=%llu\n",
            transfers, seconds, static_cast<unsigned long long>(digest));
    }
    std::printf("dma_ram_batch_test failures=%d\n", failures);
    return failures ? 1 : 0;
}
