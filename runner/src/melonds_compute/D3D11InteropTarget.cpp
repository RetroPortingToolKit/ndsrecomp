// SPDX-License-Identifier: MIT
#include "D3D11InteropTarget.h"

#if defined(_WIN32) && defined(NDS_HAVE_SDL3)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <SDL3/SDL.h>
#include "glad/glad.h"
#include <cstdio>
#include <cstring>

namespace {
using OpenDevice = HANDLE(WINAPI*)(void*);
using CloseDevice = BOOL(WINAPI*)(HANDLE);
using RegisterObject = HANDLE(WINAPI*)(HANDLE, void*, GLuint, GLenum, GLenum);
using UnregisterObject = BOOL(WINAPI*)(HANDLE, HANDLE);
using LockObjects = BOOL(WINAPI*)(HANDLE, GLint, HANDLE*);
using ExtensionString = const char*(WINAPI*)(HDC);
using ExtensionStringExt = const char*(WINAPI*)();
using CurrentDC = HDC(WINAPI*)();

bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}
std::string win_error(const char* operation) {
    return std::string(operation) + " failed (Win32 error " + std::to_string(GetLastError()) + ")";
}
bool extension(const char* list, const char* name) {
    if (!list) return false;
    const size_t length = std::strlen(name);
    for (const char* at = list; (at = std::strstr(at, name)); at += length)
        if ((at == list || at[-1] == ' ') && (at[length] == ' ' || at[length] == '\0')) return true;
    return false;
}
template<class T> T proc(const char* name) {
    return reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
}
}

struct D3D11InteropTarget::Impl {
    SDL_Renderer* renderer{};  // borrowed; outlives the registered texture
    SDL_Texture* target{};
    HANDLE device{}, object{};
    GLuint renderbuffer{}, framebuffer{};
    bool locked{};
    OpenDevice open{};
    CloseDevice close{};
    RegisterObject register_object{};
    UnregisterObject unregister_object{};
    LockObjects lock{}, unlock{};
};

D3D11InteropTarget::D3D11InteropTarget() = default;
D3D11InteropTarget::~D3D11InteropTarget() { reset(); }

bool D3D11InteropTarget::create(SDL_Renderer* renderer, int width, int height, std::string* error) {
    reset();
    if (!renderer || width <= 0 || height <= 0) return fail(error, "Invalid Direct3D interop target size or renderer");
    if (!SDL_GL_GetCurrentContext() || !GLAD_GL_VERSION_4_3)
        return fail(error, "Direct3D interop requires the current OpenGL 4.3 producer context");
    auto* dx = static_cast<ID3D11Device*>(SDL_GetPointerProperty(SDL_GetRendererProperties(renderer),
        SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, nullptr));
    if (!dx) return fail(error, "The SDL renderer does not expose a Direct3D 11 device");
    const UINT flags = dx->GetCreationFlags();
    if (flags & D3D11_CREATE_DEVICE_SINGLETHREADED)
        return fail(error, "Direct3D 11 device is single-threaded; set SDL_RENDER_DIRECT3D_THREADSAFE=1 before renderer creation");
    auto ext_arb = proc<ExtensionString>("wglGetExtensionsStringARB");
    auto ext_ext = proc<ExtensionStringExt>("wglGetExtensionsStringEXT");
    auto current_dc = proc<CurrentDC>("wglGetCurrentDC");
    const char* extensions = ext_arb && current_dc ? ext_arb(current_dc()) : ext_ext ? ext_ext() : nullptr;
    if (!extension(extensions, "WGL_NV_DX_interop") || !extension(extensions, "WGL_NV_DX_interop2"))
        return fail(error, "The current OpenGL device lacks WGL_NV_DX_interop2");
    impl_ = std::make_unique<Impl>();
    auto& state = *impl_;
    state.renderer = renderer;
    state.open = proc<OpenDevice>("wglDXOpenDeviceNV");
    state.close = proc<CloseDevice>("wglDXCloseDeviceNV");
    state.register_object = proc<RegisterObject>("wglDXRegisterObjectNV");
    state.unregister_object = proc<UnregisterObject>("wglDXUnregisterObjectNV");
    state.lock = proc<LockObjects>("wglDXLockObjectsNV");
    state.unlock = proc<LockObjects>("wglDXUnlockObjectsNV");
    auto reject = [&](const std::string& message) { reset(); return fail(error, message); };
    if (!state.open || !state.close || !state.register_object || !state.unregister_object || !state.lock || !state.unlock)
        return reject("The OpenGL driver did not expose all required Direct3D interop functions");
    state.target = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, width, height);
    if (!state.target) return reject(std::string("SDL target creation failed: ") + SDL_GetError());
    if (!SDL_SetTextureBlendMode(state.target, SDL_BLENDMODE_NONE) ||
        !SDL_SetTextureScaleMode(state.target, SDL_SCALEMODE_NEAREST))
        return reject(std::string("SDL target configuration failed: ") + SDL_GetError());
    auto* image = static_cast<ID3D11Texture2D*>(SDL_GetPointerProperty(SDL_GetTextureProperties(state.target),
        SDL_PROP_TEXTURE_D3D11_TEXTURE_POINTER, nullptr));
    if (!image) return reject("The SDL target does not expose its Direct3D 11 texture");
    D3D11_TEXTURE2D_DESC description{};
    image->GetDesc(&description);
    if (description.Usage != D3D11_USAGE_DEFAULT)
        return reject("The SDL Direct3D texture does not use D3D11_USAGE_DEFAULT");
    SetLastError(0);
    state.device = state.open(dx);
    if (!state.device) return reject(win_error("wglDXOpenDeviceNV"));
    glGenRenderbuffers(1, &state.renderbuffer);
    SetLastError(0);
    state.object = state.register_object(state.device, image, state.renderbuffer, GL_RENDERBUFFER, 1 /* READ_WRITE_NV */);
    if (!state.object) return reject(win_error("wglDXRegisterObjectNV"));
    if (!lock(error)) { const std::string message = error ? *error : "Interop target lock failed"; return reject(message); }
    GLint previous_draw{}, previous_read{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
    glGenFramebuffers(1, &state.framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, state.framebuffer);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, state.renderbuffer);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read));
    if (status != GL_FRAMEBUFFER_COMPLETE)
        return reject("Direct3D interop framebuffer is incomplete (status " + std::to_string(status) + ")");
    if (!unlock(error)) { const std::string message = error ? *error : "Interop target unlock failed"; return reject(message); }
    return true;
}

bool D3D11InteropTarget::lock(std::string* error) {
    if (!impl_ || !impl_->object) return fail(error, "Direct3D interop target is unavailable");
    if (impl_->locked) return fail(error, "Direct3D interop target is already locked for OpenGL");
    if (!SDL_FlushRenderer(impl_->renderer)) return fail(error, std::string("SDL flush before interop lock failed: ") + SDL_GetError());
    SetLastError(0);
    if (!impl_->lock(impl_->device, 1, &impl_->object)) return fail(error, win_error("wglDXLockObjectsNV"));
    impl_->locked = true;
    return true;
}
bool D3D11InteropTarget::unlock(std::string* error) {
    if (!impl_ || !impl_->locked) return fail(error, "Direct3D interop target is not locked for OpenGL");
    SetLastError(0);
    if (!impl_->unlock(impl_->device, 1, &impl_->object)) return fail(error, win_error("wglDXUnlockObjectsNV"));
    impl_->locked = false;
    return true;
}
uint32_t D3D11InteropTarget::framebuffer() const { return impl_ ? impl_->framebuffer : 0; }
SDL_Texture* D3D11InteropTarget::texture() const { return impl_ ? impl_->target : nullptr; }

void D3D11InteropTarget::reset() {
    if (!impl_) return;
    auto& state = *impl_;
    auto report = [](const char* operation) { std::fprintf(stderr, "[gpu3d] %s\n", win_error(operation).c_str()); };
    if (state.locked && !unlock()) report("interop cleanup unlock");
    if (state.renderer && !SDL_FlushRenderer(state.renderer))
        std::fprintf(stderr, "[gpu3d] interop cleanup SDL flush failed: %s\n", SDL_GetError());
    if (state.object && state.unregister_object && !state.unregister_object(state.device, state.object))
        report("wglDXUnregisterObjectNV");
    if (state.framebuffer) glDeleteFramebuffers(1, &state.framebuffer);
    if (state.renderbuffer) glDeleteRenderbuffers(1, &state.renderbuffer);
    if (state.device && state.close && !state.close(state.device)) report("wglDXCloseDeviceNV");
    if (state.target) SDL_DestroyTexture(state.target);
    impl_.reset();
}

#else
struct D3D11InteropTarget::Impl {};
D3D11InteropTarget::D3D11InteropTarget() = default;
D3D11InteropTarget::~D3D11InteropTarget() = default;
bool D3D11InteropTarget::create(SDL_Renderer*, int, int, std::string* error) {
    if (error) *error = "Direct3D 11/OpenGL interop is unavailable on this platform";
    return false;
}
bool D3D11InteropTarget::lock(std::string* error) { return create(nullptr, 0, 0, error); }
bool D3D11InteropTarget::unlock(std::string* error) { return create(nullptr, 0, 0, error); }
uint32_t D3D11InteropTarget::framebuffer() const { return 0; }
SDL_Texture* D3D11InteropTarget::texture() const { return nullptr; }
void D3D11InteropTarget::reset() {}
#endif
