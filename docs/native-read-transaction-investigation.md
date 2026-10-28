# Native read and CPU ownership investigation

Owning issue: existing `beads-yjp.60`, related to `beads-0vo` and campaign
`beads-2wy8.1`. Its older v0.6.6 measurements are historical; they do not size
the current native implementation. This investigation couples execution,
memory publication, device reads and scheduling (burndown 04/08/09/11/13).

## Private first pilot

The first implementation keeps the guest instruction stream and original bus
reads, but retains native register ownership across audited observational
register reads. Unknown/stateful reads and all writes keep the old boundary.
A changed event limit forces a boundary even for an observational register.
The audited families include math, DMA controls, IRQ controls/status, beam,
keypad and IPC status. FIFO data, cartridge data, timers and unqualified graphics
reads are excluded. This is a private diagnostic executable with a process-fixed
off/on switch, not an integrated or release feature.

Both ARM9/ARM7 synthetic probes pass: actual callback PCs and widths, continuity
through two reads and arithmetic, stateful writes, a shortened event limit and
bounded polling. A matched 180-VBlank construction diagnostic uses the same
exact executable with its own fresh-menu fixtures and resident preparation.
MainRAM, ITCM, ARM7 WRAM and the observed scene match after the interval.

Native entries fall from 8,788,241 to 8,602,748 (185,493, or 2.11%). Device
handoffs fall from 5,551,888 to 5,355,904; only 196,019 reads enter this contract.
Most of the 5,044,237 reference device reads remain outside it. There is no
qualified timing result and no CPU speedup claim. This subset does not establish
a worthwhile complete ownership replacement.

The native instruction counter is 156 higher and reference stops four higher.
At the prepared start, serialized CPU, scheduler, core I/O and guest memory
bytes match, while ARM7 write generations, runtime instruction counters and the
GPU3D device payload differ. The GPU timestamp itself matches. These observations
do not prove a correctness regression, but they prevent a claim of complete
state equivalence or a host-only change. Resolve their meaning and use the
required common simulation profile/compatibility identity before adoption.

## Next architectural decision

Attribute the remaining reads in the actual loading phase; do not substitute a
racing-phase census. Evaluate a complete service-effect contract covering
device completion, guest clock/deferred costs, event deadlines, IRQ/HALT/DMA,
mapping changes and code publication. Native register ownership should last
until an actual required observation point. Preserve all real bus operations,
access widths and instruction completion; materialize state for CPU actions.
Do not grow a register whitelist from unrelated profiles or repeat the rejected
per-function DIV helpers.

The current release candidate remains `928da54`; this pilot is outside it.
Private source, compiler/link commands, exact executable, synthetic logs, RAM
receipts and limitations are retained in
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/read-transaction-evidence.json`.
No MPH application trial, visual handoff, performance adoption or delivery has
been performed for the pilot.
