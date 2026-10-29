# Native physical RAM store transactions (candidate)

Owner: `beads-yjp.102`, related to campaign `beads-2wy8.1` and burndown areas
04/08/09. This couples native execution, mapping and physical publication;
none of those broader areas is complete yet.

The existing SDK already has qualified direct reads. This candidate instead
lowers ordinary ARM/Thumb STR/STRB/STRH/STM writes into one generated operation:
write RAM, mark the exact bytes written, advance the physical page generation
and advance the coverage write epoch. The engine owns qualified page descriptors
and alias mapping. Every fetched native code page protects all of its physical
aliases in both CPUs' tables until a full native-cache reset.

Watched/code pages, static execution guards, tracing, the firmware/context
observer page, partial TCM pages, unaligned and unsupported stores, and generation
wrap retain the original callbacks. Mapping refresh occurs at native execution
boundaries. Device accesses and guest timing/scheduling are unchanged. Inline
transactions have a separate diagnostic count; callback counts alone no longer
represent all writes.

The candidate requires the optional x64 SDK patch in
`runner/cmake/dynarmic-published-stores.patch`. CMake
`NDS_NATIVE_PUBLISHED_STORES` is OFF while it is being evaluated. A feature build
supports a process-fixed `NDS_NATIVE_PUBLISHED_STORES=0|1` selector for matched
same-binary measurements, with the existing RAM transactions enabled on both
sides. SDK-free and other host backends retain their portable callback path.

Focused checks passed: 540 generated ARM/Thumb store cases against the independent
interpreter, real-bus mapping/protection checks, physical transaction checks and
the existing ARM9/ARM7 probes. Cases cover registers, flags, data, exact written
bytes, physical generations, coverage epochs, alias remapping, wrapping
generations and code-write fallback. The SDK-free page component also compiles
for the 32-bit x86 Windows ABI; this is not a complete game/other-OS qualification.

## Per-game decision

Retain the implementation for the selected Windows packages: MKDS is a neutral
observation and MPH is positive in the bounded samples. No individual-game
regression was observed. This accepts this concrete store-transaction proposal;
it does not exhaust areas04/08/09 or complete the final combined owner review.
The generic build option remains OFF; the selected candidate configurations
explicitly enable it. No Nextcloud archive is changed.

One initial same-binary OFF/ON pair per game was sufficient for this decision;
no matrix or routine repeated gameplay was run. Both sides enable the retained
native RAM transactions and use the same title-specific optimization package.

| Workload | OFF | ON | Interpretation |
| --- | ---: | ---: | --- |
| MKDS180 natural construction frames, emulation-thread cycles |7,977,300,600 |7,757,943,610 |2.75% lower; neutral observation under unequal ambient load. |
| MKDS mean paced frame |16.696ms |16.773ms |Essentially unchanged. Max24.50/24.97ms; neither interval exceeds33ms. |
| MPH600 room92 VBlanks, process cycles |18,599,482,908 |14,665,167,307 |21.15% lower in this sample. |
| MPH process CPU / uncapped wall |5.78125s /4.28461s |4.46875s /3.48849s |22.70% /18.58% lower. |

MKDS runs on the local PC with compute4x/AA8 and streaming-LZ/vector HLE.
Its background CPU is2.455/1.868 logical cores. Process CPU decreases17.78%,
but that number is not isolated causal evidence. The paced natural-menu
intervals have slightly different guest counts and are not identical snapshots.
Preparation completes all11,550 requests before construction in both legs;
measured preparation work is84.75/87.98ms, maximum call1.84/1.83ms.

MPH runs on SERVER with nativeGL1x, native control and math/submission HLE.
Background CPU is1.415/1.349 cores. Both legs explicitly restore the same
freshly prepared current-binary snapshot and warm120 VBlanks. Native ARM9/ARM7
instruction deltas (49,727,185 /15,786,841), native entry deltas (2,890,417),
event, math and submission deltas match exactly. Game scene fields match;
the absolute debug VBlank origin differs after restore. No equivalence claim
is made from that nonserialized counter. The sampled scene is an active room
with the game's informational dialogue present, not a broad combat benchmark.

The candidate executes6,186,883 generated transactions in MKDS and9,306,083
in MPH. Private memory increases14.18/15.70MiB respectively, consistent with
the two pointer tables plus descriptors. MKDS frontend current-build save/load
passes both legs. MPH same-build restore passes both legs. Basic TCP captures
show coherent geometry/HUD/map or dialogue output in both games. These checks
do not replace the deferred final owner practical review.

Headroom admission passes both pairs; historical quiet-host performance flags
stay false. Ambient load, driver/clock variation and one pair per game limit
generalization. The result does not establish a universal speedup, a racing
gain or a cure for every loading hitch.

Engine source: `d77cda64941589ac920c4b3bf202eb6a668a6ad5`.
SDK feature source: `269cdede97414887ec52e0788abb32d0ddabe1dc`;
shared SDK archive SHA-256:
`6a843c4287f5b6519cd27de38d26e1593ccad8a9c6329e5cf52cfd0867f576b8`.
MKDS executable SHA-256:
`8530d413ff5b2b25dd2b93f788232a93ad74751d6cec9bc6d456a3621723bd39`.
MPH executable SHA-256:
`17b0a9e85aa6ba4612a43a169bef62f88e9ffbd3fbcb9a9785a0025adbadcb1f`.

Machine-readable decisions, costs, source/bank/configuration identities, raw
per-frame MKDS samples, per-game results and reservations are retained below.

Task evidence directory:
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/native-published-stores`.
Keep exact binaries and evidence during the campaign; clean disposable build
scratch when it is no longer needed. The original SDK and historical binaries
are preserved.
