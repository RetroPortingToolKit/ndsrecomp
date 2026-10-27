// GPL-3.0-or-later; shared by the presenter and focused actual-GL tests.
#pragma once
namespace nds_compute_present {
inline constexpr const char* kPresentVertex = R"GLSL(
#version 430 core
out vec2 uv;
void main()
{
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    uv = vec2(p.x, 1.0 - p.y);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)GLSL";

inline constexpr const char* kPresentFragment = R"GLSL(
#version 430 core
layout(binding = 0) uniform usampler2D tex3d;
layout(binding = 1) uniform usampler2D texObject;
layout(binding = 2) uniform usampler2D texFallback;
// Internal-resolution (HD) 3D surface. Same RGB6/alpha5 values as tex3d,
// stored normalized by 63 and 31 in UNORM8. Both quantizations are injective
// into 8 bits, so the inverse round below recovers the exact integers and the
// DS blend math downstream stays integer-identical to the native path.
layout(binding = 3) uniform sampler2D tex3dHi;
layout(location = 0) uniform uint directMode;
layout(location = 1) uniform uint outputWidth;
layout(location = 2) uniform uint frameRenderXPos;
layout(location = 3) uniform uvec3 frameBackdrop;
layout(location = 4) uniform uint frameBldcnt;
layout(location = 5) uniform uint frameEva;
layout(location = 6) uniform uint frameEvb;
layout(location = 7) uniform uint frameEvy;
layout(location = 8) uniform uint frameMasterBright;
layout(location = 9) uniform uint framePriority3d;
layout(location = 10) uniform uint scale3d;
// HD composite: the 2D stack resolved with the 3D layer removed. See
// NdsGpu2dHdFrame. Non-zero hdMode selects this path over the OBJ-only one.
layout(binding = 4) uniform usampler2D texHdTop;
layout(binding = 5) uniform usampler2D texHdBelow;
layout(location = 11) uniform uint hdMode;
layout(location = 12) uniform uint order3d;
layout(binding = 6) uniform usampler2D texRaster;
layout(location = 13) uniform uint rasterMode;
// Additional samples come from the real higher-resolution 3D raster. Keep
// the native 2D coordinate fixed across samples so HUD text remains crisp.
layout(location = 14) uniform uint resolveSamples;
uint renderXPos, bldcnt, eva, evb, evy, masterBright, priority3d;
uvec3 backdrop;
in vec2 uv;
out vec4 outColor;

uvec3 blend16(uvec3 a, uvec3 b, uint ca, uint cb)
{
    return min((a * ca + b * cb + uvec3(8u)) >> 4, uvec3(63u));
}

uvec3 blend5(uvec3 a, uvec3 b, uint alpha)
{
    uint ca = alpha + 1u;
    if (ca == 32u) return a;
    return min((a * ca + b * (32u - ca) + uvec3(16u)) >> 5,
               uvec3(63u));
}

uvec3 brighten(uvec3 c, uint factor, uint bias)
{
    return min(c + (((uvec3(63u) - c) * factor + uvec3(bias)) >> 4),
               uvec3(63u));
}

uvec3 darken(uvec3 c, uint factor, uint bias)
{
    return c - ((c * factor + uvec3(bias)) >> 4);
}

uvec3 effect(uvec3 top, uvec3 below, uint topTarget,
             uint belowTarget, uint alphaCode, uint alpha5)
{
    uint target2 = belowTarget << 8;
    if (alpha5 != 0u) {
        if ((bldcnt & target2) != 0u)
            return blend5(top, below, alpha5);
        if ((bldcnt & topTarget) != 0u) {
            uint mode = (bldcnt >> 6) & 3u;
            if (mode == 2u) return brighten(top, evy, 8u);
            if (mode == 3u) return darken(top, evy, 7u);
        }
        return top;
    }
    if (alphaCode != 0u && (bldcnt & target2) != 0u) {
        if (alphaCode == 255u) return blend16(top, below, eva, evb);
        return blend16(top, below, alphaCode, 16u - alphaCode);
    }
    if ((bldcnt & topTarget) != 0u) {
        uint mode = (bldcnt >> 6) & 3u;
        if (mode == 1u && (bldcnt & target2) != 0u)
            return blend16(top, below, eva, evb);
        if (mode == 2u) return brighten(top, evy, 8u);
        if (mode == 3u) return darken(top, evy, 7u);
    }
    return top;
}

// A resolved layer: colour plus the BLDCNT target, alpha, and the
// (priority, order) key the DS sorts by.
struct Layer {
    uvec3 color;
    uint target;
    bool effects;
    uint alpha;    // 0 = no alpha blend, 1..16 = OBJ alpha, 255 = EVA/EVB
    uint alpha5;   // non-zero only on the 3D layer: its own 5-bit coverage
    uint priority;
    uint order;
};

// The emitted 2D surfaces never contain the 3D layer, so alpha5 is always
// zero here; the presenter constructs the 3D layer itself.
Layer unpackLayer(uvec2 texel)
{
    Layer l;
    l.color = uvec3(texel.x & 63u, (texel.x >> 8) & 63u,
                    (texel.x >> 16) & 63u);
    l.target = texel.y & 63u;
    l.effects = (texel.y & 128u) != 0u;
    l.alpha = (texel.y >> 8) & 255u;
    l.alpha5 = 0u;
    l.priority = (texel.y >> 16) & 255u;
    l.order = (texel.y >> 24) & 255u;
    return l;
}

bool ahead(Layer a, Layer b)
{
    return a.priority < b.priority ||
           (a.priority == b.priority && a.order < b.order);
}

// Mirrors compose() in gpu2d.cpp exactly, including the 3D layer's forced
// per-pixel 5-bit blend whenever the pixel behind it is a second target.
uvec3 composeLayers(Layer top, Layer below)
{
    uint target2 = below.target << 8;
    if (top.alpha5 != 0u) {
        // 3D layer on top: whenever the pixel behind is a second target the
        // per-pixel 5-bit blend is forced regardless of the colour effect.
        if ((bldcnt & target2) != 0u)
            return blend5(top.color, below.color, top.alpha5);
        if (top.effects && (bldcnt & top.target) != 0u) {
            uint mode = (bldcnt >> 6) & 3u;
            if (mode == 2u) return brighten(top.color, evy, 8u);
            if (mode == 3u) return darken(top.color, evy, 7u);
        }
        return top.color;
    }
    if (top.alpha != 0u && (bldcnt & target2) != 0u) {
        uint eva1 = top.alpha == 255u ? eva : top.alpha;
        uint evb1 = top.alpha == 255u ? evb : 16u - eva1;
        return blend16(top.color, below.color, eva1, evb1);
    }
    if (top.effects && (bldcnt & top.target) != 0u) {
        uint mode = (bldcnt >> 6) & 3u;
        if (mode == 1u && (bldcnt & target2) != 0u)
            return blend16(top.color, below.color, eva, evb);
        if (mode == 2u) return brighten(top.color, evy, 8u);
        if (mode == 3u) return darken(top.color, evy, 7u);
    }
    return top.color;
}

vec4 composeAt(vec2 sampleUV, ivec2 dst)
{
    ivec2 size = textureSize(texFallback, 0);
    renderXPos = frameRenderXPos;
    backdrop = frameBackdrop;
    bldcnt = frameBldcnt; eva = frameEva; evb = frameEvb; evy = frameEvy;
    masterBright = frameMasterBright; priority3d = framePriority3d;
    if (rasterMode != 0u && directMode != 0u && hdMode == 0u) {
        uvec4 p = texelFetch(texRaster, ivec2(0, dst.y), 0);
        backdrop = uvec3(p.x, p.x >> 8, p.x >> 16) & uvec3(63u);
        bldcnt = p.y & 65535u; masterBright = p.y >> 16;
        eva = p.z & 255u; evb = (p.z >> 8) & 255u;
        evy = (p.z >> 16) & 255u; priority3d = p.z >> 24;
        renderXPos = p.w;
    }
    if (hdMode != 0u) {
        int s = int(max(scale3d, 1u));
        ivec2 hiSize = size * s;
        ivec2 dstHi = ivec2(clamp(sampleUV * vec2(hiSize), vec2(0.0),
                                  vec2(hiSize) - vec2(1.0)));
        // RenderXPos is a native-pixel scroll and the hi-res 3D surface is
        // unscrolled, so it is applied here scaled to that raster.
        int sx = dstHi.x;
        bool off = false;
        if (renderXPos != 0u) {
            if ((renderXPos & 0x100u) != 0u) {
                int shift = min(int(outputWidth), 512 - int(renderXPos)) * s;
                off = dstHi.x < shift;
                sx = dstHi.x - shift;
            } else {
                sx = dstHi.x + int(renderXPos) * s;
                off = sx >= int(outputWidth) * s;
            }
        }
        Layer top = unpackLayer(texelFetch(texHdTop, dst, 0).xy);
        Layer below = unpackLayer(texelFetch(texHdBelow, dst, 0).xy);
        if (!off) {
            vec4 hi = texelFetch(tex3dHi, ivec2(sx, dstHi.y), 0);
            uint a3 = uint(round(hi.a * 31.0));
            if (a3 != 0u) {
                Layer l3;
                // FinalFB stores red where LowResFB stores blue; swizzle to
                // the DS channel order the rest of this shader uses.
                l3.color = uvec3(round(hi.bgr * 63.0));
                l3.target = 1u;       // the 3D layer is BLDCNT first target 0
                l3.effects = top.effects;
                l3.alpha = 0u;
                l3.alpha5 = a3;
                l3.priority = priority3d;
                l3.order = order3d;
                // Insert into the top-two. (priority, order) is a total
                // order, so this yields the same pair the CPU path picks.
                if (ahead(l3, top)) { below = top; top = l3; }
                else if (ahead(l3, below)) { below = l3; }
            }
        }
        uvec3 c = composeLayers(top, below);
        uint bm = masterBright >> 14;
        uint bf = min(masterBright & 31u, 16u);
        if (bm == 1u) c = brighten(c, bf, 0u);
        else if (bm == 2u) c = darken(c, bf, 15u);
        return vec4(vec3(c) / 63.0, 1.0);
    }
    if (directMode == 0u) {
        uvec4 pixel = texelFetch(texFallback, dst, 0);
        return vec4(pixel) / 255.0;
    }

    // The 3D layer is sampled on its own grid: identical to the 2D grid at
    // scale 1, denser above it. The 2D object layer is a native surface and
    // is always fetched at native coordinates, which is exactly what the DS
    // does -- only the 3D engine gains sample density here.
    int scale = int(max(scale3d, 1u));
    ivec2 hiSize = size * scale;
    ivec2 dstHi = ivec2(clamp(sampleUV * vec2(hiSize), vec2(0.0),
                              vec2(hiSize) - vec2(1.0)));
    int sourceX = dstHi.x;
    bool outside = false;
    // RenderXPos is a native-pixel scroll, so its shift scales with the
    // raster it is being applied to.
    if (renderXPos != 0u) {
        if ((renderXPos & 0x100u) != 0u) {
            int shift = min(int(outputWidth), 512 - int(renderXPos)) * scale;
            outside = dstHi.x < shift;
            sourceX = dstHi.x - shift;
        } else {
            sourceX = dstHi.x + int(renderXPos) * scale;
            outside = sourceX >= int(outputWidth) * scale;
        }
    }
    uvec3 color3d = uvec3(0u);
    uint alpha3d = 0u;
    if (!outside) {
        if (scale > 1) {
            vec4 hi = texelFetch(tex3dHi, ivec2(sourceX, dstHi.y), 0);
            // The final pass writes the two surfaces in opposite channel
            // order: FinalFB takes red from the packed colour's bits 16..23
            // and blue from bits 0..5, while LowResFB (which this presenter
            // was written against) takes red from bits 0..7. Swizzle back
            // rather than reordering either write, so the native readback
            // every faithful consumer reads stays exactly as it is.
            color3d = uvec3(round(hi.bgr * 63.0));
            alpha3d = uint(round(hi.a * 31.0));
        } else {
            uvec4 pixel3d = texelFetch(tex3d, ivec2(sourceX, dstHi.y), 0);
            color3d = pixel3d.rgb & uvec3(63u);
            alpha3d = pixel3d.a & 31u;
        }
    }
    uvec4 object = texelFetch(texObject, dst, 0);
    if ((object.r & 64u) != 0u) alpha3d = 0u;
    bool effectsEnabled = (object.g & 64u) == 0u;
    uint objectPriority = object.b >> 6;
    uint objectAlphaCode = object.a;
    bool objectValid = objectAlphaCode != 0u;
    Layer top = Layer(backdrop, 32u, effectsEnabled, 0u, 0u, 4u, 5u);
    Layer below = top;
    if (objectValid) {
        below = top;
        top = Layer(object.rgb & uvec3(63u), 16u, effectsEnabled,
            objectAlphaCode == 17u ? 0u : objectAlphaCode, 0u,
            objectPriority, 0u);
    }
    if (alpha3d != 0u) {
        Layer l3 = Layer(color3d, 1u, effectsEnabled, 0u, alpha3d,
                         priority3d, 1u);
        if (ahead(l3, top)) { below = top; top = l3; }
        else if (ahead(l3, below)) below = l3;
    }
    uvec3 color = composeLayers(top, below);

    uint brightMode = masterBright >> 14;
    uint brightFactor = min(masterBright & 31u, 16u);
    if (brightMode == 1u)
        color = brighten(color, brightFactor, 0u);
    else if (brightMode == 2u)
        color = darken(color, brightFactor, 15u);
    return vec4(vec3(color) / 63.0, 1.0);
}

void main()
{
    ivec2 size = textureSize(texFallback, 0);
    ivec2 dst = ivec2(clamp(uv * vec2(size), vec2(0.0),
                            vec2(size) - vec2(1.0)));
    if (resolveSamples < 2u || scale3d <= 1u ||
        (hdMode == 0u && directMode == 0u)) {
        outColor = composeAt(uv, dst);
        return;
    }
    // Integrate within this output pixel's footprint, independently of window
    // size. At large windows neighbouring taps can select the same source
    // texel; this never invents detail or changes the guest/capture surface.
    vec2 dx = dFdx(uv), dy = dFdy(uv);
    vec4 sum = vec4(0.0);
    if (resolveSamples == 2u) {
        sum = composeAt(uv - 0.25 * (dx + dy), dst) +
              composeAt(uv + 0.25 * (dx + dy), dst);
        outColor = sum * 0.5;
    } else {
        int columns = resolveSamples >= 8u ? 4 : 2;
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < columns; ++x) {
                vec2 offset = vec2((float(x) + 0.5) / float(columns) - 0.5,
                                   (float(y) + 0.5) / 2.0 - 0.5);
                sum += composeAt(uv + offset.x * dx + offset.y * dy, dst);
            }
        outColor = sum / float(columns * 2);
    }
}
)GLSL";

}
