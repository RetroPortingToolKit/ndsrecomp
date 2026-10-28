# Loading-package lab qualification

Owning issues: `beads-d8um`, `beads-yjp.72`, `beads-yjp.102` and
`beads-yjp.103`; coordination: `beads-0vo` and campaign `beads-2wy8.1`.
Frozen implementation: `928da54`. The private service-clock/read-ownership
prototype is excluded.

The idle lab host is Windows 10 build 19044, i5-4670/4 logical CPUs, GTX 760,
NVIDIA 456.71/OpenGL 4.3. Disks were idle and about 8.7 GiB RAM was free before
transfer. All 89 transferred files match their source SHA-256 hashes. Runs use
isolated profiles, no native-save writes, dummy audio and hidden windows.

MKDS uses baseline `adeaef3c…` and candidate `a8605f67…`: fresh normal menus
into Pinball 150cc, compute 4x/AA8, real audio pacing, native cache retained,
no state restore. MPH uses baseline `2a9a8325…` and candidate `376fe47e…`, each
with its own exact-executable checkpoint: room 92/native GL 1x, native
control/math/submission, 120-VBlank warmup and one 600-VBlank sample. Its
baseline reconstructs old allocator behavior under the common span ABI;
it is not the delivered-release binary. Preserve these package distinctions.

The initial budget is one pair per game. One reverse-order MKDS pair follows
only to resolve the observed apparent regression. There is no route, platform,
repetition or cold/warm matrix.

All legs pass the existing CPU/competitor host gates. That does **not** prove
matched GPU operating conditions or a neutral/positive optimization result.

| MKDS order | Binary | Mean emulation ms | Worst emulation ms | Mean combined readback ms |
| --- | --- | ---: | ---: | ---: |
| Baseline then candidate | Baseline | 16.3292 | 33.9297 | 4.0481 |
| Baseline then candidate | Candidate | 19.9853 | 37.4740 | 7.8135 |
| Candidate then baseline | Candidate | 11.7227 | 15.9935 | 2.3955 |
| Candidate then baseline | Baseline | 20.1475 | 36.6977 | 7.9280 |

Both candidate legs finish all 11,550 preparation requests before construction,
with zero page mismatches/cache exhaustion and no native-cache invalidation in
the measured window. Preparation costs 160–162 ms total in existing menu slack;
the largest individual call is 3.1–3.3 ms.

The first pair's extra mean time is almost entirely in readback: time outside
it is 12.2810 versus 12.1718 ms. The reverse pair does not reproduce the
candidate slowdown, but telemetry shows **candidate P0 and baseline P5
throughout their selected windows**. Do not average the pairs into a claimed
win or call the first result a reproducible candidate regression. MKDS
adoption remains **inconclusive**. The readback counter combines fence waiting
with CPU materialization/unpack; their relative costs remain unmeasured.

MPH's eligible pair uses 9.5052 → 9.2188 CPU ms/frame (-3.01%) and
146.98 → 153.41 uncapped frames/s; wall time falls 4.19%. Background means
differ by 0.0442 cores. This is a modest positive trend in one sample, not a
general multi-host win. It cannot compensate for unresolved MKDS qualification.
There is no new release or owner handoff.

Reuse `beads-yjp.72` for complete emulation/render/readback ownership, coupled
to loading. `beads-yjp.64` already records submit-side flush, persistent PBO
and fused unpack work, including its negative heavy-scene result. Preserve
those foundations rather than repeating them as a new optimization.

Next separate fence completion, mapped-memory materialization and conversion
costs, then inspect actual display and guest-capture consumers together.
Preserve same-frame guest capture and the portable path; display-only delay
does not authorize delayed guest observations. Match or explicitly reject
differing GPU operating states in later qualification. Do not run more blind
A/B legs or repeat the failed GL-check removal.

The private phase probe now completes one fresh-menu compute 4x/AA8 sample,
with the algorithm unchanged. Its live observation interval contains 225
readbacks: mean fence/map synchronization is **1.8700 ms**, CPU unpack is
**0.1012 ms**, and release/error handling is **0.0720 ms**. Synchronization
accounts for about **91.5%** of their sum. This is bottleneck attribution, not
an A/B or adoption result. Do not pursue the already-small unpack loop first.
Raw source/binary identities and verified receipts are in
`loading-hitches/capture-phase-evidence.json`.

Evaluate the existing native OpenGL backend as a complete MKDS render/capture
alternative before accumulating local compute changes. The old MKDS renderer
comparison enabled an instruction hook, so it established neither a clean win
nor regression; see `docs/performance-architecture-audit-20261022.md`. Reuse
its existing capture-reference/view ownership and preserve the compute path.
This is a candidate to build and validate, not an assumption that native GL
will be faster or correct for MKDS.

Verified receipts, trace CSVs, images, identities, GPU telemetry and QPC/UTC
mapping live under the retained task's `loading-hitches/` in
`lab-package-evidence.json`, `nas-qualification-receipts/` and
`nas-qualification-repeat-receipts/`; generator: `record_lab_qualification.py`.
Remote `F:/Temp/Codex/ndsrecomp/qualification-20261028` is private agent-created
scratch. Retain extracted fixtures for the immediate ownership investigation,
then audit/remove them. Preserve local receipts for campaign visuals.
