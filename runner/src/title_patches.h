#pragma once

#include <cstdint>

struct NdsTitlePatchDebugState {
    bool mph_adventure_wide_enabled;
    bool mph_adventure_wide_active;
    uint16_t mph_adventure_wide_width;
    uint64_t mph_adventure_wide_site_applied[3];
    uint64_t mph_adventure_wide_frames_active;
    uint64_t mph_adventure_wide_frames_inactive;
    bool mkds_object_wide_enabled;
    bool mkds_object_wide_active;
    uint64_t mkds_object_wide_applied;
};

// Title-specific, opt-in presentation patches. Native DS execution never
// enables these paths.
void nds_title_patches_set_sm64ds_adaptive(bool enabled);
void nds_title_patches_set_mkds_adaptive(bool enabled, uint16_t adaptive_width);
// Called on a direct GX projection-mode write, after MKDS rebuilds its
// camera-space object planes and before it transforms them into world space.
void nds_title_patches_projection_begin();
void nds_title_patches_set_mph_mouse_aim(bool enabled);
// MPH adventure mode: rebuild the guest's own frusta at the host's adaptive
// top width so per-room sub-frusta and entity sphere culling cover the whole
// band. `adaptive_width` must equal the host 3D render width.
void nds_title_patches_set_mph_adventure_wide(bool enabled,
                                              uint16_t adaptive_width);
void nds_title_patches_set_mph_adaptive(bool enabled);
bool nds_title_patches_apply_mph_mouse_delta(int32_t dx, int32_t dy);
bool nds_title_patches_mph_local_morph_ball();
bool nds_title_patches_mph_should_release_touch_for_morph_boost(bool boost_held);
bool nds_title_patches_request_mph_weapon(uint8_t weapon_index);
bool nds_title_patches_mph_adaptive_centered_native();
NdsTitlePatchDebugState nds_title_patches_debug_state();
void nds_title_patches_start_frame();
