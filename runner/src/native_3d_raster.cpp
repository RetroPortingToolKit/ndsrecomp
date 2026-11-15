// SPDX-License-Identifier: MIT
#include "native_3d_raster.h"
#include "../vendor/melonds/GPU3D.h"
#include <algorithm>
#include <bit>
#include <climits>

void nds_native_3d_raster(const nds_native_3d::Frame& frame,
                          melonDS::GPU3D& g, bool guest_wide) {
    // Use the raster's owned second bank so its pointer-index serializer stays
    // valid. The native service owns the independent building/pending jobs.
    auto* vertices = g.VertexRAM + 6144;
    auto* polygons = g.PolygonRAM + 2048;
    const int width = static_cast<int>(g.GetRenderWidth());
    for (size_t i = 0; i < frame.vertices.size(); ++i) {
        const auto& v = frame.vertices[i];
        auto& out = vertices[i]; out = {};
        std::copy(v.clip.begin(), v.clip.end(), out.Position);
        std::copy(v.color.begin(), v.color.end(), out.Color);
        std::copy(v.texture.begin(), v.texture.end(), out.TexCoords);
        for (unsigned c = 0; c < 3; ++c) out.FinalColor[c] = v.color[c] >> 12;
        const int x1 = v.viewport & 255, y1 = (v.viewport >> 8) & 255;
        const int x2 = (v.viewport >> 16) & 255, y2 = v.viewport >> 24;
        int left = x1, span = (x2 - x1 + 1) & 511;
        if (guest_wide) { left = x1 * width / 256; span = span * width / 256; }
        else if (x1 == 0 && x2 == 255) span = width;
        else left += (width - 256) / 2;
        const int height = (y2 - y1 + 1) & 255;
        const int64_t w = v.clip[3];
        // A reset/zero projection can yield an entirely collapsed polygon.
        // Its vertices are legal, but must never divide by zero on the host.
        const int64_t x = w > 0 ? ((int64_t(v.clip[0]) + w) * span * 16) / (2 * w) : 0;
        const int64_t y = w > 0 ? ((int64_t(v.clip[1]) + w) * height * 16) / (2 * w) : 0;
        out.HiresPosition[0] = static_cast<int32_t>(left * 16 + x);
        out.HiresPosition[1] = static_cast<int32_t>((192 - y1) * 16 - y);
        out.FinalPosition[0] = out.HiresPosition[0] >> 4;
        out.FinalPosition[1] = out.HiresPosition[1] >> 4;
    }
    g.RenderNumPolygons = 0;
    for (const auto& p : frame.polygons) {
        auto& out = polygons[g.RenderNumPolygons]; out = {};
        out.NumVertices = p.count; out.Attr = p.attributes;
        out.TexParam = p.texture; out.TexPalette = p.palette;
        out.FacingView = p.front != 0; out.PerspectiveProjection = p.perspective != 0;
        out.WBuffer = (frame.swap_attributes & 2) != 0;
        const unsigned alpha = (p.attributes >> 16) & 31;
        const unsigned format = (p.texture >> 26) & 7;
        const bool decal = ((p.attributes >> 4) & 3) == 1;
        out.Translucent = (alpha != 0 && alpha != 31) || (!decal && (format == 1 || format == 6));
        out.IsShadow = ((p.attributes >> 4) & 3) == 3;
        out.IsShadowMask = out.IsShadow && ((p.attributes >> 24) & 63) == 0;
        out.YTop = out.XTop = INT_MAX; out.YBottom = out.XBottom = INT_MIN;
        uint32_t max_w = 0;
        for (unsigned i = 0; i < p.count; ++i)
            max_w = std::max(max_w, static_cast<uint32_t>(std::max(0, frame.vertices[p.indices[i]].clip[3])));
        const unsigned shift = max_w > 0xFFFF ? (std::bit_width(max_w) - 16 + 3) & ~3u : 0;
        for (unsigned i = 0; i < p.count; ++i) {
            auto& v = vertices[p.indices[i]]; out.Vertices[i] = &v;
            const int64_t w = v.Position[3];
            const int64_t z = w > 0 ? (int64_t(v.Position[2]) * 0x4000) / w + 0x3FFF : 0;
            out.FinalW[i] = static_cast<int32_t>(std::max<int64_t>(0, w) >> shift);
            out.FinalZ[i] = out.WBuffer ? static_cast<int32_t>(std::clamp<int64_t>(w, 0, 0xFFFFFF)) :
                static_cast<int32_t>(std::clamp<int64_t>(z * 512, 0, 0xFFFFFF));
            const int x = v.FinalPosition[0], y = v.FinalPosition[1];
            if (y < out.YTop || (y == out.YTop && x < out.XTop)) { out.YTop = y; out.XTop = x; out.VTop = i; }
            if (y > out.YBottom || (y == out.YBottom && x > out.XBottom)) { out.YBottom = y; out.XBottom = x; out.VBottom = i; }
        }
        out.Type = out.YTop == out.YBottom ? 1 : 0;
        out.Degenerate = out.Type != 0;
        out.SortKey = (uint32_t(std::clamp(out.YBottom, 0, 255)) << 8) | uint32_t(std::clamp(out.YTop, 0, 255));
        g.RenderPolygonRAM[g.RenderNumPolygons++] = &out;
    }
    std::stable_sort(g.RenderPolygonRAM.begin(), g.RenderPolygonRAM.begin() + g.RenderNumPolygons,
        [&](const auto* a, const auto* b) {
            if (a->Translucent != b->Translucent) return !a->Translucent;
            if (a->Translucent && (frame.swap_attributes & 1)) return false;
            return a->SortKey < b->SortKey;
        });
    g.RenderFrameIdentical = false;
    g.AbortFrame = !g.RenderingEnabled;
}
