# CPU execution boundary decisions

Area 04; owning issue `beads-yjp.60`; campaign `beads-2wy8.1`.
All identified proposals have final dispositions for selected Windows MKDS/MPH
packages. Independent handoff verification passed; see the retained validator assessment. Generated execution (06),
scheduler algorithms (11), device transactions (13) and other memory/publication
opportunities (08/09) remain open.

## Complete inventory

| Proposal and contract | Evidence and final disposition |
| --- | --- |
| A. Native physical RAM/publication: admitted views, aliases, widths, mapping epochs, write provenance, physical generations and executable protection. | **Retain** previously accepted implementations and reuse their focused/per-game checks. No unchanged rerun. See [RAM](native-ram-transactions.md) and [published stores](native-published-stores-20261028.md). |
| B. Actual-callback metadata: private IR/backend/config transports PC, read kind and instruction prefix only on real slow callbacks instead of unconditional memory-instruction publication. | **Reject** the exact transport and implementing a separate no-clock variant for this campaign: demonstrated useful benefit does not justify extending/maintaining the SDK contract. The tested service-clock-on transport was neutral in MKDS (about 0.6% fewer cycles); MPH was unmeasured. The new no-clock variant was not implemented/tested. No MPH regression or universal disproof is claimed. D does not remove metadata stores. |
| C. Live clock/read ownership: Run/Step stack counter lifetime, instruction prefixes, once-only partial-tick settlement, audited observational reads, deadlines, consuming/write exits, IRQ/HALT/DMA, mapping/publication fences, fallback and fixed simulation profile. | **Reject** this exact live-clock/read-whitelist package: added timing/settlement/profile contracts and unresolved full-package costs do not justify adoption. Same-clock snapshots matched; old/new-profile starting interleaving and unequal load prevent full-package qualification. This does not prove the clock intrinsically wrong. Complete timing-safe device/scheduler replacement remains 11/13. |
| D. CPU frame/dispatch/reentry/exception ownership: retain R0-R14 within an original scheduler slice; acquire before reference/HLE, exception, observer, unwind and scheduler consumers. Also consider a complete replacement of service lookup, guard and reentry interfaces. | **Reject** measured lexical frame retention for insufficient demonstrated useful runtime benefit versus a new ownership/materialization invariant. **Reject** implementing a new cross-backend dispatch/guard/unwind ABI for this campaign on unproven benefit versus broad semantic/generated-bank cost; it was not implemented/tested. Required natural reference/HLE exits stay. Generated execution/linking remains 06. |
| E. Larger quanta/deadline caches: preserve actual shared-memory/device observation points. | **Reuse final rejections** of unchanged larger-window and deadline-cache experiments. No rerun. See [native services](native-services-20261026.md), N10. |

These decisions resolve identified proposals, not every possible future CPU design.

## D source and contract

Rejected private branch: `perf/cpu-device-ownership-20261029`. Measured source:
`1b5c3d0942ce62376c52b13a730f988df65796f2`; `ed1c992` subsequently changes only
a contract comment. SDK remains `269cdede97414887ec52e0788abb32d0ddabe1dc`, archive
SHA256 `6a843c4287f5b6519cd27de38d26e1593ccad8a9c6329e5cf52cfd0867f576b8`.
Continuation branches from accepted `c0877de`, with none of D's runtime hooks,
counters, feature option or tests carried forward.

Each native return publishes PC/CPSR. A retained owner holds only R0-R14;
acquisition copies only those registers. Every canonical control writer must
relinquish ownership first. Ownership clears during SDK Run. Full acquisition
precedes faithful/HLE dispatch, IRQs, slow tick/yield observers, unwind capture
and restore, trace changes, invalidation, declined admission, scheduler saves
and scope exit. Nested scopes cannot retain. Normal callbacks use PC/control;
the existing live write callback publishes LR. Timing, quanta, event ordering
and compiled-bank ABI remain unchanged.

The interface audit also covers service lookup, call-stack clearing and reentry
checks. Current MKDS attribution has runtimeDispatch 3/431 self samples and
HLEis_entry 7/431; context switching is 5.83 ms of 1,734.7 ms active emulation.
`runtime_call_stack_restore(nullptr,0)` sets depth to zero without an element-copy
loop. Reentry checks preserve event, IRQ, reference-floor and observer contracts.
These observations select the measured frame experiment; they are not universal
upper bounds or proof that a broad ABI rewrite would win.

## Correctness and per-game cost

Real-engine OFF/ON processes each pass 268 checks, producing identical 8,972-byte
full CPU/banked-register/RAM/time/instruction transcripts. Coverage includes both
cores, ARM/Thumb, nested synthetic service dispatch, IRQs, reference fallback,
trace/observer exits, invalidation, nesting and real scheduling with controlled
HALT/DMA. Native, SDK, runtime, bus/CP15, Tier3 and scheduler are real; devices
and the service body are controlled fixtures, not actual title HLE proof. Four
probe/RAM/store checks pass. MKDS current-build frontend save/load passes in both
pairs; MPH restores a fresh same-build state before each interval. Candidate
images show plausible track/HUD and MPH scene output.

Both game binaries retain accepted RAM and published stores. Only process-fixed
frame retention changes. One pair per game was budgeted; MKDS received one reverse
repeat for a measured 4.17-core ambient jump. No tuning or matrix followed.

| Workload | Original -> candidate | Background cores | Interpretation |
| --- | --- | --- | --- |
| MKDS compute 4x/AA8, 180 construction frames | Emulation cycles 7,214,746,280 -> 7,768,442,940 (+7.67%); paced mean 16.754 -> 16.760 ms | 0.662 -> 4.837 | Inconclusive under unequal load |
| MKDS reverse order, same binary | Emulation cycles 7,901,171,410 -> 7,690,056,340 (-2.67%); paced mean 16.701 -> 16.822 ms; process CPU +3.59% | 2.913 -> 2.273 | Neutral/inconclusive, no demonstrated useful win |
| MPH native GL 1x room 92, 600 frames after same-build state and 120 warmup frames | Process cycles 16,079,101,380 -> 17,857,641,540 (+11.06%); CPU 3.40625 -> 3.515625 s (+3.21%); wall 2.46701 -> 2.90270 s (+17.66%) | 3.354 -> 4.451 | No demonstrated useful benefit; load prevents a reproducible-regression claim |

MKDS queries have asynchronous boundaries and differing native instruction
totals; those game pairs do not prove exact guest equivalence. MPH semantic
ARM9/ARM7, event, math and submission deltas match; its scene matches except the
established debug VBlank origin. Room 92 includes informational dialogue, not
all combat. Quiet-host flags remain false; capacity admission is separate.
SERVER declined MPH before any launch; the whole pair then ran locally.

More than 5.5 million ARM9 frame reuses in MKDS and 1.5 million in MPH establish
fewer copies, without establishing useful runtime benefit. This is a final
cost/benefit rejection, not a claim of a proven general slowdown.

## Retained evidence and handoff

Evidence root: `G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/`.
`cpu-device-ownership/decision.json` contains machine-readable decisions and
identities. Its `current-mkds-attribution/baseline/`, `mkds-pair/`,
`mkds-reverse-pair/` and `mph-pair/` retain raw samples, telemetry, settings,
states, logs and images. Summaries: `mkds-evaluation.json`,
`mkds-reverse-evaluation.json`, `mph-evaluation.json`. Build identity JSON files
retain source, SDK, bank, executable and CMake configuration/cache hashes.

MKDS executable SHA256:
`dcd9308ae3bd2d8d84d4a22744facbc6943eaf02e28025c9d556fdad4ddabe7b`.
MPH executable SHA256:
`a068f4ce91730837bd8335d1ea845337f58cd65137734b69afd95d4e3869ea53`.
Exact helpers, jobs, capacity receipts and focused logs remain with the evidence.
The isolated `build-mkds-cpu-frame`, `build-mph-cpu-frame` and staged SERVER frame
binary are task scratch retained for campaign reproduction; clean disposable
outputs after retaining final evidence.

Central Beads is the rejection handoff; rejected D warrants no successful-code
PR. No merge, release, owner save/settings change or interim practical review.
