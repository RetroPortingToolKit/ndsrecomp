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

Qualify Windows first with focused correctness checks, measured FPS/percentage
gain, one basic visual sanity look and the owner's playtest of all three
selected games. The clarified subsampling policy permits this bounded work;
it does not require a large matrix or a lifetime gameplay block.

### Selected games and production readiness

| Game | Useful-work route | Build readiness |
|---|---|---|
| Metroid Prime Hunters | Active Omega boss work from the established checkpoint, then normal continued play for the owner. | Oct3 runner/profile/checkpoint is reusable discovery evidence. Build a coherent maintained title/framework LLE/HLE production pair for the new candidate. |
| Mario Kart DS | GCN Luigi Circuit countdown and active driving with opponents. | Sept13 Deck profile is an older fallback-heavy floor. Establish a functioning maintained Windows production artifact and route; do not label coverage repair an HLE gain. |
| Pokemon Black | Active overworld movement and a battle/menu transition. | Aug13 executable/boot log is discovery only. Establish a maintained production artifact and active route before new candidate timing or handoff. |

These games are the selected qualification pool, not ready new HLE packages.
Use one Windows production configuration per game and reuse valid evidence.
Do not present unrelated old builds as the implementation the owner will test.
No automatic renderer/platform/route matrix or checkpoint/image/audio/state
sweep is required to reach the owner handoff.

### First shared-engine boundary

Compare a larger shared scheduler/event service against coarse geometry-command
submission/processing, using actual route cost and eligible work. MPH's native
self profile exposes GPU3D 11.07%, cycle helpers 10.99%, dispatch 8.62% and
scheduler 5.31%; these categories do not form additive speedup ceilings.
Choose a clear shared caller boundary with substantial repeated cost and a
functioning operation LLE floor, then implement a materially cheaper service.
Avoid hardcoded game addresses or title routine interception.

The ordinary-RAM DMA pilot is competitive only if eligible real-game coverage
and FPS/percentage improvement support it. Its component-only ratio does not
establish priority over broader scheduling or geometry work.

### Focused correctness and gain

Declare the supported service, caller ABI, maintained LLE alternative and
material-gain goal before code. For scheduler/geometry work, identify the
caller-visible command/buffer ownership, completion/publication and affected
CPU/IPC/IRQ ordering contracts. Preserve compatible outcomes; accepted minute
visual/timing approximation does not require full internal-state identity or
matching an old screenshot.

Reuse existing focused correctness tests. Add only a concrete missing risk case
introduced by the selected boundary. Do not require general lifecycle/campaign,
save/audio/state automation. Keep already confirmed multiplayer evidence;
repeat a focused multiplayer check only if the changed shared CPU/IPC/IRQ or
netplay contract specifically affects it.

Begin with one matched LLE/HLE timing pair per selected game, one production
configuration and comparable active gameplay work. Windows uncapping is
authorized for measurement. Report FPS and percentage gain; establish that
improvement reflects useful game work rather than merely advancing the guest
clock faster. Keep diagnostic profiles separate from timing.

Add a reverse-order pair only for actual noise or ambiguity that could change
the conclusion. There is no mandatory eight-run quota or automatic repetition
matrix. Declare materiality beforehand: roughly 10% is a provisional planning
target, not a universal acceptance threshold, and the gain must outrun noise.
An inconclusive or defective candidate remains draft with a specific next step.

### Visual sanity and owner playtest

Make one representative visual glance at the new implementation to confirm it
is not garbled. No corresponding old-frame or pixel-perfect requirement applies.
Deeper visual checks are justified only by a detected or owner-reported defect.
Rely heavily on the owner's judgment of gameplay feel.

When focused checks and material gain pass and all selected production packages
are ready, launch the new HLE **Metroid Prime Hunters, Mario Kart DS and Pokemon
Black** builds for the owner. Human play uses normal pacing and isolated test
saves where appropriate. Identify the candidate and ask whether each game looks
and plays right. Launch is already authorized; do not request permission again.
Do not substitute an older unrelated executable for an unfinished candidate.

Resolve specific owner feedback. Positive feedback plus demonstrated material
gain supports Windows default-on promotion with build-time LLE opt-out, then
merge and issue closure. Report the supported title/platform scope and known
approximation. Ports follow later rather than blocking the Windows result.

## Current implementation: shared geometry poll service

The first production experiment replaces the shared `nds_gpu3d_run` service's
no-event intervals. The October 3 MPH profile measured GPU3D at 11.07% of the
native emulation thread; repeated geometry polling is a concrete opportunity,
not a promise that this percentage can be recovered. The larger command and
rendering service remains available for intervals that actually contain work.

`NDS_GPU3D_SERVICE_IMPLEMENTATION=LLE|HLE` fixes the implementation at build time;
LLE remains the default. HLE settles disabled, flush-waiting and empty intervals,
and positive command countdowns before their next deadline, directly from live
device state. Due commands, busy-pipeline completion, status/IRQ publication,
MMIO and frame/renderer barriers retain the maintained device path. Backward
timestamps and exceptional signed-countdown intervals fall back before mutation.
No persistent eligibility cache, shadow execution or runtime handover is used.
The public caller ABI and snapshot build-identity checks remain intact; the HLE
build carries the distinct `gpu3d-service-hle` identity suffix.

Dense GX diagnostic history now requires `NDS_GPU3D_TRACE=ON`. Production LLE
and HLE both use OFF: this shared production-floor cleanup is not credited as
an HLE gain. Normal SDL rendering, compute rendering and audio remain enabled.
The maintained MPH math implementation and ordinary-RAM DMA choice are equal
in both arms, so they cannot explain a pair's difference.

The new geometry defines apply only to `gpu3d.cpp`, avoiding unrelated guest-bank
recompilation. An optional `NDS_PREBUILT_BANKS_ARCHIVE` plus mandatory expected
`NDS_PREBUILT_BANKS_SHA256` can import a separately verified archive. Its caller
must establish matching guest sources, compiler and runtime ABI; the hash check
establishes artifact identity, not ABI compatibility by itself. This experiment
uses the same copied October 3 MPH archive in both arms: SHA256
`ee36ba31bda670612de6372324eea1f236450c6840acb69838fb723030fa9f89`.
All 1,424 recorded dependencies are present and predate the archive, current
owned ABI headers match, the compiler executable matches, and bank dependencies
contain neither new geometry macro. Guest bodies were not regenerated.

The predeclared useful target is at least 5% less whole-process CPU or active
frame work on MPH beyond observed noise. Normal pacing may cap reported FPS;
all-thread process CPU and presented/emulated work counters then determine
whether the service removed useful runtime cost. This candidate-specific target
does not replace the owner's material-gain judgment or establish other titles'
acceptance thresholds.

The focused real-device test passed 36 idle, disabled, flush, pre-deadline,
due-command and busy-completion cases, plus a backward-timestamp case. It checks
the affected timestamp/countdown, status, FIFO/pipeline and matrix-command
outcomes against the maintained device implementation. Production qualification
is still pending; these tests do not establish a whole-game gain or playability.

MPH qualification starts from normal boot and an isolated copied retail save,
using the existing Adventure input route through SDL. Old Omega debug states
are incompatible with these build identities and will not be rewritten or
accepted by weakening protection. Confirm the actual active scene once, then
run one matched full-runtime pair with equal useful guest work. Mario Kart DS
and Pokemon Black packages follow only if this primary experiment is promising.

The first current LLE SDL setup completed with normal audio after unmeasured
turbo boot: 11,428 guest VBlanks, 3.848 billion ARM9 instructions, no underruns,
and a clean requested exit. Setup took 124.265 seconds, including 63.843 seconds
of turbo boot; these are route-preparation figures, not benchmark results.
The copied retail save loaded **Alinos's ship Save Game interface**, not the
older route's assumed Celestial Archives field. Therefore active-field
qualification and timing remain pending. Its visible bottom-screen EXIT is a
concrete next route action; no guessed input matrix or performance claim was
made from the ship screen. Evidence: `nds-gpu3d-route-lle/route.json`,
`stderr.log` and the single `ready.png` under the experiment artifact root.

The first deliberate EXIT correction used an incorrectly read vertical
coordinate `(120,184)`. It remained in ship-interface state 12 with no players
and position `[0,0,0]` before/after forward input; it did not qualify active
gameplay. Read-only inspection of the existing 448x384 image identifies the
button's bottom-screen vertical edges at 143/144 and 179/180: the tap was below
the button. The staged interior point is `(120,160)`. SDL's native-to-window
centering/scale conversion and at least three guest-frame input hold are
consistent; this is a route-coordinate correction, not an emulator change.
Corrected field readiness and the production pair are still pending; evidence
is in `nds-gpu3d-route-corrected-lle` and `nds-ui-coordinate-check.log`.

The next interior EXIT setup completed in 109.547 seconds with normal audio
and no underruns. Its image shows the native **EXIT SHIP: ARE YOU SURE? YES/NO**
dialog: the corrected touch worked. State 12, zero players and zero position
therefore reflect a pending confirmation, not proof of ignored input or an
emulator softlock. The observed YES interior `(70,174)` plus a bounded landing
settle is staged; no active-field timing or gain has been claimed. Evidence is
in `nds-gpu3d-route-exit-interior-lle`. Qualification, companion packages and
owner playtests remain unfinished; LLE stays default and the PR stays draft.

### Active-field result and disposition (2026-10-07)

The observed EXIT `(120,160)` followed by YES `(70,174)`, landing settle and
forward input successfully reached Alinos room 27: frontend state 14, one live
player, unpaused, with actual position change. This uses a fresh copied retail
save and normal guest input, not RAM writes or rewritten snapshot identities.
The held setup process was closed before the single serial LLE/HLE pair.

Each arm ran normal rendering/audio through an approximately 180-million-ARM9-
instruction active window after boot/turbo ended. These are active-window
figures, not the combined setup/soak summary printed at shutdown:

| Metric | LLE | HLE |
|---|---:|---:|
| All-thread process CPU seconds | 13.906250 | 13.765625 |
| Wall seconds | 25.609 | 25.625 |
| Real presents / guest VBlanks | 1,533 | 1,534 |
| Presented FPS | 59.8618 | 59.8634 |
| ARM9 instructions | 180,229,281 | 180,236,257 |
| Main emulation work seconds | 8.705607 | 8.962464 |
| Present work seconds | 1.249656 | 1.300308 |
| Audio drain / pacing tail seconds | 15.643929 | 15.353348 |

Both arms ended in room 27/state 14, unpaused, with one player, health 56 and
identical raw position `[-217812,807,-11470]`. Audio started with zero queue
errors/underruns, and neither arm used synthetic presents. A single current
HLE image shows normal terrain, gun and HUD; no image-matching sweep was used.

The raw CPU reduction is **1.01%**, approximately **1.08% per guest VBlank**;
main emulation work instead increased **2.95%**. This does not establish the
predeclared 5% material improvement. Foreign PSX Ninja/compiler processes
appear in endpoint inventories; those inventories do not prove contention
throughout the active windows. GPU trace, frame hashing and host profiling were
disabled, but the unchanged native-bank dispatcher retains its sampled cost
counters in both arms. This is a matched full-runtime screening result, not
wholly instrumentation-free qualification or a general game-performance claim.

**Park this poll-service candidate in draft with LLE default.** No automatic
repeat, companion build matrix or owner HLE handoff follows a nonmaterial
primary result. A further implementation should remove broader measured
geometry/event work rather than repeat this pre-deadline leaf experiment.

Exact artifacts are under `F:/Projects/_engine-hle-bulk-20261006/`:
`nds-gpu3d-pair-{lle,hle}/route.json`, `stdout.log`, `stderr.log`, endpoint host
inventories where nonempty, `nds-gpu3d-pair-hle/ready.png`, and
`nds-gpu3d-binary-manifest.json`. The LLE executable SHA256 is
`22e252dc9e16afc4742620168e0ae17ce03ca93e9a891f92af6e95dc43626a0c`;
HLE is `1d3568557f0bd5285fbaf0d9661d0f5a324d20c6251a2f2f609263898344abdb`.
Their precommit build IDs are respectively `77f32e6-dirty` and
`77f32e6-dirty-gpu3d-service-hle`; the implementation was published as
`0ef72a8e505860777e1c7df693b063b1780ec6fb`. Binaries were not rebuilt for this
documentation update. Focused 36-case device checks remain passed; the broader
engine optimization and owner completion gate remain open.
