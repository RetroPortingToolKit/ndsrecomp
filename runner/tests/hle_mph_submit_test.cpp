#include "hle_mph_submit.h"
#include "io.h"
#include "state.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t base = 0x02200000u, stack = base + 0x3FF00u;
std::array<uint32_t, 65536> ram{};
std::array<std::array<uint32_t, 3>, 4> dma{};
std::vector<uint32_t> output;
std::vector<uint8_t> code;
bool provenance = true, inject_irq = false, returned = false;
unsigned ticks = 0;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
uint32_t& word(uint32_t address) {
    require(address >= base && address - base < sizeof(ram) && !(address & 3u), "fixture RAM bounds");
    return ram[(address - base) / 4u];
}
void reset(uint32_t channel, uint32_t bytes) {
    ram = {}; dma = {}; output.clear(); ticks = 0; returned = false; inject_irq = false;
    g_cpu = {}; g_cpu.cpsr = 0x1Fu;
    for (unsigned r = 4; r <= 12; ++r) g_cpu.R[r] = 0x12340000u + r;
    g_cpu.R[0] = channel; g_cpu.R[1] = base; g_cpu.R[2] = bytes;
    g_cpu.R[13] = stack; g_cpu.R[14] = 0x020F0001u; g_cpu.R[15] = 0x01FF9470u;
    for (unsigned i = 0; i < ram.size() - 64u; ++i) ram[i] = i * 0x10203u;
    g_runtime_cycles = 0; g_nds_fast_limit = 128; g_nds_unwinding = 0;
}
void drain(unsigned channel) {
    auto& d = dma[channel];
    require(d[2] & 0x80000000u, "drain without pending transfer");
    const auto words = d[2] & 0x1FFFFFu;
    require(words && words <= 118u * 128u && d[1] == 0x04000400u, "wrong native transfer shape");
    for (uint32_t i = 0; i < words; ++i) output.push_back(word(d[0] + i * 4u));
    d[2] &= ~0x80000000u;
}
void finish(unsigned channel, bool snapshot_resume) {
    for (unsigned step = 0; step < 300 && !returned; ++step) {
        g_nds_unwinding = 0;
        g_nds_fast_limit = g_runtime_cycles + 128u;
        mph_submit::submit();
        if (snapshot_resume && step == 0) {
            // All continuation state must survive a plain CPU/RAM/device copy.
            const auto cpu = g_cpu; const auto memory = ram; const auto devices = dma;
            g_cpu = {}; ram = {}; dma = {};
            g_cpu = cpu; ram = memory; dma = devices;
        }
        if (dma[channel][2] & 0x80000000u) drain(channel);
    }
    require(returned, "service did not complete");
    require(g_cpu.R[13] == stack && g_cpu.R[15] == 0x020F0000u &&
            (g_cpu.cpsr & CPSR_T_BIT), "return SP/PC/interworking");
    for (unsigned r = 4; r <= 11; ++r)
        require(g_cpu.R[r] == 0x12340000u + r, "callee-saved register");
}
void tests() {
    mph_submit::initialize("90164d1ac127ee5f9815ea4ae7de798c7b5fc629");
    for (uint32_t pc : {0x01FF9420u, 0x01FF9470u, 0x01FF94ACu,
                        0x01FF94CCu, 0x01FF94F8u}) {
        require(mph_submit::is_entry(NDS_ARM9, pc, false), "submission barrier missing");
        require(!mph_submit::is_entry(NDS_ARM7, pc, false) &&
                !mph_submit::is_entry(NDS_ARM9, pc, true) &&
                !mph_submit::is_entry(NDS_ARM9, pc + 4, false), "submission barrier scope");
    }
    for (uint32_t bytes : {0u, 4u, 468u, 472u, 476u, 944u, 2048u, 60416u, 60892u, 121308u}) {
        for (unsigned channel = 0; channel < 4; ++channel) {
            reset(channel, bytes);
            // A prior transfer must complete before new source/length is published.
            if (bytes) { dma[channel] = {base + 3000u, 0x04000400u, 0x84400001u}; }
            finish(channel, true);
            const size_t prefix = bytes ? 1u : 0u;
            require(output.size() == prefix + bytes / 4u, "lost/duplicated words");
            for (unsigned i = 0; i < bytes / 4u; ++i)
                require(output[prefix + i] == i * 0x10203u, "command order/content");
            if (bytes) {
                const uint32_t last = ((bytes - 4u) % 472u) + 4u;
                require(dma[channel][0] == base + bytes - last &&
                        dma[channel][2] == (0x04400000u | (last / 4u)),
                        "last retail chunk registers not published at completion");
            }
        }
    }
    reset(2, 944);
    inject_irq = true;
    finish(2, false);
    require(output.size() == 236u, "post-operation unwind lost a chunk");

    reset(1, 476);
    mph_submit::submit();
    require(dma[1][2] & 0x80000000u, "DMA not armed");
    word(base) = 0xDEADBEEFu;
    drain(1);
    finish(1, false);
    require(output[0] == 0xDEADBEEFu, "source was copied before DMA consumed it");

    reset(0, 4); g_cpu.R[15] = 0x01FF9420u;
    g_cpu.R[2] = 0x04000400u; g_cpu.R[3] = 0x84400001u;
    mph_submit::setup();
    require(returned && dma[0][2] == 0x84400001u, "generic setup contract");
    for (unsigned invalid = 0; invalid < 4; ++invalid) {
        reset(0, 4);
        if (invalid == 0) g_cpu.R[0] = 4;
        if (invalid == 1) g_cpu.R[1] = base + 1u;
        if (invalid == 2) g_cpu.R[2] = 3;
        if (invalid == 3) { g_cpu.R[1] = 0x02FFFFFCu; g_cpu.R[2] = 8; }
        bool rejected = false;
        try { mph_submit::submit(); } catch (const std::runtime_error&) { rejected = true; }
        require(rejected && !returned && output.empty() && g_cpu.R[13] == stack,
                "invalid buffer did not fail before effects");
    }
    require(!mph_submit::resolve(NDS_ARM7, 0x01FF9470u, false).function, "ARM7 bound");
    require(!mph_submit::resolve(NDS_ARM9, 0x01FF9470u, true).function, "Thumb bound");
    require(!mph_submit::resolve(NDS_ARM9, 0x01FF9474u, false).guard_size, "unowned interior bound");
    if (!code.empty()) {
        for (uint32_t pc : {0x01FF9420u, 0x01FF9470u, 0x01FF94ACu, 0x01FF94CCu, 0x01FF94F8u})
            require(mph_submit::resolve(NDS_ARM9, pc, false).function, "valid code rejected");
        code[0x1490] ^= 1u; // outside the short generated entry prefix
        const auto rejected = mph_submit::resolve(NDS_ARM9, 0x01FF9470u, false);
        require(!rejected.function && rejected.guard_size == 0xFCu, "changed body/literal bound");
        code[0x1490] ^= 1u;
        code[0x020882E0u - 0x01FF8000u] ^= 1u;
        require(!mph_submit::resolve(NDS_ARM9, 0x01FF9470u, false).function, "modified IRQ dependency bound");
        code[0x020882E0u - 0x01FF8000u] ^= 1u;
        provenance = false;
        require(!mph_submit::resolve(NDS_ARM9, 0x01FF9470u, false).function, "unproven code bound");
        provenance = true;
        mph_submit::initialize("other ROM");
        require(!mph_submit::resolve(NDS_ARM9, 0x01FF9470u, false).function, "wrong ROM bound");
    }
}
} // namespace

bool bus_range_has_write_provenance(uint32_t, uint32_t) { return provenance; }
bool bus_debug_copy(int, uint32_t address, uint8_t* dest, uint32_t size) {
    const uint32_t off = address - 0x01FF8000u;
    if (uint64_t{off} + size > code.size()) return false;
    std::memcpy(dest, code.data() + off, size); return true;
}
uint32_t nds_io_read(uint32_t address, uint32_t width) {
    require(width == 4 && address >= 0x040000B0u && address < 0x040000E0u, "IO read bounds");
    return dma[(address - 0x040000B0u) / 12u][((address - 0x040000B0u) % 12u) / 4u];
}
void nds_io_write(uint32_t address, uint32_t value, uint32_t width) {
    require(width == 4 && address >= 0x040000B0u && address < 0x040000E0u, "IO write bounds");
    auto& d = dma[(address - 0x040000B0u) / 12u];
    require(!(d[2] & 0x80000000u), "new transfer overwrote unfinished transfer");
    d[((address - 0x040000B0u) % 12u) / 4u] = value;
}

extern "C" {
ArmCpuState g_cpu{};
NdsCpu g_nds_active = NDS_ARM9;
unsigned char g_nds_unwinding = 0;
unsigned long long g_runtime_cycles = 0, g_nds_fast_limit = 0;
uint32_t g_runtime_deep_trace = 1;
NdsBusFastWin g_busf_main{}, g_busf_wram_lo[2]{}, g_busf_wram_hi[2]{};
NdsBusFastWin g_busf_itcm{}, g_busf_dtcm{};
uint32_t g_busf_itcm_limit{}, g_busf_dtcm_base{}, g_busf_dtcm_limit{};
void runtime_note_code_write() {}
void runtime_note_code_write_pages(const uint32_t*,uint32_t) { runtime_note_code_write(); }
void runtime_note_live_write(uint32_t, uint32_t, uint32_t, uint32_t) {}
uint32_t bus_read_u32_slow(uint32_t address) { return word(address); }
void bus_write_u32_slow(uint32_t address, uint32_t value) { word(address) = value; }
void runtime_tick(uint32_t cycles) {
    require(cycles > 0 && cycles <= 128u, "invalid timing step");
    g_runtime_cycles += cycles; ++ticks;
    if (inject_irq && ticks == 2) g_nds_unwinding = 1;
}
bool runtime_unwinding() { return g_nds_unwinding; }
bool runtime_should_yield() {
    for (auto& d : dma) if (d[2] & 0x80000000u) { g_nds_unwinding = 1; return true; }
    if (g_runtime_cycles >= g_nds_fast_limit) { g_nds_unwinding = 1; return true; }
    return false;
}
int runtime_call_should_return(uint32_t pc) { require(pc == 0x020F0000u, "return PC"); returned = true; return 1; }
void runtime_dispatch_with_exchange(uint32_t) { throw std::runtime_error("unexpected tail dispatch"); }
void nds_halt(const char* why) { throw std::runtime_error(why); }
}

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::strcmp(argv[1], "--image") == 0) {
            std::ifstream stream(argv[2], std::ios::binary);
            code.assign(std::istreambuf_iterator<char>(stream), {});
            require(!code.empty(), "private fixture unreadable");
        } else require(argc == 1, "usage: hle_mph_submit_test [--image private/runtime.bin]");
        tests();
        std::puts("[hle_mph_submit_test] PASS: ordering, prior DMA, source visibility, chunk limits, continuation, ABI and binding");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[hle_mph_submit_test] FAIL: %s\n", e.what()); return 1;
    }
}
