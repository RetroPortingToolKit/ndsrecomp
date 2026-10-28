# Native loading hitches

Tracking: `beads-d8um`, MKDS `beads-04cb`, campaign `beads-2wy8.1`.

## Revised implementation direction

Owner feedback on 2026-10-27 prioritizes complete subsystem work over another
sequence of local optimizations. The immediate goal is to reduce load-triggered
long frames by removing avoidable work from the load-to-execution pipeline.
The allocator candidate below is a retained, unqualified partial improvement;
its release qualification is no longer the next substantive performance task.

The first implementation slice follows loaded code/data through RAM publication,
code identity/provenance, native preparation and first execution. Determine
which code/services and block boundaries create the observed translation burst,
then choose a coherent change: reuse qualified prepared code, prepare a validated
loaded image before its first critical use, or replace a costly complete loading
service and consolidate its bookkeeping. These are alternatives to evaluate,
not established fixes or authorization to skip executable-write checks.

Reuse the architecture audit, native-service burndown and current traces first.
The 44,000-block census establishes compilation volume; it does not identify
the service/PC distribution or prove that all those blocks must exist. Existing
native execution provides material gains over the older generated-C path, so
reuse must be assessed against both loading and steady-play costs. Larger block
limits, global-cache growth, GL error callbacks and prior rejected helper probes
are not new architectural solutions merely because they can be reconfigured.

Track graphics ownership/submission/presentation as a second complete path when
its measured cost warrants work. The separately observed driver wait is not a
compiler improvement, and cannot be counted as fixed by the allocator patch.

Judge the resulting package by loading-frame stall counts/duration, critical
thread work, steady-play cost and relevant startup/memory costs. Keep matched
per-game evidence and require neutral or positive results for every affected
game. Focused checks cover observable memory/device/event behavior, current-build
restore, native saves and portability. Retain the faithful build; an HLE service
need not keep its replaced reference machinery executing alongside it.

If another profile is necessary to select the implementation, the initial
budget is one bounded loading capture, not a workload matrix. Qualification
after implementation uses the existing bounded MKDS comparison and one MPH
shared-consumer check, expanded only for a concrete failure or ambiguity.
Player-speed loss is deferred at the owner's request, not marked resolved.

A bounded Pinball 150cc load census compiled 40,265 ARM9 blocks and 3,420 ARM7
blocks in 180 VBlanks, with no repeated block translations or dropped census
entries. Only the expected restore-start invalidations occurred. A later 48 ms
step compiled 5,849 new ARM9 blocks. This identifies first-use native compilation
in this workload; a larger code cache is not justified by these samples.

The SDK's x64 register allocator built temporary singleton vectors and copied
candidate lists to heap storage for each register selection. The patch uses
borrowed spans and a 32-entry local scratch array, preserving both partition
operations and their order. Unusually long lists retain a heap fallback.
Register choices, IR validation, instruction boundaries, simulated cycle timing,
backend selection and CPU capability checks remain unchanged.

Apply both patches to the pinned Dynarmic a41c380 SDK before configuring:

```powershell
git -C <isolated-sdk> apply <framework>/runner/cmake/dynarmic-readonly-pages.patch
git -C <isolated-sdk> apply <framework>/runner/cmake/dynarmic-cold-register-allocation.patch
```

The existing optional Windows/x64 SDK gate remains. Other hosts/builds retain
portable execution. Native build identity includes both patch digests; this
does not establish compatibility with older builds or relax netplay checks.

The differential check compiles the actual candidate selector against the
pinned original algorithm: 39,681 selections/failures match, normal scratch
allocation falls from one heap allocation to zero, and oversized fallback is
retained. It also covers reversed/permuted candidate orders, which matter
because a first-match search would change the original partition's choice.
ARM9 and ARM7 native probes pass.

Two matched 600-VBlank MKDS diagnostic pairs used compute 1x, adaptive 448,
VECTOR HLE, native ARM9/ARM7, no SSAA, governor/sampler off, dummy audio and
identical inputs. The 180-VBlank loading window excludes the first restored
frame, whose cold-cache cost is a snapshot artifact.

| Order | Loading CPU before/after | Largest loading step before/after | Steps above 33 ms |
| --- | --- | --- | --- |
| Before / after | 1.703125 / 1.500000 s | 42.55 / 39.50 ms | 2 / 1 |
| After / before | 1.843750 / 1.593750 s | 54.31 / 42.08 ms | 2 / 1 |

Start/end complete 4 MiB main RAM and race observations match in both pairs;
each compiles 49,352 blocks. Loading-window CPU reductions are 11.9% and 13.6%
in these local samples. Whole 600-VBlank CPU varies from 5.000 to 4.703125 s and
4.609375 to 4.593750 s; later racing windows are mixed. No general gameplay FPS
win or elimination of all first-use compilation is claimed.

These are diagnostic timings: a subsequent MPH check's quiet-host gate found
other running games and CPU/GPU activity, so qualification remains pending.
No MPH timed sample was collected in that rejected attempt. Preserve that
failure and do not manufacture a passing performance gate. The MPH control
reconstructs the old heap allocations with a common span ABI, identical engine
objects and all other SDK objects, rather than comparing a delivered release.
Both MPH variants have prepared their own fresh states. The candidate remains
on the local `fix/loading-hitches-20261027` branch; it has not been promoted.

A separate 229 ms presentation sample waited inside the NVIDIA driver. A callback
probe removed ordinary error-query overhead but retained the long loading
stalls. It is excluded, consistent with the earlier boundary investigation.
The qualified GL error checks/order are retained. Debug contexts and error
injection are not part of this fix.

`NDS_NATIVE_COMPILE_CENSUS=1` enables a bounded, default-off per-CPU census.
The JSON exposes compiled/recompiled blocks, dropped entries and ARM7
invalidations once the lazy native instance exists. Assert actual native entry
counts before interpreting it. A headless deep-trace-on harness attempt that
bypassed native execution is explicitly invalid, not zero-compilation evidence.

Raw source/binary identities, patches, samples, full-RAM comparisons and captures
are retained under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches`.
No complete campaign or multiplayer run was repeated. Native saves/settings and
the desktop are preserved. Delivery stays at MKDS 0.0.10 until the remaining
qualification and candidate packaging are complete.

## Integrated resident-code preparation candidate

The owner resumed the subsystem burndown and requested coupling related work.
The candidate combines item 01 with the native-code lifecycle slice of 07,
cache/provenance ownership in 09, existing pacing slack in 20 and the retained
allocator work in 28. Those broader rows remain open; one loading package does
not complete all their remaining work.

The restored-state samples above have a wider cold-cache effect than the first
frame. Excluding that frame does not make the rest a natural-load measurement.
A fresh menu-to-course census, with no state load and the JIT cache retained,
found 11,550 new ARM9 blocks and 18 ARM7 blocks over 180 VBlanks, no repeated
compilation, and no ARM9 invalidation during the transition. The observed ARM9
entries are resident main code/ITCM, not newly loaded overlay code.

For exact AMCE0, fingerprinted page/PC hints now prepare those blocks during
existing audio pacing waits. Preparation never runs guest instructions,
advances guest time, or changes guest CPU/data state. It uses ordinary live-code
provenance and physical-page watches. The SDK compile-only API refuses an
executing core and refuses speculative cache eviction. Cache/range/capacity
invalidation epochs restart preparation; completed preparation has a constant
fast path. Page identity mismatches retain ordinary demand compilation.

The frontend grants a soft 1 ms preparation budget per existing audio wait.
There is no extra guest delay when no slack is available. Faithful execution,
unknown ROMs, unsupported hosts and no-audio/no-slack paths retain demand
execution. `NDS_NATIVE_PREPARE=0` is a developer mechanism control. The game UI
does not expose a new switch.

All native SDK consumers require both the existing read-only-page/allocator
patches and `runner/cmake/dynarmic-prepared-blocks.patch`. The latter supplies
the common tested API and cache epoch, so its hash is in both title identities;
only MKDS includes and identifies the PC seed. ARM64 has an explicit unsupported
preparation result while preserving ordinary SDK execution; the engine's
portable no-SDK adapter remains functional.

`tools/generate_mkds_preparation.py` regenerates the checked-in hints from the
private census JSON and matching full Main-RAM/ITCM snapshots. Its output has
146 page hashes and 11,550 locations, no guest instruction payload. It rejects
unsupported descriptors, alignment and resident-code ranges. Reproduction
matches the retained seed content exactly; output line endings are fixed LF.

Prior same-binary mechanism evidence (`preparation-comparison.json`) reports
11,550 to zero ARM9 demand compilations and 79.48 to 26.07 ms maximum step, with
matching final Main-RAM/ITCM/ARM7 WRAM and byte-identical serialized guest state
before/after preparation. This is a diagnostic comparison without a quiet-host
census, not final-source/frontend performance qualification. A prior hidden,
muted real-GPU frontend check prepared all hints by frame 36, before the
identified demand burst at frame 136, and showed plausible track/HUD/map output.
That checkpoint-restored check is not a natural frontend loading A/B result.

## Natural frontend follow-up (2026-10-28)

The combined `928da54` candidate adds physical write publication and width-correct
direct narrow reads to resident preparation and the SDK allocation change.
Both rebuilt applications and focused checks pass; exact identities are in
`loading-hitches/coupled-memory-evidence.json` under the retained task root.

The first new cold-checkpoint frontend pair is diagnostic only: Docker load
differs by 0.8278 background cores. Its largest frames are 235.9704/188.8338 ms,
including 180.4614/130.2060 ms before emulation while loading the checkpoint;
presentation takes only 2.1211/2.1461 ms in those frames. They do not establish a
natural loading hitch or a presentation improvement. Preserve the separate
earlier driver-wait evidence rather than attributing these maxima to that lead.

The live harness now traverses normal menus and retains the native cache. Its
first setup incorrectly copied a cached configuration block across the transition
and the baseline failed. The corrected setup writes only the three intended
fields before starting; both baseline and candidate now complete it. No state
load or identity override is involved. Both runs use course 33/150cc, compute
4x/AA8 and muted real audio pacing. Images look plausible.

Under heavy **unequal** ambient CPU load, the 180-frame construction windows
observe maximum emulation frames of 53.5735 ms (baseline) and 16.7671 ms
(candidate), and whole frames of 57.6278 and 24.3065 ms. Two baseline frames
exceed 33 ms; none do in the candidate. These are promising diagnostic results,
not a qualified performance pair: background work is 8.1434 versus 7.3669 cores,
and both records explicitly retain `performance_eligible:false`.

The candidate automatically finishes all 11,550 preparation requests before
course construction, with zero page mismatches and no native-cache invalidation
during the interval. Preparation spends 95.8604 ms across existing menu pacing
slack; its longest call is 2.7263 ms under this load. No manual prepare request
is made in this live path. This validates the intended mechanism in the actual
frontend, while per-game matched timing, MPH qualification, owner review and
delivery remain open.

Raw receipts: `natural-frontend-setup-check/baseline/result.json` and
`natural-frontend-candidate-check/candidate/result.json`, including latency CSVs,
native/preparation counters, settings, background processes and images. Earlier
rejected preflights and the failed setup are retained. The private host helper
allows an explicitly identified idle owner playtest only after CPU/GPU checks;
all its CPU activity remains in the background total. Build jobs always reject.
The capacity-aware bound is 0.8 background cores on this 16-thread host, plus
matched-load checking. No passing gate has been manufactured.

After resuming, the final provenance guard and cache-epoch changes build and
pass the ARM9/ARM7 preparation/ISA probes and allocator-selector test (3/3).
The no-SDK adapter compiles. Common MPH SDK integration and the final matched
frontend/shared-consumer checks are tracked separately; no release is promoted
from the mechanism diagnostics.

## Remaining release work

- Finish common SDK integration and validate the integrated loading-pipeline
  candidate above; retain allocator evidence without treating it as completion.
- Qualify the completed package with bounded matched MKDS/MPH comparisons;
  the rejected quiet-host attempt is not a regression result. Use only states
  created by each exact binary. Existing prepared states remain useful only
  with their frozen allocator-probe executables.
- Complete the final runner build, title engine pin/version and source bundle,
  then check banks, DLL closure, licenses, private payloads and archive contents.
- Prepare a short loading check for the owner to launch. Current review notes
  have no observations; no owner playtest is claimed.
- Promote only a verified replacement archive, retaining one current MKDS ZIP
  in Nextcloud and preserving native saves/settings.

Keyboard persistence/capture and initial DPI/window fitting are already in
0.0.10 with focused checks. The reported loss of player speed against offline AI
under real CPU load remains unreproduced and owner-deferred. It is a separate open correctness
report, not established as fixed by lower compilation cost.
