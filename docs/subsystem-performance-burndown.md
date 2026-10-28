# NDS subsystem performance burndown

Recorded 2026-10-27 from the owner's requested chat burndown.
Owning engine issue: `beads-0vo`. Optimization campaign: `beads-2wy8.1`.
Loading-hitch implementation: `beads-d8um`; MKDS consumer report: `beads-04cb`.

The owner resumed work on 2026-10-28 and requested serial improvements, bundling related work. The measured loading bundle is prepared; its owner practical check is **deferred**. Continue serial engineering without repeating that handoff or replacing Nextcloud.
Item 01 is first at the owner's request;
the remaining order is an inventory, not a measured ranking of potential savings.


## Current status

- **Loading release:** combined v0.0.11 loading + canonical GPU snapshots + complete streaming LZ jobs + shared native RAM transactions is built and artifact-verified. Owner course-loading observation is **deferred**; preserve this candidate and Nextcloud v0.0.10.
- **Measured improvement retained in the candidate:** the complete streaming LZ job (`beads-yjp.105`) is implemented and measured: **7.95% / 13.45% fewer emulation-thread CPU cycles** in two180-frame MKDS construction pairs, including one reverse-order repeat for actual background-load ambiguity. Paced frame times are neutral; no guaranteed worst-frame or general racing gain is claimed. Original ARM caller/memory/timing checks pass.
- **Current serial result:** shared native physical RAM transactions (`beads-yjp.60`), coupling 04/05/08/09, are retained in the prepared v0.0.11 bundle. One same-binary pair observed **6.65% lower MKDS emulation-thread cycles** locally; one MPH pair on SERVER observed **32.12% lower process cycles / 26.32% lower uncapped wall time**, with matching guest counter deltas and scene fields. Different host/workload/counter types are labeled separately; ambient GPU/load differences limit attribution of the large MPH result. Cache-range jobs remain held, neutral/inconclusive; callback and clock experiments stay private. Next: select another complete measured job/ownership boundary using existing evidence, without retuning the held helpers.
- **GPU capture:** the native-first experiment stays off; its local pair showed
  no material whole-frame benefit. Player-speed-under-load remains deferred.

This is the complete inventory of opportunities identified so far. “Open” means
unfinished or worth evaluating; it does **not** mean we have established a speedup.

- [ ] **01. Loading hitches ? ACTIVE, loading-only candidate measured.** Native-code preparation, physical write publication and narrow reads are implemented together. A local exact-delivered v0.0.10 / new-candidate construction sample at compute 4x/AA8 measured worst whole frames **37.1 / 24.4 ms**, frames over 33 ms **1 / 0**, and normal-paced mean frames **16.74 / 16.77 ms**. Background CPU load and GPU clocks differed; this supports practical review, not a clean sustained-throughput claim. Both own-build save/load checks pass. **v0.0.11 source/runtime package and artifact checks pass. Remaining: owner loading observation, then one-current-ZIP delivery.** The GPU capture split stays off and the service-clock pilot stays excluded.

- [ ] **02. Remaining BIOS computational services.** Replace complete hot SWI operations with native services: copy/fill, decompression, unpacking/filtering, CRC, arithmetic and sound-table operations where still executed through BIOS instruction streams. Reuse common kernels and publish results once. Existing SDK vector HLE does not exhaust this opportunity.

- [ ] **03. BIOS control and exception services.** Consolidate SWI entry/return, interrupt waits, halt/sleep and relevant reset/boot services around their actual caller/device contracts. Avoid rebuilding an instruction emulator inside each replacement. Wait and interrupt behavior still needs meaningful treatment.

- [ ] **04. Native CPU ↔ HLE ↔ reference execution boundaries.** Redesign the shared execution/service interface to reduce repeated register publication, context copies, dispatch lookups and re-entry work. Native CPUs already exist; the opportunity is the machinery surrounding them.

- [ ] **05. Remaining native CPU fallbacks.** Handle additional worthwhile instruction, status/control and exception cases without repeatedly leaving native execution. Some control operations helped MPH but reproducibly cost MKDS CPU time; that rejected MKDS package stays excluded.

- [ ] **06. Generated-C/faithful execution architecture.** Revisit register ownership, state materialization, instruction accounting, linking and event exits as one execution design. The earlier small register-local pilot did not establish a repeatable gain; simply enlarging the same regions is not a new approach.

- [ ] **07. Unified code coverage and compilation lifecycle.** Reconcile baked banks, shipped shards, live compilation, interpreter gaps and native caches into a coherent lifecycle. Cover useful known code before players encounter it, avoid duplicate compilation, and keep idle discovery work negligible. Several older coverage tickets need reconciliation against current delivered behavior.

- [ ] **08. Memory access and mapping architecture.** Extend efficient access beyond the existing aligned direct-read paths where profitable. Remaining candidates include halfwords, unaligned operations, writes and mapping transitions, with correct aliases, access widths and device effects.

- [ ] **09. Code identity, provenance and invalidation ownership.** Consolidate physical-page identity, executable-write notification, generation checks and cache invalidation across execution backends. Precise notifications already shipped; their reduced comparison counts were **not** a proven timing win. The remaining opportunity requires broader ownership changes.

- [ ] **10. Whole bulk-memory and asset-construction jobs.** Replace complete copy/fill/decode/relocation jobs, potentially through to resource publication, rather than swapping individual loads and stores for `memcpy`. Share underlying work with BIOS services; do not count the same improvement twice. **Selected lead:** the complete 256-byte streaming LZ chunk function accounts for 1,564,781 tracked byte-swap fallbacks (80.8% of ARM9 reference stops) in the existing loading census. `beads-yjp.105` now implements the complete resumable native decoder with its guest frame, split tokens, overlapping backreferences, bounded scheduler continuations and physical output publication. **Measured selected job:** 7.95% / 13.45% lower emulation-thread CPU cycles in MKDS loading; practical acceptance remains open. Other copy/fill/decode/relocation jobs still need attribution and evaluation; this does not exhaust all bulk-memory work.

- [ ] **11. Dual-CPU scheduling and event ownership.** Redesign ARM9/ARM7 execution windows around actual shared-memory, IPC, DMA, timer and interrupt observation points. Reduce repeated catch-up and deadline work where the contract permits. Blindly increasing the quantum or adding CPU threads is insufficient.

- [ ] **12. Idle loops, polling and OS synchronization services.** Recognize meaningful waits and complete synchronization operations so guest polling does not repeatedly traverse execution, memory and scheduling machinery. Establish this per phase; whole-boot polling counts previously gave misleading leads.

- [ ] **13. DMA, MMIO and hardware-math transactions.** Consolidate complete transfers and device transactions across CPU callbacks, register decoding, scheduling and completion publication. Existing FIFO batching is baseline. The isolated DIV-result helper failed to justify adoption; a broader transaction design remains open.

- [ ] **14. Complete animation and skinning jobs.** Identify and replace full pose evaluation, matrix-palette construction and vertex-skinning operations where worthwhile. Existing vector/affine kernels already execute natively. We have **not yet identified a sufficiently complete hot job and contract**.

- [ ] **15. Model, material and draw preparation.** Consolidate model traversal, SBC handlers, material setup, transforms and packet construction into whole native jobs. Replacing only the SBC interpreter loop or `Send32` did not establish a useful gain.

- [ ] **16. GX command ingestion through geometry preparation.** Examine command decoding, FIFO consumption, matrix/lighting work, vertex assembly, clipping and polygon preparation as a connected pipeline. Remove repeated interpretation and representation changes rather than inserting another queue beside the existing batching.

- [ ] **17. Graphics resource preparation and residency.** Review texture/palette decoding, resource identity, remaps, prepared geometry and cache lifetimes together. Retain reusable prepared resources until meaningful changes invalidate them. Existing decoded-command and capture-view reuse must not be counted again.

- [ ] **18. Authoritative capture, GPU feedback and readback.** Reduce duplicate rendering, copying and synchronization between GPU presentation and guest-visible capture. Preserve the guest capture contract; presentation pixels cannot simply replace integer capture output without establishing equivalence.

- [ ] **19. Both 2D engines' rendering and memory ownership.** Consolidate tile/sprite preparation, scanline jobs, register latching, VRAM views and write/remap fences. Direct mapped views, scanline caching and workers already exist. The opportunity is reducing repeated preparation and unnecessary synchronization across that system.

- [ ] **20. Composition, presentation and driver ownership.** Treat GPU submission, readback, screen composition, presentation and pacing as one host pipeline. Investigate moving suitable work away from the emulation thread and reducing blocking ownership transfers. Large driver stalls remain a separate unresolved lead; removing GL checks did not establish a fix.

- [ ] **21. Complete ARM7 sound-driver services.** Replace worthwhile command/voice-update jobs across guest driver execution, IPC and sound-register publication. Both ARM7 instruction streams already run natively. A complete service needs the actual driver contract, not guessed command acknowledgements.

- [ ] **22. Host sound generation and output pipeline.** Review event batching, channel updates, mixing, buffering and pacing together if they become material. The SPU mixer already runs as host code and was relatively small in retained samples, so this currently has lower demonstrated priority.

- [ ] **23. IPC, firmware and peripheral service boundaries.** Look for complete recurring operations involving FIFO commands, touch/SPI, firmware and peripheral responses. Consolidate real services where measured work warrants it. Touch-only substitutes and unknown-command echoes are not complete replacements.

- [ ] **24. Wireless transport and emulation synchronization.** Reconcile outstanding transport work, then address remaining host waits, packet handling and synchronization costs where demonstrated. Earlier sleep-polling fixes are existing work to reuse. Preserve authentic local-wireless/WFC behavior when changing these boundaries.

- [ ] **25. Save-state and persistent-I/O stalls.** Separate coherent state capture from compression and disk writing so saving does not unnecessarily block gameplay. Review cartridge/native-save I/O where relevant. Old save-state compatibility requires no work; native saves remain protected. **Validated slice:** `beads-yjp.104` fixes GPU command snapshot host-padding nondeterminism by defining all eight entry bytes and rejecting invalid command high bits. The poisoned original fails; real FIFO/geometry/current-state tests pass after the fix. It is included in the prepared loading release; asynchronous snapshot/compression/I/O work remains open.

- [ ] **26. Runtime observability overhead.** Aggregate remaining counters, hooks, tracing gates and state publication at useful boundaries. Lean diagnostics already shipped, and deep tracing is already disabled during ordinary play. Any further work must target measured production overhead.

- [ ] **27. Generated-code layout and host compilation.** Evaluate code locality, duplicated generated machinery, linking, runner PGO and appropriate host specialization after the larger execution design is settled. Some prior compiler work remains unqualified. More emitted native code alone is not a performance result.

- [ ] **28. Host allocation, copying and resource lifetime.** Review temporary buffers, repeated snapshots, staging objects and job lifetimes across subsystem boundaries. Pool or eliminate them where attribution supports it. Allocation was tiny in one retained MKDS profile, so this is an open lower-priority opportunity, not an established bottleneck.

Already completed foundations stay off the unfinished list: native ARM9/ARM7
execution for the selected Windows games, vector/affine HLE, MPH
submission/native GL, capture-view reuse, MKDS compute/window composition,
precise code-write notifications and lean diagnostics.

The **player-speed-under-load report remains deferred**, as requested.

The GCN discussion changes how we burn this down: each item should be evaluated
as a complete service or ownership pipeline before we accumulate helper-level
patches. A deferred evaluation leaves the opportunity open; it does not
establish that the subsystem is exhausted.

## Engineering record (earlier milestones and current decisions)

The owner requested coupling related work on 2026-10-27. The current loading
package combines item 01 with native-code preparation/lifecycle work in 07,
cache epochs and provenance in 09, existing audio pacing slack in 20 and the
retained allocator change in 28. They share one implementation and combined
qualification, while each broader item retains its unresolved scope. This does
not mark five whole subsystems complete.

Candidate `db62776` builds for both MKDS and MPH; each passes the ARM9/ARM7
preparation and ISA probes plus the allocator selector (3/3 per title). The
no-SDK adapter compiles. The preparation seed reproduces from retained private
inputs, with 146 page fingerprints and 11,550 locations and no guest instruction
payload. Fresh exact-binary MKDS menu setup and MPH current-build restore/60-VBlank
functional inspection pass. Their timings are not performance evidence.

The matched frontend qualification preflight rejected the local host before
collecting any timed legs because other games were running. The NAS host was
also occupied. Matched runtime qualification and release/owner handoff remain
open; all task-owned game servers are stopped. Per-game package identities and
raw receipts are in `loading-hitches/resident-preparation-evidence.json` under
the retained task root. No item is checked off from these partial results.

Unified physical-write publication is implemented in candidate `24c9643`, owning
issue `beads-yjp.102`. Both the slow bus and generated inline stores now carry
their exact physical generation-cell ranges through one publication path to
native caches and active generated-code guards. This covers CPU, BIOS, DMA and
bulk writers using those bus paths, while retaining conservative unknown-write
and backing-replacement handling. The native-store-only ambient scope is removed.
This advances the publication slices of 08/09/10/13 with the loading work.

The full MKDS generated-bank rebuild is complete at source `1579454`; reused
older archives would not establish coverage of the changed inline header.
The bounded built-game check matches the reference MainRAM, ITCM and ARM7 WRAM
after 180 VBlanks, then reaches the expected race scene with both native CPUs
running and zero unknown-write scans. Basic track, kart, HUD and map images look
plausible. The first visual harness stopped before the race intro finished; a
focused follow-up resolved that harness limitation. These are correctness
results, not performance measurements.

Focused coverage also includes 100,957 passing real bus/dispatch checks for
unrelated data, same-page data, code aliases, nested caller guards and legacy
unknown writes. MPH's native-control profile passes all six targeted tests;
its HLE mocks now implement the new publication ABI while preserving their
existing assertions. The portable no-SDK adapter compiles. MPH's full generated-
bank rebuild and bounded built-game check also pass, including current-build
save/load, both native CPUs and zero unknown-write scans. Basic images look
plausible. Matched per-game measurements, owner review and delivery remain
pending. No performance win or completed
memory subsystem is claimed. Exact binary/bank identities and raw receipts are
in `loading-hitches/physical-publication-evidence.json` under the retained task
root; implementation details are in `docs/physical-write-publication.md` in the
retained framework worktree.

The related narrow-read boundary is now implemented under `beads-yjp.103`.
A single private MKDS loading census found that halfwords account for 845,515
of 910,818 ordinary-RAM read callbacks. The new lowering keeps their true width
and routes eligible signed/unsigned halfwords and signed bytes through the same
physical mapping, aliases and remap lifetime as existing direct reads. DS odd-
address behavior and exact device widths remain intact. Both actual ARM9/ARM7
probes pass, including 576 added ARM/Thumb differential cases per architecture.
Both games now build at `928da54` and pass their bounded functional checks;
MPH also passes current-build save/load. The MKDS interval removes all 899,958
sampled byte/halfword RAM callbacks while preserving the instruction count,
device accesses and exact RAM contents; prepared ARM9 code needs zero demand
compilations in that interval. Basic images are plausible. These structural
results are not a timing win. The matched frontend preflight rejected the busy
local host before any timed legs; the NAS also has active CPU/GPU workloads.
Per-game timing qualification, owner review and delivery remain open. Raw
machine-readable receipts are in `loading-hitches/coupled-memory-evidence.json`
under the retained task root. This advances 04/08 within the same candidate package.
See `docs/native-narrow-reads.md` in the retained framework worktree.

The actual frontend path now passes for both the baseline and combined
candidate: fresh menus, course 33/150cc, compute 4x/AA8, no state restore and no
native-cache invalidation. The candidate finishes all 11,550 preparation
requests before construction. Under heavy unequal background CPU load, worst
emulation frames are 53.6 versus 16.8 ms; whole frames are 57.6 versus 24.3 ms.
These remain diagnostic, with both performance gates false. A cold-checkpoint
comparison also remains unqualified; its largest frame includes checkpoint
loading before emulation. Matched timing, MPH qualification and delivery remain
open. Details and raw paths are in `docs/loading-hitches-20261027.md` in the
retained framework worktree.

Existing issue `beads-yjp.60` now owns a related private read/CPU ownership pilot
for 04/08/09/11/13. ARM9/ARM7 probes pass, and sampled RAM/scene match, but the
audited status subset removes only 2.11% of native entries. Runtime counters
and GPU device state still need interpretation; there is no timing win or
adoption claim. The next decision is a complete device/event-effect contract,
grounded in loading-phase attribution. This pilot remains outside the release
candidate. See `docs/native-read-transaction-investigation.md` in the retained
framework worktree.

The actual loading-phase census now identifies cartridge-ready polling as the
dominant boundary: ROMCTRL contributes 4,570,753 of 5,044,235 slow reads
(90.61%). The cartridge extension exposed stale device timestamps when native
execution continues across reads. A private shared service-clock/ownership
implementation addresses that boundary and couples 01/04/08/09/11/12/13.
Its same-clock MKDS off/on check has identical complete prepared starting
snapshots, instruction counts, reference stops, device reads, scene and sampled
RAM after 180 VBlanks. Native entries fall by 4,680,529 (52.29%). Actual ARM9/ARM7
clock probes and one bounded MPH check/current-build restore pass; both games'
basic output is plausible. **This remains structural/correctness evidence.**
The first cost attempt stopped at the unavailable shared measurement file
before launch. After it became available, one short diagnostic pair per game
has completely equal before/after guest snapshots with the clock enabled on
both sides. MKDS CPU is lower (13.28 → 11.37 ms/frame), while MPH is higher
(5.90 → 8.07) under substantially unequal background load. Both gates stay
false. Resolve MPH and measure the full package, including clock overhead,
against the prior production path. Do not aggregate these results or equate
fewer entries with a qualified speedup. The
prototype stays outside `928da54`; matched per-game costs, fixed simulation
profile/compatibility integration and practical qualification remain open.
Source, incremental SDK patch and raw receipts are retained in
`loading-hitches/cartridge-transaction-evidence.json` under the task root.

The lab host subsequently became available. One frozen-package pair per game
passes CPU/competitor host checks. MPH trends modestly positive (9.505 → 9.219
CPU ms/frame). MKDS remains inconclusive: reversing order changes the apparent
regression, and telemetry reveals candidate P0 versus baseline P5. GPU operating
conditions were not matched. Both candidate legs prepare every hinted block
before construction. Do not turn the raw host-gate booleans into an adoption
claim. Existing `beads-yjp.72` now owns the coupled emulation/render/readback
boundary with 01/18/19/20/28: split fence waiting from CPU materialization and
reuse the existing persistent-map/flush work before choosing a replacement.
The private clock prototype remains separate; its MPH thread follow-up places
94% of the extra reported CPU time in secondary threads, weakening a large
native-core regression claim while leaving qualification open. Raw per-game
receipts and GPU telemetry are retained in `loading-hitches/lab-package-evidence.json`.
The private capture phase probe then attributes about 91.5% of readback time
to fence/map synchronization (1.87 ms versus 0.10 ms CPU unpack per call).
Evaluate the existing native GL render/capture backend for MKDS before local
compute tuning; its old comparison was instruction-hook contaminated. Keep
the compute path and qualify the complete alternative. See
`docs/lab-loading-qualification-20261028.md` in the retained worktree. The
complete native-GL alternative now builds and passes basic presented-image
and actual frontend current-build save/load checks. Its one matched-settings
pair reduces the worst whole frame (34.9 → 24.0 ms) but raises the mean, with
compute at P0 and native GL at P8. It is not adopted. Atomic live scene reads
replace the unsafe multi-RPC validation reader. Keep the remaining work on
complete render/capture/presentation ownership; existing lazy capture views
must be reused, and guest-capture equivalence remains required. Per-game raw
data and limitations are in `loading-hitches/renderer-alternative-evidence.json`.

The next coupled prototype separates completion points within one scaled
compute frame job: exact native samples and the edge-marking neighbour halo
resolve/pack before the complementary enhanced-display samples. Geometry,
textures and binning are prepared once. It passes real GPU pixel-equality
checks and an MKDS basic presented-image/current-build frontend restore check.
The intended off/on timing pair was rejected before launch because the lab
host was busy; the local functional run is explicitly unqualified. Keep the
private switch off by default until whole-frame and presentation costs are
qualified. No release changed. See `docs/native-capture-frame-job-20261028.md`
and retained `loading-hitches/capture-first-evidence.json`. This advances
01/16/17/18/19/20/28 together without closing their remaining scope.

Future work should similarly group complete boundaries where useful: BIOS and
bulk services (02/03/10), execution/memory/scheduling/device ownership
(04–09/11–13), model-to-geometry preparation (14–17), render/capture/presentation
(18–20), ARM7/peripheral/network services (21–24), and snapshot/I/O work (25).
Diagnostics, code layout and resource lifetimes (26–28) should follow the jobs
they affect. These are coupling guides, not authorization to mix unrelated
changes or credit a shared improvement more than once.

### Latest measurements and execution follow-up, 2026-10-28

Local measurements are possible and have now been collected. Earlier strict
quiet-host rejections remain historical diagnostics; they do not mean this
machine cannot measure. Preserve the ambient-load caveats and original gate
values rather than relabelling them as controlled measurements.

The v0.0.11 loading-only runtime ZIP is prepared and verified, including
matching source, all six banks, DLL/license closure and private-payload checks.
The isolated loading selector and observations live under the retained task
root (`Review-Loading-v0.0.11.ps1`, `loading-review-v0.0.11-notes.json`).
Current Nextcloud v0.0.10 remains untouched until practical acceptance.

The exact-delivered MKDS sample and completed new-build sample support a
loading-only practical candidate, with paced average frame time essentially
unchanged and the worst sampled loading frame reduced. The earlier 48.9 ms
control is a retained development build, not shipped v0.0.10; do not confuse
those baselines. MPH's existing bounded CPU sample trends modestly positive
(9.505 -> 9.219 ms/frame); it is not the new MKDS GPU experiment.

The same-binary capture-first OFF/ON pair shows no material whole-frame gain
(16.77 / 16.61 ms mean; 24.42 / 24.18 ms worst) under differing ambient load
and clocks. Hold that private selector OFF and stop repeating that experiment.
The CPU service-clock pilot remains excluded pending per-game cost and fixed
simulation-profile qualification. None of the 28 broad scopes is exhausted.
Raw local samples, telemetry, exact identities and limitations are retained in
`loading-hitches/local-loading-package-measurements.json` under the task root.

The execution/device follow-up now has a full-package cost observation:
MKDS12.24->11.37 CPUms/frame, MPH6.16->7.03, with unequal background load and
cross-profile starting states. The unchanged service-clock pilot stays held;
its MKDS result cannot offset the MPH concern. Reassessment targets callback
context ownership: a private SDK replacement now publishes PC/access-kind/clock
prefix only on actual callbacks, avoiding those stores on mapped RAM reads.
Both-CPU legacy/new transport probes (including mixed RAM/MMIO clock cases)
pass, both titles build, and the A64 shared x64 memory emitter compiles.
Whole-game state/cost qualification remains open: its180s shared-reservation
wait ended with zero timed legs. This directly couples04/08/11/13/27 while
reusing existing loading/publication work, without closing a broad subsystem.
Source and raw records are in retained `loading-hitches/callback-context-evidence.json`
and `full-device-package-evidence.json`; SDK commit `54a9140` remains private.
The prepared loading-only v0.0.11 and Nextcloud archives are unchanged.


### Latest coupled result, 2026-10-28

The combined prepared v0.0.11 now includes the default GPU command snapshot
correctness fix at framework `b365a7f`, title `d6f005a`. Runtime ZIP SHA-256 is
`23b64649d9eeafecd232eba4695d11a1732e070b6b84d6655a2eb473ea73b58a`;
runner starts `cd4130cc`. Matching source, all six banks, eight-binary DLL/license
closure, ZIP integrity and private-payload checks pass. The previous candidate
was verified before moving to a bounded superseded-review backup. The same
review selector now uses the combined candidate; notes/settings/native saves
are preserved. Nextcloud0.0.10 remains unchanged pending practical observation.

The callback-context MKDS pair is complete: emulation-thread CPU1.875/1.890625s
and cycles8.939/8.884billion over180VBlanks, neutral/inconclusive. Guest counters,
sampled RAM and all raw sections exceptGP3D match. Every GP3D difference is
proved unused entry padding in the old binary (nine before,five after), not
new callback semantics; raw hashes/gates stay unchanged. MPH preparation was
corrected from Faithful to explicit Performance with both native CPUs verified.
Its timing attempts still collected zero legs because the reservation was held.
Reuse the completed MKDS pair; clock/context stay unadopted.

Reassessment selects a complete asset job: retained code/census prove the
streaming LZ chunk body at0x0214D464..0x0214D564, with literal and backreference
output sites causing80.8% of ARM9 reference stops in that load. `beads-yjp.105`
couples the whole decode, continuation and publication contract with01/02/04/05/09/10.
Code/RAM proof stays private; no new per-byte opcode patch or hostCPU speedup
claim follows. Caller/interrupt/alias ownership and full service implementation,
per-game measurement and practical qualification remain required.

Raw receipts: `loading-hitches/gpu-fifo-snapshot-evidence.json`,
`callback-context-evidence.json`, `streaming-lz-job-lead.json`, and
`snapshot-candidate-promotion-receipt.json` under the retained task root.

## Working and completion rules

- Work solo. Preserve owner files, native saves, settings and unrelated work.
- Start with item 01 when the owner resumes or invokes the burndown goal. Then
  select by demonstrated user-facing cost, considering substantial reuse,
  replacement and architectural simplification before local tuning.
- Reuse existing issue history and raw evidence. Do not rerun unchanged rejected
  experiments. Reassess after two unsuccessful improvement experiments.
- State a small purpose-driven profiling budget before collecting new samples.
  Follow the workspace's validation limits; do not run benchmark matrices or
  repeat completed campaign/multiplayer checks merely for process.
- Keep per-game measurements and exact source/binary/package/settings identities
  in the owning issue and `beads-2wy8.1`. Each affected qualified game must be
  neutral or better. Another game's gain cannot offset a reproducible regression.
- Check an implementation off only after its requested outcome is validated.
  If evaluation finds no worthwhile implementation, record an explicit supported
  disposition and follow-up conditions. Preserve deferred opportunities and
  evidence gaps; do not call an entire subsystem exhausted from one failed helper.
- Keep functional portable and faithful/LLE build paths. Internal emulator
  machinery need not survive a replacement. Native saves and meaningful caller,
  device, timing and networking contracts do.
- Prepare ready candidates for the owner's normal-window, normal-audio review
  under the existing policy. Preserve completed owner acceptance and required
  merge handoffs. Deliver one verified current archive per game when qualified.
- Never control the owner's mouse or desktop. Use game/debug interfaces for
  unattended validation; keep background tests muted.

## Evidence and retained implementation

- Paused implementation and raw receipts:
  `G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/`.
- Loading mechanism comparison:
  `loading-hitches/preparation-comparison.json` under that root. This is an
  unqualified diagnostic comparison, not a quiet-host release performance claim;
  it does not validate the final uncommitted source edits.
- Previous architecture audit:
  `docs/performance-architecture-audit-20261022.md` in the retained framework
  worktree under that root.
- Prior retained/rejected native package evaluations:
  `docs/native-burndown-20261027.json` and central `beads-yjp.100`. That prior
  issue is closed after its delivered package was accepted; deferred research
  within it is carried forward by this list.


## Serial improvement: complete streaming LZ job (2026-10-28)

`beads-yjp.105`, framework `993ce7073fb9c0c787b6b1f974a0006a69194226`,
title `9bb0100adc9f4d3b52a113f61e845279f24e27bd`. This is one complete
decode/publication job bundled with the loading candidate, not another
callback tweak. Whole-code proof owns cold continuation PCs independently of
older generated-bank splits. Unsupported mappings, diagnostics and short
budgets use the existing reference floor; the faithful/LLE path remains.

Focused correctness:60 original-ARM cases /5356 exact materialized boundaries
match all registers, CPSR, the full caller memory and the original-instruction
count under the identified two-cycle native-service profile. Split compressed
tokens, distance-one/overlapping output, input/output/context/stack aliases and
physical mirrors are covered. Built current-save/load passes in all four legs;
one candidate image sanity inspection shows plausible output.

One initial180-frame construction pair and one reverse-order repeat (solely to
resolve real load ambiguity), same compute4x/AA8 configuration and exact binaries:

| Pair | Baseline emulation CPU cycles | Native job cycles | Saving | Baseline/candidate mean paced frame |
| --- | ---: | ---: | ---: | ---: |
| A/B | 8,725,871,950 | 7,552,137,190 | 13.45% |16.730 /16.718 ms |
| B/A repeat | 8,970,385,220 | 8,257,412,140 | 7.95% |16.693 /16.828 ms |

Background activity was unequal (6.34/2.48 and5.30/4.92 cores). These are
observed positive CPU-cost results and neutral paced presentation, with that
precision limit. The large first worst-frame difference did not reproduce;
do not claim a guaranteed hitch reduction or general racing/FPS improvement.
The old quiet-host gate remains false; no passing performance JSON was invented.
Other games receive no new decoder binding and have no new qualification claim.

Runtime/source ZIP is verified and prepared as the one current v0.0.11 review
candidate, SHA256 `2a7a6aa4696d218f128e3a73158f0587908759baf6977bec8c4afb6438cf8fd4`.
Nextcloud v0.0.10 remains untouched. Remaining: owner loading observation via
`Review-Loading-v0.0.11.ps1`, then one-current-ZIP replacement. Full28-category
completion remains0/28; the complete selected decoder job is implemented and
measured, awaiting that shared practical handoff. Keep the other experiments
parked and player-speed loss deferred.

Machine-readable per-game rows, raw A/B receipts, exact identities and retained
storage/cleanup needs are in
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/streaming-lz-job-evidence.json`.

Owner follow-up2026-10-28: **practical loading check deferred**. Recorded in the isolated selector notes; Nextcloud remainsv0.0.10. Keep the verified combined candidate for later review. This defers the human check, not the serial optimization campaign. Finish this bundle bookkeeping, then select the next demonstrated cost; do not repeat the completed loading samples.

Next serial engineering work: `beads-yjp.106`, complete transparent cache-maintenance range services (invalidate, clean, drain+clean/invalidate). Retained loading counts and the current CP15 no-cache-state model select this whole-job replacement; no performance claim or new run yet. Work starts from the measured decoder head993ce70 in isolated branch `perf/cache-maintenance-jobs-20261028`; the verified v0.0.11 review artifact remains unchanged. Player-speed report and held callback/capture experiments stay deferred.

Cache-family implementation milestone: `6f381a795ecb338c374d8bca8831c43f4993270e` implements complete transparent range jobs, with216 original-ARM cases/7533 exact boundaries and the real CP15 state contract passing. A single MKDS candidate is building; one matched loading pair against the decoder baseline is next. This is not yet a measured cache-family win or a new release. The ready decoder bundle and deferred owner observation are preserved.


Cache-family decision2026-10-28 (`beads-yjp.106`): **held, neutral/inconclusive**.
One180-frame built pair on the measured decoder baseline, compute4x/AA8,
observed emulation-thread CPU cycles9,695,623,890 /9,477,776,540 (-2.25%)
and paced means16.776 /16.712ms. Background9.28/7.01 cores differed; this
does not establish a timing win. Reference stops908,759 /707,168 and all
focused/built correctness checks pass, but counts do not justify adoption.
Keep the cache option LLE by default and out of the ready release. Do not
repeat/tune this small result now. Raw per-game evidence:
`loading-hitches/cache-maintenance-job-evidence.json` in the retained task root.

Next serial decision: attribute native bulk-service/scheduler ownership before
another implementation. The decoder still materializes roughly325,000 host
batches per construction phase (about4 output bytes/batch); that is a lead,
not its host CPU share. Reuse existing profiles first and take one small
purpose-driven host sample only if needed. The supported decoder package,
owner-deferred practical check and Nextcloudv0.0.10 are unchanged.


Post-decoder attribution2026-10-28 (`beads-yjp.60`): one180-frame diagnostic,
exact runner751b/source993ce70, fresh menus/compute4x/AA8, no practical-QA repeat.
Raw45,558 host samples are unchanged; the derived filter keeps549 samples in
exact emu-active intervals. About18% are unresolved native JIT addresses. The
hybrid attribution puts ARM9 execution first (~1400ms), scheduler bookkeeping
~231ms and context copies ~7ms against2229ms selected emulation. The hybrid
query brackets slightly more than the exact sampler window and has estimator
variance; these are diagnostic magnitudes, not performance results.

The decoder's self-time share is small despite its325k batches. Native entry,
provenance and ordinary-RAM callbacks show a clearer recurring cost. Selected
implementation `c24e2e5` gives native code fetch/admission and reads/stores one
instruction-local physical view, retaining the existing physical publication
and invalidation authority. It changes no scheduling quantum or guest cycles,
keeps device/diagnostic fallbacks and preserves unaligned/signed semantics.
115,105 focused checks cover real bus mappings/provenance, WRAM/TCM, map import,
store generations and active self-modifying-code/alias/nested guards. One
same-binary180-frame MKDS off/on pair is budgeted after the build. No adoption
or new game performance result is claimed yet. Raw diagnostic/filter identities:
`loading-hitches/decoder-host-attribution/baseline/derivation.json` under the
retained task root. Ready decoder v0.0.11 remains unchanged; practical check
Deferred, Nextcloud v0.0.10, player-speed report deferred.


Native RAM transaction decision2026-10-28 (`beads-yjp.60`): **retained in the
combined MKDS candidate**, engine `c24e2e5`, title `76a22b1`.

| Game/host/workload | Original RAM path | Shared RAM view | Observed change |
| --- | ---: | ---: | ---: |
| MKDS/local,180 construction frames, compute4x/AA8; emulation-thread cycles |9,263,402,490 |8,647,364,090 | -6.65% |
| MKDS same pair, process CPU seconds |2.71875 |2.59375 | -4.60% |
| MPH/SERVER,600 room92 frames, nativeGL1x; process cycles |23,556,684,678 |15,990,829,291 | -32.12% |
| MPH same pair, uncapped wall seconds |5.148102 |3.792874 | -26.32% |

Each game used one executable with a process-fixed RAM0/1 selector. MKDS keeps
the loading/snapshot/streaming job package; MPH keeps its native-control/math/
submission package and has no MKDS decoder/cache binding. These are per-game
comparisons, not equal packages or an aggregate gain. MKDS background load was
9.05/9.25 logical core equivalents; both large emu-wall outliers used fewer
thread cycles than their respective median frame. No hitch/FPS cure is claimed.
MPH's guest instruction/event/submission/native deltas and initial/final scene
fields match, excluding diagnostic cumulative VBlank counts. Background CPU was
1.79/1.95 cores, while sampled background GPU differed (~38%/11%); the large
positive observation is not an isolated or whole-game win estimate. Existing
quiet-host gates stay unchanged. Both MPH legs passed the new headroom policy.

115,105 real-bus checks pass. MKDS's new path has plausible track/kart/HUD output
and current-build restore passes in both legs. MPH restored its own same-build
checkpoint, but its optional-Pillow screenshot helper silently skipped capture.
A prepared1-frame stdlib capture was rejected before game launch for current
SERVER capacity; that additional visual remains open. No performance pair was
repeated and no owner playtest was launched.

One current combined MKDS review ZIP is prepared and verified:
`d91f53706ecb33812791e51dd202c7f7cbf7bf34f83b0799c6980270aa2156f2`.
Matching source ZIP:
`83bfdaddb4754fda793a5b03df2c0960d80a4b576b70543af605a8af45036e49`.
The ready selector and owner **Deferred** notes are preserved. Nextcloud remains
v0.0.10. Full28-category completion remains0/28; selected decoder/RAM jobs and
the snapshot fix are real completed engineering slices, not exhausted systems.
Raw per-game identities, conditions and retention needs:
`loading-hitches/native-ram-transactions-evidence.json` under the task root.

Shared validation infrastructure (`beads-2wy8.2`) is complete: local PC, SERVER
and Steam Deck form a compatible-target pool with capacity reservations.
CPU/GPU/RAM/storage headroom replaces game/build-name vetoes. Pending claims
reserve capacity; running claims are not double charged, and total declared
budgets still fit the host. Both Deck SSH aliases share a ledger.14 tests plus
actual Windows/Linux remote commands, concurrent leases, rejection and cleanup
checks pass. Installed tool: `C:/Users/Matthew/.local/bin/benchmark_hosts.py`.
Global agent policy and the NDS adapter now use it. The MPH pair ran on SERVER
alongside another capacity-reserved job; unrelated work was preserved. Target
rotation does not require a three-host benchmark matrix or establish a Deck
build/backend qualification.
