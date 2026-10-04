#include "hle_mkds.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t base = 0x02200000, stack = base + 0xF00;
std::array<uint32_t, 1024> memory{};
std::vector<uint8_t> main_code;
unsigned ticks, exchanges, returns;
bool have_provenance = true;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
uint32_t& word(uint32_t addr) {
    require(addr >= base && addr < base + sizeof(memory) && !(addr & 3), "fixture RAM bounds");
    return memory[(addr - base) / 4];
}
void reset(uint32_t entry = 0, bool thumb_return = false) {
    memory.fill(0);
    ticks = exchanges = returns = 0;
    g_cpu = {}; g_cpu.cpsr = 0x1F;
    for (uint32_t r = 4; r < 13; ++r) g_cpu.R[r] = 0xF00D0000 + r;
    g_cpu.R[13] = stack; g_cpu.R[14] = 0x020F0000 | uint32_t(thumb_return);
    g_cpu.R[15] = entry;
    g_nds_unwinding = 0;
}
void return_contract() {
    require(g_cpu.R[13] == stack && g_cpu.R[15] == 0x020F0000, "SP/return PC");
    for (uint32_t r = 4; r < 12; ++r)
        require(g_cpu.R[r] == 0xF00D0000 + r, "callee-saved register");
    require(returns == 1 && exchanges == 0, "return continuation");
}
void math_contracts() {
    reset(0x02147FD8);
    g_cpu.R[0] = 2048; g_cpu.R[1] = base; g_cpu.R[2] = base + 32; g_cpu.R[3] = base;
    word(base) = 8192; word(base + 4) = uint32_t(-8192); word(base + 8) = 12288;
    word(base + 32) = 1; word(base + 36) = 2; word(base + 40) = 3;
    mkds_hle::scale_add();
    require(word(base) == 4097 && word(base + 4) == uint32_t(-4094) &&
            word(base + 8) == 6147 && ticks == 1, "scale/add in place");
    return_contract();
    reset(0x021483A0, true);
    g_cpu.R[0] = base; g_cpu.R[1] = base + 32; g_cpu.R[2] = base;
    word(base) = 4096; word(base + 36) = 4096;
    mkds_hle::cross_product();
    require(word(base) == 0 && word(base + 4) == 0 && word(base + 8) == 4096 &&
            (g_cpu.cpsr & CPSR_T_BIT), "cross alias/Thumb return");
    return_contract();
    reset(0x02147288);
    g_cpu.R[0] = base; g_cpu.R[1] = base + 32; g_cpu.R[2] = base;
    for (unsigned i = 0; i < 3; ++i) {
        word(base + 4*i) = 10 + i;
        word(base + 32 + 16*i) = 4096;
        word(base + 32 + 36 + 4*i) = 100;
    }
    mkds_hle::transform_translate();
    require(word(base) == 110 && word(base + 8) == 112, "affine transform alias");
    return_contract();
}
std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary); require(bool(f), "fixture image missing");
    return {std::istreambuf_iterator<char>(f), {}};
}
void bindings(const char* main_path) {
    main_code = read_file(main_path);
    for (uint32_t pc : {0x02147FD8u, 0x021483A0u, 0x02147288u}) {
        const auto binding = mkds_hle::resolve(NDS_ARM9, pc, false);
        require(binding.function && binding.validation, "exact resident routine did not bind");
        require(binding.validation->size >= 24, "short generated prefix used as whole proof");
        require(!mkds_hle::resolve(NDS_ARM7, pc, false).function, "ARM7 bound");
        require(!mkds_hle::resolve(NDS_ARM9, pc, true).function, "Thumb code bound");
        have_provenance = false;
        require(!mkds_hle::resolve(NDS_ARM9, pc, false).function, "unwritten code bound");
        have_provenance = true;
    }
    main_code[0x147FD8 + 92] ^= 1;
    require(!mkds_hle::resolve(NDS_ARM9, 0x02147FD8, false).function,
            "changed full-operation suffix accepted");
    main_code[0x147FD8 + 92] ^= 1;
    for (uint32_t pc : {0x01FFCF7Cu, 0x02147E28u, 0x0214D044u, 0x0214D048u})
        require(!mkds_hle::resolve(NDS_ARM9, pc, false).function, "draft operation bound");
    require(!mkds_hle::resolve(NDS_ARM9, 0x02147FDC, false).function, "unapproved interior bound");
    require(!mkds_hle::initialize("90164d1ac127ee5f9815ea4ae7de798c7b5fc629"), "MPH ROM accepted");
    require(!mkds_hle::resolve(NDS_ARM9, 0x02147FD8, false).function, "wrong ROM bound");
}
} // namespace

bool bus_range_has_write_provenance(uint32_t, uint32_t) { return have_provenance; }
bool bus_debug_copy(int, uint32_t addr, uint8_t* dst, uint32_t size) {
    const uint32_t offset = addr - 0x02000000;
    if (uint64_t{offset} + size > main_code.size()) return false;
    std::memcpy(dst, main_code.data() + offset, size); return true;
}

extern "C" {
ArmCpuState g_cpu{};
NdsCpu g_nds_active = NDS_ARM9;
unsigned char g_nds_unwinding = 0;
unsigned long long g_runtime_cycles = 0;
uint32_t g_runtime_deep_trace = 1;
NdsBusFastWin g_busf_main{}, g_busf_wram_lo[2]{}, g_busf_wram_hi[2]{};
NdsBusFastWin g_busf_itcm{}, g_busf_dtcm{};
uint32_t g_busf_itcm_limit{}, g_busf_dtcm_base{}, g_busf_dtcm_limit{};
void runtime_note_code_write() {}
void runtime_note_live_write(uint32_t, uint32_t, uint32_t, uint32_t) {}
uint32_t bus_read_u32_slow(uint32_t addr) { return word(addr); }
void bus_write_u32_slow(uint32_t addr, uint32_t value) { word(addr) = value; }
void runtime_tick(uint32_t cycles) {
    require(cycles && (g_cpu.R[15] == 0x020F0000),
            "invalid continuation at timing boundary");
    ++ticks;
}
bool runtime_unwinding() { return g_nds_unwinding != 0; }
bool runtime_should_yield() { return false; }
bool runtime_slice_yield() { return false; }
int runtime_call_should_return(uint32_t pc) { require(pc == 0x020F0000, "return target"); ++returns; return 1; }
void runtime_dispatch_with_exchange(uint32_t) { ++exchanges; }
}
int main(int argc, char** argv) {
    try {
        require(mkds_hle::initialize("691e00d9a5dd80b04f80cc7559503e8b06848785"), "MKDS rejected");
        require(mkds_hle::statistics().group_mask == 1, "only vector HLE is integrated");
        math_contracts();
        if (argc == 2) bindings(argv[1]);
        else require(argc == 1, "usage: hle_mkds_test [private-arm9.bin]");
        std::puts("[hle_mkds_test] PASS"); return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[hle_mkds_test] FAIL: %s\n", e.what()); return 1;
    }
}
