#include "hle_mkds.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t base = 0x02200000, stack = base + 0xF00;
std::array<uint32_t, 1024> memory{};
std::vector<uint32_t> sent;
std::vector<uint8_t> main_code, itcm_code;
uint64_t quotient, numerator, denominator, sqrt_operand;
unsigned ticks, exchanges, returns, capacity;
unsigned reschedules;
bool stalled, yield_on_tick, have_provenance = true;
void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
uint32_t& word(uint32_t addr) {
    require(addr >= base && addr < base + sizeof(memory) && !(addr & 3), "fixture RAM bounds");
    return memory[(addr - base) / 4];
}
void reset(uint32_t entry = 0, bool thumb_return = false) {
    memory.fill(0); sent.clear();
    ticks = exchanges = returns = 0; capacity = 10000;
    reschedules = 0;
    stalled = yield_on_tick = false;
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
    reset(0x01FFCF7C);
    g_cpu.R[0] = base; g_cpu.R[1] = base;
    word(base) = 8192;
    mkds_hle::normalize();
    require(word(base) == 4096 && word(base + 4) == 0 && word(base + 12) == 0,
            "four-component normalization in place");
    require(numerator == (uint64_t{1} << 56) && denominator == 67108864 &&
            sqrt_operand == 268435456, "normalization device operands");
    return_contract();
    reset(0x01FFCF7C);
    g_cpu.R[0] = base; g_cpu.R[1] = base + 4; // stores affect subsequent loads
    word(base) = 8192;
    mkds_hle::normalize();
    require(word(base + 4) == 4096 && word(base + 8) == 2048 &&
            word(base + 12) == 1024 && word(base + 16) == 512, "partial overlap ordering");
    reset(0x01FFCF7C);
    g_cpu.R[0] = base; g_cpu.R[1] = base + 32;
    mkds_hle::normalize();
    require(word(base + 32) == 0 && word(base + 44) == 0, "zero normalization");
    reset(0x02147E28);
    quotient = (uint64_t{123} << 20) + 0x80000;
    mkds_hle::divide_result();
    require(g_cpu.R[0] == 124 && ticks == 1, "rounded division result");
    return_contract();
}
void transfer_contracts() {
    reset(0x0214D044);
    g_cpu.R[0] = base; g_cpu.R[1] = 0x04000400; g_cpu.R[2] = 400;
    for (uint32_t i = 0; i < 100; ++i) word(base + 4*i) = 1000 + i;
    capacity = 7; // pressure in the middle of the first batch
    mkds_hle::send_words();
    require(sent.size() == 7 && g_cpu.R[0] == base + 28 &&
            g_cpu.R[12] == base + 400 && g_cpu.R[15] == 0x0214D048 &&
            g_nds_unwinding && returns == 0 && reschedules == 1,
            "backpressure leaves resumable guest state and ends the slice");
    stalled = false; capacity = 10000; g_nds_unwinding = 0;
    mkds_hle::send_words();
    require(sent.size() == 100 && g_cpu.R[0] == base + 400 && g_cpu.R[2] == 1099,
            "resumed batch completes exactly once");
    for (uint32_t i = 0; i < 100; ++i) require(sent[i] == 1000 + i, "FIFO word order");
    require(ticks <= 6, "submission still ticks once per word");
    return_contract();
    reset(0x0214D044, true);
    g_cpu.R[0] = base; g_cpu.R[1] = 0x04000400; g_cpu.R[2] = 256;
    for (uint32_t i = 0; i < 64; ++i) word(base + i*4) = i;
    yield_on_tick = true;
    mkds_hle::send_words();
    require(sent.size() == 32 && g_cpu.R[15] == 0x0214D048 && returns == 0,
            "interrupt/slice boundary does not replay completed batch");
    yield_on_tick = false; g_nds_unwinding = 0;
    mkds_hle::send_words();
    require(sent.size() == 64 && (g_cpu.cpsr & CPSR_T_BIT), "resumed Thumb caller");
    reset(0x0214D044);
    g_cpu.R[0] = base; g_cpu.R[1] = base + 64; g_cpu.R[2] = 9;
    word(base) = 11; word(base + 4) = 22; word(base + 8) = 33;
    mkds_hle::send_words();
    require(word(base + 64) == 33 && g_cpu.R[0] == base + 12,
            "fixed destination and non-multiple word length");
    reset(0x0214D044); g_cpu.R[0] = base; g_cpu.R[1] = 0x04000400;
    mkds_hle::send_words();
    require(sent.empty() && g_cpu.R[0] == base, "empty transfer");
}
std::vector<uint8_t> read_file(const char* path) {
    std::ifstream f(path, std::ios::binary); require(bool(f), "fixture image missing");
    return {std::istreambuf_iterator<char>(f), {}};
}
void bindings(const char* main_path, const char* itcm_path) {
    main_code = read_file(main_path); itcm_code = read_file(itcm_path);
    for (uint32_t pc : {0x02147FD8u, 0x021483A0u, 0x02147288u, 0x01FFCF7Cu,
                        0x02147E28u, 0x0214D044u, 0x0214D048u}) {
        const auto binding = mkds_hle::resolve(NDS_ARM9, pc, false);
        require(binding.function && binding.validation, "exact resident routine did not bind");
        require(binding.validation->size >= 24, "short generated prefix used as whole proof");
        require(!mkds_hle::resolve(NDS_ARM7, pc, false).function, "ARM7 bound");
        require(!mkds_hle::resolve(NDS_ARM9, pc, true).function, "Thumb code bound");
        have_provenance = false;
        require(!mkds_hle::resolve(NDS_ARM9, pc, false).function, "unwritten code bound");
        have_provenance = true;
    }
    // Corrupt the *second* page, outside the short generated entry proof.
    itcm_code[0x50E4] ^= 1;
    require(!mkds_hle::resolve(NDS_ARM9, 0x01FFCF7C, false).function,
            "changed distant literal accepted");
    itcm_code[0x50E4] ^= 1;
    require(!mkds_hle::resolve(NDS_ARM9, 0x02147FDC, false).function, "unapproved interior bound");
    require(!mkds_hle::initialize("90164d1ac127ee5f9815ea4ae7de798c7b5fc629"), "MPH ROM accepted");
    require(!mkds_hle::resolve(NDS_ARM9, 0x02147FD8, false).function, "wrong ROM bound");
}
} // namespace

bool bus_range_has_write_provenance(uint32_t, uint32_t) { return have_provenance; }
bool bus_debug_copy(int, uint32_t addr, uint8_t* dst, uint32_t size) {
    const auto& code = addr < 0x02000000 ? itcm_code : main_code;
    const uint32_t offset = addr - (addr < 0x02000000 ? 0x01FF8000 : 0x02000000);
    if (uint64_t{offset} + size > code.size()) return false;
    std::memcpy(dst, code.data() + offset, size); return true;
}
uint64_t nds_math_hle_divide(uint16_t, uint64_t n, uint64_t d) {
    numerator = n; denominator = d;
    quotient = d ? uint64_t(std::bit_cast<int64_t>(n) / std::bit_cast<int64_t>(d)) : UINT64_MAX;
    return quotient;
}
uint64_t nds_math_hle_complete_division() { return quotient; }
uint32_t nds_math_hle_sqrt(uint16_t, uint64_t v) {
    sqrt_operand = v; return uint32_t(std::sqrt(double(v)));
}
bool nds_gxfifo_stalled() { return stalled; }
uint32_t nds_gpu3d_submit_words(uint32_t, const uint32_t* data, uint32_t count) {
    const uint32_t n = std::min(count, capacity);
    sent.insert(sent.end(), data, data + n); capacity -= n;
    stalled = capacity == 0; return n;
}
extern "C" {
ArmCpuState g_cpu{};
NdsCpu g_nds_active = NDS_ARM9;
unsigned char g_nds_unwinding = 0;
unsigned long long g_runtime_cycles = 0;
void nds_reschedule_slice(unsigned long long) { ++reschedules; }
uint32_t g_runtime_deep_trace = 1;
NdsBusFastWin g_busf_main{}, g_busf_wram_lo[2]{}, g_busf_wram_hi[2]{};
NdsBusFastWin g_busf_itcm{}, g_busf_dtcm{};
uint32_t g_busf_itcm_limit{}, g_busf_dtcm_base{}, g_busf_dtcm_limit{};
void runtime_note_code_write() {}
void runtime_note_live_write(uint32_t, uint32_t, uint32_t, uint32_t) {}
uint32_t bus_read_u32_slow(uint32_t addr) { return word(addr); }
void bus_write_u32_slow(uint32_t addr, uint32_t value) { word(addr) = value; }
void runtime_tick(uint32_t cycles) {
    require(cycles && (g_cpu.R[15] == 0x0214D048 || g_cpu.R[15] == 0x020F0000),
            "invalid continuation at timing boundary");
    ++ticks; if (yield_on_tick) g_nds_unwinding = 1;
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
        math_contracts(); transfer_contracts();
        if (argc == 3) bindings(argv[1], argv[2]);
        else require(argc == 1, "usage: hle_mkds_test [private-arm9.bin private-itcm.bin]");
        std::puts("[hle_mkds_test] PASS"); return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[hle_mkds_test] FAIL: %s\n", e.what()); return 1;
    }
}
