#include "gpu2d.h"
#include "gpu3d.h"
#include "savestate.h"
#include "vram.h"
#include "NDS.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

namespace {

bool expect(bool value, const char* message) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", message);
    return value;
}

bool same_vram(const NdsVramSaveState& a, const NdsVramSaveState& b) {
    return a.vram == b.vram && a.written == b.written &&
        a.exec_generation == b.exec_generation && a.palette == b.palette &&
        a.oam == b.oam &&
        std::memcmp(a.vramcnt, b.vramcnt, sizeof(a.vramcnt)) == 0 &&
        a.texture_generation == b.texture_generation;
}

bool same_gpu2d_unit(const NdsGpu2dUnitSaveState& a,
                     const NdsGpu2dUnitSaveState& b) {
    return a.dispcnt == b.dispcnt &&
        std::equal(std::begin(a.bgcnt), std::end(a.bgcnt), std::begin(b.bgcnt)) &&
        std::equal(std::begin(a.bgx), std::end(a.bgx), std::begin(b.bgx)) &&
        std::equal(std::begin(a.bgy), std::end(a.bgy), std::begin(b.bgy)) &&
        std::equal(std::begin(a.pa), std::end(a.pa), std::begin(b.pa)) &&
        std::equal(std::begin(a.pb), std::end(a.pb), std::begin(b.pb)) &&
        std::equal(std::begin(a.pc), std::end(a.pc), std::begin(b.pc)) &&
        std::equal(std::begin(a.pd), std::end(a.pd), std::begin(b.pd)) &&
        std::equal(std::begin(a.refx), std::end(a.refx), std::begin(b.refx)) &&
        std::equal(std::begin(a.refy), std::end(a.refy), std::begin(b.refy)) &&
        std::equal(std::begin(a.win), std::end(a.win), std::begin(b.win)) &&
        a.bg_mosaic_x == b.bg_mosaic_x && a.bg_mosaic_y == b.bg_mosaic_y &&
        a.obj_mosaic_x == b.obj_mosaic_x && a.obj_mosaic_y == b.obj_mosaic_y &&
        a.bldcnt == b.bldcnt && a.bldalpha == b.bldalpha &&
        a.eva == b.eva && a.evb == b.evb && a.evy == b.evy &&
        std::equal(std::begin(a.refx_internal), std::end(a.refx_internal),
                   std::begin(b.refx_internal)) &&
        std::equal(std::begin(a.refy_internal), std::end(a.refy_internal),
                   std::begin(b.refy_internal)) &&
        a.capture == b.capture && a.master_bright == b.master_bright &&
        a.capture_latch == b.capture_latch;
}

bool same_gpu2d(const NdsGpu2dSaveState& a, const NdsGpu2dSaveState& b) {
    return same_gpu2d_unit(a.unit[0], b.unit[0]) &&
        same_gpu2d_unit(a.unit[1], b.unit[1]) &&
        a.framebuffers == b.framebuffers && a.front == b.front &&
        a.frame_capture_active == b.frame_capture_active &&
        a.present_capture_active == b.present_capture_active;
}

bool vram_and_midframe_capture_roundtrip() {
    nds_vram_reset();
    nds_gpu3d_reset();
    nds_gpu2d_reset();
    nds_vram_map(0, 0x80u);  // bank A, LCDC for display capture
    nds_vram_map(2, 0x82u);  // bank C, ARM7 executable window
    nds_video_write(9, 0x05000000u, 0x1234u, 2u);
    nds_video_write(9, 0x07000000u, 0x5678u, 2u);
    nds_video_write(7, 0x06000000u, 0xA55Au, 2u);

    nds_gpu2d_write(0x04000000u, 0x00010000u, 4u);
    nds_gpu2d_write(0x04000008u, 0x23450123u, 4u);
    nds_gpu2d_write(0x04000010u, 0x01AB0123u, 4u);
    nds_gpu2d_write(0x04000020u, 0xFF000100u, 4u);
    nds_gpu2d_write(0x04000024u, 0x01000020u, 4u);
    nds_gpu2d_write(0x04000028u, 0x00123456u, 4u);
    nds_gpu2d_write(0x0400003Cu, 0x00654321u, 4u);
    nds_gpu2d_write(0x04000040u, 0x70908020u, 4u);
    nds_gpu2d_write(0x04000048u, 0x3F1F2F0Fu, 4u);
    nds_gpu2d_write(0x0400004Cu, 0x4321u, 2u);
    nds_gpu2d_write(0x04000050u, 0x08000441u, 4u);
    nds_gpu2d_write(0x04000054u, 0x0000000Cu, 4u);
    nds_gpu2d_write(0x04000064u, 0x80000000u, 4u);
    nds_gpu2d_write(0x0400006Cu, 0x4008u, 2u);
    nds_gpu2d_write(0x04001000u, 0x00010000u, 4u);
    nds_gpu2d_write(0x04001028u, 0x00765432u, 4u);
    nds_gpu2d_set_threaded(true, 2u);
    const uint64_t generation_before = nds_vram_texture_generation();
    nds_gpu2d_render_scanline(0);

    NdsGpu2dSaveState saved_gpu2d{};
    NdsVramSaveState saved_vram{};
    std::string error;
    bool ok = expect(gpu2d_savestate_export(&saved_gpu2d),
                     "export threaded mid-frame GPU2D state") &&
        expect(vram_savestate_export(&saved_vram),
               "export VRAM after staged capture") &&
        expect(nds_gpu2d_jobs_outstanding.load() == 0u &&
               nds_gpu2d_staged_captures.load() == 0u,
               "video export drains workers and applies staged capture") &&
        expect(saved_gpu2d.unit[0].capture_latch == 1u,
               "mid-frame display capture latch is serialized") &&
        expect(saved_vram.texture_generation > generation_before,
               "capture write advances texture coherence generation") &&
        expect((saved_vram.vram[1] & 0x80u) != 0u,
               "captured source-A alpha reaches physical VRAM");

    nds_vram_map(0, 0u);
    nds_vram_map(2, 0u);
    nds_gpu2d_write(0x04000064u, 0u, 4u);
    nds_gpu2d_finish_frame();
    ok &= expect(vram_savestate_import(saved_vram, &error), error.c_str()) &&
        expect(gpu2d_savestate_import(saved_gpu2d, &error), error.c_str());

    NdsVramSaveState loaded_vram{};
    NdsGpu2dSaveState loaded_gpu2d{};
    ok &= expect(vram_savestate_export(&loaded_vram),
                 "re-export restored VRAM") &&
        expect(gpu2d_savestate_export(&loaded_gpu2d),
               "re-export restored GPU2D") &&
        expect(same_vram(saved_vram, loaded_vram),
               "VRAM banks, maps, palette/OAM, provenance and generations roundtrip") &&
        expect(same_gpu2d(saved_gpu2d, loaded_gpu2d),
               "GPU2D registers, affine phase, capture and framebuffers roundtrip");

    // A second application must be stable and must not retain stale ring jobs.
    ok &= expect(vram_savestate_import(saved_vram, &error), error.c_str()) &&
        expect(gpu2d_savestate_import(saved_gpu2d, &error), error.c_str()) &&
        expect(nds_gpu2d_jobs_outstanding.load() == 0u,
               "repeated GPU2D load remains quiescent");
    nds_gpu2d_shutdown_workers();
    return ok;
}

bool gpu3d_device_roundtrip() {
    nds_vram_reset();
    nds_gpu3d_reset();
    nds_gpu3d_set_threaded(true);
    nds_gpu3d_set_render_xpos(0x42u);
    nds_gpu3d_write(0x04000350u, 0x3F1F001Fu, 4u);
    NdsGpu3dSaveState saved{};
    std::string error;
    bool ok = expect(gpu3d_savestate_export(&saved, &error), error.c_str()) &&
        expect(gpu3d_savestate_validate(saved, &error), error.c_str());
    nds_gpu3d_set_render_xpos(7u);
    ok &= expect(gpu3d_savestate_import(saved, &error), error.c_str()) &&
        expect(nds_gpu3d_render_xpos() == 0x42u,
               "GPU3D register state restores from pointer-safe vendored stream");
    NdsGpu3dSaveState repeated{};
    ok &= expect(gpu3d_savestate_export(&repeated, &error), error.c_str()) &&
        expect(saved.device == repeated.device &&
               saved.arm9_timestamp == repeated.arm9_timestamp,
               "GPU3D device stream is deterministic after renderer rebuild");
    NdsGpu3dSaveState corrupt = saved;
    corrupt.device[0] ^= 0xFFu;
    ok &= expect(!gpu3d_savestate_validate(corrupt, &error),
                 "corrupt GPU3D stream is rejected before live apply");
    // MELN header + GP3D section header is 32 bytes. The first device field is
    // the 256-entry command FIFO occupancy; pin the vendored decode guard so a
    // checksum-recomputed section cannot install an out-of-range FIFO state.
    corrupt = saved;
    corrupt.device[32] = 0x01u;
    corrupt.device[33] = 0x01u;
    corrupt.device[34] = 0u;
    corrupt.device[35] = 0u;
    ok &= expect(!gpu3d_savestate_validate(corrupt, &error),
                 "out-of-range GPU3D FIFO is rejected during detached decode") &&
        expect(nds_gpu3d_render_xpos() == 0x42u,
               "GPU3D prevalidation does not mutate the live device");
    nds_gpu3d_use_soft_renderer(false);
    return ok;
}

void gpu3d_run_for(uint64_t& cycles, uint64_t delta = 4096u) {
    cycles += delta;
    nds_gpu3d_run(cycles);
}

void gpu3d_fifo_write(uint32_t value, uint64_t& cycles) {
    nds_gpu3d_write(0x04000400u, value, 4u);
    gpu3d_run_for(cycles);
}

void gpu3d_cmd(uint8_t command, uint32_t param, uint64_t& cycles) {
    gpu3d_fifo_write(command, cycles);
    gpu3d_fifo_write(param, cycles);
}

void gpu3d_load_projection(const int32_t (&matrix)[16],
                           uint64_t& cycles) {
    gpu3d_cmd(0x10u, 0u, cycles);  // projection matrix mode
    gpu3d_fifo_write(0x16u, cycles);  // load 4x4
    for (uint32_t value : matrix)
        gpu3d_fifo_write(value, cycles);
}

uint32_t pack_i16(int16_t lo, int16_t hi) {
    return static_cast<uint16_t>(lo) |
        (static_cast<uint32_t>(static_cast<uint16_t>(hi)) << 16u);
}

void gpu3d_submit_triangle(uint64_t& cycles) {
    gpu3d_cmd(0x29u, (31u << 16u) | (1u << 6u) | (1u << 7u), cycles);
    gpu3d_cmd(0x40u, 0u, cycles);  // triangles
    gpu3d_fifo_write(0x23u, cycles);
    gpu3d_fifo_write(pack_i16(-0x0400, -0x0400), cycles);
    gpu3d_fifo_write(0x0000u, cycles);
    gpu3d_fifo_write(0x23u, cycles);
    gpu3d_fifo_write(pack_i16(0x0400, -0x0400), cycles);
    gpu3d_fifo_write(0x0000u, cycles);
    gpu3d_fifo_write(0x23u, cycles);
    gpu3d_fifo_write(pack_i16(0, 0x0400), cycles);
    gpu3d_fifo_write(0x0000u, cycles);
    gpu3d_cmd(0x41u, 0u, cycles);
}

bool gpu3d_rendered_projection_flag_follows_submitted_geometry() {
    constexpr int32_t orthographic[16] = {
        0x1000, 0,      0,      0,
        0,      0x1000, 0,      0,
        0,      0,      0x1000, 0,
        0,      0,      0,      0x1000,
    };
    constexpr int32_t perspective[16] = {
        0x1000, 0,      0,      0,
        0,      0x1000, 0,      0,
        0,      0,      0x1000, 0x1000,
        0,      0,      0,      0x1000,
    };

    auto render_one = [&](const int32_t (&submitted)[16],
                          const int32_t (&after_submit)[16]) {
        uint64_t cycles = 0;
        nds_gpu3d_reset();
        nds_gpu3d_set_power((1u << 2u) | (1u << 3u));
        gpu3d_run_for(cycles);
        gpu3d_cmd(0x60u, 0xBFFF0000u, cycles);  // full 256x192 viewport
        gpu3d_load_projection(submitted, cycles);
        gpu3d_submit_triangle(cycles);
        gpu3d_load_projection(after_submit, cycles);
        gpu3d_cmd(0x50u, 0u, cycles);  // swap buffers
        gpu3d_run_for(cycles, 32768u);
        nds_gpu3d_vblank();
        return std::pair<uint32_t, bool>{
            nds_gpu3d_render_polygon_count(),
            nds_gpu3d_projection_has_perspective()};
    };

    const auto ortho = render_one(orthographic, perspective);
    const auto persp = render_one(perspective, orthographic);
    std::string error;
    NdsGpu3dSaveState saved_perspective{};
    bool ok = expect(ortho.first == 1u && !ortho.second,
                     "orthographic rendered polygon stays native-low-poly eligible") &&
        expect(persp.first == 1u && persp.second,
               "perspective rendered polygon remains wide even after later matrix reset") &&
        expect(gpu3d_savestate_export(&saved_perspective, &error),
               error.c_str());

    render_one(orthographic, orthographic);
    ok &= expect(nds_gpu3d_render_polygon_count() == 1u &&
                 !nds_gpu3d_projection_has_perspective(),
                 "control render clears perspective flag before import") &&
        expect(gpu3d_savestate_import(saved_perspective, &error),
               error.c_str()) &&
        expect(nds_gpu3d_render_polygon_count() == 1u &&
               nds_gpu3d_projection_has_perspective(),
               "serialized polygon perspective flag survives GPU3D savestate roundtrip");
    return ok;
}

bool gpu3d_long_strip_savestate_roundtrip() {
    uint64_t cycles = 0;
    nds_gpu3d_reset();
    nds_gpu3d_set_power((1u << 2u) | (1u << 3u));
    gpu3d_run_for(cycles);
    gpu3d_cmd(0x60u, 0xBFFF0000u, cycles);
    gpu3d_cmd(0x29u, (31u << 16u) | (1u << 6u) | (1u << 7u), cycles);
    gpu3d_cmd(0x40u, 2u, cycles);  // one triangle strip, many vertices
    auto vertex = [&](unsigned index) {
        gpu3d_fifo_write(0x23u, cycles);
        gpu3d_fifo_write(pack_i16(-0x0800 + (index / 2u) * 0x0400,
                                  (index & 1u) ? 0x0400 : -0x0400), cycles);
        gpu3d_fifo_write(0u, cycles);
    };
    for (unsigned i = 0; i < 7; ++i) vertex(i);

    NdsGpu3dSaveState saved{};
    std::string error;
    if (!expect(gpu3d_savestate_export(&saved, &error), error.c_str()))
        return false;
    // Inspect the actual command-generated state, not a hand-built fixture.
    auto device = std::make_unique<melonDS::NDS>();
    melonDS::Savestate decode(saved.device.data(),
                              static_cast<uint32_t>(saved.device.size()), false);
    device->GPU.GPU3D.DoSavestate(&decode);
    bool ok = expect(!decode.Error && device->GPU.GPU3D.VertexNum == 7u &&
                     device->GPU.GPU3D.VertexNumInPoly == 2u,
                     "strip tracks total submitted vertices separately from buffer index") &&
        expect(gpu3d_savestate_validate(saved, &error),
               "valid long strip passes GPU3D savestate validation");
    const uint64_t saved_cycles = cycles;
    vertex(7);
    vertex(8);
    NdsGpu3dSaveState uninterrupted{};
    ok &= expect(gpu3d_savestate_export(&uninterrupted, &error), error.c_str());
    if (!expect(gpu3d_savestate_import(saved, &error),
                "restore long strip GPU3D state"))
        return false;
    cycles = saved_cycles;
    vertex(7);
    vertex(8);
    NdsGpu3dSaveState resumed{};
    ok &= expect(gpu3d_savestate_export(&resumed, &error), error.c_str()) &&
        expect(resumed.device == uninterrupted.device &&
               resumed.arm9_timestamp == uninterrupted.arm9_timestamp,
               "continuing a restored long strip matches uninterrupted geometry");

    // Removing the total-count limit must keep the real buffer-index guard.
    device->GPU.GPU3D.VertexNumInPoly = 11u;
    melonDS::Savestate encode;
    device->GPU.GPU3D.DoSavestate(&encode);
    encode.Finish();
    if (!expect(!encode.Error, "encode malformed buffer-index fixture"))
        return false;
    const auto* begin = static_cast<const uint8_t*>(encode.Buffer());
    NdsGpu3dSaveState corrupt = saved;
    corrupt.device.assign(begin, begin + encode.Length());
    ok &= expect(!gpu3d_savestate_validate(corrupt, &error),
                 "out-of-range vertex buffer index remains rejected");
    return ok;
}

}  // namespace

int main() {
    bool ok = vram_and_midframe_capture_roundtrip();
    ok &= gpu3d_device_roundtrip();
    ok &= gpu3d_rendered_projection_flag_follows_submitted_geometry();
    ok &= gpu3d_long_strip_savestate_roundtrip();
    return ok ? 0 : 1;
}
