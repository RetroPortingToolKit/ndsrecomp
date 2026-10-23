# NDS performance burndown

Owner: solo Codex work. Central source of task status: **beads-k775** and its
children. Base for new candidates: `ab5e593` (includes the newer native GL and
shared MKDS work). The starting owner checkout and its AGENTS.md edit remain
untouched. Details and scope: [architecture audit](performance-architecture-audit-20261022.md).

The owner authorized building all workstreams before performance measurement.
Each implementation has its own selection so it can be measured individually
and in a bundle. A build or unit-test pass is not a measured win or Wiimmfi
qualification. Default release behavior is unchanged until retained changes
pass qualification and the owner approves final integration.

| Task | Beads | Implementation | Correctness / build | Measurement / delivery |
| --- | --- | --- | --- | --- |
| Boundaries, unrelated changes, storage and fixtures | k775 | Complete | Verified | Complete |
| Independent architecture and historical-profile audit | k775 | Complete | Evidence limitations recorded | Complete |
| Register-local, event-bounded CPU execution | 5npn | Built; 551 MKDS ITCM sites | 368,640 unit checks; game state/pixels/audio match | Rejected for retention: initial saving did not repeat |
| Exact periodic deadline reuse | p14w | Built | Unit checks and 600-frame game state/pixels/audio/event deltas match | Rejected: 2.21% more CPU in clean balanced sample |
| Versioned capture snapshots / texture coherence | 7www | Complete; selected for review | Unit and clean MPH game checks pass; snapshot copies 375 MiB to 2.5 MiB | Individual 5.42% less CPU; clean combination confirms a useful saving |
| Lean production diagnostics / honest event hooks | bbm9 | Complete; selected for review | Arming and clean game checks pass; FIFO padding difference identified | Individual MKDS 2.65% less CPU; clean MPH combination passes |
| Pinned artifacts and quiet SERVER A/A / A/B harness | hucn | Complete; hashes/manifests pinned | Reference bank byte-identical; occupied-host samples rejected; final pair shares NES queue's file lock | Individual samples, A/A controls and clean combined pair complete |
| Two-game bundle, authentic Wiimmfi and owner handoff | wtdd | Clean MPH combination built without rejected experiments | Automated checks complete; owner says it looks fine | Owner approved release and waived new Wiimmfi / further NAS checks; packaging in progress |

## Remaining-task record

This counts finished task outcomes, not elapsed effort or estimated percentage.
Implementation, measurement and qualification must all satisfy a row before it
burns down. Updates retain rejected candidates and unverified coverage.

| Snapshot | Completed | Remaining | Note |
| --- | ---: | ---: | --- |
| 2026-10-22, implementation authorized | 2 | 6 | Audit complete; four implementation candidates starting |
| 2026-10-22, prototypes built | 2 | 6 | Four implementations pass focused checks; game evidence and timings remain |
| 2026-10-22, artifacts and initial game checks | 2 | 6 | Eight builds ready; CPU/scheduler/diagnostic MKDS checks pass; both timing hosts occupied |
| 2026-10-22, first balanced results | 3 | 5 | Deadline probe rejected; CPU pilot inconclusive; remaining samples underway |
| 2026-10-22, individual probes measured | 4 | 4 | CPU/deadline probes excluded; diagnostics/capture retained for clean qualification |
| 2026-10-22, clean combined build | 4 | 4 | Clean reference/retained builds ready; focused tests pass; fresh game comparison and one combined pair remain |
| 2026-10-22, automated qualification complete | 7 | 1 | Retained combination passes; real Wiimmfi gameplay and owner approval remain |

## Measurement boundary

Use SERVER via `F:/NAS` when it is free of other benchmark/game/build work.
Put all new remote artifacts in `G:/Temp/Codex/ndsrecomp/accuracy-perf-20261022`;
never reuse its old C: scratch layout. Keep unattended launches muted. Record
runner, bank archive, compiler/profile and enabled features for every sample.

The CPU pilot is MKDS's small ROM-backed ITCM bank: all four original body
objects recompile byte-identically in the reference archive, and the candidate
contains 551 span sites. Other banks remain identical. This is a bounded test
of the general emitter, not a claim that all ARM9 code has been converted.
In the 600-frame MKDS correctness interval it executed 2,194,522 spans covering
5,702,526 of 105,488,850 ARM9 instructions (5.4%). Both CPUs executed zero
interpreter instructions in that interval. Broad CPU gains require extending
useful coverage; this small bank cannot establish a whole-engine speedup.
Capture reuse is measured with MPH's native-GL renderer, where the copies exist;
MKDS's delivered compute renderer does not pay that same capture cost.

Initial budget after the candidates build: two A/A control windows on the
pilot, one balanced A/B/B/A set per viable independently selectable candidate,
and one A/B pair for the retained bundle on the second game. Reuse same-build
fixtures and useful profiles. Expand only for a recorded defect, contamination
or ambiguity. These are purpose-driven work samples, not a release matrix.

Normal-pacing real WFC gameplay is a separate acceptance gate, preferably with
an unmodified compatible peer. Connection alone is insufficient. Rewind,
rollback and the separate netplay engine are excluded. Offline current-build
save/load, native saves, restore invalidation and portable paths remain covered.
Final owner handoff is windowed with normal audio; final integration awaits
human approval.

## Evidence and current limits

All artifacts and raw evidence are in
`G:/Temp/Codex/ndsrecomp/accuracy-perf-20261022`; SERVER uses the same G: path.
`mkds-candidates/artifacts.json` pins five executables and
`mph-candidates/artifacts.json` pins three. Every candidate has an independent
selection and build identity. The experiment tree advanced runtime and live
codegen identities, with passing golden/provider checks. The clean retained
branch excludes those CPU changes and preserves the existing codegen and ABI.

`first-state-comparisons.json` records CPU enabled/disabled and scheduler game
checks. All guest sections, pixels, recent audio and event deltas match.
`lean-state-comparison.json` records a raw GP3D difference: 42 bytes, all within
the three unused bytes of the existing FIFO entry union. The comparator
explicitly reports it and verifies the rest of GP3D byte-for-byte, alongside all
other guest sections. It does not exempt GPU state wholesale. Save/load uses
the normal exact-build checks; no state identity override is used.

The first SERVER control (`results/aa-1`) failed its quiet gate before timing;
another window launched Genesis tests, with substantial service activity too.
Local correctness runs are marked `performance_eligible=false` because builds
were active. These numbers are not performance wins. Keep checking both hosts
for a quiet slot, as the owner requested; do not suspend unrelated work.

SERVER then yielded two eligible A/A controls: CPU 12.265625/12.375 seconds
(0.89% difference), wall 10.1162084/10.1143378 seconds (0.02% difference).
The next CPU B1 completed; B2 was explicitly rejected when another window
restarted Sonic 2 during the interval. Its post-check recorded the competing
process and 1.125 background cores. The batch stopped before further samples.
The CPU A/B/B/A set remains incomplete; no retained-win verdict follows from it.

A replacement CPU set completed cleanly: reference CPU 11.765625/12.21875 s,
candidate 11.6875/11.90625 s. Its mean 1.63% CPU reduction is smaller than the
3.78% reference drift; wall improvement is 0.42%. Treat it as unproven. A real
CPU-only saving remains worth retaining even with another frame-rate bottleneck;
that is not the reason this result needs stronger evidence.

The exact-deadline cache completed cleanly: reference CPU 12.000/12.078125 s,
candidate 12.40625/12.203125 s. It costs 2.21% more CPU with essentially unchanged
wall time; reject it. Correct formulas and fewer apparent arithmetic evaluations
did not establish cheaper host execution. Retained bundles must exclude it.

The first lean run was rejected after Sonarr raised background work to 0.830
cores (no competing game). Its numbers are not used. A replacement balanced
set is purpose-driven noise/contamination resolution, not a new workload matrix.

Native saves are protected with `--no-save`; emulator audio remains active with
dummy speaker output. Offline fixtures do not qualify real Wiimmfi gameplay.

## Clean review branch

`perf/nds-retained-hostwork-20261022` in `retained-framework` contains only
diagnostic arming, the event-hook correction, immutable capture reuse, build
identity accounting and their checks/tooling. The rejected CPU and deadline
code remains solely in the separate experiment tree. The clean branch keeps
the existing ARM-core pin, code generator and live-bank ABI 6 unchanged, so
discarded experiments do not force a game-shard cache rebuild.

The CPU B/A/A/B repeat used 3.25% more process CPU time and 0.75% more process
cycles; it did not confirm the first set's 1.63% saving. No CPU speedup is
claimed or included. This is a failed prototype, not evidence that a different
execution architecture cannot help.

The focused MKDS graphics probe recorded 130.5 ms in 600 readback/map calls
(0.218 ms/frame), and 159.1 ms in submissions (0.265 ms/frame). These host
measurements do not establish GPU saturation or a GPU ceiling. The sample
still points toward CPU execution work; a broader CPU architecture/HLE effort
remains a separate follow-up rather than pretending these small changes solve
low-end NDS performance.

The clean source head is `a4db725837f4d18234e01564dd03c340fca46a88`.
Its native-GL, save-state and video save-state tests pass (3/3), and its lean
diagnostic/event-hook arming check passes. Fresh same-build game qualification
and the retained MPH combination's single matched pair pass. The final pair
uses 5.08% less process CPU time, 2.22% fewer process cycles and 1.31% less
elapsed time; both screens, audio tail, guest state and event deltas agree
(only the documented unused FIFO padding differs). This is bounded evidence,
not a whole-game or all-hardware speedup. See [results](performance-results-20261022.md).
Final normal-audio/windowed owner playtest and authentic WFC verification remain
pending; no source integration or compatibility claim has been approved.

Owner subsequently accepted the desktop visual playtest and instructed:
"we're good. jsut cut the release. we'll skip my wimmfi" and "if we measured
increase, let's be done. cut it and move on". Release/integration is authorized.
The proposed additional NAS presented run was canceled before launch. Fresh
Wiimmfi and sustained presented 60-FPS qualification remain unverified, explicitly
waived for this release. Windows v0.7.8-alpha packaging is in progress.
