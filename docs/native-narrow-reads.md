# Width-correct native reads

Owning issue: `beads-yjp.103`. Campaign: `beads-2wy8.1`.
This is part of the coupled loading, execution and memory package. It advances
burndown items 04/08; it does not complete their wider scope.

## Measured reason to attempt it

One private, fresh-menu MKDS loading census over 180 VBlanks found 910,818
ordinary-RAM read callbacks: 54,443 byte, 845,515 halfword and 10,860 word.
Halfwords were 92.8% of that remaining callback family. The same interval had
5,044,237 device/slow read callbacks, which this change does not eliminate.
MainRAM, ITCM and ARM7 WRAM match the rebuilt publication reference exactly.
These are workload counts, not CPU savings or a performance acceptance result.

Private raw evidence is retained under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/`:
`read-width-natural-loading/result.json`, RAM images, the exact diagnostic
binary, and `read-width-census-build/identity.json` with source/binary hashes and
compiler/link commands. The private counters are not included in production.

## One read boundary

The old lowering replaced every halfword with a tagged 32-bit callback so it
could express the DS's unusual alignment behavior. That also prevented ordinary
aligned RAM halfwords from using the existing read-only physical page map.

The native IR now retains the actual 16-bit operation. Unsigned loads align the
address, zero-extend the result and perform any odd-address rotation in 32 bits.
Signed aligned loads sign-extend the direct 16-bit result. Signed odd loads fail
the existing alignment guard and execute exactly one byte read in the callback;
the resulting sign survives the IR's halfword extension. Signed byte loads also
use the map. All these reads share its existing aliases, remapping epoch and
backing lifetime with word/byte/block reads.

Unmapped/device reads use their actual width exactly once and retain the
existing callback PC publication and device handoff. Writes, DMA publication,
guest instruction accounting, event deadlines and native-control selection
are unchanged. The obsolete wide-halfword callback branch/helper is removed.
The build identity includes `narrow-reads-v1`, and native JSON reports
`narrow_reads:true`. No new UI setting or SDK patch is required. Builds without
the native SDK retain the faithful implementation.

## Correctness and acceptance

The actual SDK emitter passes ARM9 and ARM7 probes, including 576 additional
ARM/Thumb differential cases per architecture for addressing, alignment,
predication, registers, flags and memory. Focused checks exercise direct
unsigned/signed halves and signed bytes, physical aliases, mapping replacement,
unmapped fallback and exact device read width/count/PC. Existing instruction,
preparation, physical-write and self-modification probes still run.

The initial private probe's final unmapped-read assertion used data overwritten
by its newly added random cases. Restoring that assertion's known input fixes
the fixture; both complete probes pass. The failed log is retained rather than
reported as a candidate failure or discarded.

Both game applications build at `928da54`, and both title profiles pass their
ARM9/ARM7 probes. The no-SDK adapter compiles. The new MKDS construction interval
removes all 899,958 sampled byte/halfword RAM callbacks; guest instruction count,
device accesses and all three RAM regions remain identical to the reference.
Resident preparation changes no serialized guest state and leaves zero ARM9
demand compilations in that interval. MPH's bounded interval and current-build
restore pass, with both native CPUs active and no unknown-write scans. Basic
images are plausible in both games.

The matched frontend preflight rejects the local host before any timed legs
because another game/compiler workload is active; the NAS is also occupied.
Raw receipts and exact per-game source/binary/bank/settings identities are in
`loading-hitches/coupled-memory-evidence.json` under the retained task root.
Continue the existing bounded per-game before/after plan when a host is eligible.
Do not infer a timing win from callback counts or passing correctness checks;
reject a reproducible regression in either affected game. Owner review and
delivery remain open.
