// Include the device unit to inspect its private register contract directly.
// Link-time optimization discards unrelated I/O devices and dependencies.
#include "../src/io.cpp"
#include <cstdio>
#include <stdexcept>

static void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main() {
    try {
        check(nds_math_hle_divide(2, 100, 7) == 14, "division quotient");
        check(math_reg_read32(0x040002A8) == 2, "division remainder visible");
        check(math_reg_read32(0x04000290) == 100 &&
              math_reg_read32(0x04000298) == 7, "division operands visible");
        check(!(g_divcnt & 0x8000) && g_div_deadline == UINT64_MAX, "division complete");
        check(nds_math_hle_divide(2, uint64_t(-100), 7) == uint64_t(-14), "signed division");
        check(words_u64(g_div_rem) == uint64_t(-2), "signed remainder");
        check(nds_math_hle_divide(2, 12, 0) == UINT64_MAX &&
              (g_divcnt & 0x4000) && words_u64(g_div_rem) == 12, "zero denominator");
        check(nds_math_hle_divide(2, uint64_t{1} << 63, UINT64_MAX) ==
              (uint64_t{1} << 63), "signed overflow");
        check(nds_math_hle_sqrt(1, 100) == 10 && g_sqrt_res == 10 &&
              g_sqrt_deadline == UINT64_MAX && !(g_sqrtcnt & 0x8000), "sqrt complete");
        check(nds_math_hle_sqrt(1, 99) == 9, "sqrt floor");
        check(nds_math_hle_sqrt(1, 0) == 0, "sqrt zero");
        check(nds_math_hle_sqrt(1, UINT64_MAX) == UINT32_MAX, "sqrt full width");
        const uint64_t square = uint64_t{UINT32_MAX} * UINT32_MAX;
        check(nds_math_hle_sqrt(1, square - 1) == UINT32_MAX - 1 &&
              nds_math_hle_sqrt(1, square) == UINT32_MAX, "sqrt rounded boundary");
        // Pending operation: complete once, then leave stable results alone.
        g_divcnt = 0x8002;
        g_div_numer[0] = 123; g_div_numer[1] = 0;
        g_div_denom[0] = 3; g_div_denom[1] = 0;
        g_div_deadline = 500;
        check(nds_math_hle_complete_division() == 41 && g_div_deadline == UINT64_MAX,
              "pending division completed without polling");
        g_div_numer[0] = 999;
        check(nds_math_hle_complete_division() == 41, "completed division not restarted");
        std::puts("[hle_mkds_io_test] PASS");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[hle_mkds_io_test] FAIL: %s\n", e.what());
        return 1;
    }
}
