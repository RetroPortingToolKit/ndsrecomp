# NDS host-work investigation results

2026-10-22. Solo work, central Beads `beads-k775`. Source review branch
`perf/nds-retained-hostwork-20261022`, tested engine head
`a4db725837f4d18234e01564dd03c340fca46a88`, based on `ab5e593`.
The original checkout, its owner AGENTS.md edit and original native saves are
preserved. No source integration has occurred. [Burndown](performance-burndown.md).

## Decision

Keep lean optional diagnostics and immutable capture snapshot reuse for owner
review. Reject both the register-local CPU prototype and periodic deadline
cache. These are modest host savings; low-end NDS performance remains an open
architectural problem. A CPU saving remains useful even when another cost
limits frame rate, but it must first survive measurement noise.

The clean retained build preserves CPU behavior, the ARM-core pin, codegen
identity and live-bank ABI 6. It excludes all code from the rejected probes.
Options retain full diagnostics and the original copying path. The event-hook
correction applies to both profiles: instruction anchors and deep tracing arm
the callback; a VBlank/device anchor alone does not.

## Measurements

SERVER: i5-4670, four cores/four threads, GTX 760, Windows 10. New remote data
is on G:, accessed via `F:/NAS`; no new C: scratch layout was used. MinGW GCC
15.2.0 Release, identical existing HLE/rendering features in each comparison.
Both games completed 600 VBlanks per measured interval, uncapped, with audio
emulation active and speaker output dummy. Preparation, state I/O, screenshots
and audio extraction are outside timing. Ordinary play uses normal pacing.

Two A/A controls differed by 0.89% in process CPU and 0.02% in elapsed time.
Each independent probe used balanced A/B/B/A ordering. The CPU ambiguity got
one B/A/A/B repeat. Occupied-host and service-contaminated samples were rejected
and preserved, with no unrelated process stopped. The final clean pair held
the same byte-range file lock as the NAS NES queue, plus NDS's global mutex.

| Probe | Process CPU change | Elapsed change | Verdict |
| --- | ---: | ---: | --- |
| CPU local spans, MKDS ITCM | -1.63% initially; +3.25% repeat | Initial -0.42% | Reject; no repeatable CPU saving |
| Periodic deadline cache, MKDS | +2.21% | Essentially flat | Reject |
| Lean diagnostics, MKDS | -2.65% | -1.21% | Keep for review |
| Capture reuse, native-GL MPH | -5.42% | -3.42% | Keep for review; candidate spread 7.36% limits precision |
| Clean lean + capture, MPH, one A/B pair | -5.08% | -1.31% | Combined automated qualification passes |

Final clean reference/candidate values: process CPU 7.078125/6.718750 s,
elapsed 5.722722/5.647553 s, process cycles
24,124,997,665/23,589,873,930 (-2.22%). Background work was 0.484/0.585 cores,
below the declared 0.75-core exclusion threshold; no competing game/build was
observed. Private committed memory was 440,225,792/371,937,280 bytes, about
65.1 MiB lower. These CPU, cycle and elapsed results measure different things;
the modest single-pair result does not warrant a precise universal percentage.

The strongest mechanism evidence is actual capture snapshot copying:
393,216,000 bytes (375 MiB) became 2,621,440 bytes (2.5 MiB) in the same MPH
interval. Immutable owned texture/palette views refresh independently when
content changes. Registers and render lists still latch each job. The software
integer capture remains authoritative for guest reads; reset/load invalidates
the host reuse state. MKDS's compute renderer does not pay this same copy cost.

## Correctness and coverage

The clean build passes native-GL, current-build save-state and video save-state
tests (3/3), plus diagnostic default/anchor arming checks. Capture tests cover
independent texture/palette mutation, repeated latching, reset with repeated
versions, deferred guest reads, software fallback and host reconstruction.
Full/lean and capture-off/on builds compile and run. No new assembly or CPU
feature requirement was added. Other operating systems were not qualified.

Fresh same-build MPH preparation and restore pass locally and on SERVER.
Both screen pixel hashes, 4096 stereo audio frames, scene state and every event
delta agree. All guest serializer sections agree apart from GP3D's known
unused FIFO union padding; the comparator explicitly proves and reports that
narrow difference, without exempting geometry state. Basic visual inspection
of both clean game screens is coherent. This is focused fixture evidence,
not campaign completion, exhaustive raster/remapping coverage or online proof.
Native saves were protected with `--no-save` throughout automation.

The CPU prototype passed 368,640 generated/interpreter checks but covered only
5.4% of active ARM9 instructions in MKDS. Both CPUs used zero interpreter
instructions in the sampled active MKDS and MPH scenes. MPH had one earlier
boot instruction; historical uncovered workloads did use substantial fallback.
Interpreters are workload dependent, not assumed universally cold.

## What remains blocked

The main architectural lead remains the per-instruction execution contract:
register/PC materialization, counters, timing helpers and exits surround even
compiled arithmetic. The small local-span probe did not solve that contract.
A useful follow-up needs broader execution coverage and measured host work,
while retaining observable CPU ordering, bus timing, IPC, DMA/timer/IRQ and
graphics FIFO/readback behavior. Wiimmfi authenticity constrains guest-visible
results; it does not forbid removing redundant host work.

The focused MKDS graphics probe measured approximately 0.218 ms/frame for
map/readback and 0.265 ms/frame for submission. Their combined synchronization
counter overlaps them. This does not establish GPU saturation or justify a
claim that the GPU is the remaining ceiling.

Rewind, rollback and the separate recomp netplay engine are out of scope by
owner instruction. Real Wiimmfi peer gameplay on this retained build is still
unverified. The old M7 acceptance is historical baseline evidence only.
The final gate is the windowed, normal-audio owner playtest, including real
peer gameplay when available, followed by explicit approval before integration.
MPH has been launched for that handoff with Wiimmfi enabled. It uses copies
of the existing paired `mph-prime-visible-wfc-yjp82/A` native save and firmware
in `owner-playtest/profile-A`; source files remain untouched. An initial launch
correctly rejected the game root's older 128 KiB firmware profile before the
supported 256 KiB pair was selected. The interactive frontend is active with
both retained options, no instruction hook, no synthetic presentations and
the governor at stage 0. Two startup audio underruns were observed; actual
audible quality and online gameplay await owner feedback.

Private raw evidence is under
`G:/Temp/Codex/ndsrecomp/accuracy-perf-20261022`: `initial-evidence` (248 files,
all hashes verified), `final-evidence`, `clean-*-state`, `clean-tests.log`,
`clean-policy.log`, and per-build artifact manifests. Raw states, native saves,
ROMs, BIOS/firmware and network identities must stay private.
