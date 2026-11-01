#include "cpu_render_handoff.h"
#include "host_profile.h"
#include <stdexcept>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
thread_local NdsCpuRenderHandoff* owner = nullptr;
uint64_t cycles() {
#if defined(_WIN32)
    ULONG64 value = 0;
    QueryThreadCycleTime(GetCurrentThread(), &value);
    return value;
#else
    return 0;
#endif
}
}
bool nds_cpu_render_on_worker() { return owner != nullptr; }
void nds_cpu_render_main(std::function<void()> request) {
    if (owner) owner->Main(std::move(request));
    else request();
}

NdsCpuRenderHandoff::NdsCpuRenderHandoff(Clock source, std::function<void()> before)
    : clock(source), before_request(std::move(before)), main_id(std::this_thread::get_id()) {
    if (!clock) throw std::invalid_argument("CPU handoff needs its frontend clock");
    // All synchronization members must exist before the new thread can run.
    producer=std::thread([this] { Produce(); });
}
NdsCpuRenderHandoff::~NdsCpuRenderHandoff() {
    // Drain even after a presentation failure: a producer might be waiting for
    // the main owner, so joining the OS thread before pumping would deadlock.
    try { Join(); } catch (...) {}
    { std::lock_guard lock(mutex); stopping = true; }
    changed.notify_all();
    producer.join();
}
void NdsCpuRenderHandoff::Start(std::function<void()> next) {
    if (!next) throw std::invalid_argument("empty CPU frame");
    if (std::this_thread::get_id() != main_id)
        throw std::logic_error("frame publication is not on the main owner");
    std::lock_guard lock(mutex);
    if (active || stopping) throw std::logic_error("CPU frame already owned");
    frame = std::move(next); frame_error = nullptr; timing = {};
    active = true; done = false;
    changed.notify_all();
}
void NdsCpuRenderHandoff::Join() {
    if (std::this_thread::get_id() != main_id)
        throw std::logic_error("CPU frame join is not on the main owner");
    std::unique_lock lock(mutex);
    while (active && !done) {
        changed.wait(lock, [&] { return bool(request) || done; });
        if (request) {
            auto action = std::move(request);
            request = nullptr;
            lock.unlock();
            std::exception_ptr error;
            try { if (before_request) before_request(); action(); }
            catch (...) { error = std::current_exception(); }
            lock.lock();
            request_error = error; request_done = true;
            changed.notify_all();
        }
    }
    if (!active) return;
    active = false;
    auto error = frame_error;
    lock.unlock();
    if (error) std::rethrow_exception(error);
}
void NdsCpuRenderHandoff::Main(std::function<void()> action) {
    const auto begin = clock();
    std::unique_lock lock(mutex);
    if (!active || request) throw std::logic_error("invalid native borrow");
    if (!timing.first_request) {
        timing.first_request = begin;
        timing.cycles_first_request = cycles();
    }
    ++timing.main_requests;
    request_error = nullptr; request_done = false; request = std::move(action);
    changed.notify_all();
    changed.wait(lock, [&] { return request_done; });
    timing.wait_ticks += clock() - begin;
    auto error = request_error;
    lock.unlock();
    if (error) std::rethrow_exception(error);
}
void NdsCpuRenderHandoff::Produce() {
    NdsHostProfThreadScope profile(NDS_HOSTPROF_ROLE_EMU);
    owner = this;
    std::unique_lock lock(mutex);
    for (;;) {
        changed.wait(lock, [&] { return stopping || bool(frame); });
        if (stopping) break;
        auto action = std::move(frame);
        frame = nullptr;
        lock.unlock();
        timing.begin = clock(); timing.cycles_begin = cycles();
        std::exception_ptr error;
        try { action(); } catch (...) { error = std::current_exception(); }
        timing.cycles_end = cycles(); timing.end = clock();
        lock.lock();
        frame_error = error; done = true;
        changed.notify_all();
        // Start cannot publish another action until Join consumes this slot.
    }
    owner = nullptr;
}
