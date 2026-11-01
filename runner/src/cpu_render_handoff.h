// One simulation producer, with synchronous borrowing by the main GL owner.
#pragma once
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

// There is deliberately no unbounded command/frame queue. Start accepts exactly
// one frame; Join services its main-thread requests before returning ownership.
// A request may borrow live device state ONLY while its calling producer waits.
class NdsCpuRenderHandoff final {
public:
    using Clock = uint64_t (*)();
    struct Timing {
        uint64_t begin{}, end{}, first_request{}, wait_ticks{};
        uint64_t cycles_begin{}, cycles_end{}, cycles_first_request{};
        uint64_t main_requests{};
    };
    explicit NdsCpuRenderHandoff(Clock clock, std::function<void()> before_request = {});
    ~NdsCpuRenderHandoff();
    NdsCpuRenderHandoff(const NdsCpuRenderHandoff&) = delete;
    NdsCpuRenderHandoff& operator=(const NdsCpuRenderHandoff&) = delete;
    void Start(std::function<void()> frame);
    void Join();
    const Timing& LastTiming() const { return timing; } // only after Join
    void Main(std::function<void()> request);
private:
    void Produce();
    Clock clock;
    std::function<void()> before_request;
    std::thread::id main_id;
    std::thread producer;
    std::mutex mutex;
    std::condition_variable changed;
    std::function<void()> frame, request;
    std::exception_ptr frame_error, request_error;
    Timing timing{};
    bool active{}, done{}, stopping{}, request_done{};
};

// Only the CPU producer has this thread-local binding. Main and the existing
// scanline/audio/compiler workers never redirect calls or become CPU owners.
bool nds_cpu_render_on_worker();
void nds_cpu_render_main(std::function<void()> request);
