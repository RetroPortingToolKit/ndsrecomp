// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Matthew Stanley. See ../../../LICENSE-GPL.
#pragma once

namespace melonDS {
// Preserve the existing compute/presentation transport: native RGB6/alpha5,
// with the six polygon-ID bits occupying the spare high bits of RGB.
inline constexpr const char* NativePackFS = R"(#version 140
uniform sampler2D ColorInput;
uniform sampler2D AttrInput;
uniform int SourceScale;
out uvec4 oColor;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy) * SourceScale;
    uvec4 bytes = uvec4(round(clamp(texelFetch(ColorInput,p,0).bgra,0.0,1.0)*255.0));
    uint id = uint(round(clamp(texelFetch(AttrInput,p,0).r,0.0,1.0)*63.0));
    oColor = uvec4((bytes.rgb >> uvec3(2u)) |
        (uvec3(id, id >> 2u, id >> 4u) & uvec3(3u)) << uvec3(6u), bytes.a >> 3u);
}
)";
inline constexpr const char* NativeHiResFS = R"(#version 140
uniform sampler2D ColorInput;
out vec4 oColor;
void main() {
    // The shared presenter expects the HD texture in the opposite RGB order
    // from the native integer surface, matching ComputeRenderer::FinalFB.
    uvec4 bytes = uvec4(round(clamp(texelFetch(ColorInput,ivec2(gl_FragCoord.xy),0).rgba,0.0,1.0)*255.0));
    oColor = vec4(vec3(bytes.rgb >> uvec3(2u))/63.0, float(bytes.a >> 3u)/31.0);
}
)";
}
