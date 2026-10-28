# Physical write publication

Owning issue: `beads-yjp.102`, related to `beads-d8um`, `beads-0vo` and
optimization campaign `beads-2wy8.1`. This is the all-writer publication slice
of the subsystem burndown, coupled with the resident-code preparation package.
It is not completion of the entire memory subsystem.

## Implementation

Generated-code fast stores and slow-bus stores already update physical-page
generations and exact per-byte write provenance. They now publish the touched
generation-cell range through `runtime_note_code_write_pages`, instead of
discarding that identity and making consumers rediscover which code changed.
Virtual aliases share the same physical generation cells.

Both native CPU caches receive these publications, including writes made by
generated code, BIOS execution, DMA and bulk operations using the bus. An
unrelated data write preserves cached code without scanning all watched pages.
A code hit invalidates the affected cache immediately. The active native writer
retains its existing post-store self-modification check, while nested I/O/DMA
publishes independently to both CPUs. The old native-store-only ambient scope
has been removed. Large published ranges choose between range traversal and
watched-page traversal rather than requiring one lookup per written byte.

The same identity filters active generated-code guards. A potentially affected
guard still performs its existing generation/content/provenance validation;
guards without physical identity remain conservative. Saved outer guards still
validate on nested-call unwind. The coverage write epoch, write ordering,
generation increments, provenance flags and guest timing are unchanged.

`runtime_note_code_write` remains a conservative addressless publication for
backing replacement and compatible older compiled banks. This is not a
save-state migration mechanism. Native caches are cleared before replacing
generation backing storage. The new symbol is exported for Windows live banks;
their existing runtime-header closure identity changes with the public header.

## Checks and remaining qualification

The publisher test checks real inline bookkeeping, page-crossing ranges,
generation wrap, exact provenance and aliases. Native probes cover ordinary and
bulk notifications to both caches, unknown-write fallback, physical filter
collisions, immediate self-modification and nested DMA-modified execution. The
real bus timing and current-build save/load tests pass. All five focused checks
pass in the MKDS configuration (7.14 seconds total).

The real-runtime memory fixture additionally exercises ten active generated-code
guard cases through actual dispatch and both fast/slow bus paths: unrelated
data, same-page data outside the validated span, writes through a code alias,
a nested child modifying its saved caller, and the legacy addressless ABI.
The child retains its own valid guard while its caller becomes invalid on
unwind. All 100,957 fixture checks pass. This extension is maintained in
`runner/tests/mem_timing_test.cpp`; it was built against the already-built real
runtime objects without running a second Ninja against the active bank build.

Building these existing bus/state fixtures in a title performance configuration
exposed a pre-existing missing `nds_performance_execution` test stub. The fixture
now explicitly selects faithful timing; production execution selection is
unchanged.

The current developer application configurations reused older bank archives.
Those archives remain ABI-compatible through the addressless callback, but do
not establish complete all-writer coverage. Rebuild the generated banks against
the changed header before attributing built-game results to this pipeline.
Retain exact source/binary/archive identities and observed notification/scan
counts. The first runtime budget is one bounded functional sample per affected
title and one matched performance pair per title when the host is eligible;
reuse prior loading evidence and expand only for a concrete failure or noise.

No performance gain, per-game neutrality, owner acceptance or release promotion
has yet been established for this implementation. Small mixed results remain
neutral/inconclusive; a reproducible regression in either game requires repair
or exclusion, independently of the other game's result.

New builds and bounded compiler cache stay under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027`. Keep the exact prior candidate
fixtures/binaries intact. Clean disposable build/cache data after qualification
and retain source, raw samples and the selected deliverables.
