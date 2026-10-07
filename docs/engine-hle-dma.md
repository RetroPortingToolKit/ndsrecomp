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

## Production candidate milestones and owner handoff

Complete one winning shared-engine candidate through actual-game comparison,
implementation, material gain and the owner's final playable feel check. The
three-title pool does not authorize an automatic matrix or roughly 90 legs.
Use one production configuration per selected title, reuse valid profiles, and
spend build/gameplay budget on the winning candidate's unanswered questions.

| Title and concrete useful-work route | Production floor gap / next milestone | Candidate boundary and observable checks |
|---|---|---|
| Metroid Prime Hunters (primary): restore `omega-active.state`, advance one bounded active Omega boss guest-frame/event window with confirmed status/progression. | Oct3 Release production runner/profile/checkpoint are ready discovery evidence. Build a coherent maintained title/framework LLE/HLE pair for candidate comparison; carry exact renderer, BIOS, coverage and checkpoint identity. | Scheduler/event ownership or geometry submission: guest progress, command completion/order, GPU frame publication, pending IRQs and ARM9/ARM7 handoff, boss movement/combat, audio/input. |
| Mario Kart DS (companion): GCN Luigi Circuit countdown then active driving with opponents for a bounded frame window. | Sept13 Deck profile has copied-code fallback dominance and a different host/pin. Establish functioning maintained native/coverage floor and matching route identity before using it to rank new shared service work; do not treat a coverage fix as an HLE speedup. | Event/geometry completion during racing, opponents/countdown, motion/steering, visual publication, audio and progression. Existing confirmed multiplayer evidence remains valid unless the chosen shared contract affects it. |
| Pokemon Black (companion diversity): overworld walking plus one battle/menu transition, each with observed guest progress. | Aug13 production executable/boot log exists, but no active checkpoint/profile qualified. Establish one production pin/configuration and isolated active route before its single companion comparison. | Distinguish scheduling/event cost from 3D submission in a different workload; verify transition/menu completion, input/audio, save/load and progression. |

First implementation theories are a **larger shared scheduling/event service**
and **coarse geometry-command submission/processing**, compared using route
cost and eligible coverage. MPH's native self profile exposes GPU3D 11.07%,
cycle helpers 10.99%, dispatch 8.62%, scheduler 5.31% and DMA/timer/IRQ 5.77%.
These overlapping engine categories are not additive gain ceilings. Choose a
clear shared caller boundary with a maintained operation LLE floor and a
measurable repeated cost whose replacement can remove meaningful total work.
Avoid hardcoded game/routine intercepts. The ordinary-RAM DMA pilot becomes the
first winner only if measured real-route eligibility and total cost justify it;
component-only ratios do not establish that priority.

Before implementation, declare the candidate's supported operation family,
material useful-work goal and permitted tiny visual/timing approximations.
Roughly 10% primary-route total-work reduction is a provisional planning target,
not a universal acceptance threshold; gain must exceed noise. Separate diagnostic
profiles from uninstrumented timing. Compare equal guest-frame/event work, with
all-thread CPU and wall/framework/pacing-tail observations and eligible service
coverage; a faster guest clock alone is not success. Primary candidate ABBA gives
two balanced pairs; each ready companion gets one A/B pair, without automatic
repeats, route expansion or renderer/platform matrices.

Define observable caller contracts before replacement: command/buffer ownership,
geometry completion and frame publication, scheduler pending events, IRQ and
ARM9/ARM7/IPC ordering, visible memory/provenance and completion results. Assert
exact outputs only where exactness is claimed; minute accepted approximation
need not preserve all intermediate state. Automated production checks cover
boot, active play and progression, pause/resume, reset, isolated save/load and
persistence, audio and input at affected boundaries in both build selections.
Keep already confirmed multiplayer coverage; rerun only a focused multiplayer
check when the winning shared CPU/IPC/IRQ/netplay contract is specifically
affected, not as a blanket lifetime campaign/multiplayer rule.

Final playable handoff: a production **Metroid Prime Hunters** HLE package from
coherent winning title/framework pins, with its LLE build alternative, isolated
Omega checkpoint and instructions for active boss play followed by normal
continued play. Automated contracts and material same-work gain must pass before
the owner receives it for the final feel/progression playtest. Report candidate
scope, approximations, companion outcomes and any multiplayer contract touched.
Only after owner feedback passes should it merge, become default on the proven
platform/workload scope with build-time LLE opt-out, and close. Until then keep
the pilot draft/default LLE.

## Measurement, decision and delivery protocol

Owner completion rule: establish a material game-workload gain and automated
compatibility, then deliver the final playable build for the owner's feel check.
After that check passes, integrate the prepared default change and close the
scoped work. Exhaustive game coverage and completed campaigns are not additional
completion requirements.

1. **Pin the workload and floor.** Use the three games and concrete routes above.
   Build LLE and HLE from the same title/framework revisions, compiler/options,
   ROM/firmware identities, presentation/audio settings and initial game state;
   only the selected implementation differs. Keep the replaced LLE service
   runnable. An old executable is discovery evidence, not a mismatched control.
   Use native game saves or replayed inputs when private savestates cannot cross
   builds. First resolve the named route/build gaps; do not perfect unrelated
   hardware before replacing a functioning operation.
   Verify that companion routes actually exercise the replacement; an unaffected
   title is a regression control, not evidence for that HLE service. If the
   chosen service changes, replace an unsuitable companion in the three-title
   set instead of accumulating extra games or claiming unexercised coverage.
2. **Attribute only what is missing.** Reuse suitable profiles and collect at
   most one new active-workload attribution capture per selected game in this
   implementation round. Identify the intended service's eligible dynamic work.
   Include worker threads and external modules or report them unresolved; a
   main-thread symbol histogram cannot supply a whole-process cost percentage.
   Capture diagnostics separately from performance. End discovery when there
   is enough evidence to select a useful service, not when every subsystem has
   a profile. The earlier six-launch discovery cap applied to that completed
   pass, not to the whole implementation/qualification program.
3. **Choose one replacement.** Record its caller ABI, inputs, outputs, observable
   side effects, supported operation scope, permitted tiny differences, expected
   cost removed, and candidate-specific useful gain before coding. Implement a
   shared service with build-time LLE/HLE selection and explicit build identity.
   Do not stack several speculative replacements into the same comparison.
4. **Measure equivalent active play.** Delimit a fixed gameplay window by guest
   frames and meaningful game events, excluding boot, warmup and teardown.
   Choose enough active work to dominate measurement granularity once, then keep
   it fixed. Report total process CPU milliseconds per guest frame (all threads),
   critical-path frame work, median/p95 frame time and missed presentation/audio
   deadlines where available. Record peak memory and code size, since constrained
   targets matter. Preserve normal renderer and audio production; a benchmark
   that omits presentation/audio is a core-only diagnostic, not end-to-end proof.
   Normal capped play can show reduced CPU/frame even when FPS stays unchanged.
   Uncapped throughput is optional corroboration only when it performs equivalent
   rendering/audio work. Measure GPU completion/queue cost when work moves there;
   a shorter submission call alone is not a win. Check actual movement/progress
   and audio duration so changed guest timing cannot inflate the result.
5. **Use a fixed comparison budget.** The primary game gets LLE/HLE/HLE/LLE:
   two order-balanced pairs, four measured executions. Each of the two companion
   games gets one LLE/HLE pair, two executions each. That is eight measured runs
   per candidate on one declared host/configuration, not a Cartesian matrix.
   Reuse their progression telemetry and final outputs; take expensive milestone
   captures outside timing, and use isolated LLE/HLE fixtures for detailed
   contracts. Do not automatically add separate full campaigns or trace runs.
   Keep team builds/profiling out of the timed window, record host load/power/
   thermal conditions, and preserve every result. A noisy or contradictory result
   stops that screen; fix an identified condition before a bounded replacement
   measurement. Never repeat until a passing subset appears.
6. **Decide from useful gain and compatibility.** Report both paired percentage
   and absolute savings, with the observed pair spread. About 10% lower whole
   active-workload CPU time is a planning aim, not a universal acceptance rule.
   A candidate may instead solve a declared frame-budget or stutter problem.
   Both primary pairs must show a clear consistent useful improvement beyond
   observed noise; two pairs are not a formal confidence interval. Companion
   single pairs screen for large regressions, not proof of zero performance
   change. Explain any apparent regression before broadening defaults. Exact
   promises require exact outputs; permitted approximations use a declared
   practical image/audio/result comparison. Check input, audio, progression,
   affected completion/IRQ consumers, transitions and relevant pause/reset/save
   behavior. No crash, softlock, stale buffer, lost completion or save corruption
   passes. A huge isolated kernel ratio cannot substitute for this decision.
7. **Hand off the actual finished candidate.** Provide the named primary game as
   a ready-to-launch normal-paced HLE package, an LLE comparison build, isolated
   save/checkpoint setup, launch instructions and checksums/build identity. Include
   a short before/after report, companion results and any tiny known differences.
   Prepare the intended default-selection/integration change in the draft PR so
   the owner tests the package intended to ship. Ask the owner to play normally
   and assess response, motion/collision, camera/scrolling, stereo where relevant,
   audio rhythm and continued progression. There is no prescribed full-campaign
   completion or multi-game human test matrix. Owner rejection reopens the
   affected behavior; fix and recheck that change before another handoff.
8. **Finish the scoped delivery.** After owner acceptance, integrate the reviewed
   candidate, make HLE the default for the supported titles/platform/service,
   retain a documented build-time LLE opt-out, and record the measured and manual
   evidence before closing the issue. Do not add unrelated qualification gates
   after the agreed playtest. If the replacement cannot deliver material gain,
   preserve its branch and draft PR with results, explain why, and choose a new
   boundary deliberately; an unsuccessful experiment is not a completed system.

Initial measurements can use Windows x64 already available here. Choose the
first constrained target with the owner, then carry only the winning candidate
and the relevant route to that target. Measure there before claiming mobile or
original-Xbox savings; desktop results do not establish a port's performance.
A target-specific build/default is qualified separately rather than multiplying
all hosts into the discovery matrix.
