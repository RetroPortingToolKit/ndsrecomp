#include <cstdint>
#include "cpu_render_handoff.h"

#include "net/net_ring.h"

extern "C" unsigned long long g_runtime_cycles = 0;

uint16_t nds_powercontrol9() { return 0xFFFFu; }
#if defined(NDS_NATIVE_3D)
uint32_t nds_video_test_irq_flags[2]{};
void nds_raise_irq(int cpu, uint32_t mask) { nds_video_test_irq_flags[cpu] |= mask; }
void nds_clear_irq(int cpu, uint32_t mask) { nds_video_test_irq_flags[cpu] &= ~mask; }
#else
void nds_raise_irq(int, uint32_t) {}
void nds_clear_irq(int, uint32_t) {}
#endif
void nds_dma_trigger(int, unsigned) {}
void nds_gxfifo_set_stall(bool) {}
void scheduler_terminal_halt_all(const char*) {}
bool nds_title_patches_mph_adaptive_centered_native() { return false; }
void nds_title_patches_projection_begin() {}
// This fixture renders synchronously and has no frontend producer thread.
bool nds_cpu_render_on_worker() { return false; }
void nds_cpu_render_main(std::function<void()> request) { request(); }

uint64_t net_ring_push(NdsNetEventKind, uint8_t, uint16_t, uint32_t,
                       const uint8_t*, const uint8_t*, uint32_t, uint32_t,
                       uint16_t, uint16_t, uint16_t, uint32_t) {
    return 0;
}
