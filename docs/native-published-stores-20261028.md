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

Runtime evidence is pending. The initial measurement budget is one MKDS
construction OFF/ON pair, then one MPH active-room pair, each on one capacity-
qualified Windows host. Retain per-game raw samples, exact source/SDK/binary
identities and host conditions. No speedup or acceptance is claimed yet.

Task evidence directory:
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/native-published-stores`.
Keep exact binaries and evidence during the campaign; clean disposable build
scratch when it is no longer needed. The original SDK and historical binaries
are preserved.
