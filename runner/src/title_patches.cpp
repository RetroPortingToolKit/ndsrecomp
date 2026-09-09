#include "title_patches.h"

#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "gpu3d.h"
#include "runtime_arm.h"
#include "state.h"

namespace {

constexpr uint32_t kMainRamBase = 0x02000000u;
constexpr uint32_t kSm64dsClipper = 0x0209F43Cu;
constexpr uint32_t kPlane0 = kSm64dsClipper + 0x04u;
constexpr uint32_t kPlane2 = kSm64dsClipper + 0x1Cu;
constexpr uint32_t kAspect = kSm64dsClipper + 0x4Cu;
constexpr int32_t kNativeAspect = 0x1555;
constexpr int32_t kWideAspect = 0x2555;  // round(0x1555 * 448 / 256)
constexpr double kWideScale = 448.0 / 256.0;
constexpr double kFix12One = 4096.0;
// AMHE0's native touch-look routine consumes these signed, per-frame fields.
// Feeding deltas here while holding the stylus at center preserves the game
// path but removes the finite physical touchscreen edge.
constexpr uint32_t kMphUs10PlayerPosition = 0x020D9CB8u;
constexpr uint32_t kMphUs10MorphState = 0x020DA818u;
constexpr uint32_t kMphUs10JumpFlag = 0x020DABD9u;
constexpr uint32_t kMphUs10WeaponChange = 0x020DABDBu;
constexpr uint32_t kMphUs10SelectedWeapon = 0x020DABE3u;
constexpr uint32_t kMphUs10GameMode = 0x020E78FCu;
constexpr uint32_t kMphUs10MapOrUserActionPaused = 0x020FB458u;
constexpr uint32_t kMphUs10AimX = 0x020DE526u;
constexpr uint32_t kMphUs10AimY = 0x020DE52Eu;
constexpr uint32_t kMphUs10MorphStride = 0xF30u;
constexpr uint32_t kMphUs10AimStride = 0x48u;
constexpr uint8_t kMphUs10MaxPlayerPosition = 3u;
constexpr uint32_t kMphOverlay0Identity = 0x02102228u;
constexpr uint32_t kMphOverlay0IdentityValue = 0xE59F106Cu;
constexpr uint32_t kMphFrontendMenuList = 0x0214C7E0u;
constexpr int32_t kMphFrontendMenuListValue = 0x021D8DD4u;
constexpr uint32_t kMphFrontendCurrentMenu = 0x0214C7E4u;
constexpr int32_t kMphBriefingMenuValue = 0x021DB9CCu;

// AMHE0 adventure mode builds every frustum from a 4:3 literal and from two
// aspect numerators baked into Camera_SetupProjection and Rooms_TraverseAndDraw.
// Widening those three words in place makes the guest itself produce band-wide
// frusta, sub-frusta and portal screen bboxes; the neighbours are residency
// guards, so multiplayer overlays and menus simply fail the match.
constexpr uint32_t kMphWideSiteCount = 3u;
struct MphWideSite {
    uint32_t addr;
    uint32_t native;
    uint32_t neighbor_addr;
    uint32_t neighbor;
};
constexpr MphWideSite kMphWideSites[kMphWideSiteCount] = {
    {0x02110820u, 0x00001555u, 0x0211081Cu, 0x021230CCu},
    {0x0211C620u, 0xE2810001u, 0x0211C618u, 0xE59A1670u},
    {0x02110FF8u, 0xE5990670u, 0x02110FFCu, 0xE5991664u},
};

bool g_sm64ds_adaptive = false;
bool g_mph_mouse_aim = false;
bool g_mph_adaptive = false;
bool g_mph_adaptive_centered_native = false;
bool g_logged_sm64ds_clipper = false;

bool g_mph_adventure_wide = false;
uint16_t g_mph_adventure_width = 0;
uint32_t g_mph_wide_words[kMphWideSiteCount] = {};
uint32_t g_mph_jump_restore_addr = 0;
uint8_t g_mph_jump_restore_value = 0;
uint8_t g_mph_jump_restore_player_position = 0;
uint8_t g_mph_jump_restore_frames = 0;
bool g_mph_wide_active = false;
bool g_logged_mph_wide_active = false;
bool g_logged_mph_wide_lost = false;
bool g_logged_mph_wide_width = false;
uint64_t g_mph_wide_applied[kMphWideSiteCount] = {};
uint64_t g_mph_wide_frames_active = 0;
uint64_t g_mph_wide_frames_inactive = 0;

bool read_main_ram32(uint32_t addr, int32_t* out) {
    if (!out || addr < kMainRamBase) return false;
    BusRegion main_ram{};
    if (!bus_get_region("mainram", &main_ram)) return false;
    const uint32_t offset = addr - kMainRamBase;
    if (offset > main_ram.len || main_ram.len - offset < sizeof(*out))
        return false;
    std::memcpy(out, main_ram.ptr + offset, sizeof(*out));
    return true;
}

bool read_main_ram_word(uint32_t addr, uint32_t* out) {
    int32_t value = 0;
    if (!out || !read_main_ram32(addr, &value)) return false;
    *out = static_cast<uint32_t>(value);
    return true;
}

bool read_main_ram8(uint32_t addr, uint8_t* out) {
    if (!out || addr < kMainRamBase) return false;
    BusRegion main_ram{};
    if (!bus_get_region("mainram", &main_ram)) return false;
    const uint32_t offset = addr - kMainRamBase;
    if (offset >= main_ram.len) return false;
    *out = main_ram.ptr[offset];
    return true;
}

bool mph_local_player_position(uint8_t* out) {
    uint8_t player_position = 0;
    if (!read_main_ram8(kMphUs10PlayerPosition, &player_position) ||
        player_position > kMphUs10MaxPlayerPosition) {
        return false;
    }
    if (out) *out = player_position;
    return true;
}

void cancel_mph_jump_restore() {
    g_mph_jump_restore_addr = 0;
    g_mph_jump_restore_value = 0;
    g_mph_jump_restore_player_position = 0;
    g_mph_jump_restore_frames = 0;
}

uint16_t clamp_signed16_bits(int32_t value) {
    return static_cast<uint16_t>(
        std::clamp(value, static_cast<int32_t>(INT16_MIN),
                   static_cast<int32_t>(INT16_MAX)));
}

// ARM data-processing immediates are an 8-bit value rotated right by an even
// amount; not every width is representable, so the caller disables the feature
// rather than emitting a wrong constant.
bool encode_arm_mov_r0_imm(uint32_t value, uint32_t* out) {
    for (uint32_t rot = 0; rot < 16u; ++rot) {
        const uint32_t shift = rot * 2u;
        const uint32_t imm8 = shift == 0u
            ? value
            : ((value << shift) | (value >> (32u - shift)));
        if (imm8 <= 0xFFu) {
            *out = 0xE3A00000u | (rot << 8) | imm8;
            return true;
        }
    }
    return false;
}

void patch_mph_adventure_wide() {
    // The wide words bake the adaptive width into guest constants, so they
    // are only correct when the 3D engine actually renders at that width.
    // Headless/serve runs keep the native 256-wide surface; leave the guest
    // untouched there instead of widening its frusta against a native render.
    if (nds_gpu3d_output_width() != g_mph_adventure_width) {
        ++g_mph_wide_frames_inactive;
        if (!g_logged_mph_wide_width) {
            std::fprintf(stderr,
                         "[mph] adventure wide frustum idle (render width %u, "
                         "configured %u)\n",
                         static_cast<unsigned>(nds_gpu3d_output_width()),
                         static_cast<unsigned>(g_mph_adventure_width));
            g_logged_mph_wide_width = true;
        }
        g_mph_wide_active = false;
        nds_gpu3d_set_guest_wide_projection(false);
        return;
    }

    bool active = true;
    for (uint32_t i = 0; i < kMphWideSiteCount; ++i) {
        const MphWideSite& site = kMphWideSites[i];
        uint32_t word = 0;
        uint32_t neighbor = 0;
        if (!read_main_ram_word(site.addr, &word) ||
            !read_main_ram_word(site.neighbor_addr, &neighbor)) {
            active = false;
            continue;
        }
        if (word == site.native && neighbor == site.neighbor) {
            bus_write_u32_slow(site.addr, g_mph_wide_words[i]);
            ++g_mph_wide_applied[i];
            if (!read_main_ram_word(site.addr, &word)) {
                active = false;
                continue;
            }
        }
        active &= (word == g_mph_wide_words[i]);
    }

    if (active) ++g_mph_wide_frames_active;
    else ++g_mph_wide_frames_inactive;

    if (active && !g_logged_mph_wide_active) {
        std::fprintf(stderr,
                     "[mph] adventure wide frustum enabled (%u px)\n",
                     static_cast<unsigned>(g_mph_adventure_width));
        g_logged_mph_wide_active = true;
    } else if (!active && g_logged_mph_wide_active &&
               !g_logged_mph_wide_lost) {
        std::fprintf(stderr,
                     "[mph] adventure wide frustum inactive "
                     "(overlay not resident)\n");
        g_logged_mph_wide_lost = true;
    }

    g_mph_wide_active = active;
    nds_gpu3d_set_guest_wide_projection(active);
}

void widen_horizontal_plane(uint32_t addr) {
    int32_t x = 0;
    int32_t z = 0;
    if (!read_main_ram32(addr, &x) ||
        !read_main_ram32(addr + 8u, &z))
        return;

    const double wide_z = static_cast<double>(z) * kWideScale;
    const double length = std::hypot(static_cast<double>(x), wide_z);
    if (length < 1.0) return;

    const int32_t new_x =
        static_cast<int32_t>(std::lround(x * kFix12One / length));
    const int32_t new_z =
        static_cast<int32_t>(std::lround(wide_z * kFix12One / length));
    bus_write_u32_slow(addr, static_cast<uint32_t>(new_x));
    bus_write_u32_slow(addr + 8u, static_cast<uint32_t>(new_z));
}

void patch_sm64ds_clipper() {
    int32_t aspect = 0;
    if (!read_main_ram32(kAspect, &aspect) || aspect != kNativeAspect)
        return;

    // SM64DS derives four fixed-point frustum planes from this aspect field.
    // The horizontal planes are 0 and 2. Scale their Z component by the host
    // presentation ratio and renormalize them to Fix12 unit length. Updating
    // the aspect field makes later game-side recomputations preserve the wide
    // frustum; if the game installs a fresh native camera, this runs again.
    widen_horizontal_plane(kPlane0);
    widen_horizontal_plane(kPlane2);
    bus_write_u32_slow(kAspect, static_cast<uint32_t>(kWideAspect));

    if (!g_logged_sm64ds_clipper) {
        std::fprintf(stderr,
                     "[sm64ds] adaptive 21:9 actor frustum enabled\n");
        g_logged_sm64ds_clipper = true;
    }
}

bool mph_briefing_active() {
    int32_t overlay_identity = 0;
    int32_t menu_list = 0;
    int32_t current_menu = 0;
    return read_main_ram32(kMphOverlay0Identity, &overlay_identity) &&
           read_main_ram32(kMphFrontendMenuList, &menu_list) &&
           read_main_ram32(kMphFrontendCurrentMenu, &current_menu) &&
           static_cast<uint32_t>(overlay_identity) == kMphOverlay0IdentityValue &&
           menu_list == kMphFrontendMenuListValue &&
           current_menu == kMphBriefingMenuValue;
}

}  // namespace

void nds_title_patches_set_sm64ds_adaptive(bool enabled) {
    g_sm64ds_adaptive = enabled;
}

void nds_title_patches_set_mph_mouse_aim(bool enabled) {
    g_mph_mouse_aim = enabled;
    if (!enabled) cancel_mph_jump_restore();
}

void nds_title_patches_set_mph_adventure_wide(bool enabled,
                                              uint16_t adaptive_width) {
    g_mph_adventure_wide = false;
    g_mph_adventure_width = 0;
    if (enabled) {
        uint32_t mov_r0_width = 0;
        if (adaptive_width < 256u || adaptive_width > 510u) {
            std::fprintf(stderr,
                "[mph] adaptive width %u is out of range for the adventure "
                "wide frustum; leaving it disabled\n",
                static_cast<unsigned>(adaptive_width));
        } else if (!encode_arm_mov_r0_imm(adaptive_width, &mov_r0_width)) {
            std::fprintf(stderr,
                "[mph] adaptive width %u is not an ARM immediate; leaving the "
                "adventure wide frustum disabled\n",
                static_cast<unsigned>(adaptive_width));
        } else {
            g_mph_wide_words[0] = static_cast<uint32_t>(
                (kNativeAspect * adaptive_width + 128) / 256);
            g_mph_wide_words[1] = 0xE2810000u | (adaptive_width - 255u);
            g_mph_wide_words[2] = mov_r0_width;
            g_mph_adventure_wide = true;
            g_mph_adventure_width = adaptive_width;
        }
    }
    if (!g_mph_adventure_wide) {
        g_mph_wide_active = false;
        nds_gpu3d_set_guest_wide_projection(false);
    }
}

void nds_title_patches_set_mph_adaptive(bool enabled) {
    g_mph_adaptive = enabled;
    if (!enabled) g_mph_adaptive_centered_native = false;
}

bool nds_title_patches_apply_mph_mouse_delta(int32_t dx, int32_t dy) {
    if (!g_mph_mouse_aim || (dx == 0 && dy == 0)) return false;
    uint8_t player_position = 0;
    if (!mph_local_player_position(&player_position)) {
        return false;
    }
    const uint32_t player_aim_offset =
        static_cast<uint32_t>(player_position) * kMphUs10AimStride;
    if (dx != 0)
        bus_write_u16_slow(kMphUs10AimX + player_aim_offset,
                           clamp_signed16_bits(dx));
    if (dy != 0)
        bus_write_u16_slow(kMphUs10AimY + player_aim_offset,
                           clamp_signed16_bits(dy));
    return true;
}

bool nds_title_patches_mph_local_morph_ball() {
    if (!g_mph_mouse_aim) return false;
    uint8_t player_position = 0;
    if (!mph_local_player_position(&player_position)) {
        return false;
    }
    uint8_t morph_state = 0;
    return read_main_ram8(
               kMphUs10MorphState +
                   static_cast<uint32_t>(player_position) * kMphUs10MorphStride,
               &morph_state) &&
           morph_state == 0x02u;
}

bool nds_title_patches_request_mph_weapon(uint8_t weapon_index) {
    if (!g_mph_mouse_aim || weapon_index > 8u) return false;
    uint8_t player_position = 0;
    if (!mph_local_player_position(&player_position)) return false;
    uint8_t game_mode = 0;
    uint8_t map_paused = 0;
    if (read_main_ram8(kMphUs10GameMode, &game_mode) && game_mode == 0x02u &&
        read_main_ram8(kMphUs10MapOrUserActionPaused, &map_paused) &&
        map_paused == 0x01u) {
        return false;
    }

    const uint32_t player_offset =
        static_cast<uint32_t>(player_position) * kMphUs10MorphStride;
    const uint32_t selected_weapon_addr =
        kMphUs10SelectedWeapon + player_offset;
    const uint32_t weapon_change_addr = kMphUs10WeaponChange + player_offset;
    const uint32_t jump_flag_addr = kMphUs10JumpFlag + player_offset;
    uint8_t selected_weapon = 0;
    uint8_t weapon_change = 0;
    uint8_t jump_flag = 0;
    if (!read_main_ram8(selected_weapon_addr, &selected_weapon) ||
        !read_main_ram8(weapon_change_addr, &weapon_change) ||
        !read_main_ram8(jump_flag_addr, &jump_flag)) {
        return false;
    }
    if (selected_weapon == weapon_index) return false;

    const bool is_transforming = (jump_flag & 0x10u) != 0;
    const uint8_t original_jump_low = jump_flag & 0x0Fu;
    const bool needs_jump_restore =
        !is_transforming && original_jump_low == 0 &&
        !nds_title_patches_mph_local_morph_ball();
    if (needs_jump_restore) {
        bus_write_u8_slow(jump_flag_addr,
                          static_cast<uint8_t>((jump_flag & 0xF0u) | 0x01u));
        g_mph_jump_restore_addr = jump_flag_addr;
        g_mph_jump_restore_value = original_jump_low;
        g_mph_jump_restore_player_position = player_position;
        g_mph_jump_restore_frames = 4;
    }

    bus_write_u8_slow(weapon_change_addr,
                      static_cast<uint8_t>((weapon_change & 0xF0u) | 0x0Bu));
    bus_write_u8_slow(selected_weapon_addr, weapon_index);
    return true;
}

bool nds_title_patches_mph_adaptive_centered_native() {
    return g_mph_adaptive_centered_native;
}

static_assert(kMphWideSiteCount ==
              sizeof(NdsTitlePatchDebugState::mph_adventure_wide_site_applied) /
                  sizeof(uint64_t));

NdsTitlePatchDebugState nds_title_patches_debug_state() {
    NdsTitlePatchDebugState state{};
    state.mph_adventure_wide_enabled = g_mph_adventure_wide;
    state.mph_adventure_wide_active = g_mph_wide_active;
    state.mph_adventure_wide_width = g_mph_adventure_width;
    for (uint32_t i = 0; i < kMphWideSiteCount; ++i)
        state.mph_adventure_wide_site_applied[i] = g_mph_wide_applied[i];
    state.mph_adventure_wide_frames_active = g_mph_wide_frames_active;
    state.mph_adventure_wide_frames_inactive = g_mph_wide_frames_inactive;
    return state;
}

void nds_title_patches_start_frame() {
    // The frontend presents the completed framebuffer after this boundary.
    // Keep this title-specific scene decision on the same frame boundary.
    g_mph_adaptive_centered_native =
        g_mph_adaptive && mph_briefing_active();
    if (!g_mph_mouse_aim) cancel_mph_jump_restore();
    if (g_mph_jump_restore_frames != 0) {
        uint8_t player_position = 0;
        uint8_t jump_flag = 0;
        const uint32_t expected_addr =
            kMphUs10JumpFlag +
            static_cast<uint32_t>(g_mph_jump_restore_player_position) *
                kMphUs10MorphStride;
        if (!mph_local_player_position(&player_position) ||
            player_position != g_mph_jump_restore_player_position ||
            g_mph_jump_restore_addr != expected_addr ||
            !read_main_ram8(g_mph_jump_restore_addr, &jump_flag) ||
            (jump_flag & 0x10u) != 0 || (jump_flag & 0x0Fu) != 0x01u) {
            cancel_mph_jump_restore();
        } else {
            --g_mph_jump_restore_frames;
            if (g_mph_jump_restore_frames == 0) {
                bus_write_u8_slow(
                    g_mph_jump_restore_addr,
                    static_cast<uint8_t>((jump_flag & 0xF0u) |
                                         (g_mph_jump_restore_value & 0x0Fu)));
                cancel_mph_jump_restore();
            }
        }
    }
    if (g_sm64ds_adaptive) patch_sm64ds_clipper();
    if (g_mph_adventure_wide) patch_mph_adventure_wide();
}
