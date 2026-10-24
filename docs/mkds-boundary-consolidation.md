# Experimental MKDS boundary consolidation

Five process-fixed switches, all default OFF, apply only to the optional native
MKDS Performance implementation. Faithful and non-native builds retain their
portable paths. None has a measured speed claim or integration approval yet.

| Switch | Implemented candidate | Boundary retained |
|---|---|---|
| `NDS_ARM9_DIRECT_READS=1` | Read-only direct page loads for aligned ordinary word/byte/block loads | All stores, halfwords, unaligned reads, devices and unmapped pages use callbacks |
| `NDS_ARM9_LONG_BLOCKS=1` | Arithmetic block cap 16 to 64 instructions | Memory instructions still end linked blocks; these already continue inside the JIT for RAM |
| `NDS_ARM9_DATA_HANDOFFS=1` | Continue after palette/VRAM/OAM data access | Original bus/video access, mapping controls and all MMIO synchronization |
| `NDS_ARM9_GX_BATCH=1` | Continue across complete aligned geometry command writes | Existing ordered modeled FIFO, every write/status/IRQ/DMA/stall effect; readbacks and non-command registers still halt |
| `NDS_ARM9_EVENT_WINDOWS=1` | Up to 256 system cycles while ARM7 is asleep and cannot wake or own DMA | Scheduled device/timer deadlines, all CPU9 MMIO accesses end the expanded interval |

The last option explicitly changes simulated scheduling. The arithmetic block
cap can also change instruction-budget overshoot. Coalescing data/GX handoffs
can give more accesses the same coarse virtual timestamp: ticks commit when
native execution returns, while commands and bus effects occur in order.
Their save-state identities include those choices. These are experimental
timing profiles, with live Wiimmfi
interoperability explicitly unverified (owner waived that test). Larger CPU
windows are never enabled while ARM7 is running. Direct reads use the existing
timing profile and still require checks.

Direct reads require pinned Dynarmic
`a41c380246d3d9f9874f0f792d234dc0cc17c180` plus
[`dynarmic-readonly-pages.patch`](../runner/cmake/dynarmic-readonly-pages.patch).
Apply that patch to the explicitly configured SDK source before configuring.
The patch adds opt-in read-only page-table lowering and a translation-time
eligibility filter to the x64 emitter; default SDK behavior is unchanged. It is
not a general all-architecture SDK feature. Optional native execution remains
Windows/x64 gated and checks CPU support before running. Builds without the
SDK retain the existing portable implementation.

The direct-read table mirrors the existing fast bus classifier, with a refresh
generation covering backing replacement, WRAM/TCM changes and restore. No
callback writes are bypassed: provenance, alias-aware code invalidation and live
write observation remain active. Existing ARM9 halfword lowering uses a wide
callback with private metadata, so those instructions explicitly stay off the
direct table. Word misalignment falls back for ARM9 rotation/alignment behavior.

The geometry probe uses the device's existing FIFO as the batch. Introducing a
second deferred queue would duplicate state and risk hiding stalls. It submits
each command at the existing virtual timestamp and stops on actual stall,
pending interrupt, DMA ownership, debug break, sleep, or a shortened deadline.
The larger-window probe's MMIO fence takes priority over geometry batching.

Each probe has its own control/candidate comparison with the other four OFF,
generation validation batching OFF and GPU window experiment OFF. Use the same
binary and race segment, recorded contention and no timing sampler. Reject
concrete correctness failures before timing. Combine only retained candidates.
Current-build save/load and native saves remain required; old snapshot
compatibility is intentionally unsupported. Owner approval precedes integration.
