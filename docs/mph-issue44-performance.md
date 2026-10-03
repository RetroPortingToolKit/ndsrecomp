# MPH issue 44: coverage-first performance assessment

2026-10-03; Beads `beads-yjp.90`, game issue `beads-lqa.53`.

The campaign harvest and Omega shortcut fix are documented in the game
repository's `docs/ISSUE-44-COVERAGE.md`. Start performance analysis after
promoting the missing code: the original harvest spent 49,700,886 ARM9
instructions in Tier 3, including two completely undeclared Gorea overlays.
Those totals are discovery evidence, not a normalized performance result.

## Measurement scope

The focused check restores private AMHE0 Gorea checkpoints into the rebuilt
Release runner. It uses `--serve --no-save`, real BIOS files, compute rendering
at native width, and a Ryzen 7 9800X3D / RTX 3080 Ti host. It does not measure
the reporter's Ryzen 5600G / Arc A770, HD presentation, audio pacing, or a
normal frontend's FPS. There is no cold/warm, repeated-route or platform
matrix, and no performance-gate JSON is manufactured.

`deep_trace` is explicitly disabled after each restore. The sampling interval
is driven with `run_rounds`: `run_to_event` arms instruction-level event hooks
even when deep tracing is off. The earlier pre-promotion sample used those
hooks and included a blocking gunship dialog. It is unsuitable as a baseline
for a claimed speedup and is deliberately excluded.

Host samples use the always-on sampler. Self/leaf counts are the primary
evidence; inclusive stacks can be truncated by unwind failures. The
`emu_attrib` interval subtracts cumulative counters, then scales sampled
buckets by interval rounds / sampled rounds; exact buckets need no scaling.
Bus and dispatch timings are subdivisions of guest execution, not extra
addends to that partition. `pc_hot kind=exec` ranks dispatch entries, not
elapsed time or instruction count; it cannot alone justify an HLE candidate.

## LLE and HLE policy

The owner permits HLE to be enabled by default. A working LLE path must stay
linked and explicitly selectable, and it defines the same guest contract.
No new HLE routine is introduced by this investigation.

A future fast path must establish all of the following before changing state:

1. The ROM, instruction set and resident code bytes match the intended
   routine. Overlay address alone is insufficient. Code writes and overlay
   replacement invalidate the same assumptions as native dispatch.
2. All guest-observable register results, flags, scratch-register effects,
   return PC/mode, memory writes and permitted aliases match LLE, including
   fixed-point overflow and rounding. Host floating point is not an automatic
   substitute for integer vector/matrix code.
3. Guest cycles, bus timing and scheduler/IRQ deadlines are honored. A routine
   that cannot complete before the current deadline must decline before any
   side effect or provide an exact resumable implementation.
4. RAM-only bulk operations guard range, overlap, alignment and permissions;
   they must not silently bypass MMIO, VRAM semantics, DMA visibility or code
   invalidation. Device commands keep their native ordering and effects.
5. Explicit opt-out forces the LLE implementation. Guard failures use the same
   fallback. Contract tests compare complete relevant state and timing for
   ordinary, boundary, overlap and near-event inputs, not just output values.

The current `--hle-manifest` machinery is candidate heat instrumentation for
verified straight-line leaves. It is not a general MPH HLE implementation or
a user-facing HLE/LLE switch. `--force-tier3` selects the interpreter floor
today; a future HLE switch must also bypass every introduced HLE path.

## Other actionable findings

* **Persistent graphics reduction:** `apply_performance_governor_stage` in
  `runner/src/frontend.cpp` sets internal/sample scale to 1 at stage 2.
  `runner/src/perf_governor.cpp` can hold that stage after repeated engagement.
  This explains a mechanism matching issue 44, but the reporter's actual
  transition needs `governor_history`, `frontend_stats` and renderer logs.
  `--performance-governor off` is already an available diagnostic override.
  Consider a visible quality-reduction reason and explicit recovery/reset;
  the coverage change does not alter the governor's policy.
* **Coverage ingestion provenance:** the generic ingest's `page_target` map
  is keyed by CPU and virtual page address, so its overlay-split seed output
  can mix resident generations. This harvest instead uses the game seeder's
  byte-matched page observations and per-generation roots. A generic fix
  should key attribution by generation and test two different overlays at the
  same address before other titles rely on that output. Follow-up:
  `beads-yjp.91`.
* **Avoid repeating rejected work:** the September 13 dispatch cache/layout
  experiment was reverted after no observed improvement. Existing fetch-cycle
  inlining, memory fusion, direct linking and GPU2D workers should be measured
  as existing features, not proposed again as missing optimizations.

## Focused results

One active Omega segment advanced 4,969 VBlanks in 24,000,000 scheduler
rounds. The sampled window was 22.375 host seconds. There were 4,455
interpreted ARM9 instructions and zero ARM7 instructions: **0.897 ARM9
instructions per VBlank**, with only one interpreter self sample. This is
not a frontend FPS measurement or a before/after speedup claim.

The initial post-promotion first-form check recorded 4,284 interpreted ARM9
instructions / 388 VBlanks; the transformed-form check recorded 1,029 / 294.
Both had zero ARM7 fallback. All residual observations belonged to two
already-known page generations. Five additional proven entries were then
promoted: three into `coverage_arm9_02133000_59decd88` and two into overlay 12.
The profile below predates only those five final coverage entries; it already
shows that Tier 3 is no longer the material cost in this boss segment.
The final rebuilt coverage-only check recorded **zero ARM9/ARM7 fallback**
across 388 first-form, 294 transformed-form and 417 Omega VBlanks (1,099 total).
All 266 declared banks were verified in the final executable.

The interpreter selector was enabled during the same session for another
42 VBlanks. Gorea remained in active gameplay, Omega remained equipped, and
the counters advanced by 3,522,579 ARM9 and 1,036,330 ARM7 interpreted
instructions. This is a functioning-floor smoke check, not an exhaustive
LLE/HLE equivalence proof. No HLE was installed.

The host sampler captured 5,594 emulation-thread samples, 3.0% in wait/idle.
The table uses **all** emulation-thread samples, so the shares sum to the
same denominator. Stack unwinding stopped with `read_failed` in 95.9% of
samples; use self counts rather than inclusive-stack rankings.

| Self category | Share |
| --- | ---: |
| Generated guest bodies | 31.78% |
| GPU3D host implementation | 11.07% |
| Cycle and instruction helpers | 10.99% |
| Dispatch and lookup | 8.62% |
| Bus/memory | 6.19% |
| DMA/timer/IRQ helpers | 5.77% |
| Scheduler/synchronization | 5.31% |
| GPU driver | 2.34% |
| MMIO | 2.02% |
| GPU2D | 1.14% |
| Interpreter | 0.02% |

Other categories include OS, unclassified code, audio, network, allocation
and waiting. These percentages describe where the emulation thread was
sampled, not whole-process CPU usage or potential speedup ceilings for
overlapping changes.

The independent emulation partition estimates about 2.00 ms/VBlank in ARM9
execution, 0.81 in ARM7, 0.52 in scheduler machinery, 0.47 in ARM9 DMA, 0.35
at GPU3D frame boundaries, and 0.32 in geometry processing. **Its scaled total
is 25.17 seconds against 22.375 seconds of wall time, a 12.5% overestimate.**
Treat those sampled absolute costs as directional; the discrepancy is not
hidden by renormalizing them into a passing gate. Host self sampling is the
primary ranking evidence.

Dispatch timing independently estimates roughly 0.44 ms/VBlank across both
CPUs. ARM9 resume dispatch averages 58 ns versus about 18 ns for literal
calls/branches; ARM7 resume dispatch averages 52 ns. The interval contains
15.34 million ARM9 and 6.86 million ARM7 resume dispatches. This points to
reducing repeated dispatch/scheduler work, not merely rearranging cache data.

## Ranked next work

1. **Reduce cycle/dispatch overhead across both CPUs.** Generated code still
   performs guest timing, memory access and scheduling work; native coverage
   does not turn it into an ordinary host game engine. The cycle helpers and
   dispatch alone account for about 20% of self samples. Investigate longer
   proven execution spans, fewer returns through the resume path, and
   coalesced cycle accounting bounded by the next device/IRQ event. Preserve
   the existing instruction-accurate fallback at uncertain boundaries. Do
   not globally increase scheduler slices or remove cycle charges.
2. **Target DMA-to-GXFIFO and geometry submission overhead.** The hot guest
   ITCM routine at `0x01FF9420` programs DMA; `0x01FF9470` chunks transfers and
   polls completion. Their bytes also occur in the verified ROM image.
   Host samples include `nds_dma_run`, `bus_write_u32[_slow]`,
   `WriteToGXFIFO`, and `ExecuteCommand`. A guarded batch path could reduce
   per-word host overhead while retaining FIFO capacity/stalls, DMA timing,
   memory provenance, command order and IRQ delivery. This is a concrete
   cross-cutting candidate; it is not permission to bypass DMA completion.
3. **Pilot HLE on the measured fixed-point math family.** Verified ARM9 code
   identifies a cross product at `0x02080DE0`, a vector/matrix transform with
   translation at `0x0207FD38`, and a vector scale/add at `0x02080BD8`.
   ITCM `0x01FF8C2C` contains fixed-point matrix multiplication, with the
   observed byte span also at ROM address `0x0207FDEC`. These four bodies
   contribute 112 self samples (2.0% of the interval), plus some shared helper
   cost. That supports a bounded pilot, not a promised large speedup. Measure
   complete routine cost before expanding the work. Preserve integer
   rounding, overflow, aliasing, stack/scratch writes and guest cycles; retain
   the LLE implementations and an explicit opt-out.
4. **Investigate HD/driver cost on the reporter's actual configuration.**
   This native-width NVIDIA sample cannot rank Arc HD readback/presentation.
   Inspect governor transitions and renderer timings before changing GPU
   defaults. A recovery/reset affordance could fix the persistent-quality
   symptom independently of lowering the underlying frame cost.

The frequently sampled guest routines `0x020882E0` and `0x020882F4` modify
CPSR interrupt masks. Their high entry count does not make them harmless
math helpers: any fusion or HLE must preserve IRQ visibility and timing.

Private evidence: game `scratch/issue44/post-profile.json`,
`post-hostprof.json`, `post-host-symbols.json`, `performance-summary.json`,
the final coverage-only check, and screenshots. No profiler replay or extra
benchmark leg was used to manufacture a cleaner result.
