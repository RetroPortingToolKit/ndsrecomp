# Native read and CPU ownership investigation

Historical investigation. The remaining clock and metadata proposals now have
final campaign rejections in [CPU boundary decisions](cpu-boundary-final-decision-20261029.md).
The private prototypes below remain evidence, not active or accepted features.

Owning issue: existing `beads-yjp.60`, related to `beads-0vo` and campaign
`beads-2wy8.1`. Its older v0.6.6 measurements are historical; they do not size
the current native implementation. This investigation couples execution,
memory publication, device reads, waits and scheduling (burndown
01/04/08/09/11/12/13).

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

## Loading attribution selects the cartridge boundary

One exact-binary, own-checkpoint 180-VBlank address census accounts for all
5,044,235 slow reads. ROMCTRL at `0x040001A4` contributes 4,570,753 reads
(90.61%); the consuming cartridge data port at `0x04100010` contributes
273,920 (5.43%). There are 204 read slots. Other census maps overflow the shared
aggregate drop counter, but read hits exactly equal the independent slow-read
total. This is complete read coverage, not a zero-drop claim for all maps.

The dominant current loading boundary is cartridge-ready polling. Replacing a
whole SDK transfer with an atomic copy would also need ownership of ARM7,
events, IRQ/DMA and destination publication. The selected first replacement is
the common CPU/device transaction boundary: preserve actual instructions and
bus operations, while retaining CPU ownership until a real synchronization
point. Cartridge data, backup SPI, writes and shortened deadlines still stop it.

The first cartridge variant roughly halves native entries, but changes RAM and
instruction counts. It remains rejected for adoption. It exposed a missing
timing contract: SDK `AddTicks` normally publishes elapsed time when native
execution returns. Extending that execution can leave a later device action
using an old timestamp. A wider whitelist alone is insufficient.

## Common service-clock prototype

The private SDK now supports an optional CPU-thread-only view of the active
x64 Run/Step cycle counters. It publishes the pointers after stack setup and
clears them before return. Null configuration emits the existing path;
unsupported backends leave the view inactive and callers synchronize normally.
The code uses caller-save x64 registers under the supported ABIs and writes
caller data, without altering executable pages. This is not integrated into the
production bootstrap or selected release candidate.

The SDK counters cover completed blocks. Private IR metadata supplies the
current instruction prefix, so each real bus callback can publish the actual
elapsed guest time. Partial accounting is credited and settled exactly once by
`AddTicks`. Observation continuity requires an active clock, the audited read
contract and the original event budget; consuming operations and writes retain
their boundaries. The SDK builds and both actual ARM9/ARM7 probes pass,
including ARM/Thumb, failed predication, multiword reads, subsequent device
writes, exact callback timestamps, partial accounting and Run/Step lifetime.

One targeted same-clock off/on MKDS pair isolates ownership from the corrected
timestamp semantics. Both legs use the same exact executable, fresh normal
menus and preparation, with service clock enabled. **Every serialized prepared
starting section matches.** After 180 VBlanks, instruction counts, reference
stops, slow reads, callback totals, scene and sampled MainRAM/ITCM/ARM7 WRAM
match. No partial ticks remain unsettled. Native entries fall from 8,951,904 to
4,271,375: **4,680,529 fewer, or 52.29%**. Device handoffs fall from 5,551,880 to
785,566. Basic track/kart/HUD output is plausible. The subsequent same-clock
cost diagnostic below also establishes complete before/after snapshot equality
in both games; none of these checks establishes a qualified runtime speedup.

The shared MPH prototype also builds and passes one bounded 60-VBlank
functional interval with its selected native-control/math/submission profile,
including current-build save/load, both native CPUs, no unknown-write scans
and zero unsettled ticks. Its room/weapon/HUD output is plausible. No MPH timing
or off/on equivalence claim follows from that check.

The host remains busy with other game/compiler work. The first short CPU-cost
attempt could not open the shared measurement file, before any launch/timing;
nothing holding it was stopped or bypassed. Once available, a setup assertion
showed that restore leaves native mapping identity stale until CPU re-entry.
One setup VBlank re-establishes that identity before preparation; this interval
is excluded from both MKDS cost samples. This is not a natural loading-hitch
measurement.

The budgeted diagnostic then completes one 180-VBlank pair per game. Both use
the same exact executable/own checkpoint and keep the service clock enabled;
only native read ownership differs. **All complete serialized before and after
state sections match within each game pair.** Both performance gates remain
false:

| Game | CPU ms/frame, ownership off → on | Background mean difference | Disposition |
| --- | ---: | ---: | --- |
| MKDS, prepared construction | 13.2813 → 11.3715 | 0.0093 cores | Promising diagnostic under heavy load; unqualified |
| MPH, warm room 92/native GL 1x | 5.9028 → 8.0729 | 1.1521 cores | CPU increase unresolved under unequal load; unqualified |

Do not aggregate the games or promote from this result. These samples isolate
ownership with the clock enabled on both sides: they **do not measure the full
clock/ownership package against the old native path**. The previous instrumented
MKDS functional samples (2.140625 versus 2.078125 CPU seconds) remain historical
and inconclusive. Actual full-package per-game costs, including the MPH concern,
must decide adoption; fewer native entries alone are insufficient.

## MPH thread attribution follow-up

One follow-up MPH pair uses read-only thread CPU/cycle queries outside the
180-VBlank interval, with no suspension, affinity or priority changes. About
94% of the extra reported CPU time belongs to secondary threads: NVIDIA's
OpenGL driver and CRT-started workers whose exact roles are unestablished.
The main emulation thread uses 0.953125 versus 0.968750 CPU seconds, one
accounting tick apart. Thread totals account for all process CPU time.

Complete after snapshots match. Starting GPU3D payloads differ by ten bytes
at an equal ARM9 timestamp; other starting sections and guest execution/read
counters match. Heavy unequal background load still prevents qualification.
This weakens the claim of a large native-core regression without establishing
neutrality. Raw data and limitations: `loading-hitches/mph-thread-cost-evidence.json`.
The separate frozen-package lab comparison is described in
`docs/lab-loading-qualification-20261028.md`; it excludes this prototype.

## Remaining decision

Evaluate the complete service-effect contract covering
device completion, guest clock/deferred costs, event deadlines, IRQ/HALT/DMA,
mapping changes and code publication. Native register ownership should last
until an actual required observation point. Preserve all real bus operations,
access widths and instruction completion; materialize state for CPU actions.
Do not grow a register whitelist from unrelated profiles or repeat the rejected
per-function DIV helpers. Obtain one matched purpose-driven pair per selected
game against the prior production path when the measurement lock and host are
available; include clock overhead and resolve the MPH cost concern. Use actual
CPU/frame costs to decide whether this implementation is worthwhile. Do not accumulate
more local variants from the entry-count result alone. Before adoption,
integrate supported-host fallback, a fixed simulation profile and compatibility
identity; do not claim cross-build or cross-profile equivalence.

The current release candidate remains `928da54`; this pilot is outside it.
Private source, compiler/link commands, exact executable, synthetic logs, RAM
receipts and limitations are retained in
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/read-transaction-evidence.json`.
The cartridge/clock source, incremental SDK patch, compiler/link commands,
exact executables, fixtures, synthetic probes and machine-readable per-game
receipts are in `loading-hitches/cartridge-transaction-evidence.json` under the
same task root. No owner handoff, performance adoption or delivery has occurred
for these private prototypes.
