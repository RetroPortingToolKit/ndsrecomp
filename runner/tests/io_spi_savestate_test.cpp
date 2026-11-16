#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Like gpu2d_window_test, compile the production implementation directly.
// Section GC drops unrelated devices; the SPI handlers and all three core
// state operations below are the real io.cpp code, not fixture copies.
#include "../src/io.cpp"

namespace {
unsigned refreshes = 0;
unsigned irq_refreshes = 0;
NdsIoCoreSaveState reset_state;

bool expect(bool value, const char* message) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

bool same_spi(const NdsIoCoreSaveState& a, const NdsIoCoreSaveState& b) {
    return a.pm_index == b.pm_index && a.pm_hold == b.pm_hold &&
        std::memcmp(a.pm_regs, b.pm_regs, sizeof(a.pm_regs)) == 0 &&
        std::memcmp(a.pm_masks, b.pm_masks, sizeof(a.pm_masks)) == 0 &&
        a.tsc_ctrl == b.tsc_ctrl && a.tsc_conv == b.tsc_conv &&
        a.tsc_datapos == b.tsc_datapos && a.tsc_x == b.tsc_x &&
        a.tsc_y == b.tsc_y;
}

bool roundtrip(const char* message) {
    NdsIoCoreSaveState before, after;
    std::string error;
    io_savestate_export(&before);
    if (!expect(io_savestate_validate(before, &error), message)) return false;
    // Disturb both devices, then restore a potentially held transaction.
    pm_release();
    pm_write(1);
    tsc_write(0xd0);
    const unsigned old_refreshes = refreshes;
    const unsigned old_irqs = irq_refreshes;
    if (!expect(io_savestate_import(before, &error), "restore real IO state"))
        return false;
    io_savestate_export(&after);
    return expect(same_spi(before, after), "held SPI state survives import") &&
        expect(refreshes == old_refreshes + 1 && irq_refreshes == old_irqs + 2,
               "import refreshes bus and both IRQ caches");
}

bool power_management() {
    io_savestate_import(reset_state, nullptr);
    g_pm_regs[0] = 0x5a;
    pm_write(0x80);
    if (!roundtrip("normal PM read command must be saveable")) return false;
    if (!expect(pm_write(0) == 0x5a, "restored PM read resumes correctly"))
        return false;
    pm_release();
    for (unsigned command = 0; command < 256; ++command) {
        pm_write(static_cast<uint8_t>(command));
        const uint8_t expected = command & 0x80 ? g_pm_regs[command & 7] : 0;
        if (!roundtrip("every handled PM command byte must be saveable"))
            return false;
        if (!expect(pm_write(0) == expected, "PM command survives restore"))
            return false;
        pm_release();
    }
    return roundtrip("released PM transaction must be saveable");
}

bool touchscreen() {
    io_savestate_import(reset_state, nullptr);
    g_tsc_y = 0xabc;
    tsc_write(0x90);
    if (!roundtrip("touchscreen control state must be saveable") ||
        !expect(tsc_write(0) == 0x55, "restored high result byte")) return false;
    if (!roundtrip("first touchscreen data byte must be saveable") ||
        !expect(tsc_write(0) == 0xe0, "restored low result byte")) return false;
    if (!roundtrip("completed held touchscreen read must be saveable"))
        return false;
    for (unsigned i = 0; i < 8; ++i)
        if (!expect(tsc_write(0) == 0, "idle transfer returns zero")) return false;
    if (!expect(g_tsc_datapos == 3, "idle transfer position stays bounded") ||
        !roundtrip("idle touchscreen state must be saveable")) return false;
    tsc_release();
    return expect(g_tsc_datapos == 0, "chip release resets transfer position") &&
        roundtrip("released touchscreen must be saveable");
}

bool rejects_invalid_states() {
    io_savestate_import(reset_state, nullptr);
    auto rejected = [](NdsIoCoreSaveState bad) {
        NdsIoCoreSaveState before, after;
        io_savestate_export(&before);
        const unsigned old_refreshes = refreshes;
        const unsigned old_irqs = irq_refreshes;
        std::string error;
        const bool ok = !io_savestate_validate(bad, &error) && !error.empty() &&
            !io_savestate_import(bad, &error);
        io_savestate_export(&after);
        return expect(ok && same_spi(before, after) &&
                      old_refreshes == refreshes && old_irqs == irq_refreshes,
                      "invalid state rejected before mutation or refresh");
    };
    bool ok = true;
    auto bad = reset_state; bad.next_vcount_valid = 2; ok &= rejected(bad);
    bad = reset_state; bad.pm_hold = 2; ok &= rejected(bad);
    bad = reset_state; bad.powercontrol7 = 4; ok &= rejected(bad);
    bad = reset_state; bad.tsc_datapos = -1; ok &= rejected(bad);
    bad = reset_state; bad.tsc_datapos = 4; ok &= rejected(bad);
    bad = reset_state; bad.fifo_head[0] = 16; ok &= rejected(bad);
    bad = reset_state; bad.dma[0][0].start_mode = 8; ok &= rejected(bad);
    bad = reset_state; bad.dma[1][0].start_mode = 0x14; ok &= rejected(bad);
    bad = reset_state; bad.timer[0][0].accum = 1024; ok &= rejected(bad);
    return ok;
}
} // namespace

void nds_gpu2d_stop() { std::abort(); }
void nds_spu_stop() { std::abort(); }
void bus_fast_refresh() { ++refreshes; }
extern "C" void runtime_clear_fast_limit() { ++irq_refreshes; }

int main() {
    io_savestate_export(&reset_state);
    bool ok = power_management();
    ok &= touchscreen();
    ok &= rejects_invalid_states();
    if (ok) std::puts("real SPI handlers and IO state roundtrips pass");
    return ok ? 0 : 1;
}
