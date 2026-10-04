#include "hle_mph_math.h"
#include "arm_decode.h"
#include "interpreter.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
constexpr uint32_t base = 0x02200000u;
constexpr uint32_t stack = base + 0xF80u;
void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}
struct Memory : armv4t::Bus {
    std::array<uint8_t, 4096> bytes{};
    uint8_t read8(uint32_t a) override {
        require(a >= base && a - base < bytes.size(), "read outside fixture RAM");
        return bytes[a - base];
    }
    uint16_t read16(uint32_t a) override { return read8(a) | (uint16_t{read8(a + 1)} << 8); }
    uint32_t read32(uint32_t a) override { return read16(a) | (uint32_t{read16(a + 2)} << 16); }
    void write8(uint32_t a, uint8_t v) override {
        require(a >= base && a - base < bytes.size(), "write outside fixture RAM");
        bytes[a - base] = v;
    }
    void write16(uint32_t a, uint16_t v) override { write8(a, v); write8(a + 1, v >> 8); }
    void write32(uint32_t a, uint32_t v) override { write16(a, v); write16(a + 2, v >> 16); }
};
Memory* memory;
unsigned ticks, return_checks, exchanges;
bool unwind_on_tick, match_return;
uint32_t expected_return;

void reset_call(uint32_t lr = 0x020F0000u) {
    g_cpu = {};
    g_cpu.R[13] = stack;
    g_cpu.R[14] = lr;
    g_cpu.cpsr = 0x1Fu;
    expected_return = lr & ((lr & 1u) ? ~1u : ~3u);
    ticks = return_checks = exchanges = 0;
    unwind_on_tick = false;
    match_return = true;
}

void smoke() {
    Memory mem;
    memory = &mem;
    reset_call();
    g_cpu.R[0] = 4096;
    g_cpu.R[1] = base + 64;
    g_cpu.R[2] = base + 128;
    g_cpu.R[3] = base + 256;
    for (uint32_t i = 0; i < 3; ++i) {
        mem.write32(base + 64 + 4 * i, i + 1);
        mem.write32(base + 128 + 4 * i, 10 * (i + 1));
    }
    mph_hle::scale_add();
    for (uint32_t i = 0; i < 3; ++i)
        require(mem.read32(base + 256 + 4 * i) == 11 * (i + 1), "scale/add output");
    require(ticks == 1 && return_checks == 1 && exchanges == 0, "call return boundary");

    mem = {};
    reset_call(0x020F0001u);
    g_cpu.R[0] = base + 64;
    g_cpu.R[1] = base + 128;
    g_cpu.R[2] = base + 64; // in-place cross product
    mem.write32(base + 64, 4096);
    mem.write32(base + 132, 4096);
    mph_hle::cross_product();
    require(mem.read32(base + 64) == 0 && mem.read32(base + 68) == 0 &&
            mem.read32(base + 72) == 4096, "cross output/in-place alias");
    require((g_cpu.cpsr & CPSR_T_BIT) && ticks == 1, "Thumb return");

    mem = {};
    reset_call();
    g_cpu.R[0] = base + 64;
    g_cpu.R[1] = base + 128;
    g_cpu.R[2] = base + 64;
    for (uint32_t i = 0; i < 3; ++i) {
        mem.write32(base + 64 + 4 * i, i + 1);
        mem.write32(base + 128 + 16 * i, 4096);
        mem.write32(base + 128 + 36 + 4 * i, 10 * (i + 1));
    }
    match_return = false;
    mph_hle::transform_translate();
    require(mem.read32(base + 64) == 11 && mem.read32(base + 72) == 33, "transform output");
    require(exchanges == 1 && return_checks == 1, "tail-call continuation");

    reset_call();
    g_cpu.R[0] = base + 64;
    g_cpu.R[1] = base + 128;
    g_cpu.R[2] = base + 256;
    unwind_on_tick = true;
    mph_hle::cross_product();
    require(ticks == 1 && return_checks == 0 && exchanges == 0, "IRQ unwind boundary");

    std::array<uint8_t, 256> wrong_code{};
    NdsStaticValidation validation{0x02080BD8u, 256u, wrong_code.data(), nullptr, 0};
    require(!mph_hle::initialize("wrong-rom"), "unsupported ROM accepted");
    require(!mph_hle::resolve(NDS_ARM9, 0x02080BD8u, false, &validation), "wrong ROM bound");
    require(mph_hle::initialize("90164d1ac127ee5f9815ea4ae7de798c7b5fc629"), "supported ROM rejected");
    require(!mph_hle::resolve(NDS_ARM9, 0x02080BD8u, false, &validation), "wrong code bound");
    require(!mph_hle::resolve(NDS_ARM9, 0x02080BD8u, false, nullptr), "unguarded code bound");
}

uint32_t random_state = 0x68756E74u;
uint32_t next_random() {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    return random_state ^= random_state << 5;
}

uint32_t packed(const armv4t::CPSR& c) {
    return uint32_t{c.mode} | (uint32_t{c.t} << 5) | (uint32_t{c.f} << 6) |
        (uint32_t{c.i} << 7) | (uint32_t{c.q} << 27) | (uint32_t{c.v} << 28) |
        (uint32_t{c.c} << 29) | (uint32_t{c.z} << 30) | (uint32_t{c.n} << 31);
}

void differential(const char* file) {
    std::ifstream stream(file, std::ios::binary);
    require(bool(stream), "cannot open local ARM9 fixture");
    const std::vector<uint8_t> code((std::istreambuf_iterator<char>(stream)), {});
    struct Case { uint32_t pc, size; mph_hle::Function fn; };
    const Case cases[] = {{0x02080BD8u, 96, mph_hle::scale_add},
                          {0x02080DE0u, 156, mph_hle::cross_product},
                          {0x0207FD38u, 180, mph_hle::transform_translate}};
    constexpr uint32_t edge[] = {0, 1, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u,
        0x800u, 0xFFFFF800u, 4096, 0xFFFFF000u};
    unsigned comparisons = 0;
    for (const auto& test : cases) {
        const auto offset = test.pc - 0x02004000u;
        require(code.size() >= offset + test.size, "ARM9 fixture is too short");
        NdsStaticValidation validation{test.pc, test.size, code.data() + offset, nullptr, 0};
        require(mph_hle::resolve(NDS_ARM9, test.pc, false, &validation) == test.fn,
                "fixture routine fingerprint differs from supported code");
        NdsStaticValidation enclosing{test.pc - 4u, test.size + 4u,
                                     code.data() + offset - 4u, nullptr, 0};
        require(mph_hle::resolve(NDS_ARM9, test.pc, false, &enclosing) == test.fn,
                "enclosing bank span did not bind the whole routine");
        require(!mph_hle::resolve(NDS_ARM7, test.pc, false, &validation), "ARM7 bound");
        require(!mph_hle::resolve(NDS_ARM9, test.pc, true, &validation), "Thumb code bound");
        require(!mph_hle::resolve(NDS_ARM9, test.pc + 4, false, &validation), "interior entry bound");
        --validation.size;
        require(!mph_hle::resolve(NDS_ARM9, test.pc, false, &validation), "short proof bound");
        ++validation.size;
        for (unsigned n = 0; n < 512; ++n) {
            Memory actual;
            for (uint32_t p = base; p < base + actual.bytes.size(); p += 4)
                actual.write32(p, n < 18 ? edge[(n + (p - base) / 4) % 9] : next_random());
            reset_call(0x020F0000u | (n & 1u));
            for (unsigned r = 0; r < 13; ++r) g_cpu.R[r] = next_random();
            uint32_t a = base + 64, b = base + 128, out = base + 256;
            switch (n % 8) {
            case 1: out = a; break;
            case 2: out = b; break;
            case 3: out = a + 4; break;
            case 4: out = b + 4; break;
            case 5: out = b + 36; break;
            case 6: b = a; break;
            case 7: ++a; b += 3; out += 2; break;
            }
            if (test.fn == mph_hle::scale_add) {
                g_cpu.R[0] = n < 18 ? edge[n % 9] : next_random();
                g_cpu.R[1] = a; g_cpu.R[2] = b; g_cpu.R[3] = out;
            } else {
                g_cpu.R[0] = a; g_cpu.R[1] = b; g_cpu.R[2] = out;
            }
            g_cpu.R[15] = test.pc;
            g_cpu.cpsr = (next_random() & 0xF80000C0u) | 0x1Fu;
            Memory reference = actual;
            armv4t::CPUState cpu{};
            std::memcpy(cpu.R, g_cpu.R, sizeof(cpu.R));
            const uint32_t flags = g_cpu.cpsr;
            cpu.cpsr = {bool(flags & CPSR_N_BIT), bool(flags & CPSR_Z_BIT),
                bool(flags & CPSR_C_BIT), bool(flags & CPSR_V_BIT), bool(flags & (1u << 27)),
                bool(flags & 128u), bool(flags & 64u), false, 0x1F};
            unsigned instructions = 0;
            while (cpu.R[15] != expected_return) {
                require(++instructions <= 64, "reference did not return");
                const uint32_t pc = cpu.R[15];
                require(pc >= test.pc && pc < test.pc + test.size, "reference escaped routine");
                uint32_t opcode;
                std::memcpy(&opcode, code.data() + pc - 0x02004000u, 4);
                const auto result = armv4t::Interpreter::step(cpu, reference,
                    armv4t::ArmDecoder::decode(opcode, pc));
                require(result == armv4t::Interpreter::Result::Normal ||
                        result == armv4t::Interpreter::Result::Branched, "unsupported reference opcode");
            }
            memory = &actual;
            test.fn();
            for (unsigned r = 0; r < 16; ++r) {
                if (g_cpu.R[r] != cpu.R[r]) {
                    std::fprintf(stderr, "pc=%08X case=%u r%u HLE=%08X LLE=%08X\n",
                                 test.pc, n, r, g_cpu.R[r], cpu.R[r]);
                    throw std::runtime_error("return register mismatch");
                }
            }
            require(g_cpu.cpsr == packed(cpu.cpsr), "return flags mismatch");
            for (uint32_t i = 0; i < actual.bytes.size(); ++i) {
                // Dead stack-frame contents are internal implementation state.
                if (i >= stack - base - 64 && i < stack - base) continue;
                require(actual.bytes[i] == reference.bytes[i], "caller memory mismatch");
            }
            require(ticks == 1 && return_checks == 1 && exchanges == 0, "operation completion boundary");
            ++comparisons;
        }
    }
    std::printf("[hle_mph_math_test] %u original-code differential cases passed\n", comparisons);
}
} // namespace

extern "C" {
ArmCpuState g_cpu{};
NdsCpu g_nds_active = NDS_ARM9;
uint32_t g_runtime_deep_trace = 1;
NdsBusFastWin g_busf_main{}, g_busf_wram_lo[2]{}, g_busf_wram_hi[2]{};
NdsBusFastWin g_busf_itcm{}, g_busf_dtcm{};
uint32_t g_busf_itcm_limit{}, g_busf_dtcm_base{}, g_busf_dtcm_limit{};
void runtime_note_code_write() { throw std::runtime_error("unexpected fast bus write"); }
void runtime_note_live_write(uint32_t, uint32_t, uint32_t, uint32_t) {
    throw std::runtime_error("unexpected live code write");
}
uint32_t bus_read_u32_slow(uint32_t address) { return memory->read32(address); }
void bus_write_u32_slow(uint32_t address, uint32_t value) { memory->write32(address, value); }
void runtime_tick(uint32_t cycles) {
    require(cycles != 0 && g_cpu.R[15] == expected_return, "timing boundary precedes completed return");
    ++ticks;
}
bool runtime_unwinding() { return unwind_on_tick; }
int runtime_call_should_return(uint32_t pc) {
    require(pc == expected_return, "return stack target");
    ++return_checks;
    return match_return;
}
void runtime_dispatch_with_exchange(uint32_t target) {
    require((target & ~1u) == expected_return, "tail-call target");
    ++exchanges;
}
}

int main(int argc, char** argv) {
    try {
        smoke();
        if (argc == 3 && std::strcmp(argv[1], "--arm9") == 0) differential(argv[2]);
        else require(argc == 1, "usage: hle_mph_math_test [--arm9 private/arm9.bin]");
        std::puts("[hle_mph_math_test] PASS");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[hle_mph_math_test] FAIL: %s\n", e.what());
        return 1;
    }
}
