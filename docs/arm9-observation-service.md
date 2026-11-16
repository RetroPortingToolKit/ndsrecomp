# ARM9 observation service

`NDS_ARM9_OBSERVATION_SERVICE=ON` builds the observation service for the optional
Windows x64 native ARM9 Performance path. The build requires the pinned SDK
observation-callback patch. The service is selected automatically in that build;
`NDS_ARM9_OBSERVATION_SERVICE=0` disables it for diagnosis. Faithful execution,
ARM7 execution and builds without the optional SDK retain their existing paths.
The selected execution identity includes `observation-v1` before state loading
or session setup. Profiles are fixed for the process/session.

The service combines native math-register accesses, same-mode interrupt masking,
stateless cache operations, existing direct RAM access and native geometry
submission. It keeps executing native code until an actual observable device,
interrupt, control, debug or budget boundary needs scheduler ownership.

## Time and observations

The SDK supplies the instruction prefix only for actual IO callbacks. The
prefix is settled once before the callback and restored to the remaining native
block accounting afterward. The callback can shorten the live deadline. Direct
RAM and unmatched callback accesses do not pay this clock transport. Multiple
accesses from one instruction share its prefix rather than retiring it twice.

Divide and square-root registers reuse the existing arithmetic, width merging,
restart behavior and result publication. Native-started operations use their
18/34/13-system-cycle deadlines without forcing a scheduler return merely to
publish an unobserved result. Reads, exports and normal observers complete due
work. Legacy starts retain scheduled ownership; import conservatively restores
pending operations to normal scheduling. Native saves are unaffected.

Privileged same-mode CPSR control writes can update I/F natively. A newly
unmasked pending interrupt returns after the instruction retires. Mode, T/E,
privilege and other unsupported changes fall back before any effect. Conditional
execution, full CPSR reads and existing exception ownership are preserved.
Stateless ARM9 CP15 cache maintenance can remain native; WFI, mapping, MPU/TCM,
exception and other real control effects keep their existing boundaries.

The compiler still ends a block after a memory instruction. Its native
`CheckHalt`/`LinkBlock` continuation usually enters the next block without a
host return. This change removes unnecessary host/scheduler transfers; it does
not remove every compiler block boundary or replace the exception controller.

## Qualification

Focused SDK checks cover callback/page-table retirement, repeated block
transfers, shortened deadlines, untimed RAM, conditional control writes,
unsupported pre-effect fallback and post-retirement interrupt observation.
Production math checks cover arithmetic edge cases, busy/deadline/restart
behavior, widths, ownership and current-build export/import on Windows and
Linux. The no-SDK portable implementation compiles and declines availability.
Both game builds pass the ARM9/ARM7 probes, execution-profile test and video
save-state test; their current-build scene/pixel roundtrip and continuation pass.

One bounded hidden, unpaced Windows frontend comparison retained guest audio
with host output discarded and equal diagnostic settings. Its baseline already
included native geometry and GPU-resident display.

| Per frontend frame | MKDS baseline | MKDS service | MPH baseline | MPH service |
| --- | ---: | ---: | ---: | ---: |
| Process CPU, ms | 9.546 | 8.821 | 6.612 | 6.353 |
| Wall time, ms | 7.094 | 6.333 | 4.728 | 4.514 |
| Instrumented ARM9 execution including Tier3, ms | 3.668 | 3.155 | 1.447 | 1.341 |
| Native Run entries | 9,920 | 4,980 | 3,417 | 1,520 |
| Reference fallback stops | 2,007 | 306 | 545 | 114 |

MKDS used identical preparation/input but different current-profile trajectories:
instructions fell about 4.7% and polygon submissions about 29.5%. Its whole-process
gain cannot be attributed solely to this service. MPH scene, health and positions
matched; instructions differed about 0.6% and polygon submissions about 0.1%.
Actual-observation GX boundary requests remain about 8.84/frame in MKDS and
6.11/frame in MPH; geometry-stall rounds and ordinary readbacks remain zero.
These are shared-host observations, not general performance guarantees. Sampled
exclusive execution timing is scaled by actual sampled rounds; Tier3 is measured
separately and carved out before addition. Final combined practical review is
still pending.

Diagnostic `entries` counts actual native Run calls. `device_handoffs` counts
callback requests and can exceed returns. The historical `irq_control_writes`
key counts control-hook attempts, including requests declined by SDK guards;
it does not count accepted guest writes.
