#include "title_patches.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "state.h"

namespace {

constexpr uint32_t kMainRamBase = 0x02000000u;
constexpr uint32_t kPlayerPosition = 0x020D9CB8u;
constexpr uint32_t kChosenHunter = 0x020CB51Cu;
constexpr uint32_t kMorphState = 0x020DA818u;
constexpr uint32_t kJumpFlag = 0x020DABD9u;
constexpr uint32_t kWeaponChange = 0x020DABDBu;
constexpr uint32_t kSelectedWeapon = 0x020DABE3u;
constexpr uint32_t kSpecialWeaponSlot = 0x020DA86Eu;
constexpr uint32_t kGameMode = 0x020E78FCu;
constexpr uint32_t kMapOrUserActionPaused = 0x020FB458u;
constexpr uint32_t kAimX = 0x020DE526u;
constexpr uint32_t kAimY = 0x020DE52Eu;
constexpr uint32_t kMorphStride = 0xF30u;
constexpr uint32_t kAimStride = 0x48u;
constexpr uint32_t kBoostingOffset = 0x46u;
constexpr uint32_t kMainRamSize = 0x400000u;
constexpr uint8_t kSentinel = 0xA5u;

std::array<uint8_t, kMainRamSize> g_main_ram{};
uint16_t g_render_width = 256;
bool g_guest_wide = false;

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
    return g_render_width;
}

void nds_gpu3d_set_guest_wide_projection(bool enabled) {
    g_guest_wide = enabled;
}

bool test_mkds_object_clipping() {
    constexpr uint32_t camera = 0x02300000u;
    constexpr uint32_t planes = 0x0217B550u;
    g_main_ram.fill(0);
    const auto put = [](uint32_t addr, int32_t value) {
        bus_write_u32_slow(addr, static_cast<uint32_t>(value));
    };
    const auto get = [](uint32_t addr) {
        int32_t value;
        std::memcpy(&value, &g_main_ram[main_ram_offset(addr)], sizeof(value));
        return value;
    };
    put(0x0207722Cu, 0xFA0175FBu);
    put(0x020D5094u, planes);
    // The render camera can differ from the player camera (intro/replay).
    put(0x0217AC74u, 0x02310000u);
    put(0x0217AA4Cu, camera);
    put(camera + 0x6Cu, 0x1555);
    put(camera + 0x70u, 1024);
    put(camera + 0x80u, -795);
    put(camera + 0x84u, 795);
    // Native planes captured at the user's Luigi Circuit reproduction.
    put(planes, 3234);
    put(planes + 8u, 2511);
    put(planes + 12u, -3234);
    put(planes + 20u, 2511);
    const std::vector<uint8_t> native(g_main_ram.begin(), g_main_ram.end());
    g_render_width = 448;
    nds_title_patches_set_mkds_adaptive(false, 448);
    nds_title_patches_projection_begin();
    if (std::memcmp(native.data(), g_main_ram.data(), native.size())) return false;
    nds_title_patches_set_mkds_adaptive(true, 448);
    g_render_width = 256;
    nds_title_patches_projection_begin();
    if (std::memcmp(native.data(), g_main_ram.data(), native.size())) return false;
    g_render_width = 448;
    nds_title_patches_projection_begin();
    const int32_t x = get(planes), z = get(planes + 8u);
    // A point with |x/z| == 1 is outside the native side plane but inside
    // the widened side plane. Keep normalized normals and both side signs.
    if (2511 - 3234 >= 0 || z - x <= 0 || x < 2427 || x > 2431 ||
        z < 3296 || z > 3301 || get(planes + 12u) != -x ||
        get(planes + 20u) != z) return false;
    std::vector<uint8_t> allowed = native;
    for (uint32_t offset : {0u, 8u, 12u, 20u})
        std::memcpy(allowed.data() + main_ram_offset(planes + offset),
                    &g_main_ram[main_ram_offset(planes + offset)], 4);
    // Camera zoom, vertical planes, near/far clipping and unrelated memory
    // must remain byte-identical.
    if (std::memcmp(allowed.data(), g_main_ram.data(), allowed.size())) return false;
    nds_title_patches_projection_begin();
    if (std::memcmp(allowed.data(), g_main_ram.data(), allowed.size()) ||
        nds_title_patches_debug_state().mkds_object_wide_applied != 2u) return false;

    // A fresh native frustum from a changed FOV is widened again.
    put(camera + 0x80u, -1024);
    put(camera + 0x84u, 1024);
    put(planes, 2896);
    put(planes + 8u, 2896);
    put(planes + 12u, -2896);
    put(planes + 20u, 2896);
    nds_title_patches_projection_begin();
    if (get(planes + 8u) <= get(planes) ||
        nds_title_patches_debug_state().mkds_object_wide_applied != 4u) return false;
    const std::vector<uint8_t> wide(g_main_ram.begin(), g_main_ram.end());
    // Loading already widened planes into fresh host patch state must not
    // widen them cumulatively.
    nds_title_patches_set_mkds_adaptive(true, 448);
    nds_title_patches_projection_begin();
    if (std::memcmp(wide.data(), g_main_ram.data(), wide.size()) ||
        !nds_title_patches_debug_state().mkds_object_wide_active ||
        nds_title_patches_debug_state().mkds_object_wide_applied != 0u) return false;
    // The current output width, rather than the maximum, determines culling.
    put(planes, 2896); put(planes + 8u, 2896);
    put(planes + 12u, -2896); put(planes + 20u, 2896);
    g_render_width = 320;
    nds_title_patches_projection_begin();
    if (get(planes) < 2557 || get(planes) > 2561 ||
        get(planes + 8u) < 3196 || get(planes + 8u) > 3200) return false;
    put(0x0207722Cu, 0);
    nds_title_patches_projection_begin();
    if (nds_title_patches_debug_state().mkds_object_wide_active) return false;
    nds_title_patches_set_mkds_adaptive(false, 448);
    g_render_width = 256;
    return true;
}

bool test_mph_exact_wide_width() {
    constexpr uint32_t addresses[] = {0x02110820u,0x0211C620u,0x02110FF8u,0x02111000u};
    constexpr uint32_t native[] = {0x1555u,0xE2810001u,0xE5990670u,0xE1A00600u};
    const auto get=[](uint32_t address) {
        uint32_t value;std::memcpy(&value,g_main_ram.data()+main_ram_offset(address),4);return value;
    };
    const auto seed=[&]() {
        g_main_ram.fill(0);
        for(unsigned i=0;i<4;++i)bus_write_u32_slow(addresses[i],native[i]);
        bus_write_u32_slow(0x0211081Cu,0x021230CCu);
        bus_write_u32_slow(0x0211C618u,0xE59A1670u);
        bus_write_u32_slow(0x02110FFCu,0xE5991664u);
    };
    seed();const std::vector<uint8_t> original(g_main_ram.begin(),g_main_ram.end());
    // Start with the actual 16:9 width, then change existing supported widths
    // on the resident overlay. Check the emitted ARM computation numerically.
    for(uint16_t width:{342u,256u,320u,384u,448u,510u,342u}) {
        g_render_width=width;
        nds_title_patches_set_mph_adventure_wide(true,width);
        nds_title_patches_start_frame();
        const auto state=nds_title_patches_debug_state();
        if(!state.mph_adventure_wide_enabled || !state.mph_adventure_wide_active ||
            state.mph_adventure_wide_width!=width || !g_guest_wide)return false;
        uint32_t mov=get(addresses[2]),shift=get(addresses[3]);
        if((mov&0xFFFFF000u)!=0xE3A00000u || (shift&~0xF80u)!=0xE1A00000u)return false;
        unsigned rot=((mov>>8)&15u)*2u;uint32_t value=mov&255u;
        if(rot)value=(value>>rot)|(value<<(32u-rot));
        unsigned bits=(shift>>7)&31u;
        if((value<<bits)!=(uint32_t(width)<<12) ||
            get(addresses[0])!=(0x1555u*width+128u)/256u ||
            get(addresses[1])!=0xE2810000u+width-255u)return false;
        auto allowed=original;
        for(uint32_t address:addresses)
            std::memcpy(allowed.data()+main_ram_offset(address),g_main_ram.data()+main_ram_offset(address),4);
        if(!std::equal(g_main_ram.begin(),g_main_ram.end(),allowed.begin()))return false;
        nds_title_patches_start_frame();
        const auto repeated=nds_title_patches_debug_state();
        if(!std::equal(g_main_ram.begin(),g_main_ram.end(),allowed.begin()) || std::memcmp(state.mph_adventure_wide_site_applied,
            repeated.mph_adventure_wide_site_applied,sizeof state.mph_adventure_wide_site_applied))return false;
    }
    // A different instruction in the adjacent consumer must reject the whole
    // patch, preserving all words rather than installing half a constant.
    seed();bus_write_u32_slow(addresses[3],0xE1A00000u);const std::vector<uint8_t> foreign(g_main_ram.begin(),g_main_ram.end());
    nds_title_patches_set_mph_adventure_wide(true,342);g_render_width=342;
    nds_title_patches_start_frame();
    if(!std::equal(g_main_ram.begin(),g_main_ram.end(),foreign.begin()) || nds_title_patches_debug_state().mph_adventure_wide_active || g_guest_wide)return false;
    seed();g_render_width=256;const std::vector<uint8_t> narrow(g_main_ram.begin(),g_main_ram.end());
    nds_title_patches_start_frame();
    if(!std::equal(g_main_ram.begin(),g_main_ram.end(),narrow.begin()) || g_guest_wide)return false;
    nds_title_patches_set_mph_adventure_wide(false,342);g_render_width=256;
    return true;
}

int main() {
    if (!test_mph_exact_wide_width()) return 101;
    if (!test_mkds_object_clipping()) return 100;
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

    reset_main_ram(0);
    write8(kChosenHunter, 0x00u);
    write8(kMorphState, 0x02u);
    write8(kMorphState + kBoostingOffset, 0x00u);
    if (!require(nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 45;
    if (!require(!nds_title_patches_mph_should_release_touch_for_morph_boost(false)))
        return 46;

    reset_main_ram(0);
    write8(kChosenHunter, 0x00u);
    write8(kMorphState, 0x02u);
    write8(kMorphState + kBoostingOffset, 0x80u);
    if (!require(!nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 47;

    reset_main_ram(3);
    write8(kChosenHunter + 3u, 0x00u);
    write8(kChosenHunter + 2u, 0x05u);
    write8(kMorphState + 3u * kMorphStride, 0x02u);
    write8(kMorphState + 3u * kMorphStride + kBoostingOffset, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x02u);
    write8(kMorphState + 2u * kMorphStride + kBoostingOffset, 0x01u);
    if (!require(nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 51;

    reset_main_ram(1);
    write8(kChosenHunter + 1u, 0x05u);
    write8(kMorphState + kMorphStride, 0x02u);
    write8(kMorphState + kMorphStride + kBoostingOffset, 0x00u);
    if (!require(!nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 48;

    reset_main_ram(2);
    write8(kChosenHunter + 2u, 0x00u);
    write8(kMorphState + 2u * kMorphStride, 0x00u);
    write8(kMorphState + 2u * kMorphStride + kBoostingOffset, 0x00u);
    if (!require(!nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 49;

    reset_main_ram(0xFFu);
    write8(kChosenHunter, 0x00u);
    write8(kMorphState, 0x02u);
    if (!require(!nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 50;

    reset_main_ram(0);
    nds_title_patches_set_mph_mouse_aim(false);
    write8(kChosenHunter, 0x00u);
    write8(kMorphState, 0x02u);
    write8(kMorphState + kBoostingOffset, 0x00u);
    if (!require(!nds_title_patches_mph_should_release_touch_for_morph_boost(true)))
        return 52;
    nds_title_patches_set_mph_mouse_aim(true);

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

    // The campaign Omega slot survives every special-weapon hotkey, and can
    // be selected again after using Power Beam or missiles. Verify each local
    // player slot so another player's inventory cannot trigger the guard.
    for (uint8_t slot = 0; slot < 4u; ++slot) {
        const uint32_t offset = static_cast<uint32_t>(slot) * kMorphStride;
        for (uint8_t requested : {1u, 3u, 4u, 5u, 6u, 7u}) {
            reset_main_ram(slot);
            write8(kGameMode, 2);
            write8(kMapOrUserActionPaused, 0);
            write8(kSpecialWeaponSlot + offset, 8);
            write8(kSelectedWeapon + offset, 0);
            write8(kWeaponChange + offset, 0);
            write8(kJumpFlag + offset, 0);
            write8(kMorphState + offset, 0);
            if (!require(nds_title_patches_request_mph_weapon(requested)) ||
                !require(g_main_ram[main_ram_offset(kSelectedWeapon + offset)] == 8))
                return 45;
            if (!require(!nds_title_patches_request_mph_weapon(requested)))
                return 46;
            if (!require(nds_title_patches_request_mph_weapon(2)) ||
                !require(g_main_ram[main_ram_offset(kSelectedWeapon + offset)] == 2) ||
                !require(nds_title_patches_request_mph_weapon(requested)) ||
                !require(g_main_ram[main_ram_offset(kSelectedWeapon + offset)] == 8) ||
                !require(nds_title_patches_request_mph_weapon(0)))
                return 47;
            for (uint8_t other = 0; other < 4u; ++other) {
                if (other != slot &&
                    !require(unchanged(kSelectedWeapon + other * kMorphStride)))
                    return 48;
            }
            // Omega has no campaign lock in multiplayer.
            write8(kGameMode, 3);
            if (!require(nds_title_patches_request_mph_weapon(requested)) ||
                !require(g_main_ram[main_ram_offset(kSelectedWeapon + offset)] == requested))
                return 49;
            // Ordinary campaign special slots retain direct weapon selection.
            write8(kGameMode, 2);
            write8(kSpecialWeaponSlot + offset, 7);
            write8(kSelectedWeapon + offset, 0);
            if (!require(nds_title_patches_request_mph_weapon(requested)) ||
                !require(g_main_ram[main_ram_offset(kSelectedWeapon + offset)] == requested))
                return 50;
        }
    }

    return 0;
}
