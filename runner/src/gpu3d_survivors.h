// SPDX-License-Identifier: MIT
// Native-density 2D survivors paired with a separately owned GPU 3D image.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace nds_survivors {
inline constexpr unsigned Rows=192, NativeWidth=256, MaxWidth=448, RowWords=12;
enum Field : unsigned {
    Bldcnt=0, MasterBright=1, Eva=2, Evb=3, Evy=4, Priority3D=5,
    RenderX=6, Screen=7, LayerWidth=8, Flags=9, SourceWidth=10, Reserved=11
};
enum Flag : uint32_t { Needs3D=1, Visible3D=2, CenterNative=4, SkyboxFill=8 };
// Four uints per native cell: top RGB6/meta, below RGB6/meta. Metadata is
// target[0:5], BG0-hidden[6], effects[7], alpha[8:15], priority[16:23],
// order[24:31]. CPU native and adaptive views share decoded BG inputs but
// retain their own OBJ placement/window selection. Native observations must
// not recover an anchored native HUD by cropping the adaptive view.
struct Frame {
    std::vector<uint32_t> native_layers,wide_layers;
    std::array<uint32_t,Rows*RowWords> rows{};
    std::array<uint16_t,2> widths{256,256};
    std::array<const uint32_t*,2> native_base{};
    std::array<bool,2> needs_gpu{};
    uint64_t identity{};
    uint32_t buffer{};
    uint16_t lines{};
    bool capture{};
    uint32_t* row(unsigned y){return rows.data()+y*RowWords;}
    const uint32_t* row(unsigned y)const{return rows.data()+y*RowWords;}
};
}

void nds_gpu2d_set_survivors(bool enabled);
bool nds_gpu2d_survivors_enabled();
void nds_gpu2d_set_survivor_widths(uint16_t top,uint16_t bottom);
// Current joins the existing CPU workers before returning immutable data.
// Pending is used at line192 only to pair the GPU source; it does not join.
const nds_survivors::Frame* nds_gpu2d_survivor_current();
const nds_survivors::Frame* nds_gpu2d_survivor_pending();
// Explicit snapshot callers already drain the CPU line jobs before this call.
const nds_survivors::Frame* nds_gpu2d_survivor_building();
