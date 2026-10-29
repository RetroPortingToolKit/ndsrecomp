# Generated and faithful execution architecture decisions

Area06, `beads-yjp.109`; shared component `beads-3qv.2.1`;
optimization campaign `beads-2wy8.1`.

Retain the shared canonical interpreter storage implementation. All identified
alternatives below have final dispositions; independent PR/tracker handoff is
recorded separately. This does not complete compilation lifecycle07, memory
ownership08/09, scheduler algorithms11 or compiler qualification27.

## Complete alternatives and dispositions

| Proposal | Final disposition and reason |
|---|---|
| A. One canonical per-core layout across generated C, native SDK registers/flags/banks, scheduler contexts and observers | **Reject this replacement on cost/benefit.** The SDK owns optimized register storage and an internal CPSR representation; generated C and scheduler/observer boundaries consume packed architectural state. Replacing all these contracts is authorized, but existing attribution and area04's measured frame-retention experiment establish no useful demonstrated benefit for that wider coupling. This is an unimplemented architectural decision, not a claim that a whole-layout replacement was benchmarked or that the current ABI is inviolable. |
| B1. Generated traces/blocks, local registers, aggregate accounting, guest LR/stack calls and linked exits | **Reuse the prior rejection of the complete flat-AOT architecture**, separately from the earlier small register-local pilot. Flat-AOT already replaced recursive host calls, grouped regions and revised checkpoint/accounting costs. Three qualifications failed to establish useful gains:49.19→42.30FPS;48.42→44.00FPS; then an apparent5% improvement invalidated by a cheaper Faithful A/A control. The small local-span repeat also cost3.25% more processCPU and0.75% more cycles. Enlarging either unchanged design is rejected. New shared-device/event algorithms belong11/13. |
| B2. Replace the native/faithful execution machinery with the upstream melonDS JIT and system model | **Reject this wholesale replacement on cost/benefit for the selected pipeline.** Its CPU/JIT/memory objects depend on the full NDS model, requiring replacement/integration of device ownership, existing HLE hooks, publication and frontend/state contracts. Existing native CPU execution is retained; no equivalent-feature result establishes a benefit that justifies this migration. This alternative was considered and rejected, not timed or declared intrinsically slow. |
| C. Canonical packed storage shared by generated/runtime and faithful execution | **Implemented and validated; retain.** Replace the full per-entry/per-instruction active-register and bank marshaling stream with a portable storage view. Preserve instruction-control publication, guest semantics, clocks, retirement and exits. MKDS is neutral in bounded measurements; MPH has a promising bounded positive observation, with load limitations below. |
| D. Shared semantic helpers and specialization instead of a second emulator | **Implemented together withC.** One templated instruction body serves the original structured API and the packed view. Both are compiled and checked. No second opcode implementation, new CPU model or host-specific assembly is introduced. Whole-bank LTO/compiler work remains27. |

Historical architecture evidence is in [the execution model contract](execution-model-contract.txt),
[performance results](performance-results-20261022.md), and the central
Beads area04 CPU-boundary decision.

## Storage and observation contract

Generated code and runtime retain `ArmCpuState` and its existing C ABI. The
view aliases R0–R14 and all bank arrays. PC and modeled packed CPSR form a
small local control projection, published at every unchanged Tier3 `sync_out`
site. Instruction-entry callbacks therefore retain the old PC/T/control state,
including multi-access and special-PC shapes. Reserved CPSR bits are dropped
exactly as by the old structured bridge.

Multi-access instructions can expose partially updated general registers to
diagnostics. Current functional bus/timing consumers use the preserved control
state; this diagnostic difference is explicit in the common API contract.
No new mid-instruction execution or external observation boundary is added.

The portable original `CPUState` API remains. `NDS_TIER3_CANONICAL_STATE` defaults
ON; OFF compiles the original storage path. A fixed process selector with the
same name (0/1) supports the regression comparison in a feature build. Selection
happens at entry, with no new per-instruction selector branch. Activation
counters increment only on entry and are exposed with the performance policy.

Scheduler deadlines, fetch/data/internal/refill costs, once-only deferred debt,
IRQ/SWI entry, code provenance, native/static takeover, call-return identity and
preserved host unwind follow the unchanged Tier3 loop. Native saves and current
state layout are unchanged; no old-state migration was needed.

## Focused correctness

The shared core [PR7](https://github.com/mstan/arm-recomp-core/pull/7) retains the
structured API and one semantic implementation. Its common sources were
unchanged between the old705e pin and upstream949357a; that upstream update
adds an independent GBA profile that NDS does not select.

The common CTest compares115,767 decoded instructions: every65,536 Thumb
encoding,50,000 deterministic ARM samples and deliberate edge cases. It checks
full active/banked state, memory, access order/value/control/cycles, reserved
bits, boolean/proxy assignment, Thumb aliasing and exception banks. This is
storage equivalence, not a new hardware-accuracy claim.

The consumer CTest runs the same real Tier3/runtime/bus/CP15/scheduler binary
with original and canonical storage. Controlled device fixtures cover both
cores, failed conditions, ARM7-specific Thumb MUL carry, multi-access/aliases,
SWI/IRQ, debt, HALT/DMA/event stops, exact observers, interworking returns and
nested static unwind. Each mode passed3,606 checks; full33,364-byte state,
memory, instruction and control/timing transcripts match SHA256
`a79a7c141ec75082bc4311cf0b4e0740c62d4c003c7d4e9cc255a82ed2b500df`.

The isolated consumer PR is based on frozen current main
`2d9f4f3cb33daa78a724a1c17fdbcc2048580c2c`. Its focused build/CTest is checked
separately; built-game qualification used the combined accepted development
foundation plus this change. Execution source is identical; the isolation
omits unrelated foundation changes from this PR.

## Bounded game measurements

One accepted-versus-complete-candidate pair per game was authorized. One
additional reversed MKDS pair resolved a concrete mixed-cost finding. No
matrix, extra gameplay sweep or owner playtest was run. Both pairs use the
same physical host/configuration within a capacity reservation. Required
CPU/GPU/RAM/storage telemetry passed admission; historical quiet-host gate
fields remain diagnostic, not fabricated passes.

| Workload | Candidate cost relative to accepted | Interpretation |
|---|---|---|
| MKDS180 construction frames, compute4x/AA8, fresh menus | Emu cycles−2.925%; processCPU+33.333%; frame cycles+11.125%. Ambient2.579→2.200cores. | First mixed result; no win declared. |
| Same MKDS workload, candidate first | Emu cycles+1.180%; processCPU−3.623%; frame cycles+2.518%. Ambient2.563→2.016cores. | Large process penalty did not recur. Emu/process costs reverse sign; paced elapsed remains approximately neutral. |
| MPH600 room92 VBlanks, nativeGL1x,120 warmup, each build's own state | Process cycles−7.238%; CPUtime−10.407%; elapsed−7.698%. Ambient2.659→1.798cores. | Promising bounded positive observation; lower background load prevents assigning the entire change to this implementation. |

MKDS is **neutral**, with no isolated gain established. Presentation cycles
were higher in both samples, but varied substantially while whole-process
costs reversed; this does not establish a reproducible per-game regression.
The presentation/driver lead remains20. MPH instruction, guest-cycle, event,
math and submission deltas matched, including49,727,185 native instructions,
2,890,417 entries and12,752 submit calls. Its dialogue/room sample does not
qualify a full campaign or multiplayer session. Small per-game observations
are not a universal percentage claim, and headroom alone is not evidence of
a performance win. No further runs were warranted.

One candidate image per game showed coherent output without obvious garbling.
This is basic sanity only. Owner review remains deferred until all remaining
campaign candidates have final decisions.

## Reproducibility and handoff

Measured framework `a41b285ce63f9a129e3a3b366686127476a0530f`, core
`90b6c510a7471c9c7146c0e8c8f5d9e6d7b13e57`, unchanged SDK
`269cdede97414887ec52e0788abb32d0ddabe1dc`. Final default-ON selection matches
the explicit ON configuration used in every candidate build.

MKDS accepted/candidate SHA256:
`8530d413ff5b2b25dd2b93f788232a93ad74751d6cec9bc6d456a3621723bd39` /
`52e40de48731ec2f219ad3a88c415a086e0bc70935417cd92cfb9b04cd41e102`.
MPH:
`17b0a9e85aa6ba4612a43a169bef62f88e9ffbd3fbcb9a9785a0025adbadcb1f` /
`f43c57e7c19d5860980ef35ba64e20d7fd4ee7486975c71091050585fe0a5232`.

Bank archives remain MKDS`be071f047ee081c9…` and MPH`0f1528fd878a494f…`.
Settings, full identities, raw frame samples, before/after counters, host
telemetry, focused logs and independent derivations are retained in the task's
private `loading-hitches/faithful-architecture` evidence directory and central
Beads. Native saves/settings and delivery folders were untouched. Disposable
object trees are cleaned after retaining binaries/evidence; source review
worktrees and needed regression artifacts remain until the handoff is complete.
