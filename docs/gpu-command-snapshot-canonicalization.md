# Canonical GPU command snapshots

Owner beads-yjp.104; optimization campaign beads-2wy8.1. This fixes a shared
pre-existing correctness defect found while comparing native callback transport;
it is separate from that unadopted optimization and advances a slice of
burndown16/25/26 without exhausting those scopes.

The command FIFO stored an eight-byte union containing a32-bit parameter and
an8-bit command. All three producers assigned those fields, leaving three host
padding bytes undefined. FIFO snapshots copy the entire entry, including stale
slots. MKDS's before/after comparison differed only at those padding bytes:
nine bytes before and five after; all other serialized sections and guest
instruction/read/entry counters match. Raw hashes remain unequal in that old
binary; semantic analysis must not relabel them as a passing raw comparison.

The command now occupies a32-bit field beside its32-bit parameter, retaining
the8-byte entry size/alignment. Every producer defines every stored bit, with
no extra zeroing pass or runtime option. Load validates all stored FIFO/PIPE
commands fit eight meaningful bits before execution can index command tables.
Old save-state compatibility is not required; native saves are unaffected.

A regression poisons otherwise identical entry storage with different host
byte patterns and calls the real GPU serializer. It fails on the original
implementation and passes after the fix. It also checks exact current-build
GPU state roundtrip and rejection of high command bits in FIFO and PIPE.
The complete existing FIFO/parser/geometry/cache/stall regression and video
save/load test pass,2/2, in a portable software-only build. No gameplay matrix
or whole campaign/multiplayer rerun is involved, and no speedup is claimed.

Source evidence and baseline/candidate logs are retained under
G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/:
gpu-fifo-snapshot-baseline-result.json, gpu-fifo-snapshot-candidate-ctest.log
and build-gpu-fifo-snapshot/.
