// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Matthew Stanley. See ../../LICENSE-GPL.
// Differential checks against the actual maintained melonDS FIFO/geometry
// implementation, including source mutation between scheduler slices.
#include "GPU3D_CommandBatch.h"
#include "NDS.h"
#include "Platform.h"
#include "Savestate.h"

#include <cstdio>
#include <stdexcept>
#include <memory>

namespace melonDS {
void NDS::SetIRQ(u32, u32) {}
void NDS::ClearIRQ(u32, u32) {}
void NDS::CheckDMAs(u32, u32) {}
void NDS::GXFIFOStall() {}
void NDS::GXFIFOUnstall() {}
bool GPU::MakeVRAMFlat_TextureCoherent(NonStupidBitField<1024>&) noexcept { return false; }
bool GPU::MakeVRAMFlat_TexPalCoherent(NonStupidBitField<256>&) noexcept { return false; }
namespace Platform {
void Log(LogLevel, const char*, ...) {}
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
using namespace melonDS::CommandBatch;
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
State state(const GPU3D& gpu) { return {gpu.CurCommand, gpu.NumCommands, gpu.ParamCount, gpu.TotalParams}; }
template<class Queue> void queue_equal(Queue& a, Queue& b) {
    require(a.Level() == b.Level(), "FIFO level differs");
    for (uint32_t i = 0; i < a.Level(); ++i)
        require(a.Peek(i).Command == b.Peek(i).Command && a.Peek(i).Param == b.Peek(i).Param,
                "FIFO command/parameter differs");
}
void equal(GPU3D& a, GPU3D& b) {
    require(state(a) == state(b), "partial packed-command continuation differs");
    require(a.GXStat == b.GXStat && a.NumPushPopCommands == b.NumPushPopCommands &&
            a.NumTestCommands == b.NumTestCommands, "busy/test/matrix flags differ");
    queue_equal(a.CmdPIPE, b.CmdPIPE); queue_equal(a.CmdFIFO, b.CmdFIFO);
    queue_equal(a.CmdStallQueue, b.CmdStallQueue);
}
void replay(GPU3D& gpu, uint32_t value, const Word& decoded) {
    gpu.WriteDecodedGXFIFO(value, decoded.commands.data(), decoded.count);
    gpu.CurCommand = decoded.after.command; gpu.NumCommands = decoded.after.count;
    gpu.ParamCount = decoded.after.param; gpu.TotalParams = decoded.after.total;
}
void write(std::vector<uint8_t>& data, uint32_t index, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) data[index * 4 + i] = static_cast<uint8_t>(value >> (8 * i));
}
std::vector<u8> gpu_snapshot(GPU3D& gpu) {
    Savestate saved;
    require(!saved.Error, "snapshot allocation");
    gpu.DoSavestate(&saved);saved.Finish();
    require(!saved.Error, "GPU snapshot serialization");
    const auto* bytes=static_cast<const u8*>(saved.Buffer());
    return {bytes,bytes+saved.Length()};
}
void fifo_snapshot_tests() {
    auto left=std::make_unique<NDS>();auto right=std::make_unique<NDS>();
    left->GPU.GPU3D.Reset();right->GPU.GPU3D.Reset();
    const auto seed=[](GPU3D& gpu,u8 poison) {
        auto entry=gpu.CmdFIFO.Peek();
        static_assert(sizeof(entry)==8, "FIFO wire footprint");
        std::memset(&entry,poison,sizeof(entry));
        entry.Param=0x12345678;entry.Command=0x10;gpu.CmdFIFO.Write(entry);
        std::memset(&entry,poison,sizeof(entry));
        entry.Param=0x89abcdef;entry.Command=0x20;gpu.CmdPIPE.Write(entry);
    };
    seed(left->GPU.GPU3D,0xa5);seed(right->GPU.GPU3D,0x5a);
    const auto saved=gpu_snapshot(left->GPU.GPU3D);
    require(saved==gpu_snapshot(right->GPU.GPU3D),
            "identical FIFO commands must not snapshot prior host stack bytes");
    auto restored=std::make_unique<NDS>();restored->GPU.GPU3D.Reset();
    Savestate load(const_cast<u8*>(saved.data()),static_cast<u32>(saved.size()),false);
    restored->GPU.GPU3D.DoSavestate(&load);
    require(!load.Error&&gpu_snapshot(restored->GPU.GPU3D)==saved,
            "canonical current-build GPU snapshot roundtrip");
    // The first command follows the MELN header, GP3D section header and
    // three FIFO counters. Its high bits are reserved, never guest commands.
    auto invalid=saved;constexpr size_t command_high=16+16+12+4+1;
    require(invalid.size()>command_high&&!std::memcmp(invalid.data()+16,"GP3D",4),"FIFO snapshot envelope");
    for(size_t offset:{command_high,command_high+256*8+12}) {
        invalid=saved;invalid[offset]=1;
        Savestate bad(invalid.data(),static_cast<u32>(invalid.size()),false);
        restored->GPU.GPU3D.DoSavestate(&bad);
        require(bad.Error,"out-of-range stored FIFO/PIPE command must be rejected");
    }
}

void adaptive_box_tests() {
    auto nds = std::make_unique<NDS>();
    auto& gpu = nds->GPU.GPU3D;
    const auto box = [&](u32 width, s16 x, s16 y, s16 z,
                         bool guest_wide = false, u32 viewport_width = 256) {
        gpu.Reset(); gpu.SetEnabled(true, true); gpu.SetRenderWidth(width);
        gpu.SetGuestWideProjection(guest_wide);
        gpu.Viewport[0] = 0; gpu.Viewport[4] = viewport_width;
        const auto pack = [](s16 lo, s16 hi) {
            return u32(u16(lo)) | (u32(u16(hi)) << 16);
        };
        // Actual BOX_TEST FIFO execution with identity matrices (w = 1).
        const u32 words[] = {0x70, pack(x, y), pack(z, 0x100), pack(0x100, 0x100)};
        for (u32 word : words) gpu.WriteToGXFIFO(word);
        nds->ARM9Timestamp += 2000; gpu.Run();
        require(!gpu.NumTestCommands && gpu.CmdPIPE.IsEmpty() && gpu.CmdFIFO.IsEmpty(),
                "BOX_TEST fixture did not execute");
        require(gpu.ProjMatrix[0] == 0x1000 && gpu.ProjMatrix[5] == 0x1000 &&
                gpu.ProjMatrix[10] == 0x1000 && gpu.ProjMatrix[15] == 0x1000,
                "adaptive BOX_TEST changed guest projection");
        return (gpu.Read32(0x04000600) & 2) != 0;
    };
    require(box(256, 0, 0, 0), "native visible box rejected");
    require(!box(256, 0x1400, 0, 0), "native right margin box accepted");
    require(!box(256, -0x1500, 0, 0), "native left margin box accepted");
    require(box(448, 0x1400, 0, 0), "adaptive right margin box rejected");
    require(box(448, -0x1500, 0, 0), "adaptive left margin box rejected");
    require(box(320, 0x1200, 0, 0), "resized adaptive box rejected");
    require(!box(448, 0x1D00, 0, 0), "outside adaptive band box accepted");
    require(!box(448, 0, 0x1400, 0), "vertical clipping was widened");
    require(!box(448, 0, 0, 0x1400), "far clipping was widened");
    require(!box(448, 0, 0, -0x1500), "near clipping was widened");
    require(!box(448, 0x1400, 0, 0, true), "guest-wide projection widened twice");
    require(!box(448, 0x1400, 0, 0, false, 128), "subviewport box was widened");
}
void tests() {
    adaptive_box_tests();
    auto first = std::make_unique<NDS>(); auto second = std::make_unique<NDS>();
    auto& a = first->GPU.GPU3D; auto& b = second->GPU.GPU3D;
    a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
    uint32_t seed = 0x95AB41u;
    // Includes unknown/zero commands, all parameter lengths and arbitrary
    // split points. The reference parser is never replaced or copied here.
    for (unsigned i = 0; i < 20000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const Word decoded = decode(state(b), seed, a.CommandParamCounts());
        a.WriteToGXFIFO(seed); replay(b, seed, decoded); equal(a, b);
        if (i % 100 == 99) {
            a.CmdPIPE.Clear(); a.CmdFIFO.Clear(); a.CmdStallQueue.Clear();
            b.CmdPIPE.Clear(); b.CmdFIFO.Clear(); b.CmdStallQueue.Clear();
        }
    }
    // Deliberately fill and overflow the hardware FIFO/stall queue.
    a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
    for (unsigned i = 0; i < 400; ++i) {
        const uint32_t value = i % 5 == 0 ? 0x20202020u : i;
        const Word decoded = decode(state(b), value, a.CommandParamCounts());
        a.WriteToGXFIFO(value); replay(b, value, decoded); equal(a, b);
    }
    Cache cache;
    std::vector<uint8_t> data(16384);
    std::array<uint32_t, 4> gen{};
    for (unsigned i = 0; i < 4096; ++i) write(data, i, i % 5 ? i : 0x20202020u);
    for (unsigned run = 0; run < 3; ++run) {
        a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
        for (uint32_t offset = 0; offset < 4096; offset += 8) {
            if (run == 1 && offset == 64) { write(data, 65, 0x12345678u); ++gen[0]; }
            if (run == 1 && offset == 1024) { write(data, 1025, 0x10u); ++gen[1]; }
            if (run == 2 && offset == 2048) ++gen[2]; // unrelated page write, same bytes
            const Word* decoded = cache.get(0, 4096, offset, 8, data.data(), gen.data(),
                                             state(b), a.CommandParamCounts());
            require(decoded != nullptr, "cache unexpectedly rejected valid source");
            for (uint32_t i = 0; i < 8; ++i) {
                const uint32_t value = load(data.data() + (offset + i) * 4);
                a.WriteToGXFIFO(value); replay(b, value, decoded[i]); equal(a, b);
            }
            a.CmdPIPE.Clear(); a.CmdFIFO.Clear(); a.CmdStallQueue.Clear();
            b.CmdPIPE.Clear(); b.CmdFIFO.Clear(); b.CmdStallQueue.Clear();
        }
    }
    require(cache.statistics.hits && cache.statistics.invalidations && cache.statistics.reused_words,
            "did not exercise cached hits and mutation invalidation");
    cache.clear(); gen = {}; write(data, 0, 0x10u);
    a.Reset(); b.Reset();
    require(cache.get(0, 4096, 0, 8, data.data(), gen.data(), state(b), a.CommandParamCounts()) != nullptr,
            "cache reset with reused generations");
    require(!cache.get(0, 16385, 0, 1, data.data(), gen.data(), {}, a.CommandParamCounts()),
            "unbounded cache source accepted");

    // Exercise the real geometry executor after batched ingress: matrix
    // mode/identity, color, begin triangle, three vertices and end.
    a.Reset(); b.Reset(); a.SetEnabled(true, true); b.SetEnabled(true, true);
    const uint32_t stream[] = {0x10u, 1u, 0x15u, 0x20u, 0x7FFFu,
        0x29u, 0x001F00C0u, 0x60u, 0xBFFF0000u, 0x40u, 0u,
        0x23u, 0u, 0u, 0x23u, 0x1000u, 0u, 0x23u, 0x10000000u, 0u, 0x41u};
    for (uint32_t value : stream) {
        const Word decoded = decode(state(b), value, a.CommandParamCounts());
        a.WriteToGXFIFO(value); replay(b, value, decoded);
        first->ARM9Timestamp += 100; second->ARM9Timestamp += 100;
        a.Run(); b.Run(); equal(a, b);
        require(a.CycleCount == b.CycleCount && a.NumVertices == b.NumVertices &&
                a.NumPolygons == b.NumPolygons &&
                !std::memcmp(a.PosMatrix, b.PosMatrix, sizeof(a.PosMatrix)) &&
                !std::memcmp(a.VertexRAM, b.VertexRAM, sizeof(a.VertexRAM)),
                "geometry/matrix execution differs");
    }
    require(a.NumVertices >= 3 && a.NumPolygons > 0, "geometry fixture did not emit a triangle");

    // Exact integer projection against the cache-disabled retained body.
    // Include repeated shared vertices, collisions, viewport/width changes,
    // the 16-bit W divider boundary, zero W and guest widescreen projection.
    b.NativeProjectionCache.enabled = true;
    for (unsigned i = 0; i < 4096; ++i) {
        const unsigned sample = i / 2;
        Vertex left{}, right{};
        seed = sample * 1664525u + 1013904223u;
        left.Position[0] = static_cast<s32>(seed & 0xFFFF) - 32768;
        left.Position[1] = static_cast<s32>((seed >> 16) & 0xFFFF) - 32768;
        left.Position[2] = seed & 0xFFFFF;
        const u32 ws[] = {0, 1, 0xFFFF, 0x10000, 0xFFFFFF, 0x1001000};
        left.Position[3] = ws[sample % 6];
        left.HiresPosition[0] = 123; left.HiresPosition[1] = 456;
        right = left;
        const u32 widths[] = {256, 448, 384};
        a.SetRenderWidth(widths[(sample / 32) % 3]); b.SetRenderWidth(a.GetRenderWidth());
        a.SetGuestWideProjection((sample / 96) % 2); b.SetGuestWideProjection(a.GetGuestWideProjection());
        const u32 viewport[] = {sample % 2 ? 8u : 0u, 0, 255, 3, sample % 2 ? 128u : 256u, 189};
        std::memcpy(a.Viewport, viewport, sizeof(viewport));
        std::memcpy(b.Viewport, viewport, sizeof(viewport));
        a.ProjectVertex(&left); b.ProjectVertex(&right);
        require(!std::memcmp(&left, &right, sizeof(Vertex)), "cached integer projection differs");
    }
    require(b.NativeProjectionCache.statistics.hits >= 2048 && b.NativeProjectionCache.statistics.misses,
            "projection cache did not exercise hits and misses");
    b.Reset();
    require(!b.NativeProjectionCache.statistics.hits, "projection cache survived reset");
}
}
int main() {
    try { fifo_snapshot_tests(); tests(); std::puts("[gx_command_batch_test] PASS: real FIFO/parser/geometry, cache, mutation, reset and stalls"); }
    catch (const std::exception& error) { std::fprintf(stderr, "FAIL: %s\n", error.what()); return 1; }
}
