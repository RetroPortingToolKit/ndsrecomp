# MKDS CPU/render overlap profile

The underlying b0cf mechanism passed the bounded MKDS OriginalCompute4x
runtime/current-NSS gate. This branch's final title-admission guard is a source
selection change, not another measured implementation. `NDS_CPU_RENDER_HANDOFF=1`
elects the worker only in an `NDS_MKDS_PERFORMANCE` build running verified ROM
`691e00d9a5dd80b04f80cc7559503e8b06848785`, with OriginalCompute selected.
Missing or zero keeps the default serial loop. Unqualified titles, including
MPH, remain serial even if the generic environment toggle is set. The startup
notice explicitly states the one completed-frame presentation/PCM latency.
Resident/binned/primitive providers are not admitted. Interpolation, performance-governor modes,
frontend input self-tests and guest-job sampling retain the serial path. The
normal SDL/non-accelerated fallback remains available on every host.

Main owns all SDL windows, input, UI, audio transport and GL resources. The
producer owns one scheduler-frame transaction and removes its completed PCM
from the SPU output ring. Producer SDL calls are the thread-safe performance
clock and one current-thread HIGH priority initialization using the existing
SDL2/SDL3 helper, with the same warning-on-failure policy as serial. There is no concurrent ARM9/ARM7 execution and no extra
guest frame or scheduler yield introduced inside a transaction.

Native VCount144/VCount215, actual frame-start work, power changes and readback
requests synchronously return to main. The producer waits without mutating
the machine while main borrows it. Existing NDS shim IRQ/DMA/FIFO callbacks
run inside that borrow, with the same stopped CPU context; they do not dispatch
another CPU. Runtime registers/native providers use the existing shared context,
not a second TLS CPU context. Main restores its GL context before every borrow.
The native renderer and lazy `CaptureReference` are retained directly: guest
capture/readback joins the original current-job operation at its original guest
observation point. No presentation pixels replace guest pixels.

There are exactly two reusable owned presentation slots and one producer slot.
Each publication copies native/adaptive LCD pixels, direct OBJ/row metadata,
HD survivors and their latched blend state, and a bounded owned PCM block.
These images have native/adaptive width (at most448), independent of 3D scale.
The packet also latches native/enhanced texture IDs and scale. GL commands remain
main-only and cannot replace those textures until main finishes presenting the
packet and starts pumping the producer's next native request. No live VRAM,
compositor surface or SPU ring is read by main during concurrent production.

The first completed packet primes the pipeline without being presented twice.
Then main samples controls at a joined guest boundary, starts CPU frame N and
presents completed frame N-1. After both windows finish presenting, main joins
the producer, services its pending native work, then performs audio transport,
diagnostics and the next control/debug boundary. This bounds presentation lag
to one completed native frame. Native output was already completed before its
presentation; a guest capture still observes its own current job. Input remains
sampled once per CPU frame. There is no unbounded frame queue or silent skip.

A current-build load invalidates the delayed packet via the existing SPU epoch
and digest epoch; scale reconstruction invalidates it by the latched scale.
The new state primes a fresh packet. Load, save capture, debug commands, resize,
control changes and native persistence execute with the producer joined.
Shutdown drains outstanding main requests before joining the OS thread or
destroying SDL/GL resources. Packet storage is declared before the handoff so
exception unwinding also drains before destroying captured output vectors.
Main resolves quit before publishing another producer frame.
Producer/native exceptions return to the main
failure path. The separately qualified atomic log-budget fix is included.

Existing frontend CSV and RPC counters record actual producer/presented job IDs
and presented guest timestamps. `cycles_emu`/`cycles_present` delimit the CPU
worker in handoff mode; loop/drain/end cycles remain main-thread counters.
Emulation and presentation wall intervals overlap and are not additive. Process
CPU is the primary whole-process cost. The overlap counter is the intersection
of presentation with the producer prefix before its first blocking native
request, accompanied by actual worker prefix cycles and main-request counts.
It is not continuous CPU utilization or GPU in-flight time. A zero-overlap
actual run cannot qualify this as successful overlap.

The focused `cpu_render_handoff_test` exercises real main/producer ownership,
bounded publication, progress while main withholds its native pump, immutable
prior output, native/CPU errors, rearm and shutdown draining. This fixture passed
one direct GCC15.2 CPU run (zero warnings), with logs retained at
`C:/Temp/Codex/ndsrecomp-area20-handoff/component`. Its tested class bytes are
unchanged by the frontend lifecycle/priority repairs and the clean native-accessor port.
The clean port uses the original renderer's native/high-resolution texture ID
getters and latched scale; it imports no experimental provider or enhanced-surface
lease interface. It is stacked on the accepted native-access foundation and
reviewed PR30-36 integration at01ba3b2. Its frontend/GPU3D production TUs compile
for MKDS and MPH, and its ComputeHost TU compiles for MKDS, with zero warnings
using the matching GCC15.2 target definitions and accepted-base headers. This
establishes compilation, not a linked clean-port runtime result.

The measured b0cf mechanism used one same-binary OriginalCompute4x/AA8 MKDS
pair:180VBlank/180CSV per mode, wall1.587805->1.3767951s and process
CPU2.28125->2.078125s. Produced guest timestamps match exactly; presented
timestamps trail by exactly one completed frame. Positive actual overlap and
current-build Saving->Saved->Loaded continuation pass. Unequal host load and
GPU clocks limit attribution of the observed13.29% wall/8.90% CPU reductions.
The analogous MPH profile is unselected: observed wall improved10.89% while
process CPU rose12.86%. MPH and generic/default execution stay serial.

Raw results and existing focused checks are retained under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/gpu-high-resolution/mkds-primitive-runtime-prepared/runs/mkds-cpu-render-handoff/measurements`,
`C:/Temp/Codex/ndsrecomp-area20-handoff/mph/pair/measurements`, and
`C:/Temp/Codex/ndsrecomp-area20-handoff/native-port-compile`.
Final combined linking/runtime integration and the owner's practical check
with normal audio remain before merge/delivery. Preserve the original loading,
save-hitch/audio and native-rendering correctness gaps in Beads72/65/112.
