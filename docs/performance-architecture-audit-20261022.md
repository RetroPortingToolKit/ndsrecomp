# NDS performance architecture and constraints audit

2026-10-22. Central Beads: `beads-k775`. Solo investigation.

This records the pre-implementation audit and owner scope decisions. Current
implementation, evidence and measurement status are in the
[task burndown](performance-burndown.md).

## Assessment

The strongest lead is the cost of the CPU execution architecture, including
its interactions with scheduling, memory timing and code identity. Compiling
ARM arithmetic to C has not removed the work surrounding each instruction.
This is an implementation limitation, not evidence that accurate DS emulation
inherently needs this much host CPU. Removing more small dispatch operations
without changing that execution contract has already produced modest results.

The owner explicitly changed this phase from optimization implementation to
a high-level audit of the architecture and restrictions, followed by discussion
before redefining the contract. No optimization has been implemented or promoted
by this investigation. The recommendations below are hypotheses to test, not
promised speedups or authorization to change hardware behavior.

## Scope and evidence discipline

`F:/Projects/ndsrecomp` is a workspace container. Its `ndsrecomp` child is the
framework Git repository. The starting checkout is `041c4df`, branch
`perf/mph-submit-hle-20261018`, with a pre-existing owner change to `AGENTS.md`.
That edit and checkout were preserved. The isolated audit worktree is
`G:/Temp/Codex/ndsrecomp/accuracy-perf-20261022/framework`, branch
`perf/accuracy-hostwork-20261022`. The initial drive census showed both F: and
G: ready and lightly loaded; G: had approximately 8.9 TB free. All new build,
temporary, Python-cache and audit output goes under that G: task directory.

There is no single branch that describes all current delivered behavior:

| Boundary inspected | Revision | Meaning |
| --- | --- | --- |
| Framework `main` | `3a57236` | Does not contain all newer preview work |
| Starting framework checkout | `041c4df` | MPH submission/math/projection experiments, compute renderer |
| MPH v0.7.7 source manifest | `f5f529b` | Native GL preview, MPH HLE work, maintained capture reference |
| MKDS v0.0.6 source manifest | `ab5e593` | Delivered compute renderer with MKDS HLE/cache work |
| Connected CPU experiment | `3ffddb1` | Experimental regions/guards, excluded from delivered preview |

This fragmentation is an audit and qualification risk. An experiment measured
against the starting checkout is not automatically representative of a released
game. Both the runner and generated-bank/compiler profile must be pinned.

Historical measurements below were read from the retained raw JSON and source,
not rerun. The desktop had active compiler/game jobs. The F:/NAS helper reached
SERVER, but SERVER was running another window's SMW benchmarks repeatedly.
No fresh performance or gameplay run was started on either machine. This avoids
presenting contaminated timings as new evidence. Existing SERVER work used
i5-4670/GTX760; it is a useful low-end canary, not a universal hardware floor.

## What the retained evidence establishes

- MKDS's delivered compute binary completed two uninstrumented 600-VBlank
  windows at 59.67 and 59.58 VBlanks/s on SERVER. The former used 11.969 process
  CPU-seconds over 10.056 wall-seconds. This is already near the nominal frame
  budget before interactive presentation/pacing overhead. It supports the
  owner's concern about lack of headroom. It is not worst-frame, whole-game,
  laptop or integrated-GPU qualification.
- A retained 10.15-second MKDS emulation-thread profile attributes self samples
  to generated code 45.06%, dispatch 12.05%, runtime cycle/helpers 10.40%, GPU3D
  8.82%, bus/memory 5.16%, scheduler/sync 4.33%, DMA/timer/IRQ helpers 2.91%,
  GPU2D 0.91%, audio 0.79%, and libc/allocation 0.08%. Generated code includes
  inlined helpers; it must not be relabeled as pure guest arithmetic. Unwinding
  failed in 97.8% of samples, so inclusive stacks are weak evidence. These are
  one workload's emulation-thread shares, not all-thread savings ceilings.
- Covered MKDS and MPH samples had zero interpreted ARM9 and ARM7 instructions.
  Earlier unpromoted MPH campaign coverage included tens of millions of ARM9
  interpreted instructions. Interpretation is therefore workload/generation
  dependent. Neither "always cold" nor "the primary universal bottleneck" is
  supported. Registration also does not prove that a bank actually executed.
- The MKDS connected-region experiment reduced ARM9 dispatches from 15,091,540
  to 7,331,842. The two retained comparisons improved throughput 3.77% and
  1.86%, while process CPU changed -3.26% and +0.53%. A large dispatch-count
  reduction did not establish a large robust CPU win. Shared target-guard reuse
  was effectively flat in another retained comparison.
- MPH native GL was already implemented and measured: one matched Gorea
  canary went from 89.70 to 98.69 VBlanks/s and from 9.109 to 7.641 CPU-seconds.
  This is a real lead, with limited workload/compatibility coverage. It is not
  a missing feature to propose again, nor a prediction for MKDS.
- The original MKDS 50.26/49.15/51.88-VBlank/s release/nativeGL/compute comparison
  enabled a callback on every guest instruction through the measurement command.
  Its release notes were subsequently corrected. The clean same-binary compute
  samples above supersede its ordinary-speed interpretation. The old renderer
  comparison cannot establish a clean nativeGL regression or improvement.

The last point is especially important: diagnostic workload was mistaken for
player workload. Turning off deep tracing alone was insufficient. Normal play
already disables deep tracing, so fixing a harness does not itself speed up
the owner's gameplay. New experiments must report actual armed hooks, sampler
state, renderer, code coverage and feature selections.

Private primary evidence locations:

- `F:/Projects/ndsrecomp/scratch/mkds-cpu-20261018/`: `clean/measurement.json`,
  `baseline-match.json`, `baseline-confirm.json`, `profile-symbols.txt`,
  `connected-match-summary.json`, `connected-confirm-summary.json`,
  `pair-summary.json`, and `release-corrected.txt`.
- `F:/Projects/ndsrecomp/scratch/mph-native-prerelease-20261018/`:
  `source-build.json`, `release-notes.txt`, and framework source at `f5f529b`.
- `F:/Projects/ndsrecomp/scratch/mph-quick-burndown-20261018/server/` and
  `F:/Projects/ndsrecomp/scratch/mph-submit-hle-20261018/server/`: earlier MPH
  raw intervals and process censuses. Background-load and timing-profile caveats
  remain attached to those comparisons.

## Architectural blockers, in priority order

### 1. The CPU ABI exposes too much state too frequently

The emitter writes the current PC, checks yield, increments instruction counts,
checks the instruction-hook gate, performs guest arithmetic through global
`g_cpu`, calculates timing and charges cycles at instruction boundaries. Many
helpers are now forcibly inline; suggesting ordinary helper inlining again
would miss existing work. Remaining opaque helpers and global state still limit
compiler optimization and require frequent state publication.

Connected regions replace some host transfers with labels. Their own design
explicitly retains per-instruction ticks and global registers. They do not yet
provide register-local block execution or combined timing. Merely increasing
their 4-page guard limit repeats an experiment whose structural limitation is
already visible. The guard-array size is an internal design choice, not hardware.

The larger candidate is an execution/IR redesign: keep registers/flags local
within a validated region; use proven event horizons; materialize exact state at
required exits; retain precise paths for MMIO, executable writes, exceptions,
debug instruction anchors and imminent events. Conditional instructions,
data-dependent timing and early exits require real handling. This is materially
more work than adding another cache, and its gain needs an early prototype.

Relevant source: ARM-core `profiles/armv5te_nds/arm_codegen.cpp` around 2170;
framework `recompiler/armv4t/runtime_arm.h` around 904; `runner/src/runtime_arm.cpp`;
connected experiment `docs/connected-cpu-regions.md`.

### 2. Accurate ordering has become coupled to a particular scheduler structure

`scheduler_run_round()` runs ARM9 first and catches ARM7 up to ARM9's actual
timestamp, with a normal 64-system-cycle cap and event snapping. Every round
also discovers deadlines, handles device catch-up, copies CPU/call-return state
when switching, and enters instrumentation. Retained MPH work performs roughly
3 million rounds for 600 VBlanks. Small per-round costs therefore matter.

The periodic LCD/RTC/SPU deadline arithmetic is recomputed even when unchanged;
exact deadline caching is a bounded candidate if profiling warrants it. It is
not evidence that deadlines can be removed. IPC, shared-memory publication,
timer overflows, DMA arbitration/completion, GX FIFO stalls/status/IRQs and
Wi-Fi timing are meaningful boundaries. An arbitrary larger quantum or naive
parallel ARM9/ARM7 execution can reorder them.

Separate the contract (when another modeled agent can observe a value/event)
from the current implementation (round count, repeated deadline calculations,
global slot copying). Exact equivalence to every internal trace of today's
model is stronger than hardware-observable equivalence and does not itself
prove hardware accuracy. Broader scheduling changes need an explicit timing
contract and differential event tests, not a blanket accuracy claim.

### 3. Graphics has distinct presentation and guest-readback contracts

Compute rasterization/readback is already overlapped, and some scenes support
direct presentation. The residual fence/map wait still sits on the emulation
thread when CPU pixels are required. Native GL exists in the MPH preview;
choosing it everywhere is not supported by the current evidence.

Native GL freezes 512 KiB of texture data plus 128 KiB of palette data at each
render boundary, and lazily runs an independent integer rasterizer when guest
display capture needs 3D pixels. Capture-heavy frames can pay both presentation
and reference rendering. This is a deliberate correctness cost, not an
accidental second renderer that can simply be deleted. Immutable/versioned
views may reduce snapshot work, but their value is unmeasured here. 640 KiB per
render boundary is unlike a full VRAM snapshot per scanline; SNES's result
does not establish the priority or payoff.

The two 2D engines also already have direct mapped VRAM views, a scanline map
cache and asynchronous jobs. VRAM writes/remaps, capture feedback and raster
register changes fence those jobs. A faster implementation must retain the
latched per-line values, both screens and overlap-bank OR semantics. Measure
fence causes and capture frequency before redesigning the pool or snapshots.

### 4. Coverage and observability require separate performance accounting

Known code, live shards and actual interpreter execution need separate counts
for both CPUs and each important workload. AOT coverage is not automatically
cheap execution, and an interpreter-heavy route may have a different answer.
Code generation also produces very large executables and archives; cache/code
locality is a concern to measure, not a diagnosed instruction-cache miss rate.

Deep per-access/register tracing is already off in interactive play. Other
observability remains on: per-instruction ordinals, dispatch counters, sampled
emulation/bus attribution, PC sampling, and the 250-Hz suspend-based host sampler.
The retained MKDS profile reports 50 microseconds suspended per sample. This is
not zero cost, but it does not explain the entire performance deficit. A lean
production profile with diagnostic arming and block-aggregated host counters is
a candidate; diagnostic counters must not accidentally become modeled hardware.

Audio and allocations were small in the sampled MKDS emulation-thread profile.
They are lower-priority leads for that workload, not globally ruled out. ARM7
audio, capture timing and IPC must survive any service specialization.

### 5. The qualification contract mixes unlike scopes

Current save/load has substantial deterministic core/video coverage, with cache
invalidation on restore. Old-state compatibility is unnecessary. Native saves
are a separate contract and must remain protected.

This tree does not implement gameplay rewind or rollback netplay. Network
capture/replay is not rollback. Live networking save/load is deliberately refused,
and the Wi-Fi serializer lacks parts of the deterministic device/AP timeline.
Making rollback a prerequisite for every performance change would first require
a separate networking/state feature project. Ordinary real-peer Wiimmfi/local
wireless behavior still needs targeted qualification when affected.

## Proposed contract to discuss

### Owner decision: Wiimmfi authenticity is the acceptance boundary

Owner clarification, 2026-10-22: the proposed execution, timing, presentation,
diagnostic and investigation-policy changes are acceptable provided Wiimmfi
gameplay remains authentic. Rewind, rollback and the separate recomp netplay
engine are out of scope for this work. This supersedes their appearance in the
original investigation checklist; they are not missing qualification gates.

These changes can preserve Wiimmfi compatibility, but compatibility is not
established merely by connecting successfully. The retained M7 evidence in
`m7-wiimmfi-acceptance.md` shows the intended path: the game's Nintendo WFC
code authenticates and matchmakes through the service, then exchanges gameplay
traffic directly with its peer. That path does not depend on a new whole-machine
lockstep/rollback engine or a common host CPU execution profile.

For this investigation, authentic online behavior means:

- Keep the original game-level network protocol, identity and native-save data
  intact, with normal guest speed and input/gameplay rules during live play.
- Preserve the CPU/IPC/device effects used by WFC: protocol-visible packet
  semantics and causal ordering,
  Wi-Fi DMA/IRQ/status behavior, relevant timer/deadline progression and buffer
  visibility. Internal executor/ABI/round structure may change.
- Permit host-only presentation differences while preserving graphics readbacks
  that feed game state. Do not infer harmlessness from the absence of artifacts.
- Treat timing-approximating HLE and scheduling changes as candidates until the
  affected gameplay and networking contracts pass focused qualification. A pair
  of identically modified clients can conceal a shared error; an unmodified
  compatible peer (hardware or a stock reference emulator) is the preferred
  interoperability check. Matchmaking, actual bidirectional gameplay, expected
  race/combat progression and normal completion/exit matter; login alone is
  insufficient. Neither generic whole-machine bit identity across peers nor
  unchanged packet arrival times through the Internet is required.
- Announce a bounded real-peer test budget before running it. Use packet/device
  evidence and affected gameplay observations together. Preserve existing
  network identity/eligibility checks; excluding the separate netplay engine
  does not remove the DS Wi-Fi/WFC transport or its correctness requirements.

Wiimmfi is the chosen online path for modes that originally support Nintendo
WFC. There is no new online compatibility claim from this scope decision, and
no live peer gameplay has been run in this audit. Offline current-build
save/load, native-save integrity, restore invalidation and portable fallbacks
remain in scope. Final integration still awaits the owner's playtest approval.

The table below records the audit's original recommendations, now accepted
subject to that Wiimmfi condition.

| Choice | Recommendation | Why |
| --- | --- | --- |
| Hardware correctness vs today's instruction-by-instruction software trace | Preserve hardware-observable state/order; allow a new executor and event contract | Removes an architectural constraint without granting arbitrary timing changes |
| General accurate core vs title-specific timing approximations | Maintain both categories explicitly; approve each approximation's scope | Existing HLE can change IRQ/timer opportunities even when screenshots match |
| Presentation identity vs guest graphics readback | Permit qualified native presentation; keep deterministic guest capture | Avoids paying for exact presentation pixels unnecessarily while protecting gameplay |
| Exploratory run limits | Authorize a small hypothesis-driven budget, A/A and matched interleaving; expand only for ambiguity | Two arbitrary runs can be insufficient; a matrix is still unnecessary |
| Validation breadth | Match checks to changed contracts; exclude rewind/rollback and separate netplay engine; qualify authentic WFC peer gameplay | Concentrates online qualification on the game's existing multiplayer path |
| Always-on diagnostics | Allow measurement of a lean release profile and on-demand expensive collection | Establishes overhead instead of assuming all observability is free |
| Platform scope | Windows/Haswell first, portable fallback maintained; other targets qualified in later milestones | Focuses delivery without allowing unsupported imports/CPU instructions |

Do not relax native-save integrity, code identity/invalidation, invalid-input
rejection, or real CPU/device communication semantics as a convenient shortcut.
Do not replace the global framework with an unqualified per-game bypass.

Set a concrete delivery target before the next implementation: e.g. nominal
speed with meaningful headroom on the i5-4670/GTX760 at native resolution, both
screens and normal audio, followed by selected adaptive/HD settings separately.
The exact headroom and supported low-end hardware remain owner decisions.
Benchmark the same workload on the reference emulator too: a general accuracy
goal should be informed by what an established implementation can deliver on
that machine, rather than by assuming that AOT must automatically be faster.

## Work completed and next boundary

- Repository/instruction/storage/fixture/history audit completed; unrelated
  changes and native saves preserved. No agents delegated.
- Baseline LLE math/submit runner built successfully in G: using the identical
  existing bank archive `ee36ba31...f89` through a private build hook. Its SHA256
  is `9680196c4f82165990dc77cc3a4a1ac5affc7560829fe67f49c465b386fe4d67`.
  This is the starting checkout, not a replacement for the newer MPH release.
- Focused `savestate_test` and `video_savestate_test` passed (2/2). These establish
  the local baseline test floor only; no new game/renderer/netplay qualification
  is claimed.
- Existing low-end profiles and rejected/neutral experiments reviewed, including
  the corrected instruction-hook measurement error. No fresh timing samples,
  performance win, owner playtest or final integration were performed.
- Owner has accepted the proposed flexibility conditional on authentic Wiimmfi
  gameplay, with rewind/rollback and the separate netplay engine excluded. An
  executor prototype, followed by focused hardware-boundary tests, a clean
  low-end sample and affected WFC interoperability qualification, is the next
  investigation direction. No new implementation is qualified by this decision.
