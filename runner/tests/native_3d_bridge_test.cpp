#include "gpu2d.h"
#include "gpu3d.h"
#include "io.h"
#include "runtime_arm.h"
#include "savestate.h"
#include "vram.h"

#include <array>
#include <cstdio>
#include <initializer_list>
#include <stdexcept>
#include <string>

// The production batch entry point links the normal Main-RAM window. These
// focused checks use register/native-operation ingress and need no guest RAM.
NdsBusFastWin g_busf_main{};
extern uint32_t nds_video_test_irq_flags[2];

namespace {
constexpr uint32_t kGxIrq = 1u << 21;

void expect(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void fifo(uint32_t value) { nds_gpu3d_write(0x04000400u, value, 4); }
void op(uint8_t command, std::initializer_list<uint32_t> parameters = {}) {
    nds_gpu3d_native_operation(command, {parameters.begin(), parameters.size()});
}
uint32_t xy(int16_t x, int16_t y) {
    return uint16_t(x) | (uint32_t(uint16_t(y)) << 16);
}
NdsGpu3dSaveState save() {
    NdsGpu3dSaveState result;
    std::string error;
    expect(gpu3d_savestate_export(&result, &error), error.c_str());
    expect(!result.native_geometry.empty(), "native production bridge must serialize its geometry service");
    expect(gpu3d_savestate_validate(result, &error), error.c_str());
    return result;
}
void load(const NdsGpu3dSaveState& state) {
    std::string error;
    expect(gpu3d_savestate_import(state, &error), error.c_str());
}
void render_pending() {
    nds_gpu3d_vblank();
    nds_gpu3d_vcount215();
    nds_gpu3d_vcount144();
    nds_gpu3d_start_frame();
}
void triangle(uint16_t color) {
    op(0x10, {0}); op(0x15);  // projection identity
    op(0x10, {2}); op(0x15);  // position/direction identity
    op(0x60, {0xBFFF0000u});
    op(0x29, {(31u << 16) | (1u << 6) | (1u << 7)});
    op(0x20, {color}); op(0x40, {0});
    op(0x23, {xy(-1024, -1024), 0});
    op(0x23, {xy(1024, -1024), 0});
    op(0x23, {xy(0, 1024), 0});
    op(0x41); op(0x50, {0});
}

void poll_irq_and_partial_command_roundtrip() {
    nds_gpu3d_reset();
    nds_video_test_irq_flags[0] = nds_video_test_irq_flags[1] = 0;
    nds_gpu3d_write(0x04000600u, 2u << 30, 4);  // empty FIFO IRQ
    expect((nds_video_test_irq_flags[0] & kGxIrq) != 0,
           "empty-FIFO IRQ reaches the production ARM9 callback");
    nds_clear_irq(0, kGxIrq);
    nds_gpu3d_check_fifo_irq();
    expect((nds_video_test_irq_flags[0] & kGxIrq) != 0,
           "acknowledged level IRQ reasserts while the FIFO remains empty");
    nds_gpu3d_write(0x04000603u, 0, 1);  // byte write disables IRQ mode
    nds_clear_irq(0, kGxIrq);
    nds_gpu3d_check_fifo_irq();
    expect((nds_video_test_irq_flags[0] & kGxIrq) == 0 && nds_video_test_irq_flags[1] == 0,
           "disabling FIFO IRQ leaves both CPU callbacks clear");

    // Snapshot a genuinely incomplete packed POS_TEST, continue it, and then
    // repeat from the restored current-build bridge state.
    fifo(0x71u); fifo(xy(0x100, 0x200));
    const auto partial = save();
    auto finish_position = [] {
        fifo(0x300u);
        expect(nds_gpu3d_read(0x04000620u, 4) == 0x100u &&
               nds_gpu3d_read(0x04000624u, 4) == 0x200u &&
               nds_gpu3d_read(0x04000628u, 4) == 0x300u &&
               nds_gpu3d_read(0x0400062Cu, 4) == 0x1000u,
               "result polling publishes the completed native position test");
        expect(nds_gpu3d_read(0x04000621u, 1) == 1u &&
               nds_gpu3d_read(0x04000624u, 2) == 0x200u,
               "native result registers preserve byte and halfword read lanes");
        const auto status = nds_gpu3d_read(0x04000600u, 4);
        expect((status & ((1u << 25) | (1u << 26))) == ((1u << 25) | (1u << 26)) &&
               (status & (1u << 27)) == 0 && nds_gpu3d_cycles_to_run() == 0,
               "completed result polling exposes an empty, ready native job queue");
    };
    finish_position();
    nds_gpu3d_reset(); load(partial);
    expect(save().native_geometry == partial.native_geometry,
           "partial packed command state roundtrips without alteration");
    finish_position();

    const auto stable = save();
    std::string error;
    auto corrupt = stable;
    corrupt.native_geometry.pop_back();
    expect(!gpu3d_savestate_import(corrupt, &error), "truncated native state is rejected before apply");
    corrupt = stable; corrupt.device[0] ^= 0xFFu;
    expect(!gpu3d_savestate_import(corrupt, &error), "corrupt raster device envelope is rejected before apply");
    const auto unchanged = save();
    expect(unchanged.native_geometry == stable.native_geometry && unchanged.device == stable.device,
           "rejected bridge state leaves both native geometry and raster device unchanged");
}

void rendered_capture_and_pending_swap_roundtrip() {
    nds_vram_reset(); nds_gpu2d_reset(); nds_gpu3d_reset();
    nds_gpu3d_use_soft_renderer(false);
    nds_gpu3d_set_power((1u << 2) | (1u << 3));
    nds_vram_map(0, 0x80u);  // bank A is capture-writable LCDC
    triangle(0x001Fu);
    expect((nds_gpu3d_read(0x04000600u, 4) & (1u << 27)) != 0,
           "submitted native swap remains pending until production VBlank");
    expect(nds_gpu3d_read(0x04000604u, 4) == (3u << 16 | 1u),
           "guest polygon/vertex counters expose the pending native triangle");
    const auto pending = save();
    render_pending();
    const auto red = nds_gpu3d_capture_line(96)[128];
    expect((red & 0x1F3F3F3Fu) == 0x1F00003Fu,
           "production portable raster capture contains the submitted red triangle");

    // Exercise the actual 2D display-capture consumer, not just GetLine.
    nds_gpu2d_write(0x04000000u, 0x00010108u, 4);
    nds_gpu2d_write(0x04000064u, 0x81300000u, 4);  // 256x192, 3D source A, bank A
    nds_gpu2d_render_scanline(0);
    nds_gpu2d_render_scanline(96);
    nds_gpu2d_finish_frame();
    expect(nds_video_read(9, 0x06800000u + (96u * 256u + 128u) * 2u, 2) == 0x801Fu,
           "native red RGB6/alpha5 capture becomes guest-visible RGB5/alpha1 VRAM");

    nds_gpu3d_reset(); load(pending);
    expect((nds_gpu3d_read(0x04000600u, 4) & (1u << 27)) != 0,
           "pending swap survives bridge save/load before VBlank");
    render_pending();
    expect(nds_gpu3d_capture_line(96)[128] == red,
           "restored pending native frame produces the same capture pixel");
    const auto displayed = save();
    triangle(0x03E0u); render_pending();
    expect((nds_gpu3d_capture_line(96)[128] & 0x3F3F3Fu) == 0x003F00u,
           "control frame replaces red with a distinct green triangle");
    load(displayed);
    expect(nds_gpu3d_capture_line(96)[128] == red,
           "loading a displayed native frame immediately rebuilds its raster capture");
}
}  // namespace

int main() {
    try {
        poll_irq_and_partial_command_roundtrip();
        rendered_capture_and_pending_swap_roundtrip();
        nds_gpu2d_shutdown_workers();
        std::puts("native production capture, polling/IRQ and current-build bridge savestate passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        nds_gpu2d_shutdown_workers();
        return 1;
    }
}
