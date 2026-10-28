# NDS subsystem performance burndown

Recorded 2026-10-27 from the owner's requested chat burndown.
Owning engine issue: `beads-0vo`. Optimization campaign: `beads-2wy8.1`.
Loading-hitch implementation: `beads-d8um`; MKDS consumer report: `beads-04cb`.

The owner resumed the full burndown goal on 2026-10-27. Loading work is **active**.
Item 01 is first at the owner's request;
the remaining order is an inventory, not a measured ranking of potential savings.

This is the complete inventory of opportunities identified so far. “Open” means
unfinished or worth evaluating; it does **not** mean we have established a speedup.

- [ ] **01. Loading hitches — ACTIVE, prototype implemented.** Consolidate loading, code publication, native-code preparation and first execution. The natural MKDS loading sample found **11,550 first-use ARM9 compilations**, with no repeated compilation or cache invalidation during that transition. Preparing those blocks ahead of demand removed the compilation burst; one diagnostic comparison reduced the worst step from **79.5 to 26.1 ms**. Remaining: finish the candidate's checks, qualify actual frontend performance and shared-engine impact, then package it. **Not release-ready yet.**

- [ ] **02. Remaining BIOS computational services.** Replace complete hot SWI operations with native services: copy/fill, decompression, unpacking/filtering, CRC, arithmetic and sound-table operations where still executed through BIOS instruction streams. Reuse common kernels and publish results once. Existing SDK vector HLE does not exhaust this opportunity.

- [ ] **03. BIOS control and exception services.** Consolidate SWI entry/return, interrupt waits, halt/sleep and relevant reset/boot services around their actual caller/device contracts. Avoid rebuilding an instruction emulator inside each replacement. Wait and interrupt behavior still needs meaningful treatment.

- [ ] **04. Native CPU ↔ HLE ↔ reference execution boundaries.** Redesign the shared execution/service interface to reduce repeated register publication, context copies, dispatch lookups and re-entry work. Native CPUs already exist; the opportunity is the machinery surrounding them.

- [ ] **05. Remaining native CPU fallbacks.** Handle additional worthwhile instruction, status/control and exception cases without repeatedly leaving native execution. Some control operations helped MPH but reproducibly cost MKDS CPU time; that rejected MKDS package stays excluded.

- [ ] **06. Generated-C/faithful execution architecture.** Revisit register ownership, state materialization, instruction accounting, linking and event exits as one execution design. The earlier small register-local pilot did not establish a repeatable gain; simply enlarging the same regions is not a new approach.

- [ ] **07. Unified code coverage and compilation lifecycle.** Reconcile baked banks, shipped shards, live compilation, interpreter gaps and native caches into a coherent lifecycle. Cover useful known code before players encounter it, avoid duplicate compilation, and keep idle discovery work negligible. Several older coverage tickets need reconciliation against current delivered behavior.

- [ ] **08. Memory access and mapping architecture.** Extend efficient access beyond the existing aligned direct-read paths where profitable. Remaining candidates include halfwords, unaligned operations, writes and mapping transitions, with correct aliases, access widths and device effects.

- [ ] **09. Code identity, provenance and invalidation ownership.** Consolidate physical-page identity, executable-write notification, generation checks and cache invalidation across execution backends. Precise notifications already shipped; their reduced comparison counts were **not** a proven timing win. The remaining opportunity requires broader ownership changes.

- [ ] **10. Whole bulk-memory and asset-construction jobs.** Replace complete copy/fill/decode/relocation jobs, potentially through to resource publication, rather than swapping individual loads and stores for `memcpy`. Share underlying work with BIOS services; do not count the same improvement twice.

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

- [ ] **25. Save-state and persistent-I/O stalls.** Separate coherent state capture from compression and disk writing so saving does not unnecessarily block gameplay. Review cartridge/native-save I/O where relevant. Old save-state compatibility requires no work; native saves remain protected.

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

## Coupled work in progress

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
`docs/lab-loading-qualification-20261028.md` in the retained worktree.

Future work should similarly group complete boundaries where useful: BIOS and
bulk services (02/03/10), execution/memory/scheduling/device ownership
(04–09/11–13), model-to-geometry preparation (14–17), render/capture/presentation
(18–20), ARM7/peripheral/network services (21–24), and snapshot/I/O work (25).
Diagnostics, code layout and resource lifetimes (26–28) should follow the jobs
they affect. These are coupling guides, not authorization to mix unrelated
changes or credit a shared improvement more than once.

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
