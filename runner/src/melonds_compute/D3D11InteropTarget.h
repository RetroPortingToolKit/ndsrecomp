// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <memory>
#include <string>

struct SDL_Renderer;
struct SDL_Texture;

// One SDL-owned D3D11 render target with explicit GL/D3D ownership transfer.
// The caller keeps the renderer alive and the producer GL context current
// through reset/destruction. Unsupported platforms return unavailable.
class D3D11InteropTarget final {
public:
    D3D11InteropTarget();
    ~D3D11InteropTarget();
    D3D11InteropTarget(const D3D11InteropTarget&) = delete;
    D3D11InteropTarget& operator=(const D3D11InteropTarget&) = delete;

    bool create(SDL_Renderer* renderer, int width, int height, std::string* error = nullptr);
    bool lock(std::string* error = nullptr);
    uint32_t framebuffer() const;
    bool unlock(std::string* error = nullptr);
    SDL_Texture* texture() const;
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
