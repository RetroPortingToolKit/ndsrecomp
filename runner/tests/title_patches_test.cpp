#include "title_patches.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "state.h"

namespace {

constexpr uint32_t kMainRamBase = 0x02000000u;
constexpr uint32_t kPlayerPosition = 0x020D9CB8u;
constexpr uint32_t kAimX = 0x020DE526u;
constexpr uint32_t kAimY = 0x020DE52Eu;
constexpr uint32_t kAimStride = 0x48u;
constexpr uint32_t kMainRamSize = 0x400000u;
constexpr uint8_t kSentinel = 0xA5u;

std::array<uint8_t, kMainRamSize> g_main_ram{};

bool require(bool value) {
    return value;
}

uint32_t main_ram_offset(uint32_t addr) {
    return addr - kMainRamBase;
}

uint16_t read16(uint32_t addr) {
    uint16_t value = 0;
    std::memcpy(&value, &g_main_ram[main_ram_offset(addr)], sizeof(value));
    return value;
}

void reset_main_ram(uint8_t player_position) {
    g_main_ram.fill(kSentinel);
    g_main_ram[main_ram_offset(kPlayerPosition)] = player_position;
    nds_title_patches_set_mph_mouse_aim(true);
}

bool unchanged(uint32_t addr) {
    const uint32_t offset = main_ram_offset(addr);
    return g_main_ram[offset] == kSentinel &&
           g_main_ram[offset + 1u] == kSentinel &&
           g_main_ram[offset + 2u] == kSentinel &&
           g_main_ram[offset + 3u] == kSentinel;
}

}  // namespace

bool bus_get_region(const char* name, BusRegion* out) {
    if (!name || !out || std::strcmp(name, "mainram") != 0) return false;
    out->ptr = g_main_ram.data();
    out->len = static_cast<uint32_t>(g_main_ram.size());
    return true;
}

extern "C" void bus_write_u16_slow(uint32_t addr, uint16_t val) {
    std::memcpy(&g_main_ram[main_ram_offset(addr)], &val, sizeof(val));
}

extern "C" void bus_write_u32_slow(uint32_t addr, uint32_t val) {
    std::memcpy(&g_main_ram[main_ram_offset(addr)], &val, sizeof(val));
}

uint16_t nds_gpu3d_output_width() {
    return 256;
}

void nds_gpu3d_set_guest_wide_projection(bool) {
}

int main() {
    for (uint8_t slot = 0; slot < 4u; ++slot) {
        reset_main_ram(slot);
        const int32_t dx = slot == 2u ? -17 : static_cast<int32_t>(10 + slot);
        const int32_t dy = slot == 3u ? -23 : -static_cast<int32_t>(20 + slot);
        if (!require(nds_title_patches_apply_mph_mouse_delta(dx, dy)))
            return 1;

        const uint32_t slot_x = kAimX + static_cast<uint32_t>(slot) * kAimStride;
        const uint32_t slot_y = kAimY + static_cast<uint32_t>(slot) * kAimStride;
        if (!require(read16(slot_x) == static_cast<uint16_t>(dx)) ||
            !require(read16(slot_y) == static_cast<uint16_t>(dy)))
            return 2;

        if (!require(g_main_ram[main_ram_offset(slot_x + 2u)] == kSentinel) ||
            !require(g_main_ram[main_ram_offset(slot_y + 2u)] == kSentinel))
            return 3;

        for (uint8_t other = 0; other < 4u; ++other) {
            if (other == slot) continue;
            const uint32_t other_x =
                kAimX + static_cast<uint32_t>(other) * kAimStride;
            const uint32_t other_y =
                kAimY + static_cast<uint32_t>(other) * kAimStride;
            if (!require(unchanged(other_x)) || !require(unchanged(other_y)))
                return 4;
        }
    }

    reset_main_ram(1);
    if (!require(nds_title_patches_apply_mph_mouse_delta(-1, 0)))
        return 5;
    if (!require(read16(kAimX + kAimStride) == 0xFFFFu))
        return 6;
    if (!require(unchanged(kAimY + kAimStride)))
        return 7;

    reset_main_ram(3);
    if (!require(nds_title_patches_apply_mph_mouse_delta(40000, -40000)))
        return 8;
    if (!require(read16(kAimX + 3u * kAimStride) == 0x7FFFu) ||
        !require(read16(kAimY + 3u * kAimStride) == 0x8000u))
        return 9;

    reset_main_ram(0xFFu);
    if (!require(!nds_title_patches_apply_mph_mouse_delta(5, 7)))
        return 10;
    if (!require(unchanged(kAimX)) || !require(unchanged(kAimY)))
        return 11;

    reset_main_ram(2);
    nds_title_patches_set_mph_mouse_aim(false);
    if (!require(!nds_title_patches_apply_mph_mouse_delta(5, 7)))
        return 12;
    if (!require(unchanged(kAimX + 2u * kAimStride)) ||
        !require(unchanged(kAimY + 2u * kAimStride)))
        return 13;

    return 0;
}
