#include "spu.h"
#include "savestate.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {
std::vector<std::pair<uint32_t, uint32_t>> writes;
bool ok = true;
void expect(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ok = false; }
}
NdsSpuSaveState state() {
    NdsSpuSaveState out{};
    expect(spu_savestate_export(&out), "export real SPU");
    return out;
}
void write(uint32_t address, uint32_t value, unsigned width = 4) {
    nds_spu_write(address, value, width);
}
} // namespace

uint32_t bus_device_read32(int, uint32_t) { return 0x34343434u; }
void bus_device_write32(int cpu, uint32_t address, uint32_t value) {
    expect(cpu == 7 && address >= 0x02200000u && address < 0x02200400u,
           "capture stays inside synthetic guest destination");
    writes.emplace_back(address, value);
}

int main() {
    nds_spu_reset();
    write(0x04000500u, 0x807fu, 2);
    write(0x04000404u, 0x02100000u);
    write(0x04000408u, 0xfe00u, 2);
    write(0x0400040cu, 64);
    write(0x04000400u, 0xa800037fu); // looping PCM16, nonzero left mix
    write(0x04000418u, 0xfe00u, 2); // capture 0 uses voice 1's reload
    nds_tick_spu(8 * 1024);
    write(0x04000510u, 0x02200000u);
    write(0x04000514u, 0x100u, 2);
    write(0x04000508u, 0x88u, 1); // running 8-bit capture
    nds_tick_spu(23 * 1024);
    const auto before = state();
    expect(before.capture[0].fifo_write == 15 && writes.empty(), "15 bytes queued before switch");
    write(0x04000508u, 0x80u, 1); // change to 16-bit without disabling
    nds_tick_spu(24 * 1024);
    const auto saved = state();
    const auto& capture = saved.capture[0];
    expect(capture.fifo_read == 0 && capture.fifo_write == 1 && capture.fifo_level == 1,
           "straddling sample wraps without corrupting read cursor");
    expect(writes.size() == 4, "first sixteen bytes flush as four guest words");
    const uint16_t sample = static_cast<uint16_t>(saved.channel[0].sample >> 4);
    std::array<uint8_t, 16> expected{};
    std::memcpy(expected.data(), before.capture[0].fifo, 15);
    expected[15] = static_cast<uint8_t>(sample);
    for (unsigned i = 0; i < 4 && i < writes.size(); ++i) {
        uint32_t word = 0; std::memcpy(&word, expected.data() + i * 4, 4);
        expect(writes[i] == std::make_pair(0x02200000u + i * 4, word),
               "guest receives only queued sample bytes in order");
    }
    expect(capture.fifo[0] == sample >> 8, "high byte wraps to start of FIFO");
    std::string error;
    expect(spu_savestate_validate(saved, &error), "odd 16-bit write cursor is now safe to save");
    writes.clear(); nds_tick_spu(40 * 1024);
    const auto reference = writes;
    expect(spu_savestate_import(saved, &error), "restore wrapped capture");
    writes.clear(); nds_tick_spu(40 * 1024);
    expect(writes == reference && !writes.empty(), "restored capture writes identical guest bytes");
    auto malformed = saved;
    malformed.capture[0].fifo_read = 16;
    expect(!spu_savestate_validate(malformed, &error), "out-of-range read cursor still rejected");
    malformed = saved; malformed.capture[0].fifo_read = 1;
    expect(!spu_savestate_validate(malformed, &error), "unaligned flush cursor still rejected");
    if (ok) std::puts("capture format switch wraps sample bytes safely");
    return ok ? 0 : 1;
}
