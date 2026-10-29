# BIOS service candidate decisions, 2026-10-28

Owner issue `beads-yjp.107`, campaign `beads-2wy8.1`; burndown areas02/03,
with related bulk/audio/scheduler areas kept separate.

## Decision

Reject an additional native BIOS computational/control layer for the current
selected Windows MKDS/MPH packages. This is a final campaign decision based on
source contracts and the bounded profiles below. The current compiled BIOS
and existing HALT/event services stay. No runtime patch or performance win is
claimed. This resolves areas02/03; it does not resolve SDK/game asset jobs,
whole ARM7 voice/IPC services or the general CPU/device scheduler.

Reviewed whole-service alternatives: intercept a supported SWI before exception
entry and own its completion/return; bind complete service bodies behind the
existing verified BIOS/dispatch boundary; reuse portable copy/decode/CRC kernels
and physical-write publication. A body-only shortcut retains much of the entry
machinery. A whole replacement carries banked state, reentrant interrupts,
callbacks, store widths/aliases, memory publication and completion ordering.
The sampled work does not justify another layer with those contracts.

## Retained evidence

| Selected workload | Emulation samples | BIOS body self samples | Computational body self samples |
| --- | ---: | ---: | ---: |
| MKDS180 construction frames, existing decoder profile |549 |1 IRQ |0 |
| MPH300 active-room frames,120 warm-up |464 |2 (SWI vector + pitch) |1 pitch |
| MPH403 construction frames from own-build pre-load state |1342 |4 IRQ/SWI control |0 |

These are diagnostic samples, not an A/B, call counts or universal upper bounds.
MPH uses the same exact measured executable `cd2c64de` / engine `c24e2e5` and
retail BIOS; MKDS uses its retained FreeBIOS profile. The MPH exec tables fit
fully in256 returned rows with zero overflow. ARM7 has frequent BIOS sound-table
entries (783 of908 entry samples in the active interval), demonstrating why
entry frequency cannot stand in for CPU time. Larger voice-driver fusion stays
open in area21.

Construction starts at room45/frontend11 with no players and advances through
real loading to room92/frontend14/player creation. Health remains0 at the403-frame
boundary; this is construction attribution, not a gameplay completion/pass.
Only the existing fixture's room selection and A input are used; no success flags
are set. The existing active-room setup is reused without another route.

The first harness attempt failed before its interval because it assumed a
DebugClient.scene method. Its120 warm-up frames, logs and failed receipt are
retained; it contributes no accepted sample. Both accepted samples ran under
measured SERVER capacity reservations alongside unrelated work, hidden and muted.
Raw dumps are unchanged. Derived windows filter emulation-role samples by exact
run-command QPC bounds. Only the exact-SHA executable is remapped locally for
symbolization; SERVER OS/driver modules remain unavailable, avoiding false local
DLL symbol matches. Unwind failures, unresolved JIT frames and sampler overhead
remain in the raw/derived receipts. No historical gate is rewritten.

## Concrete computational candidates: all rejected

| Candidate | Reviewed complete contract |
| --- | --- |
| CpuSet / CpuFastSet (0B/0C) | Alignment, count/control bits, copy/fill and overlap order, bus width, aliases, executable-write publication. |
| Divide / square root (09/0D) | Signed quotient/remainder and zero/overflow behavior, unsigned root, returned registers and scratch clobbers. |
| Seeded CRC16 (0E) | Seed, input length/width and final CRC; backend-specific odd-length behavior. |
| Bit unpack (10) | Descriptor widths, zero handling/addend, packing order, output stores/publication. |
| LZ77 WRAM / VRAM (11/12) | Header/length, overlapping backreferences, byte versus halfword output, callbacks and completion. |
| Huffman decompression (13) | Tree/bit order, packed result and output width; FreeBIOS source has an existing stub, so this cannot be credited as a new optimization. |
| Run-length WRAM / VRAM (14/15) | Literal/run tokens, output bound/order and VRAM store contract. |
| 8-/16-bit differential unfilter (16/18) | Accumulator wrap, source/output widths, byte order and publication. |
| Sine / pitch / volume tables (1A/1B/1C) | CPU/backend-specific tables, indexing/scaling and caller results; full voice/IPC driver remains area21. |

Copy/decode/filter families have no sampled material cost in either construction
interval. Sound tables are observed, but a standalone BIOS layer has little
sampled work to remove; the full sound driver is a separate opportunity. This
is a final rejection of these proposals in this campaign, not proof that they
are unused by every game. FreeBIOS Huffman/other existing gaps are not credited
as performance work or silently corrected by this evaluation.

## Concrete control candidates: all rejected

| Candidate | Reviewed complete contract / existing implementation |
| --- | --- |
| Whole SWI entry/return | Banked registers, SPSR/CPSR, saved LR/SP, nested IRQ/SWI, return mode and deterministic completion. |
| IRQ entry/return | Guest ISR callback, software IRQ flags, saved bank/register state and exception return; guest ISR time cannot be charged entirely to BIOS. |
| IntrWait / VBlankIntrWait / HALT / STOP | IRQ masks and software flags, IME, HALT debt, timer/rendezvous wake ordering. Existing scheduler already has halt and whole-console idle fast-forward. |
| WaitByLoop | Requested delay, final register/flags, interrupt/preemption points; no sampled hot counted-delay body. |
| Boot / soft reset | Existing direct-boot host service bypasses firmware for the selected packages; optional LLE firmware boot remains untouched. |
| SoundBias / debugger / boot-proc / POST services | I/O effects, deliberate timing, returned identity/procedure values; no measured material cost, existing FreeBIOS gaps are correctness work, not performance gains. |

`runtime_swi`/`runtime_irq` own architectural exception entry/return with the
compiled BIOS. `scheduler.cpp` skips execution for halted cores and fast-forwards
whole-console idle periods to modeled events; CP15 WFI and ARM7 HALTCNT enter
that service. Replacing a parked-PC loop duplicates existing work. Counted-delay,
reset and miscellaneous services are not demonstrated hot costs in the selected
packages. Guest ISR execution and broader scheduler overhead are still real work
and remain separate opportunities (04/11/12/13).

Raw evidence and machine-readable candidate dispositions are retained at
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/bios-service-evaluation/decision.json`.
This directory also retains source scripts, exact dump/result hashes, telemetry
and failure receipts. Current Nextcloud and internal checkpoint ZIPs are unchanged.
