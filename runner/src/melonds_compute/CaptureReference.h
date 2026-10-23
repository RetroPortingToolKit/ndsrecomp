// Copyright 2026 Matthew Stanley. GPL-3.0-or-later.
// Derived from the melonDS GPU/render interfaces.
#pragma once
#include "NDS.h"
#include <memory>

namespace melonDS {
// Independent of the host renderer's lifetime and internal resolution. A job
// is latched at VCount215 and consumed only if DISPCAPCNT needs 3D pixels.
class CaptureReference {
public:
    void Reset() { Frame.reset(); Ready = false; SnapshotValid = false; }
    u64 TextureBytesCopied() const { return TextureCopies; }
    u64 PaletteBytesCopied() const { return PaletteCopies; }
    bool HasFrame() const { return Frame != nullptr; }
    void Latch(GPU& source);
    const u32* GetLine(int line, u16 renderXPos, bool abort);
private:
    std::unique_ptr<NDS> Frame;
    bool Ready = false;
    bool SnapshotValid = false;
    u64 TextureVersion = 0, PaletteVersion = 0;
    u64 TextureCopies = 0, PaletteCopies = 0;
    u32 ZeroLine[448] {};
};
}
