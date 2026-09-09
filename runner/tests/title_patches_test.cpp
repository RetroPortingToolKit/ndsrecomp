#include "title_patches.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "state.h"

namespace {

constexpr uint32_t kMainRamBase = 0x02000000u;
constexpr uint32_t kPlayerPosition = 0x020D9CB8u;
constexpr uint32_t kMorphState = 0x020DA818u;
constexpr uint32_t kJumpFlag = 0x020DABD9u;
constexpr uint32_t kWeaponChange = 0x020DABDBu;
constexpr uint32_t kSelectedWeapon = 0x020DABE3u;
constexpr uint32_t kGameMode = 0x020E78FCu;
constexpr uint32_t kMapOrUserActionPaused = 0x020FB458u;
constexpr uint32_t kAimX = 0x020DE526u;
constexpr uint32_t kAimY = 0x020DE52Eu;
constexpr uint32_t kMorphStride = 0xF30u;
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

void write8(uint32_t addr, uint8_t value) {
    g_main_ram[main_ram_offset(addr)] = value;
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

extern "C" void bus_write_u8_slow(uint32_t addr, uint8_t val) {
    g_main_ram[main_ram_offset(addr)] = val;
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

    for (uint8_t slot = 0; slot < 4u; ++slot) {
        reset_main_ram(slot);
        for (uint8_t other = 0; other < 4u; ++other) {
            write8(kMorphState + static_cast<uint32_t>(other) * kMorphStride,
                   other == slot ? 0x02u : 0x00u);
        }
        if (!require(nds_title_patches_mph_local_morph_ball()))
            return 14;

        write8(kMorphState + static_cast<uint32_t>(slot) * kMorphStride,
               0x00u);
        write8(kMorphState + static_cast<uint32_t>((slot + 1u) % 4u) *
                   kMorphStride,
               0x02u);
        if (!require(!nds_title_patches_mph_local_morph_ball()))
            return 15;
    }

    reset_main_ram(0xFFu);
    write8(kMorphState, 0x02u);
    if (!require(!nds_title_patches_mph_local_morph_ball()))
        return 16;

    reset_main_ram(2);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon + 2u * kMorphStride, 0);
    write8(kWeaponChange + 2u * kMorphStride, 0xA4u);
    write8(kJumpFlag + 2u * kMorphStride, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x00u);
    if (!require(nds_title_patches_request_mph_weapon(7)))
        return 17;
    if (!require(g_main_ram[main_ram_offset(kSelectedWeapon +
                                            2u * kMorphStride)] == 7u))
        return 18;
    if (!require(g_main_ram[main_ram_offset(kWeaponChange +
                                            2u * kMorphStride)] == 0xABu))
        return 19;
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            2u * kMorphStride)] == 0x01u))
        return 20;
    for (int i = 0; i < 4; ++i) nds_title_patches_start_frame();
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            2u * kMorphStride)] == 0x00u))
        return 21;

    reset_main_ram(2);
    nds_title_patches_set_mph_mouse_aim(false);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon + 2u * kMorphStride, 3);
    write8(kWeaponChange + 2u * kMorphStride, 0x20u);
    write8(kJumpFlag + 2u * kMorphStride, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x00u);
    if (!require(!nds_title_patches_request_mph_weapon(6)))
        return 41;
    if (!require(g_main_ram[main_ram_offset(kSelectedWeapon +
                                            2u * kMorphStride)] == 3u))
        return 42;
    if (!require(g_main_ram[main_ram_offset(kWeaponChange +
                                            2u * kMorphStride)] == 0x20u))
        return 43;
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            2u * kMorphStride)] == 0x00u))
        return 44;
    nds_title_patches_set_mph_mouse_aim(true);
    reset_main_ram(1);
    write8(kGameMode, 0x02u);
    write8(kMapOrUserActionPaused, 1);
    write8(kSelectedWeapon + kMorphStride, 2);
    write8(kWeaponChange + kMorphStride, 0x40u);
    if (!require(!nds_title_patches_request_mph_weapon(6)))
        return 22;
    if (!require(g_main_ram[main_ram_offset(kSelectedWeapon +
                                            kMorphStride)] == 2u))
        return 23;
    if (!require(g_main_ram[main_ram_offset(kWeaponChange +
                                            kMorphStride)] == 0x40u))
        return 24;

    reset_main_ram(1);
    write8(kGameMode, 0x03u);
    write8(kMapOrUserActionPaused, 1);
    write8(kSelectedWeapon + kMorphStride, 2);
    write8(kWeaponChange + kMorphStride, 0x40u);
    write8(kJumpFlag + kMorphStride, 0x10u);
    if (!require(nds_title_patches_request_mph_weapon(6)))
        return 25;
    if (!require(g_main_ram[main_ram_offset(kSelectedWeapon +
                                            kMorphStride)] == 6u))
        return 26;
    if (!require(g_main_ram[main_ram_offset(kWeaponChange +
                                            kMorphStride)] == 0x4Bu))
        return 27;

    reset_main_ram(3);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon + 3u * kMorphStride, 3);
    write8(kWeaponChange + 3u * kMorphStride, 0x20u);
    write8(kJumpFlag + 3u * kMorphStride, 0x10u);
    if (!require(nds_title_patches_request_mph_weapon(4)))
        return 28;
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            3u * kMorphStride)] == 0x10u))
        return 29;

    reset_main_ram(0);
    nds_title_patches_set_mph_mouse_aim(true);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon, 0);
    write8(kWeaponChange, 0x00u);
    write8(kJumpFlag, 0x00u);
    write8(kMorphState, 0x00u);
    if (!require(nds_title_patches_request_mph_weapon(6)))
        return 30;
    write8(kJumpFlag, 0x05u);
    nds_title_patches_start_frame();
    write8(kJumpFlag, 0x01u);
    for (int i = 0; i < 4; ++i) nds_title_patches_start_frame();
    if (!require(g_main_ram[main_ram_offset(kJumpFlag)] == 0x01u))
        return 31;

    reset_main_ram(2);
    nds_title_patches_set_mph_mouse_aim(true);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon + 2u * kMorphStride, 0);
    write8(kWeaponChange + 2u * kMorphStride, 0x00u);
    write8(kJumpFlag + 2u * kMorphStride, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x00u);
    if (!require(nds_title_patches_request_mph_weapon(6)))
        return 32;
    write8(kPlayerPosition, 1);
    for (int i = 0; i < 4; ++i) nds_title_patches_start_frame();
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            2u * kMorphStride)] == 0x01u))
        return 33;

    reset_main_ram(2);
    nds_title_patches_set_mph_mouse_aim(true);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon + 2u * kMorphStride, 0);
    write8(kWeaponChange + 2u * kMorphStride, 0x00u);
    write8(kJumpFlag + 2u * kMorphStride, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x00u);
    if (!require(nds_title_patches_request_mph_weapon(6)))
        return 34;
    nds_title_patches_set_mph_mouse_aim(false);
    for (int i = 0; i < 4; ++i) nds_title_patches_start_frame();
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            2u * kMorphStride)] == 0x01u))
        return 35;
    nds_title_patches_set_mph_mouse_aim(true);

    reset_main_ram(2);
    write8(kMapOrUserActionPaused, 0);
    write8(kSelectedWeapon + 2u * kMorphStride, 0);
    write8(kWeaponChange + 2u * kMorphStride, 0x00u);
    write8(kJumpFlag + 2u * kMorphStride, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x00u);
    if (!require(nds_title_patches_request_mph_weapon(6)))
        return 36;
    write8(kPlayerPosition, 0xFFu);
    nds_title_patches_start_frame();
    write8(kPlayerPosition, 2);
    for (int i = 0; i < 4; ++i) nds_title_patches_start_frame();
    if (!require(g_main_ram[main_ram_offset(kJumpFlag +
                                            2u * kMorphStride)] == 0x01u))
        return 37;

    return 0;
}
