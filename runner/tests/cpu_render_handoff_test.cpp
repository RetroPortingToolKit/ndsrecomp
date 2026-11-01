#include "cpu_render_handoff.h"
#include "host_profile.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <stdexcept>
#include <thread>

// This focused ownership test has no sampler, SDL, guest or GPU dependency.
void nds_hostprof_register_current_thread(NdsHostProfRole) {}
void nds_hostprof_unregister_current_thread() {}
namespace {
uint64_t clock_ticks() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
void require(bool condition,const char* text) {
    if (!condition) throw std::runtime_error(text);
}
}
int main() {
    try {
        const auto main=std::this_thread::get_id();
        unsigned borrows=0;
        NdsCpuRenderHandoff handoff(clock_ticks,[&] {
            require(std::this_thread::get_id()==main,"GL borrow left main owner");
            ++borrows;
        });
        std::array<uint32_t,4> live{1,2,3,4};
        const auto completed=live; // represents the prior immutable packet
        std::promise<void> reached_native;
        auto reached=reached_native.get_future();
        std::atomic<bool> resumed=false, executed=false;
        handoff.Start([&] {
            require(nds_cpu_render_on_worker(),"CPU did not execute on producer");
            live.fill(9); // producer can change its next frame during present
            reached_native.set_value();
            nds_cpu_render_main([&] {
                require(!resumed.load(),"producer resumed before native completion");
                require(live[0]==9,"borrow did not observe stopped producer state");
                executed.store(true);
            });
            require(executed.load(),"native return preceded main execution");
            resumed.store(true);
        });
        require(reached.wait_for(std::chrono::seconds(2))==std::future_status::ready,
                "producer could not progress during a main present");
        require(completed==std::array<uint32_t,4>{1,2,3,4},"presentation packet changed");
        require(!executed.load() && !resumed.load(),"borrow escaped explicit main pump");
        bool bounded=false;
        try { handoff.Start([] {}); } catch (const std::logic_error&) { bounded=true; }
        require(bounded,"second producer publication was not bounded");
        handoff.Join();
        require(resumed.load() && borrows==1,"native completion was lost");
        require(handoff.LastTiming().main_requests==1 && handoff.LastTiming().end,
                "actual execution accounting unavailable");

        // Native/producer failure must return ownership and remain drainable.
        handoff.Start([] { nds_cpu_render_main([] { throw std::runtime_error("native fault"); }); });
        bool native_error=false;
        try { handoff.Join(); } catch (const std::runtime_error&) { native_error=true; }
        require(native_error,"native error was hidden");
        handoff.Start([] { throw std::runtime_error("CPU fault"); });
        bool cpu_error=false;
        try { handoff.Join(); } catch (const std::runtime_error&) { cpu_error=true; }
        require(cpu_error,"CPU error was hidden");
        handoff.Start([] {}); handoff.Join(); // no stale request/error on rearm
        unsigned drained=0;
        {
            NdsCpuRenderHandoff exiting(clock_ticks);
            exiting.Start([&] { nds_cpu_render_main([&] { ++drained; }); });
        }
        require(drained==1,"shutdown joined without draining the native request");
        std::puts("cpu_render_handoff_test: ownership/backpressure, overlap-prefix, native+CPU faults and shutdown PASS");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr,"cpu_render_handoff_test: %s\n",e.what()); return 1;
    }
}
