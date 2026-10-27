# Native service investigation: MPH, then MKDS

Owner-authorized implementation of the October 26 plan; central issue
`beads-ova0`. Work is solo, on isolated branch `perf/native-services-20261026`
from engine `88ca461`. MPH reference source is `3f5e195`; MKDS reference is
`e7c5c40`. Original checkouts, owner edits and native saves are preserved.

## Candidate decisions

| Candidate | Evidence / current decision |
| --- | --- |
| Reuse the retained native ARM9 executor for MPH | Implemented experimentally. Exact AMHE0 image selection; title HLE entries and resumable submission continuations remain barriers to translation and use the ordinary guarded dispatcher. Focused checks and fresh Gorea preparation pass; one matched NAS pair pending. |
| Remove redundant native-code validation at inactive CPU boundaries | Next bounded candidate. A 60-VBlank MPH observation performs 7.7 million watched-generation checks, most attributed to ARM7. Existing batching still flushes every ARM7 slice and after JIT return, although cached native instructions cannot execute until the next native entry. Preserve immediate self-modifying-code exits and validate before every native entry. Counts alone are not a speedup claim. |
| Native RAM access | Existing MKDS direct reads are reused in MPH. Halfwords, unaligned accesses and all writes retain their existing callbacks. Further changes require new attribution and exact ARM alignment/device-width checks. |
| Larger geometry/model services | Submission batches, decoded-command reuse, native affine kernels and projection reuse already exist. The earlier MKDS SBC-loop profile mostly charged its handlers; replacing the loop alone has little useful ceiling. Reassess residual work after CPU changes. |
| Graphics duplication | Native GL and immutable capture reuse already ship for MPH; MKDS's window-aware compositor already exists. Do not count them again as new work. Preserve authoritative guest capture. |
| ARM7 sound/IPC HLE | A complete sound/WFC service contract is needed. Reviewed touch-only/unknown-command-echo donors do not implement it. Not justified by an entry count or a screenshot. |
| Bigger generated regions / deadline caches / speculative graphics handoffs | Prior neutral or losing experiments remain rejected; no unchanged reruns. |

## Correctness repair discovered during reuse

Threaded engine-A LCDC display mode called the guest VRAM-read API. With a
capture queued, that API drained the rendering pool and waited for the very
worker making the read. A native-boot stall exposed the cycle. The renderer now
reads its stable physical LCDC bank directly; guest writes/remaps still fence,
and staged capture dependencies still drain before line publication.

A real threaded display/capture fixture times out at 20 seconds before the fix
and passes in 0.07 seconds afterward. This correction is unconditional. It is
not a performance percentage or an excuse to disable the render workers.

## Checks and measurement boundary

Execution selection, native ISA/memory/invalidation probe, MPH math/submission
contracts and native GL checks pass. The private ARM9 fixture passes 2,048
original-instruction math comparisons; MKDS's shared math/binding fixture also
passes. A no-native selection executable compiles and passes.

Fresh same-build MPH native boot reaches active Gorea with normal-looking
output. The 60-VBlank sanity sample executes 1,211 native submission calls and
4,387,720 bytes, matching the reference sample; native instructions and math
services execute, and current-build save/load preserves scene state. Busy local
samples are explicitly ineligible for performance claims.

The first native attempt also revealed that the source-specific submission HLE
definition omitted the native executor. Correcting that definition is required
for an equal-feature comparison. The discarded attempt's zero HLE calls and
retail transfer behavior remain recorded rather than credited as an HLE win.

Initial timing budget: one matched 600-VBlank faithful/native pair on the
i5-4670/GTX760 NAS, Normal process priority, identical retained rendering/audio
features and 120 untimed warm-up VBlanks. Continue from warm-up rather than
clearing the JIT through another restore immediately before timing. Use the
shared file lock and named mutex, reject occupied samples, and retain raw data.
No full campaign or multiplayer repetition; online compatibility is unverified.

Private task data, frozen executables and cleanup inventory are under
`G:/Temp/Codex/ndsrecomp/native-services-20261026` on each host. Nothing is
published, merged, promoted by default or placed in a delivery folder.
