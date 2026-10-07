# Shared-engine HLE opportunities and main-RAM DMA pilot

Investigation: 2026-10-06, central issue `beads-qpws`. The pilot replaces a
shared DMA service rather than a title math leaf. Both ordinary framework and
title defaults remain LLE for this new family.

## Opportunity assessment

| Engine boundary | Evidence status | Opportunity and disposition |
| --- | --- | --- |
| Graphics: geometry/FIFO/GPU3D submission | **Measured** 11.07% GPU3D host self samples plus bus/device/guest overhead in historical MPH profile | A whole submit/transform or command batch could remove polling, transfer and command dispatch. Need completion, FIFO/IRQ and command-order contracts. Strong broader candidate; not eliminated by the bounded DMA pilot. |
| Graphics: GPU2D raster/composition | **Measured** 1.14% in that same native-width MPH sample; **unknown** other-title/HD share | Scanline batching/caching/native renderer remains a separate shared-engine route. Existing workers/fast paths are already present, not missing features. |
| Audio: SPU channel/mixing buffers | **Unknown** whole-title cost in available ranking | Native buffer service or block decoding could replace per-sample work. Needs queue, IRQ, pacing and guest buffer visibility checks. Not ruled out. |
| DMA / bulk memory | **Measured** DMA/timer/IRQ group 5.77% self; directional ARM9 DMA estimate 0.47 ms/VBlank; **unknown** ordinary main-RAM subset share | Selected bounded main-RAM→main-RAM service burst. Bulk copy and one provenance publication remove per-unit bus/memory/invalidation work. FIFO geometry DMA is a larger separate boundary. |
| Scheduler / polling / CPU-device ownership | **Measured** cycle helpers 10.99%, dispatch 8.62%, scheduler 5.31% self | Broader batched execution/events and explicit completion can remove repeated scheduling and polling. These are promising shared-engine HLE boundaries beyond title leaves; they need IRQ/shared-CPU/audio ordering contracts. |
| Firmware / BIOS services | **Existing** recompiled BIOS/firmware and optional direct boot; **unknown** service hotness during play | Entire IPC/firmware service replacements may remove CPU/bus traffic, once inputs, outputs, notifications and supported firmware are understood. No unsupported firmware floor is asserted. |
| Hardware math / other coprocessor services | **Known risk** draft MKDS hardware math/submission showed inconsistent/no gain and gameplay divergence | Whole-service replacement remains possible; do not promote the previous draft merely because instruction costs look expensive. Shared family profiling and contract evidence must justify it. |

Ranking source: `mph-issue44-performance.md`. Its native-width NVIDIA sample
is historical and title/platform scoped; attribution estimates were 12.5%
above elapsed time. Self samples rank cost, not additive speedup ceilings.
No HD/Arc/Steam Deck or universal benefit is claimed. Existing title vector
HLE is separate from this shared-engine experiment.

## Selection and contract

Configure `NDS_DMA_RAM_IMPLEMENTATION=LLE|HLE`, default `LLE`. The effective
choice is printed by configuration, and HLE build identity carries
`dma-ram-hle`. Separate build directories retain artifact/cache identities.
No runtime mode or player control is added.

The covered family is incrementing 16/32-bit ordinary main-RAM DMA between
non-overlapping physical spans. Source/destination alignment follows the
original per-word service. Mirror-crossing/aliasing, TCM overlays, MMIO,
fixed/decrementing streams, gamecard and GXFIFO request families retain their
existing service. No mid-operation HLE/LLE conversion is designed.

One bulk copy replaces a supported burst and publishes all written-byte
provenance plus code-write invalidation before returning. Generation values
advance once per physical page rather than once per word; their required
contract is rejecting stale code, not reproducing a diagnostic counter.
Exact main/main unit cost is inexpensive, so the pilot retains it: 18/16 CPU
cycles per word/halfword, ARM9 scaled to the system clock. It retains the
original one-final-unit target overshoot, addresses, remaining count, wrapped
burst index and completion state. The existing completion/IRQ/repeat code
after the loop is unchanged. The scheduler already observes the DMA burst
after `nds_dma_run` returns, so there is no new inter-word CPU/device observer.

## Checks and disposition

Both selected Release component builds pass `dma_ram_batch_test` and the
existing `mem_timing_test`; production `io.cpp` compiles in both selections.
The new fixture uses the real bus and runtime/provenance machinery with
device stubs, comparing the shared burst implementation against the retained
per-unit main-RAM service. It covers both CPUs/widths, target clipping/overshoot,
unaligned inputs, uint16 burst-index wrap, outputs/continuations, physical
alias/mirror/MMIO refusal and a TCM window crossing the interior of a span.
It does not execute firmware, a game or the surrounding full device scheduler.

`dma_ram_batch_test --probe` measures 2,000,000 ordinary RAM service bursts
in the selected build: 1,500,000 of 64 bytes, 437,500 of 1 KiB and 62,500
of 64 KiB. It prints elapsed time and an exact 64-KiB destination digest
after checking every output byte outside timing. This
isolates the selected service, not FPS or aggregate DMA benefit. HLE currently
has no new gameplay qualification. No new NDS gameplay was run during the
initial DMA pilot; it did not manufacture performance-gate files. The owner
subsequently clarified that the former gameplay cap was intended to prevent
excessive matrices, and authorized bounded representative subsampling (see
below). Previously valid
title-math gameplay evidence does not prove this new DMA path. Keep the pilot
draft/default-off until materially faster service and the required gameplay
completion/order/progression checks justify platform-scoped promotion.

HLE deliberately omits per-unit bus-device diagnostic ring/watch traces for
this RAM service; those diagnostic logs are not equivalent across selections.
Functional RAM writes, executable-page invalidation and write provenance are
still published through `note_ram_write`. Use the LLE build when per-access
diagnostic reconstruction is required. This choice does not introduce a live
backend switch.

The probe reports each size separately before its aggregate. These synthetic
size counts are a coverage corpus, not measured game transfer weights; the
aggregate bytes are dominated by 64-KiB copies and cannot establish a typical
title benefit. Balanced LLE/HLE run order is required for comparisons.

## Bounded Windows component screen (2026-10-06)

Native MinGW GCC 15.2 Release, same compiler options and source revision,
serial order LLE/HLE/HLE/LLE. Parent held team builds/timing for this window;
foreign machine activity was not controlled. No warmup, game, renderer or
firmware workload is implied. Per-size times below are seconds; HLE size
intervals and HLE aggregate intervals (about 87 ms) are below 100 ms and
therefore indicative. Two balanced pairs are a screening result, not a
confidence interval or a gate-quality performance result.

| Run | Build | 64 B | 1 KiB | 64 KiB | Aggregate |
|---|---|---:|---:|---:|---:|
| 0 | LLE | 0.327991 | 1.340993 | 13.545827 | 15.214875 |
| 1 | HLE | 0.013354 | 0.007675 | 0.066933 | 0.088016 |
| 2 | HLE | 0.012951 | 0.007482 | 0.066245 | 0.086717 |
| 3 | LLE | 0.435652 | 1.455983 | 12.500583 | 14.392273 |

Median aggregate LLE 14.803574 s, HLE 0.087366 s (169.44x service ratio).
Every run returned zero failures and digest `5932369013024162691` after
checking all 65,536 destination bytes. The shared memory fixture's generated
pattern is public synthetic data. Transfer weights are deliberately synthetic;
this large service ratio does not imply a whole-title speedup or authorize
default-on promotion. Keep the pilot experimental/default LLE.

Screen executable SHA-256 identities (local Release artifacts):

- LLE: `16b4acf4434fc891829314109bfd361c604554509b0d9e94db8b8fca9a72eb46`
- HLE: `1fc9793c9ff5bcbc21044420e78cdfb9a4b1c90cdd9aa5069d39959cd523d339`

Raw stdout/stderr was retained in the workspace artifact directory as
`nds-probe-0-LLE.log`, `nds-probe-1-HLE.log`,
`nds-probe-2-HLE.log` and `nds-probe-3-LLE.log`.
The source test emits all rows and verdicts needed to reproduce the screen.

## Representative workload discovery (2026-10-06)

The owner clarified the NDS gameplay rule to permit useful bounded subsampling
rather than excessive roughly 90-leg matrices; the root AGENTS instructions
have been updated accordingly. The pool below is not an automatic matrix.
The shared six-system pass allows at most six fresh captures total, one
configuration per selected workload. Existing NDS evidence is reusable, so no
new NDS run is required merely to fill a quota. No new NDS gameplay occurred
in this pilot or this discovery inventory.

| Workload | Exact reusable evidence, age/build/config | Shared-engine costs and uncertainty |
|---|---|---|
| MPH, active Omega boss | `metroidprimehuntersrecomp/scratch/issue44/post-profile.json`, `post-hostprof.json`, `omega-active.state`; Oct3 Release runner `ndsrecomp/runner/build-mph-release-0610/nds_runner.exe`, real BIOS, native-width compute, Ryzen 9800X3D/RTX3080Ti. `runner.json` records exact arguments/coverage manifest. | GPU3D 11.07%, cycle helpers 10.99%, dispatch 8.62%, bus 6.19%, DMA/timer/IRQ 5.77%, scheduler 5.31% of emulation-thread self samples. Eligible RAM DMA and worker/audio shares remain unknown. |
| MKDS, GCN Luigi Circuit | `mariokartdsrecomp/generated/captures/steamdeck-20260913/1789342917316118042-gcn-luigi-driving-profile/stack-summary.json`; Sept13 Deck build title `544fb7046130e591fbda79831525786f322a7cd1`, framework `d117ecfe295b199a0d20aa575700b5401f0cb091`, SDL3/compute, matching ELF build ID `2975b4c8c31f42c3a95de4c980111b8667b74097`. | Tier3 inclusive 47.06%, lookup self 9.76%, GPU3D inclusive 2.99%. Inclusive categories overlap. Copied-code fallback dominates this older floor; this ranking cannot be applied to current native MPH. |
| Pokemon Black, RPG diversity | `pokemonblackwhiterecomp/probe-runner.stderr.log`; `build/PokemonBlack.exe` exists from Aug13. Real BIOS/firmware, software renderer, networking off. | Boot evidence only; no active-game host-cost profile identified. Candidate for a single diversity capture after production artifact/route readiness, not another MPH/MKDS configuration leg. |

The MPH profile is primary reusable native-floor evidence: 22.375 seconds,
5,594 emulation-thread samples, near-zero interpreter work. Unwinding failed
in 95.9% of samples, so self counts take priority over inclusive stacks. Its
independent emulation partition overestimates wall time by 12.5%; sampled
absolute costs are directional. Source discussion and limitations are in
`ndsrecomp/docs/mph-issue44-performance.md` (Oct3). This is one active boss
segment on one host, not a universal engine ranking.

A next single short profile that could change priority would reuse the exact
MPH production runner and `omega-active.state`, disable `deep_trace`, advance
one bounded `run_rounds` interval, and dump the always-on `hostprof` together
with before/after `emu_attrib`, dispatch and coverage counters. It requires no
external sampler build. Its purpose would be to reassess GPU3D/scheduler versus
DMA cost on the maintained native floor; it would not prove a whole-title gain.
An active Pokemon route offers greater workload diversity if that established
MPH evidence already answers the question. Preserve the DMA pilot's experimental
status/default LLE until its actual eligible service share, benefit and gameplay
completion/order/progression gates justify promotion.
