#pragma once

// Diagnostic only. Allocate once before opening audio; never allocate, lock or
// write a file in the audio callback or frame recorder. Bounded first 32768
// frames / 2048 shortage callbacks. Dump only after closing the audio stream.
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace nds_frontend_latency {
inline std::atomic<bool> enabled{false};
inline std::string path;
inline std::vector<std::array<uint64_t, 27>> frames;
inline size_t frame_count = 0;
inline std::array<std::array<uint64_t, 4>, 2048> audio{};
inline std::atomic<uint64_t> audio_count{0};
inline uint64_t frequency = 0;

inline uint64_t cycles() {
#if defined(_WIN32)
    ULONG64 value = 0;
    QueryThreadCycleTime(GetCurrentThread(), &value);
    return value;
#else
    // Host CPU-cycle attribution is optional; timestamps/queue data remain
    // available everywhere SDL is supported. Zero means unsupported.
    return 0;
#endif
}

inline bool begin() {
    const char* value = std::getenv("NDS_FRONTEND_LATENCY_TRACE");
    if (!value || !*value) return false;
    path = value;
    frames.resize(32768);
    frame_count = 0;
    audio_count.store(0, std::memory_order_relaxed);
    frequency = SDL_GetPerformanceFrequency();
    enabled.store(true, std::memory_order_release);
    return true;
}

inline void shortage(int additional, int total) {
    if (!enabled.load(std::memory_order_acquire)) return;
    const uint64_t index = audio_count.fetch_add(1, std::memory_order_relaxed);
    if (index < audio.size())
        audio[index] = {SDL_GetPerformanceCounter(),
                        static_cast<uint64_t>(additional),
                        static_cast<uint64_t>(total), cycles()};
}

inline void frame(const std::array<uint64_t, 27>& row) {
    if (frame_count < frames.size()) frames[frame_count++] = row;
}

inline void finish() {
    if (!enabled.exchange(false, std::memory_order_acq_rel)) return;
    std::ofstream out(path + ".frames.csv");
    out << "# frequency=" << frequency << ",capacity=" << frames.size()
        << ",audio_capacity=" << audio.size() << '\n';
    out << "frame,loop_begin,emu_begin,emu_end,present_begin,present_end,drain_end,frame_end,cycles_loop,cycles_emu,cycles_present,cycles_drain,cycles_end,queue_begin,queue_before_drain,queue_after_drain,underruns,adaptive,upload,draw,swap,readback_ns,audio_started,pool_wait,gl_setup,gl_error,overlay\n";
    for (size_t i = 0; i < frame_count; ++i) {
        for (size_t j = 0; j < frames[i].size(); ++j) {
            if (j) out << ',';
            out << frames[i][j];
        }
        out << '\n';
    }
    std::ofstream events(path + ".audio.csv");
    events << "# frequency=" << frequency << ",events="
           << audio_count.load(std::memory_order_relaxed) << '\n';
    events << "counter,additional_bytes,total_bytes,thread_cycles\n";
    const auto count = std::min<uint64_t>(audio.size(), audio_count.load());
    for (uint64_t i = 0; i < count; ++i)
        events << audio[i][0] << ',' << audio[i][1] << ',' << audio[i][2]
               << ',' << audio[i][3] << '\n';
}
} // namespace nds_frontend_latency
