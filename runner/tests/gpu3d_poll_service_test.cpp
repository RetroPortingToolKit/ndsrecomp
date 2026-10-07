#include "gpu3d_poll_service.h"
#include "NDS.h"
#include <cstdio>
#include <memory>

static bool same(const melonDS::GPU3D& a, const melonDS::GPU3D& b) {
    return a.Timestamp == b.Timestamp && a.CycleCount == b.CycleCount &&
        a.GXStat == b.GXStat && a.VertexPipeline == b.VertexPipeline &&
        a.NormalPipeline == b.NormalPipeline && a.PolygonPipeline == b.PolygonPipeline &&
        a.VertexSlotCounter == b.VertexSlotCounter && a.VertexSlotsFree == b.VertexSlotsFree &&
        a.CmdPIPE.Level() == b.CmdPIPE.Level() && a.CmdFIFO.Level() == b.CmdFIFO.Level() &&
        a.NumPushPopCommands == b.NumPushPopCommands && a.NumTestCommands == b.NumTestCommands &&
        a.ProjMatrix[0] == b.ProjMatrix[0];
}
int main() {
    auto reference = std::make_unique<melonDS::NDS>();
    auto replacement = std::make_unique<melonDS::NDS>();
    for (unsigned mode = 0; mode < 6; ++mode) {
        for (uint64_t elapsed : {0ull, 1ull, 31ull, 32ull, 33ull, 96ull}) {
            auto& a = reference->GPU.GPU3D;
            auto& b = replacement->GPU.GPU3D;
            for (auto* g : {&a, &b}) {
                g->Reset(); g->GeometryEnabled = mode != 1;
                g->FlushRequest = mode == 2; g->Timestamp = 100;
                g->CycleCount = 32;
                if (mode >= 3) {
                    g->GXStat = (1u << 27) | (1u << 14) | 1u;
                    g->VertexPipeline = 8; g->NormalPipeline = 6;
                    g->PolygonPipeline = 12;
                }
                if (mode == 4) g->CmdPIPE.Write({uint64_t{0x15} << 32}); // matrix identity
                if (mode == 5) g->CycleCount = 0; // FinishWork already due
            }
            reference->ARM9Timestamp = replacement->ARM9Timestamp = (100 + elapsed) << 1;
            const auto before = b.Timestamp;
            const int count = b.CycleCount;
            const bool settled = nds_gpu3d_settle_poll(b, replacement->ARM9Timestamp, 1);
            if (!settled && (before != b.Timestamp || count != b.CycleCount)) {
                std::fprintf(stderr, "due work mutated before fallback\n"); return 1;
            }
            a.Run(); if (!settled) b.Run();
            if (!same(a, b)) {
                std::fprintf(stderr, "poll contract differs mode=%u elapsed=%llu\n", mode,
                    static_cast<unsigned long long>(elapsed)); return 1;
            }
            if (mode == 3 && elapsed == 31 && !(b.GXStat & (1u << 27))) return 1;
            if (mode == 3 && elapsed == 96 && (b.GXStat & (1u << 27))) return 1;
            if (mode == 4 && elapsed >= 32 && b.ProjMatrix[0] != (1 << 12)) return 1;
        }
    }
    auto& b = replacement->GPU.GPU3D;
    b.Reset(); b.GeometryEnabled = true; b.GXStat = 1u << 27;
    b.Timestamp = 100; b.CycleCount = 32;
    if (nds_gpu3d_settle_poll(b, 198, 1) || b.Timestamp != 100 || b.CycleCount != 32) return 1;
    std::puts("36 real-device poll/deadline cases and timestamp discontinuity passed");
}
