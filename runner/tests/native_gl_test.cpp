// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Matthew Stanley. Derived from melonDS device interfaces.
#include "GPU3D_OpenGL.h"
#include "CaptureReference.h"
#include "Platform.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
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
std::vector<u32> pixels(GLRenderer& renderer, unsigned width) {
    std::vector<u32> result(width * 192);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, renderer.GetLowResTexture());
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, result.data());
    require(glGetError() == GL_NO_ERROR, "native readback GL error");
    return result;
}
u32 color(u32 packed) { return packed & 0x1F3F3F3Fu; }
u32 id(u32 packed) { return ((packed >> 6) & 3) | (((packed >> 14) & 3) << 2) | (((packed >> 22) & 3) << 4); }
void tests() {
    auto nds = std::make_unique<NDS>();
    auto& gpu = nds->GPU;
    auto& g = gpu.GPU3D;
    g.Reset(); g.SetEnabled(true, true); g.SetRenderWidth(448);
    auto renderer = GLRenderer::New();
    require(renderer != nullptr, "native shaders failed to compile");
    auto* native = renderer.get();
    native->SetRenderWidth(448); native->SetRenderSettings(2, true);
    g.SetCurrentRenderer(std::move(renderer));
    // Unequal channels expose swizzle mistakes; nontrivial ID exercises all
    // six transport bits. Full clear also catches incomplete wide FBO setup.
    g.RenderClearAttr1 = (45u << 24) | (31u << 16) | (31u << 10) | (7u << 5) | 15u;
    g.RenderClearAttr2 = 0x7FFF;
    native->RenderFrame(gpu);
    auto clear = pixels(*native, 448);
    require(std::all_of(clear.begin(), clear.end(), [&](u32 p) { return p == clear[0]; }), "wide clear is incomplete");
    require(id(clear[0]) == 45 && color(clear[0]) == 0x1F3F0E1Eu, "RGB6/alpha5/polyID transport differs");
    GLint width = 0, height = 0;
    glBindTexture(GL_TEXTURE_2D, native->GetHiResTexture());
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
    require(width == 896 && height == 384, "HD output dimensions differ");

    // Execute actual guest geometry commands, then draw its resulting list.
    const u32 stream[] = {0x10u, 1u, 0x15u, 0x20u, 0x001Fu,
        0x29u, 0x121F00C0u, 0x60u, 0xBFFF0000u, 0x40u, 0u,
        0x23u, 0u, 0u, 0x23u, 0x1000u, 0u, 0x23u, 0x10000000u, 0u, 0x41u};
    for (u32 word : stream) { g.WriteToGXFIFO(word); nds->ARM9Timestamp += 1000; g.Run(); }
    require(g.NumPolygons > 0, "fixture did not emit geometry");
    g.FlushRequest = 1; g.ClearAttr1 = 0; g.ClearAttr2 = 0x7FFF; g.VBlank();
    native->RenderFrame(gpu);
    auto triangle = pixels(*native, 448);
    require(std::count_if(triangle.begin(), triangle.end(), [](u32 p) { return color(p) != 0; }) > 100,
            "native triangle draw produced no area");

    CaptureReference capture;
    capture.Latch(gpu);
    std::vector<u32> reference(capture.GetLine(50, 0, false), capture.GetLine(50, 0, false) + 256);
    require(std::any_of(reference.begin(), reference.end(), [](u32 p) { return p != 0; }), "reference triangle is empty");
    // Host reconstruction and newer live render registers/texture bytes must
    // not change a latched capture. The geometry render bank remains intact.
    g.RenderClearAttr1 = 0x1F007FFF; g.RenderNumPolygons = 0;
    std::fill_n(gpu.VRAMFlat_Texture, sizeof gpu.VRAMFlat_Texture, 0xFF);
    native->SetRenderSettings(1, false); native->RenderFrame(gpu);
    require(std::equal(reference.begin(), reference.end(), capture.GetLine(50, 0, false)), "host rebuild changed guest capture");
    CaptureReference deferred;
    g.RenderClearAttr1 = 0; g.RenderNumPolygons = 1;
    deferred.Latch(gpu);
    g.RenderNumPolygons = 0; g.RenderClearAttr1 = 0x1F007FFF;
    require(std::equal(reference.begin(), reference.end(), deferred.GetLine(50, 0, false)), "deferred render did not use latched registers/list");
    require(std::all_of(capture.GetLine(50, 0, true), capture.GetLine(50, 0, true) + 256,
                        [](u32 p) { return p == 0; }), "aborted capture is not blank");

    // Clear-bitmap jobs go through the retained integer renderer, rather
    // than the upstream GL renderer's unimplemented clear-bitmap path.
    g.RenderDispCnt = 1u << 14; g.RenderClearAttr1 = 0; g.RenderClearAttr2 = 0;
    std::fill_n(gpu.VRAMFlat_Texture, sizeof gpu.VRAMFlat_Texture, 0);
    gpu.VRAMFlat_Texture[0x40000] = 0x1F; gpu.VRAMFlat_Texture[0x40001] = 0x80;
    native->RenderFrame(gpu);
    auto bitmap = pixels(*native, 448);
    require(native->GetHiResTexture() == 0, "software fallback exposed stale HD texture");
    require(color(bitmap[0]) == 0x1F00003Fu, "clear bitmap fallback differs");
    g.RenderFrameIdentical = true;
    native->RenderFrame(gpu);
    require(pixels(*native, 448) == bitmap, "identical clear-bitmap job reused cleared buffers");
    gpu.VRAMFlat_Texture[0x40000 + 96 * 2] = 0x1F;
    gpu.VRAMFlat_Texture[0x40001 + 96 * 2] = 0x80;
    CaptureReference frozenBitmap;
    frozenBitmap.Latch(gpu);
    std::fill_n(gpu.VRAMFlat_Texture, sizeof gpu.VRAMFlat_Texture, 0);
    require(frozenBitmap.GetLine(0, 0, false)[0] == 0x1F00003Fu,
            "deferred guest capture used newer texture memory");
#if defined(NDS_CAPTURE_VIEW_REUSE)
    const u64 textureBytes = frozenBitmap.TextureBytesCopied();
    const u64 paletteBytes = frozenBitmap.PaletteBytesCopied();
    // This fixture supplies coherent flat arrays directly, so it publishes
    // their owner content versions at the same boundary as the real bridge.
    ++gpu.TextureViewVersion;
    frozenBitmap.Latch(gpu);
    require(frozenBitmap.GetLine(0, 0, false)[0] == 0,
            "next capture retained a previous texture generation");
    require(frozenBitmap.TextureBytesCopied() == textureBytes + sizeof gpu.VRAMFlat_Texture,
            "texture mutation did not refresh the immutable snapshot");
    require(frozenBitmap.PaletteBytesCopied() == paletteBytes,
            "texture mutation unnecessarily copied palette data");
    frozenBitmap.Latch(gpu);
    require(frozenBitmap.TextureBytesCopied() == textureBytes + sizeof gpu.VRAMFlat_Texture,
            "unchanged texture snapshot copied again");
    gpu.VRAMFlat_TexPal[0] ^= 0x5A;
    ++gpu.TexPalViewVersion;
    frozenBitmap.Latch(gpu);
    require(frozenBitmap.PaletteBytesCopied() == paletteBytes + sizeof gpu.VRAMFlat_TexPal,
            "palette-only generation was not copied independently");
    require(frozenBitmap.TextureBytesCopied() == textureBytes + sizeof gpu.VRAMFlat_Texture,
            "palette-only mutation copied texture data");
    frozenBitmap.Reset();
    frozenBitmap.Latch(gpu);
    require(frozenBitmap.TextureBytesCopied() == textureBytes + 2u * sizeof gpu.VRAMFlat_Texture &&
            frozenBitmap.PaletteBytesCopied() == paletteBytes + 2u * sizeof gpu.VRAMFlat_TexPal,
            "reset reused a snapshot with repeated content versions");
#endif
    require(glGetError() == GL_NO_ERROR, "GL error after fallback");
    // Destroy GL objects before destroying the context (also exercises the
    // renderer destructor with both native and software jobs completed).
    g.SetCurrentRenderer(std::make_unique<SoftRenderer>());
}
}
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) return 77;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    auto* window = SDL_CreateWindow("native GL contract test", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    auto context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::fprintf(stderr, "GL context unavailable: %s\n", SDL_GetError());
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        SDL_Quit(); return 77;
    }
    int result = 0;
    try { tests(); std::puts("native GL transport, geometry, capture and fallback passed"); }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); result = 1; }
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return result;
}
