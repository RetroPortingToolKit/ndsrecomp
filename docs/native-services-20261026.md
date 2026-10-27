# Native service investigation: MPH, then MKDS

Owner-authorized implementation of the October 26 plan; central issue
`beads-ova0`, related to optimization campaign `beads-2wy8.1`. Work is solo, on isolated branch `perf/native-services-20261026`
from engine `88ca461`. MPH reference source is `3f5e195`; MKDS reference is
`e7c5c40`. Original checkouts, owner edits and native saves are preserved.

## Candidate decisions

| Candidate | Evidence / current decision |
| --- | --- |
| Reuse the retained native ARM9 executor for MPH | Material win: 20–29% lower process CPU and 22–39% higher uncapped throughput in two matched NAS pairs. Exact AMHE0 selection; HLE entries/continuations remain ordinary guarded dispatch barriers. Focused checks, fresh boot, basic visual and current-build restore pass. Owner confirmed it looks and plays right. |
| Remove redundant native-code validation at inactive CPU boundaries | Not retained for MPH: native baseline 4.77415s wall/5.953125s CPU, validation candidate 5.22496s/6.453125s in one pair. Fewer checks did not produce a demonstrated win. MKDS qualification remains open; do not generalize a one-title outcome. |
| Native RAM access | Existing MKDS direct reads are reused in MPH. Halfwords, unaligned accesses and all writes retain their existing callbacks. Further changes require new attribution and exact ARM alignment/device-width checks. |
| Larger geometry/model services | Submission batches, decoded-command reuse, native affine kernels and projection reuse already exist. The earlier MKDS SBC-loop profile mostly charged its handlers; replacing the loop alone has little useful ceiling. Reassess residual work after CPU changes. |
| Graphics duplication | Native GL and immutable capture reuse already ship for MPH; MKDS's window-aware compositor already exists. Do not count them again as new work. Preserve authoritative guest capture. |
| ARM7 sound/IPC HLE | A complete sound/WFC service contract is needed. Reviewed touch-only/unknown-command-echo donors do not implement it. Not justified by an entry count or a screenshot. |
| Native ARM7 execution | Explicit build option `NDS_ARM7_NATIVE`, qualified for Windows MKDS. Reuse the pinned translator with ARMv4T architecture, a separate per-CPU cache, shared physical invalidation, modeled bus/device operations and portable exception/privileged steps. Timing profile is explicitly identified as two ARM7 cycles per instruction. MKDS matched Pinball sample: 13.6% lower process CPU and 13.7% greater uncapped throughput. Focused ISA/memory/invalidation checks, fresh race construction, basic visual and same-build restore of both CPU register files pass. Owner confirmed it looks and plays right. Not yet qualified for MPH. |
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

The initial pair was 7.06434s / 8.859375s process CPU faithful versus 5.80060s /
7.078125s native. Observed host drift while evaluating the independent
validation candidate justified one reverse-order pair: 7.20007s / 9.4375s
faithful versus 5.17831s / 6.671875s native. Report the range, not one precise
universal percentage. Both pairs use identical 600-VBlank workloads and equal
existing HLE/rendering features. Native timing changes some instruction/event
ordering; these samples are not a bit-identical whole-state claim.

Machine-readable campaign rows and raw receipts live in the task root. Each
package is qualified per game. Neutral/inconclusive results are acceptable;
reproducible regressions require repair or per-title exclusion. Identical
baseline/candidate failures are shared defects, not optimization disqualifiers.
No final campaign figures or blog publication are claimed.

The MKDS pair uses Compute, adaptive top448/bottom256, scale1, native AI on
eight karts at150cc in Waluigi Pinball, retained VECTOR HLE, direct reads and
batched write validation in both executables. Native ARM9 alone takes7.61974s
wall /9.4375s CPU; adding ARM7 takes6.70291s /8.15625s CPU over600 VBlanks.
Both have120 untimed warm-up frames, ordinary race-frame and position progress,
HLE activity and restored current-build state. Other processes consume0.546 and
0.274 cores respectively; this is one bounded sample, not an all-courses or
every-frame guarantee. Existing full-race evidence is reused, not rerun. No
campaign, multiplayer or live Wiimmfi qualification is added by this result.

Both owner playchecks passed. An additional explicitly requested NAS check ran
MKDS at normal pacing through the real WASAPI endpoint with stream output muted.
It delivered1,853 race frames in30.97seconds (59.83FPS), zero audio underruns,
11.64ms mean active work (about30% headroom), p9514.00–14.25ms and
p9916.50–16.75ms. Sixteen frames exceeded the work budget. Other jobs averaged
2.30cores; this is a real-time diagnosis under observed load, not a controlled
A/B speedup claim. The initial quiet preflight collected no game sample and is
retained separately. The old v0.0.7 record also contained a clean Pinball run;
this is not claimed as the first ever. Raw receipts: `nas-mkds-realtime-actual-load`.

The isolated title build paths select the qualified Windows packages by default:
MPH native ARM9 with existing HLE, MKDS native ARM9+ARM7 with existing HLE.
MPH explicitly leaves native ARM7 off. `-PerformanceBackend OFF` retains portable
Faithful builds without the optional SDK; MKDS's `-Arm7Native OFF` keeps native
ARM9 only, and the existing math/submission LLE build choices remain. MPH's
launcher child profile and environment restoration pass in native and portable
configurations. No release archive, merge or publication is part of this work.

Private task data, frozen executables and cleanup inventory are under
`G:/Temp/Codex/ndsrecomp/native-services-20261026` on each host. Nothing is
published, merged, promoted by default or placed in a delivery folder.

## Remaining-native burndown (owner goal)

Tracking: `beads-yjp.100`, related to optimization campaign `beads-2wy8.1`.
The owner requested evaluation of every item below, a final combined build of
both games for a fresh final visual/play verdict, then merging the accepted
engine/title branches to main and cutting title releases pinned to the merged
engine after that verdict. The first-pass acceptance above remains valid for
its exact builds; it does not approve unreviewed follow-up changes.

| ID | Work item | Current evidence and evaluation needed | Status |
| --- | --- | --- | --- |
| N01 | Native ARM7 reuse in MPH | MKDS implementation is accepted; MPH still explicitly disables it. Build the same adapter for MPH, check focused contracts/fresh boot, and take one matched pair. | Retained: additional MPH ARM7 pair CPU -6.17%, throughput +8.27%. |
| N02 | Remaining ARM9/ARM7 native fallbacks | Accepted MKDS ARM7 sample has about 551k reference stops per 600 frames. Attribute PCs/operations and evaluate cheaper complete handling of common stops. | Retain MPH (-13.72% CPU); exclude MKDS (+2.91/+4.64% CPU). |
| N03 | Animation and skinning services | Identify complete hot jobs after native CPU changes. Existing small matrix kernels are already native; replacing cold wrappers is insufficient. | Evaluated/deferred: no complete additional hot job/contract established. |
| N04 | Model setup and geometry services | Review residual model/material/packet handlers. Existing submission/cache/projection work and the old thin SBC-loop result are baseline, not new gains. | Evaluated: neutral/inconclusive Send32 pair; helper not retained. |
| N05 | Bulk memory operations | Find hot copy/fill/decode operations; preserve overlap, alias/provenance, executable writes, device effects and yield boundaries. | Evaluated: hot bulk device ingress is N04; no additional RAM replacement justified. |
| N06 | ARM7 sound-driver update services | Identify real guest jobs and their sound/IPC/WiFi contract. Hardware SPU mixing already runs natively; do not replace commands with guessed replies. | Evaluated/deferred: native ARM7 retained; full voice/IPC/BIOS contract unresolved. |
| N07 | Code validation and invalidation | Accepted MKDS sample still has about 120m physical generation comparisons per 600 frames. MPH's lazy-entry experiment did not win; evaluate different structure/per-title behavior rather than blindly enabling it. | V1 rejected; v2 correctness passes, timing neutral/inconclusive. |
| N08 | RAM and device handoffs | Native reads already cover ordinary aligned RAM. Quantify callbacks and remaining host transitions; preserve odd/unaligned reads, exact I/O width and single completion. | Evaluated: v1 loses MPH; v2 neutral MPH/slower MKDS. Helper not retained. |
| N09 | Graphics/capture/presentation | Reuse existing NativeGL, capture views and window composition. Attribute remaining readback, software capture and synchronization cost before changing architecture. | Evaluated/deferred: existing native render/capture paths retained; no new qualified replacement. |
| N10 | Scheduling and CPU publication | Identify repeated context/deadline/IRQ work on the new paths. Prior deadline cache and larger-window probes are already negative evidence; no unchanged reruns. | Evaluated: reuse prior negative deadline evidence; reduce exits via N02/N04/N08. |
| N11 | Final selected bundle and owner handoff | Record package per title, focused checks, bounded matched performance, exact source/binary/settings, native-save protection and normal-window/audio owner builds. | Building selected combined candidates; final owner verdict pending. |
| N12 | Owner-approved merge and pinned releases | After owner says the final N11 builds are good: refresh/freeze remote heads, merge accepted branches to main, rebuild/package titles pinned to merged engine, deliver one verified current archive per title. | Waiting for final owner verdict |

An evaluation ends with a retained positive/neutral implementation, a measured
rejection, or a documented lack of useful cost/usable contract with supporting
evidence. A row is not complete merely because its name was listed. Reassess
after two unsuccessful implementation experiments. Per-game regressions cannot
be offset by other titles' gains; identical baseline defects are tracked apart.

Initial fresh collection budget: one bounded diagnostic sample per title and
one 600-frame MPH ARM7 A/B pair. Reuse exact-build checkpoints and prior useful
profiles. Additional comparisons are one pair per viable affected package,
only repeated for concrete noise/ambiguity. No route/platform/cold-warm matrix
or repeated complete campaign/multiplayer check. Diagnostic runs are explicitly
ineligible as performance claims.

Current detailed dispositions, contract gaps and private evidence paths are in
`native-burndown-20261027.json` and central `beads-yjp.100`. Small mixed results
remain neutral/inconclusive. A deferred row means the stated candidate was
evaluated without establishing a useful complete replacement; it is not a
claim that no future optimization exists. Final combined owner acceptance,
merging and pinned releases remain pending.

The final selected Windows package adds native ARM7 and the qualified pure
status/cache instructions to MPH. MKDS retains native ARM7 with the control
option explicitly off because of repeatable CPU cost. Both select revised
physical code-write notifications, measured as neutral/inconclusive. The SDK
Send32 and DIV-result prototypes supplied no retained gain and were removed
from production source after preserving their source, tests and raw evidence.
A final matched pair per title will qualify the selected combination before
the owner handoff; it is not another route/platform or gameplay matrix.
