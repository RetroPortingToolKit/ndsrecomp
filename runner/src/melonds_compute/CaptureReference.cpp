// Copyright 2026 Matthew Stanley. GPL-3.0-or-later.
// Derived from the melonDS GPU/render interfaces.
#include "CaptureReference.h"
#include <cstring>

namespace melonDS {
void CaptureReference::Latch(GPU& source) {
    if (!Frame) Frame = std::make_unique<NDS>();
    auto& target = Frame->GPU;
    auto& out = target.GPU3D;
    const auto& in = source.GPU3D;
    target.FrozenTextureViews = true;
    NonStupidBitField<1024> textureDirty;
    NonStupidBitField<256> paletteDirty;
    source.MakeVRAMFlat_TextureCoherent(textureDirty);
    source.MakeVRAMFlat_TexPalCoherent(paletteDirty);
    // Frame owns immutable copies. A renderer may already have consumed the
    // live dirty flags, so use owner content versions rather than those flags.
    // A changed render list/register set is still latched on every frame.
#if defined(NDS_CAPTURE_VIEW_REUSE)
    if (!SnapshotValid || TextureVersion != source.TextureViewVersion)
#endif
    {
        std::memcpy(target.VRAMFlat_Texture, source.VRAMFlat_Texture, sizeof target.VRAMFlat_Texture);
        TextureCopies += sizeof target.VRAMFlat_Texture;
    }
#if defined(NDS_CAPTURE_VIEW_REUSE)
    if (!SnapshotValid || PaletteVersion != source.TexPalViewVersion)
#endif
    {
        std::memcpy(target.VRAMFlat_TexPal, source.VRAMFlat_TexPal, sizeof target.VRAMFlat_TexPal);
        PaletteCopies += sizeof target.VRAMFlat_TexPal;
    }
    TextureVersion = source.TextureViewVersion;
    PaletteVersion = source.TexPalViewVersion;
    SnapshotValid = true;
    out.SetEnabled(true, true);
    out.SetRenderWidth(in.GetRenderWidth());
    out.RenderDispCnt = in.RenderDispCnt;
    out.RenderAlphaRef = in.RenderAlphaRef;
    out.RenderClearAttr1 = in.RenderClearAttr1;
    out.RenderClearAttr2 = in.RenderClearAttr2;
    out.RenderFogColor = in.RenderFogColor;
    out.RenderFogOffset = in.RenderFogOffset;
    out.RenderFogShift = in.RenderFogShift;
    std::memcpy(out.RenderEdgeTable, in.RenderEdgeTable, sizeof out.RenderEdgeTable);
    std::memcpy(out.RenderToonTable, in.RenderToonTable, sizeof out.RenderToonTable);
    std::memcpy(out.RenderFogDensityTable, in.RenderFogDensityTable, sizeof out.RenderFogDensityTable);
    // Render-bank polygons/vertices remain immutable until the next VBlank
    // swaps banks at line192. Capture consumes lines0..191 on the scheduler
    // thread. Copy the list because VBlank rebuilds the source list itself.
    out.RenderPolygonRAM = in.RenderPolygonRAM;
    out.RenderNumPolygons = in.RenderNumPolygons;
    out.AbortFrame = in.AbortFrame;
    out.RenderFrameIdentical = false;
    Ready = false;
}

const u32* CaptureReference::GetLine(int line, u16 renderXPos, bool abort) {
    if (!Frame || line < 0 || line >= 192 || abort || Frame->GPU.GPU3D.AbortFrame)
        return ZeroLine;
    auto& gpu = Frame->GPU;
    if (!Ready) {
        // Frozen VRAM deliberately reports no live dirty chunks. Rebuild the
        // derived software buffers once when this reference job is first used.
        gpu.GPU3D.GetCurrentRenderer().Reset(gpu);
        gpu.GPU3D.GetCurrentRenderer().RenderFrame(gpu);
        Ready = true;
    }
    gpu.GPU3D.SetRenderXPos(renderXPos);
    return gpu.GPU3D.GetLine(line) + (gpu.GPU3D.GetRenderWidth() - 256u) / 2u;
}
}
