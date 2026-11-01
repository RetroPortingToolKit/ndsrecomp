# Native physical RAM transactions

Owner: `beads-yjp.60`; campaign: `beads-2wy8.1`.

One post-decoder MKDS loading attribution (exact runner751b/source993ce70,
180 fresh-menu construction frames, compute4x/AA8) found recurring native-entry,
provenance and RAM-callback work. The decoder itself was a small self-time share.
The derived dump retains549 samples in the exact emu-active frame intervals;
18.0% are unresolved JIT addresses. These are diagnostic shares, not an A/B win.
Raw data, filter provenance and caller stacks are in the retained task directory
`loading-hitches/decoder-host-attribution/baseline`.

This package gives native code admission, instruction fetch, callback reads and
stores one instruction-local physical RAM view. It reuses the existing bus fast
windows, their per-byte provenance and physical generation cells, and the common
`nds_busf_note_write` publication authority. No view survives a callback, CPU
switch, map refresh or store publication. Mapping is obtained anew per access;
this adds neither a persistent shadow map nor a speculative executable cache.

The view accepts only naturally aligned1/2/4-byte ordinary RAM. The model's TCM
boundaries are at least512-byte aligned; shared-WRAM splits and mirrors are
larger. Such an access cannot cross a mapping boundary. Unaligned admission,
MMIO, unmapped, missing provenance and diagnostic accesses keep their existing
paths. Four-byte native-entry provenance rules are unchanged, including Thumb
entries at halfword offsets. Reads preserve ARM block/unaligned rotation and
signed-halfword behavior. Stores preserve the live-context observer, exact
bytes, provenance, generation wrap, all-CPU notification, active-code guards
and the writer's immediate self-modification check. Guest cycles, CPU scheduling
and service simulation profiles do not change.

`NDS_NATIVE_RAM_TRANSACTIONS=0` selects the original native path for a same-binary
comparison; the selection is fixed on first use and appears in `arm9_native`.
It is an engineering selector, not a launcher setting. The whole package stays
private until its per-game built runtime costs are assessed.

`native_ram_transactions_test` compares the view against the real slow bus for
both CPUs, all four WRAM mappings, reset/normal/moved/minimum-size TCM views,
unaligned/device rejection, noncanonical nonzero provenance flags, every missing
provenance byte, native stores and generation wrap. It also checks state-import
map refresh and the actual dispatch guard for unrelated data, same-page data,
mirrored executable writes and nested active callers. The no-SDK test link is a
portable build check, not complete non-Windows game qualification.
