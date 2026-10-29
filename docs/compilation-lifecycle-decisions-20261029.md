# Compilation and coverage lifecycle: area07 decisions

Owning doctrine: beads-yjp.63. Implemented component: beads-yjp.91.
Campaign: beads-2wy8.1. Source continues the accepted area06 foundation at
b45eb8433f35a2e9225e6cedbd9a8acdca92a61c. Independent handoff is required
before counting this area complete or starting08.

## A. Generation-bound capture to regeneration: implement

The producer already records `(CPU, page, SHA-1, PC, ISA)`, and runtime banks
already guard guest bytes. The generic offline splitter discarded that identity
at its final address-only map. Distinct overlays could overwrite attribution;
union spans could lose a second body's adjacent start. An address-only owned
row also cannot establish coverage of another generation.

The new splitter retains identity through unique ROM resolution, per-body
target/root collection, span promotion and final module/load-base or captured
bank output. It merges neighboring pages only within a uniquely identified ROM
module or an unambiguous captured run. A duplicate ROM match or an invalid
whole-module load range falls back to captured bytes and existing runtime
guards. Modern bodies never inherit session-only roots, targets or callers.
Schema2's own session targets retain a single-version fallback; modern sessions
cannot borrow legacy eligibility. Caller ISA is unknown and remains diagnostic.
Global per-CPU span budgets are preserved across bodies.

`entry_points.toml` and range reports remain explicitly labeled address-only
legacy diagnostics. Qualified `seeds/` and `images/` are the regeneration
inputs. A fresh/empty output directory is required; the tool refuses nonempty
output before mutation so stale unsafe seeds cannot survive a new corpus.
See [the ingestion contract](coverage-submission-ingest.md).

Focused checks invoke the real CLI and parse actual output. The original
splitter fails the overlapping-body fixture. The candidate covers generation
and ISA isolation, adjacent spans, CPU separation, caller uncertainty,
schema2/3/4 and mixed-schema behavior, ordering, repeated hit merging, hit and
span budgets, owned-row ambiguity, malformed bitmap/coordinates/hash,
duplicate ROM content, invalid module ranges, cross-page spans and exact
captured bytes. The older span-promotion CLI check is retained. Both are
registered in CTest. No runtime/codegen ABI or provider identity changes.

## B. Single-flight compilation, persistence and activation: reuse; reject a second owner

`compile_live_shards.py` already owns durable pending discovery and the index
under one kernel file lock. Candidate identity includes CPU/page bytes/entries
and provider identity; hit-count changes do not cause recompilation. Provider
identity covers compiler, semantic codegen and runtime headers, with separate
GCC/TCC namespaces. The runner already separates filesystem/compiler preparation
from emulation-thread admission, suppresses duplicate commissioning and backs
off futile/no-progress work. The bundled provider includes roots.

`live_overlay.cpp` already retains dormant candidates, retries on actual Tier3
entry, validates live bytes and bounds attempts at8. Parking/requeue semantics
allow later real discovery rather than permanent tombstones. Keep this one
lifecycle. Reject replacing it with another queue/cache manager: the demonstrated
remaining defect was offline attribution in A, not absent single-flight or
activation machinery, and duplicate ownership adds races and invalidation work.

Reuse beads-yjp.62's controlled proofs: clean bundled TCC generated6 shards,
loaded4, zero compile errors and about92% less Tier3 versus that historical
baseline; an intentionally omitted page was compiled and activated in-session
with306 rows and30,868 hits. These are existence/correctness proofs, not new
comparable FPS measurements. Later baked coverage reduced the historical route
to about100 interpreted instructions from870k, making an empty shard cache
legitimate. The initial saturation claim in .50 was disproved by later new-route
coverage; shard/page counts do not establish a universal coverage ceiling.

The .62 request for future fresh-install field confirmation stays open: no
new tester response is invented. The .63 release doctrine also remains a
recurring delivery obligation, separate from this optimization decision gate.
No old four-route matrix is repeated.

## C. Stage-aware native preparation: retain; reject speculative expansion

Keep the accepted MKDS idle preparation and its ROM/page/generation checks,
bounded time, ordinary executable provenance and cache-epoch restart.
`arm9_native.cpp` checks invalidation before its completion flag; the SDK bumps
the epoch on both whole-cache and range invalidation, pinned by the existing
native probe. The suspected stale-completion defect does not exist.

The reused accepted sample prepared146 pages/11,550 locations before the loading
burst, with zero page mismatches, complete=true, no cache exhaustion,73.1192ms
total work and2.0116ms maximum call. Earlier loading mechanism evidence showed
11,550 demand compilations reduced to0 and maximum execution step79.48 to26.07ms;
this is diagnostic evidence, not an isolated whole-game speedup. Preparation
was already complete by frame36, ahead of the selected burst at136.

Reject unconditional idle warming, all-entry/all-CPU hint sets and adding MPH
or ARM7 datasets without a demonstrated remaining demand-compilation obstacle.
They would perform and retain speculative compilation with no selected consumer
benefit, while the current MKDS obstacle is already removed. This is a final
cost/benefit decision for those proposals, not a claim they were implemented
or timed. General memory/invalidation and compiler quality remain08/09/27.

## D. Persistent IR/native SDK cache: reject replacement; retain portable banks/shards

A serialized SDK block is not just guest bytes: generated host code contains
process callback/data pointers, links and host/backend-specific assumptions.
A portable persistent format would need relocation, ABI/CPU-feature identities,
IR semantic versioning, dependency publication, code-write invalidation and a
portable fallback. Native preparation already removes the demonstrated MKDS
demand spike; portable generated banks and loader-relocated GCC/TCC libraries
already persist useful known bodies under explicit guards and identities.

Reject raw JIT-memory persistence as an invalid process/portability contract.
Reject a new persistent IR/provider architecture for this campaign on benefit
versus the complete ownership/relocation contract: no remaining selected demand
cost justifies duplicating the existing persistent execution tiers. No speedup,
regression or built prototype is claimed for either rejected replacement.

## E. Wider baking and title promotion: reuse qualified route; reject blind coverage

Both title-specific `promote_*_static_coverage.py` tools accept raw address
traces and intentionally seed main banks only. Expanding their range filter to
overlays would invent generation ownership. Reject that proposal; use A's
byte-matched module/captured-bank outputs with normal title regeneration.
The prior MPH issue44 workaround deliberately used a game-specific byte-matched
seeder rather than trusting the broken generic split; existing game banks are
not retroactively claimed defective or regenerated without need.

Reject seeding every known halfword/word as a function. Known bytes do not
identify ISA, function/resume boundaries or simultaneous runtime generations.
Every interior entry also changes finder body splitting and dispatch coverage.
This is a semantic/coverage-quality problem, not a veto based on developer
build time or binary size. Use observed qualified targets and one start per
span, retain fallback for unknown code, and feed new observations back into the
same lifecycle. Reuse existing bake/shard/TCC proofs; no claim every possible
course or scene is already baked. Unknown future content is not an unfinished
identified optimization proposal.

## Measurement scope and retained evidence

This package changes developer ingestion and CTest registration only. No guest
payload, runtime source, game bank or existing game executable is modified;
there is no new game timing claim. MKDS and MPH continue using the accepted
area06 binaries (SHA256 prefixes52e40de4 andf43c57e7). Consequently no fresh
gameplay, performance pair or visual launch is justified for this tooling fix.
Regenerated title payloads will use the final combined release validation;
the v0.0.11 owner practical check remains Deferred.

Machine-readable inventory, baseline failure, candidate logs, host reservation,
source identities, final decisions and independent assessment are retained in
`loading-hitches/compilation-lifecycle` under the campaign task workspace.
Temporary CLI fixtures clean themselves; no delivery folder is touched.
