// SPDX-License-Identifier: GPL-3.0-or-later
// Compare the actual GL dispatches and the pack queued before display work.
#include "GPU3D_Compute.h"
#include "GPU3D_Soft.h"
#include "NDS.h"
#include "Platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <vector>

namespace melonDS {
void NDS::SetIRQ(u32, u32) {}
void NDS::ClearIRQ(u32, u32) {}
void NDS::CheckDMAs(u32, u32) {}
void NDS::GXFIFOStall() {}
void NDS::GXFIFOUnstall() {}
bool GPU::MakeVRAMFlat_TextureCoherent(NonStupidBitField<1024>&) noexcept { return false; }
bool GPU::MakeVRAMFlat_TexPalCoherent(NonStupidBitField<256>&) noexcept { return false; }
namespace Platform {
void Log(LogLevel, const char* fmt, ...) {
    va_list args; va_start(args, fmt); std::vfprintf(stderr, fmt, args); va_end(args);
}
struct Thread {};
struct Semaphore {};
Thread* Thread_Create(std::function<void()>) { throw std::runtime_error("unexpected renderer thread"); }
void Thread_Free(Thread*) {}
void Thread_Wait(Thread*) {}
Semaphore* Semaphore_Create() { return new Semaphore; }
void Semaphore_Free(Semaphore* s) { delete s; }
void Semaphore_Reset(Semaphore*) {}
void Semaphore_Wait(Semaphore*) {}
void Semaphore_Post(Semaphore*, int) {}
}
}

namespace {
using namespace melonDS;
void require(bool value, const char* why) { if (!value) throw std::runtime_error(why); }
bool required = false;
unsigned callbacks = 0;
ComputeRenderer* active = nullptr;
GLsync packed = nullptr;
bool captureRequired() { return required; }
void captureReady() {
    ++callbacks;
    // Exercise the original GL4.3 PBO path and its binding/lifetime contract.
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
    active->PrepareCaptureFrame();
    packed = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
}
std::vector<u32> pixels(GLuint texture, unsigned width, unsigned height, GLenum format) {
    std::vector<u32> result(width * height);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glGetTexImage(GL_TEXTURE_2D, 0, format, GL_UNSIGNED_BYTE, result.data());
    require(glGetError() == GL_NO_ERROR, "texture readback GL error");
    return result;
}
void compile(ComputeRenderer& renderer) {
    while (renderer.NeedsShaderCompile()) {
        int current = 0, count = 0;
        renderer.ShaderCompileStep(current, count);
    }
    require(glGetError() == GL_NO_ERROR, "compute shaders/setup GL error");
}
void compare(GPU& gpu, const char* label) {
    auto& g = gpu.GPU3D;
    required = false;
    g.RenderFrameIdentical = false;
    active->RenderFrame(gpu);
    auto expected = pixels(active->GetLowResTexture(), 448, 192, GL_RGBA_INTEGER);
    const auto scale = active->GetScaleFactor();
    auto display = pixels(active->GetHiResTexture(), 448 * scale, 192 * scale, GL_RGBA);
    unsigned before = callbacks;
    required = true;
    active->RenderFrame(gpu);
    require(callbacks == before + 1 && packed, "native completion did not fire exactly once");
    auto status = glClientWaitSync(packed, GL_SYNC_FLUSH_COMMANDS_BIT, 5000000000ull);
    require(status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED, "native pack fence failed");
    auto* data = static_cast<const u32*>(glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY));
    require(data && std::equal(expected.begin(), expected.end(), data), "early native pack differs from complete original raster");
    require(glUnmapBuffer(GL_PIXEL_PACK_BUFFER) == GL_TRUE, "native pack was invalidated");
    glDeleteSync(packed); packed = nullptr;
    require(pixels(active->GetLowResTexture(), 448, 192, GL_RGBA_INTEGER) == expected,
            "display continuation changed native capture");
    require(pixels(active->GetHiResTexture(), 448 * scale, 192 * scale, GL_RGBA) == display,
            "complete enhanced display differs");
    std::printf("%s: early packed native + complete enhanced pixels equal\n", label);
}
void tests() {
    auto nds = std::make_unique<NDS>();
    auto& gpu = nds->GPU;
    auto& g = gpu.GPU3D;
    g.Reset(); g.SetEnabled(true, true); g.SetRenderWidth(448);
    auto renderer = ComputeRenderer::New();
    require(renderer != nullptr, "compute renderer unavailable");
    active = renderer.get();
    active->SetNativeCaptureCallbacks(captureRequired, captureReady);
    active->SetRenderWidth(448); active->SetRenderSettings(4, true); compile(*active);
    g.SetCurrentRenderer(std::move(renderer));
    g.RenderClearAttr1 = (45u << 24) | (31u << 16) | (31u << 10) | (7u << 5) | 15u;
    g.RenderClearAttr2 = 0x7FFF;
    compare(gpu, "empty clear");

    const u32 stream[] = {0x10u, 1u, 0x15u, 0x20u, 0x001Fu,
        0x29u, 0x121F00C0u, 0x60u, 0xBFFF0000u, 0x40u, 0u,
        0x23u, 0u, 0u, 0x23u, 0x1000u, 0u, 0x23u, 0x10000000u, 0u, 0x41u};
    for (u32 word : stream) { g.WriteToGXFIFO(word); nds->ARM9Timestamp += 1000; g.Run(); }
    require(g.NumPolygons > 0, "fixture did not emit geometry");
    g.FlushRequest = 1; g.ClearAttr1 = 0; g.ClearAttr2 = 0x7FFF; g.VBlank();
    // Fractional enhanced coordinates exercise the exact existing scaled
    // samples; this is deliberately not a substitute native-resolution draw.
    g.RenderPolygonRAM[0]->Vertices[0]->HiresPosition[0] += 7;
    g.RenderPolygonRAM[0]->Vertices[1]->HiresPosition[1] += 11;
    g.RenderPolygonRAM[0]->Attr |= 1u << 15; // actually exercise enabled fog
    for (unsigned i = 0; i < 8; ++i) {
        g.RenderDispCnt = ((i & 1) ? 1u << 5 : 0) |
                          ((i & 2) ? 1u << 7 : 0) |
                          ((i & 4) ? 1u << 4 : 0);
        g.RenderFogColor = 0x103F001Fu;
        g.RenderFogOffset = 0;
        std::fill_n(g.RenderFogDensityTable, 34, 64);
        std::fill_n(g.RenderEdgeTable, 8, 0x03E0);
        compare(gpu, "solid geometry/edge/fog/AA");
        if (i == 0) {
            auto drawn = pixels(active->GetLowResTexture(), 448, 192, GL_RGBA_INTEGER);
            require(std::count_if(drawn.begin(), drawn.end(), [](u32 p) { return (p & 0x1F3F3F3Fu) != 0; }) > 100,
                    "geometry comparison was vacuous");
        }
    }
    g.RenderPolygonRAM[0]->WBuffer = true;
    compare(gpu, "W-buffer geometry");
    auto* polygon = g.RenderPolygonRAM[0];
    polygon->TexParam = 7u << 26; // direct-colour 8x8 texture at offset zero
    polygon->Attr = (polygon->Attr & ~0x1F0000u) | 0x100000u;
    polygon->Translucent = true;
    for (unsigned i = 0; i < 64; ++i) {
        const u16 texel = static_cast<u16>(0x8000u | ((i & 31u) << 5u) | 0x401Fu);
        std::memcpy(gpu.VRAMFlat_Texture + 2u * i, &texel, sizeof texel);
    }
    polygon->Vertices[1]->TexCoords[0] = 7 * 16;
    polygon->Vertices[2]->TexCoords[1] = 7 * 16;
    g.RenderDispCnt |= (1u << 0) | (1u << 3);
    compare(gpu, "textured translucent geometry");
    // No consumer: preserve the ordinary complete job, with no native pack.
    required = false; unsigned before = callbacks;
    active->RenderFrame(gpu);
    require(callbacks == before, "display-only job queued a guest readback");
    // Identical frame reuse has no new job and must not enqueue another pack.
    required = true; g.RenderFrameIdentical = true;
    active->RenderFrame(gpu);
    require(callbacks == before, "identical frame queued a duplicate native pack");
    // Odd scale exercises the different spacing of the neighbour halo.
    active->SetRenderSettings(3, true); compile(*active);
    g.RenderFrameIdentical = false;
    compare(gpu, "odd-scale native sample/edge halo");
    before = callbacks;
    // Low scales retain the unsplit portable compute job and compile without
    // the qualification shader interface, even with callbacks configured.
    active->SetRenderSettings(2, true); compile(*active);
    g.RenderFrameIdentical = false;
    active->RenderFrame(gpu);
    require(callbacks == before, "low-scale fallback queued a split capture");
    require(glGetError() == GL_NO_ERROR, "GL error after capture/display jobs");
    g.SetCurrentRenderer(std::make_unique<SoftRenderer>()); active = nullptr;
}
}
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("compute capture contract test", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    auto context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::fprintf(stderr, "GL context unavailable: %s\n", SDL_GetError());
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 77;
    }
    int result = 0;
    try { tests(); std::puts("native capture before enhanced display contract passed"); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); result = 1; }
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return result;
}
