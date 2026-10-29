# Remaining native instruction fallback decisions

Area 05; owning issue `beads-yjp.108`; campaign `beads-2wy8.1`.
Final proposal dispositions for selected Windows MKDS/MPH packages are below.
Independent durable handoff verification remains pending. No production changes
or new implementation PR are proposed.

## Instruction and control inventory

| Family / complete proposed contract | Final decision |
| --- | --- |
| Existing N02: pure CPSR reads, flags-only writes, stateless CP15 cache maintenance, safe nonflag ARM7 MUL. | Reuse the retained MPH package and MKDS rejection. Matched MPH CPU improved 13.72%, throughput 4.31%; MKDS CPU increased 2.91% and 4.64% in reversed order. Do not repeat unchanged tests. A new independently selected subset/unbundled profile is rejected for this campaign: no new demonstrated benefit justifies another qualification/selection contract. This does not establish an individual subset's regression. |
| 1. Remaining ARM7 MUL and architecture exclusions: ARM flags/conditions/aliases; Thumb N/Z and destroyed C; operand-dependent faithful retirement versus native timing2; v5-only CLZ/BLX. | Reject implementing additional ARM-flag/Thumb MUL lowering for lack of demonstrated recurring selected-workload benefit versus new flag/timing qualification. Current MPH census has zero MUL; both older cohorts contain only nonflag ARM MUL. Do not admit ARMv5 instructions on ARMv4T. Preserve current faithful ARM7 Thumb C-clear and undefined handling. |
| 2. MRS/MSR banking: SPSR, mode/IRQ masks, privilege, user/FIQ registers, exception entry/return and canonical/native bank ownership. | Reject a new banked/privileged native execution layer on benefit versus contract cost. SDK status lowering covers arithmetic/status fields, not the engine's complete bank/mode/IRQ behavior. Existing control transfers remain faithful; unchanged pure subset follows N02. Dominant remaining MSR counts identify real control effects, not safe flags-only instructions. No new layer was implemented/tested. |
| 3. CP15/coprocessors and ARM 0b11 admission: control, MPU/TCM views, WFI, maintenance, mapping/invalidation and device effects. | Retain only N02's exact stateless maintenance subset for MPH. Reject a broader native coprocessor layer for this campaign: it must still enter actual mapping/event/halt services and has no demonstrated useful complete replacement. Do not weaken the encoding barrier. Actual device/scheduler algorithms remain 11/13. |
| 4. SWI/undefined, SDK CallSVC/ExceptionRaised: exact instruction retirement, exception PC/LR/SPSR, bank switching and BIOS/handler effects. | Reject replacing this with a second native exception controller. Reuse faithful exception ownership and prior complete BIOS decisions (02/03); a generic SDK exception callback is not a complete DS replacement. No opcode-count claim proves useful end-to-end savings. Unknown/unsupported encodings remain faithful. |
| 5. SWP/SWPB: entire read/exchange/write, old-value destination, byte/word/odd alignment, aliases, device side effects, provenance and one completed instruction before observation. | Reject simply admitting SDK SWP: its source explicitly leaves bus locking/alignment unresolved, whereas current bus access/publication and instruction completion must be preserved. Reject a new dedicated exchange lowering for this campaign on demonstrated benefit versus full bus/invalidation/retirement contract; none of the reused selected cohorts establishes a worthwhile remaining hot exchange job. The previously hot streaming decoder is already replaced as a complete job under 10, not counted as a fresh SWP win. |
| 6. LDRD/STRD: ARMv5 register restrictions, two ordered word accesses, alignment, base/writeback aliases, MMIO/publication and completion timing. | Reject enabling SDK doubleword lowering unchanged: its 64-bit atomic IR and restrictions are not a demonstrated match for the engine's two-access faithful contract. Reject a new paired-transfer lowering on unsupported useful-benefit versus validation/semantic cost for these workloads. Preserve faithful behavior; no claim that a future proved hot transfer cannot justify it. |
| 7. Thumb BL/BLX pair and isolated suffix resume: prefix LR, separate suffix PC, interworking and scheduler continuation. | Retain valid existing pair translation and reject bypassing suffix admission. A suffix can be a genuine resumed PC and must not be consumed as a newer Thumb32 instruction. Existing focused pair/suffix checks already cover that distinction. A separate suffix-native continuation implementation is rejected on benefit versus additional decode/link/retirement contract. |
| 8. LDM/STM S-bit and base-in-writeback-list: user banks, exception return, register order, base alias, ordered bus transactions and observation. | Reject unguarded SDK admission: user/exception variants already return to interpretation, and some writeback overlaps are SDK-unpredictable. Reject a new complete banked block-transfer lowering for this campaign on benefit versus its complete contract; retain ordinary already-supported block transfers. |
| 9. Memory rd15 and other flag-setting PC writes: ARM7/ARM9 interworking, pipeline alignment, full SPSR-to-CPSR/bank return and memory ordering. | Reject removing guards or adding a second PC/exception-return implementation without demonstrated useful benefit. Preserve real faithful interworking/exception behavior; ordinary native branch arithmetic remains supported. These are not simply arithmetic operations with an extra destination register. |

Cost/benefit rejections above are final decisions not to implement those identified
proposals in this campaign, not deferred experiments or claims of impossibility.
They do not assert unmeasured performance regressions. Nine families plus N02's
reuse/subset decision exhaust the identified instruction proposals.

## Intentional exits and reusable alternatives

Known service entries/continuations are proof/dispatch barriers even when their
first instruction is ordinary STM/LDR. Removing those boundaries would bypass
positive and negative code proofs. Whole-service replacements belong to their
own jobs, not a missing-instruction count. Code mapping/provenance/host admission,
deep trace and instruction hooks, endian and deferred-cycle conditions also
retain their existing safe fallback. They are not additional opcode gaps.

The complete cross-backend ownership replacement was evaluated/rejected under
04; generated/faithful linking remains 06, coverage/lifecycle 07/09, and actual
event/IRQ/HALT/DMA algorithms 11/13. Generic SDK reuse was considered for each
family; merely removing guards would preserve neither the original CPU contract
nor a complete native alternative. No machinery is retained as a performance
requirement solely because it already exists.

## Existing per-game evidence and the one new decision sample

Reused N02 source landed at `52f241464b73201c526b86b0584072623b37b140`.
The original measured builds identify as `5e6b973-dirty`; frozen executables,
configurations and logs are authoritative, not an invented clean-source identity.
MPH candidate SHA256 `27314f4151083536868790f9f031aafa751f98afb22fcade7746f0d1faa0a18a`;
MKDS candidate `292c9ecf57c4bbd5b40bacf2f181d00c5e2717760270cb9a66eba941e3b195b8`.
The preserved broader SDK patch also includes later rejected helpers and is not
described as equal to the measured N02 package.

Raw N02 evidence root:
`G:/Temp/Codex/ndsrecomp/native-services-20261026/followup/`.
`nas-control-results/result.json` and per-game `measurement.json` preserve the
matched 600-frame results; `early-results/nas-control-reverse-results/` preserves
the reversed MKDS check. Control manifests, configure logs, focused ARM9/ARM7
probe logs and profile inspections are retained. No old experiment was rerun.

New budget: one accepted-package MPH room-92 120-frame diagnostic after fresh
same-build setup and 120 warmup frames. Binary is accepted published-store
`17b0a9e85aa6ba4612a43a169bef62f88e9ffbd3fbcb9a9785a0025adbadcb1f`, source
`d77cda64941589ac920c4b3bf202eb6a668a6ad5`, with accepted RAM/stores enabled,
native GL 1x and native control. Rejected area04 code is absent. The locally
admitted capacity lease retains CPU/GPU/RAM/storage telemetry; no timed A/B,
new screenshot, owner review or routine restore sweep was collected.

Recorded InterpreterFallback stop deltas equal census hits: ARM9 62,358 at 24 PCs;
ARM7 25,279 at 22 PCs. Both have zero drops and valid queried bytes. ARM7 queried
groups are MSR 16,946; SWI 4,420; LDM 1,309; LDR 1,269; SUB 459; STM 438; MRS 438;
MUL zero. The older 600-frame cohorts contain only nonflag ARM MUL: MKDS 158,404
hits at 22 PCs, MPH 111,497 at 12 PCs. This rejects the distinct proposed extra
Thumb/flag-setting coverage on available selected-workload evidence, not a
universal statement that either game never uses those instructions.

Census keys are historical PC/Thumb/hits; opcode labels decode query-time RAM.
Changed overlays cannot be treated as historical opcode proof. Before/after
states are retained as supporting code identity, not proof that no transient
bytes changed. InterpreterFallback totals do not count every possible SDK
exception exit. Historical MPH ARM7 invalid aliases were independently recovered
from CRC-valid states using WRAMCNT/mapping; default invalid labels were ignored.
Body times and counts are not an upper bound on cross-boundary optimization.

## Durable records

Current evidence root:
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/native-cpu-fallbacks/`.
`decision.json` records final proposals, raw identities/hashes and per-game evidence;
`mph-floor-census/` contains raw current census, settings, own states, logs and
host observations. `validator-historical-arm7-recovery.json` retains independent
state CRC/mapping/word recovery. No production implementation, temporary build
or delivery archive changed. Keep this bounded evidence for the campaign;
remove disposable setup state/profile scratch after final evidence retention.
Central Beads is the rejection handoff; a successful-code PR does not apply.
