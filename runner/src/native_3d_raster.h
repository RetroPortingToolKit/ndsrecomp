// SPDX-License-Identifier: MIT
#pragma once
#include "native_3d.h"
namespace melonDS { class GPU3D; }
// Adapter to the existing raster ABI. Geometry and guest observations are
// owned by Service; renderer state/tables remain owned by the bridge.
void nds_native_3d_raster(const nds_native_3d::Frame& frame,
                          melonDS::GPU3D& raster, bool guest_wide);
void nds_native_3d_latch_registers(melonDS::GPU3D& raster);
