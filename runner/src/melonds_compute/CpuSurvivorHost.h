// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Window;
struct NdsGpu2dSaveState;
struct NdsComputePresentTicks;
namespace nds_survivors { struct Frame; }
bool nds_survivor_host_requested();
bool nds_survivor_host_start(SDL_Renderer* bottom=nullptr);
bool nds_survivor_host_enabled();
void nds_survivor_host_stop();
void nds_survivor_host_capture();
void nds_survivor_host_invalidate();
// Detach every shared target before SDL can process a pending renderer reset.
bool nds_survivor_host_reset_bottom();
// Snapshot callers first drain CPU scanline jobs. Ordinary capture never
// joins them. The host redirects GL work from the producer to its main owner.
bool nds_survivor_host_snapshot(NdsGpu2dSaveState*);
// Returned pixels belong to independent [screen][view] caches until that
// same screen/view is read again. Frame pointers require the existing CPU
// presentation ownership/join contract; read() never substitutes current().
const uint32_t* nds_survivor_host_observe(int screen,bool adaptive,uint16_t* width);
const uint32_t* nds_survivor_host_read(const nds_survivors::Frame*,int screen,bool adaptive,uint16_t* width);
std::string nds_survivor_host_json();
bool nds_survivor_host_present_top(SDL_Window*,const nds_survivors::Frame*,const uint32_t* black,const uint32_t* white,unsigned antialiasing,NdsComputePresentTicks*);
SDL_Texture* nds_survivor_host_present_bottom(const nds_survivors::Frame*,int viewportWidth,int viewportHeight,const uint32_t* black,const uint32_t* white,unsigned antialiasing,NdsComputePresentTicks*);
