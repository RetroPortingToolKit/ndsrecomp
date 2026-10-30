# Memory access and mapping architecture

Area08; owner `beads-yjp.111`, campaign `beads-2wy8.1`.
Retained runtime source `cc586c93b32c7bfe168e46c532988af381c3b1ff`; rejected callback prototype
`b688ba801e926a47a8f264c19433ecc32acc051f`.
**The area is open:** PR and independent
durable handoff gate are outstanding. The following records the complete
identified alternatives; it does not advance the campaign.

## Complete service and alternatives

The service owns one normalized CPU memory access, from CPU-specific mapping
through real width-specific device dispatch or physical RAM bytes, observation,
provenance and executable-write publication. Instruction semantics remain with
the CPU: a raw bus operation cannot decide LDR rotation, signed odd-half behavior,
LDM semantics or exception completion. Every producer remains covered: native
callbacks/direct pages, generated/BIOS banks, faithful execution, bulk HLE,
DMA/SPU/device masters, debugger/trace, and host backing/reset/import operations.

| Inventory | Proposal and decision |
| --- | --- |
| A: physical transactions | Retain accepted ordinary RAM and published-store ownership from `.60/.102/.103`. The new candidate carries both successful and failed RAM classification through the complete native callback, exact-width device fallback and write publication. Final decision: reject this new callback package; retain only the unconditional page proof. Two MKDS orders failed to establish neutral-or-better net cost. A new MPH-only/narrowed package is also finally rejected for absent demonstrated useful isolated benefit versus another selection/maintenance contract; it was not separately measured. No unchanged accepted pilot is retested. |
| B: widths/alignment/families | Retain current byte/half/word, signed/unsigned and ordinary block-transfer coverage. Reject blanket removal of SDK alignment or instruction filters: an odd signed half is a byte access, an unsigned half aligns, single-word LDR rotates and block words do not, stores align down, and doubleword callbacks comprise ordered words. Existing tests exercise these contracts. Additional SWP, paired-transfer, privileged block and PC/exception families already have final complete decisions in area05; reuse them. A new second width/instruction layer without a demonstrated remaining hot job is rejected on benefit versus its additional retirement/ordering contract, not asserted to be impossible or measured slower. |
| C: shared mapping/page proof | Retain the common physical window/provenance authority and the new shared read/store whole-page proof. It closes the actual imported subpage-DTCM overlap defect independently of the callback selector. Reject replacing the SDK flat tables with a sparse/two-level lookup for this package: its public interface consumes flat 1,048,576-entry arrays; a real replacement changes the SDK's emitted access path and adds another lookup to recurring accesses, whereas the current rebuild is epoch-gated. Reject incremental refresh and a larger persistent shared mapping graph on current benefit evidence: source and retained sampled stacks do not identify transition rebuild as the recurring cost. These are cost/benefit decisions, not measured runtime-regression claims. The selected change shares the safety proof without adding another map or retaining instruction-local pointers across callbacks. |
| D: generated/reference timed access | Retain existing CPU-specialized static-inline data and timing helpers. Reject a universal combined data/timing descriptor: timing intentionally uses programmed TCM spans while backed data uses physical limits; CP15 cacheability, sequential history, GBA slot ownership/waitstates and live Wi-Fi registers are additional timing inputs. A correct descriptor must preserve those distinct decisions, including slow/traced behavior. Reject a new fused generated ABI/codegen service for the selected packages on available attribution versus benefit, after considering complete regenerated banks; generation time or code size is not the rejection reason. Most measured execution is already native, existing common paths are inline, and the retained native-memory profile selects repeated callback classification instead. No bank regeneration or ABI change is needed for the selected callback/page change. |
| E: device-backed windows | Reject a universal raw-pointer mapping of VRAM/palette/OAM, BIOS, cartridge/GBA and Wi-Fi as a complete replacement. Existing source contracts include overlapping/remapped VRAM, CPU/width write rules, guest capture/readback, firmware/BIOS visibility, register-dependent timing and device publication. These cannot be inferred from a backing pointer. A second generic descriptor/dispatch facade that still invokes those algorithms is rejected on absent useful algorithm removal. Retain actual device services, invoked once at normalized width by the retained accepted callback implementation. Complete changes to transfers, GPU resource ownership, capture, 2D and sound stay explicitly in13/16/17/18/19/21; none is declared resolved by this access-layer decision. |
| F: whole host mapping/fastmem or replacement service | Evaluate the full alternative, not just earlier callback-metadata experiments. Reserve separate CPU virtual views, map physical aliases, remap WRAM/TCM, route MMIO faults and widths, protect executable aliases, retain live-write/provenance publication, trace semantics, restore/reset and portable fallback. Reject that new architecture for this package on available benefit versus its full machinery: ordinary native reads already use direct pages, published stores still require physical metadata and observation, and only10,856 of5,202,120 retained MKDS callbacks were ordinary RAM. Remaining callbacks predominantly need actual device services, which fault-based fastmem cannot remove. Host mapping also cannot be the sole representation of imported subpage overlays or the tested small WRAM splits; this Windows host reports64KiB allocation granularity versus4KiB pages and16/32KiB WRAM. A complete design needs subpage fallbacks, store-publication codegen and remap/fault ownership in addition to virtual mappings. SDK fastmem and failure recompilation were considered; they supply neither DS publication nor this lifecycle. Reject a new backend-independent region-service object graph for the same package: the existing shared windows already supply that RAM authority, and generic routing alone does not replace the expensive device algorithms. No unimplemented fastmem speed or regression is claimed; virtual address reservation is not falsely counted as committed RAM. |

These rejection decisions apply to the identified proposals in the selected
Windows MKDS/MPH campaign packages. A distinct, proved hot complete service in
the remaining areas retains its own evaluation. The change does not require
old save-state compatibility, introduce a new scheduling/timing profile or
authorize changes to native saves.

## Evidence and scope

Evidence root:
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/memory-mapping`.

The reusable current-native-path MKDS profile has431 emulation-active samples,
17.4% unresolved. Native memory files are byte-identical between its source
`d77cda6` and pre08 accepted `853c059`. It selects classification/callback work;
zero named refresh/timing-helper self samples do not prove zero cost or bound
inlined/rare work. Callback counts are not CPU-share measurements.

Prototype evidence is retained:126,580 actual RAM checks,4,891 store-page checks
and actual ARM9/ARM7 native probes, both full runners built. Coverage includes
all WRAM modes/CPU aliases, TCM moves/import, interior imported DTCM overlap,
alignment/sign/rotation, exact-width device fallback, physical provenance,
code-page aliases, live observations and tracing. Final selected source cc586c9 rebuilt both games and passed115,125 actual RAM
checks,4,891 store-page checks and both actual ARM9/ARM7 native probes. Actual
bank and linked SDK hashes match their frozen identities; receipts are in
final-page-proof/verified-artifacts.json. Independent source review
found no introduced correctness blocker. No unchanged test was repeated merely
to satisfy a new session.

Sources examined: `runner/src/native_ram.h`, `native_store_pages.h`,
`arm9_native.cpp`, `bus.cpp`, `cp15.cpp`, `recompiler/armv4t/runtime_arm.h`,
the frozen SDK's `src/dynarmic/interface/A32/config.h`, relevant real-bus tests
and area05's complete instruction decisions. This is access-layer source review;
it does not qualify the unfinished graphics pipeline.

## Recovered MPH pair

Both timed legs completed previously on SERVER; comparison failed afterward
because its helper expected nonexistent `package`/`framework_head` metadata
instead of the actual identity's `source`. Original failed helper/output and
capacity receipt are preserved. Nineteen existing raw JSON/log/PNG files were
retrieved and manifest/hash-verified; no game or pair was repeated.

Same executable SHA256
`31a83295e9bea395a8a7d76a0ddf45fc0d46ea8bbe9a80658d2361e540e7e9f5`,
nativeGL1x,120 warmup/600 measured VBlanks, own-build prepared/loaded states,
native RAM/published stores/canonical Tier3 on, callback selector0/1.
Both before/after scenes and exact guest event/native/math/submission deltas
match, including49,727,185 native ARM9 instructions each.

| Cost | Callback reference | Owned callback |
| --- | ---: | ---: |
| Process cycles |16,553,917,870 |15,527,885,802 |
| CPU ms/VBlank |7.786458 |7.630208 |
| Uncapped interval seconds |3.629259 |3.583156 |
| Background CPU cores |0.838828 |0.572026 |
| Ending private bytes |424,423,424 |424,411,136 |

Observation:6.198% fewer process cycles,2.007% less process CPU and1.270%
less elapsed time. Headroom passed both legs; unequal background load and a
single short sample prevent an isolated percentage-win claim. There is no
observed regression in this interval. Its positive observation cannot offset the
MKDS adoption gap; the new callback package is finally rejected.
Basic candidate image inspection shows coherent weapon/enemy/HUD and dialogue,
without obvious garbling. This is not final owner gameplay acceptance.

Raw files: `recovered-server-20261030/mph-pair/{baseline,candidate}/result.json`,
candidate `A.png`; exact analysis in `mph-recovered-analysis.json`.

## MKDS setup repair and pending sample

Zero timed legs existed in the failed original run. The remote staging helper
matched only MPH's no-space PATH expression, leaving MKDS pointed at a local-only
v0.0.11 directory that does not exist on SERVER. Exact PE import inventory found
five non-system dependencies including SDL3's transitive `libiconv-2.dll`.
These were staged privately and hash-verified under remote `runtime-area08`.
Old helper/failure remains; new helper changes only that runtime path and uses
new output `mkds-pair-2`, with compute4x/AA8/acceleration assertions retained.
See `mkds-runtime-repair.json` for imports and exact file/helper hashes.

The planned pair subsequently ran on the admitted local PC (CPU2/GPU50/RAM3GiB,
the retained original local budget; SERVER requires GPU90). Acceleration4x/AA8
was asserted before timing. The same exact executable and accepted features were
used with callback selector0/1; the candidate image is coherent track/kart/HUD.
First pair: emulation cycles-1.392%, whole-frame cycles+13.590%, wider process
CPU+21.138%, paced frame wall-0.226%; background1.062vs0.920cores. Extra CPU
was concentrated in presentation; recorded CPU-wall waits migrated from swap
into GL error checking. Those timers are not GPU timestamps.

One additional reverse-order pair was justified solely by that actual mixed
outcome. Same local host/binary/settings: emulation cycles+20.325%, whole-frame
cycles+23.563%, wider processCPU+25.874%, paced frame wall+0.128%; background
2.783vs1.043cores. The load difference prevents an isolated causal regression
percentage. Construction scenes match across all four legs and each selected
180-row guest span differs by at most two cycles. Wider polling counters are
not exactly those180-row measurements. Driver checking was costly in the reverse
reference too, retaining the existing20 driver-ownership lead.

**Final decision: reject deploying the new global callback transaction package.**
It does not establish useful per-game-neutral net performance, and MPH's bounded
positive observation cannot offset MKDS. This is a final benefit/qualification-cost
decision, not an indefinite hold or a claim that every CPU increase was caused
by the selector. Reject separately deploying an MPH-only/narrowed version for
absent demonstrated useful isolated benefit versus another selection/maintenance
contract; that variant was not independently measured. No third pair is needed.

The final source restores callback methods and ordinary map semantics
byte-for-byte to853c059. It excludes the prototype selector/helpers/stubs and
retains only the unconditional shared read/store page proof and its actual
interior imported-DTCM regression case. The page correctness fix was present in
both timed selectors; these pairs do not isolate its timing and no speedup is
claimed for it. Exact extraction proof: `page-proof-extraction.json`.

Raw final per-game/package identities and analysis:
`per-game-comparison.json`, `mkds-reverse-analysis.json`,
`mkds-pair/{baseline,candidate}` and `mkds-reverse-pair/{baseline,candidate}`.

## Remaining handoff

Per-game decisions and machine-readable statuses are recorded; rejected runtime
is excluded. Final selected-source builds/checks pass. Create the focused
page-correctness PR, complete central records
and successful sync, and obtain the independent final gate. Area08 remains
open until that handoff. The next named item is **area17**, coupled16/18/19/20,
with16x first and the explicit32x/64x full-speed objective for both games.
No merge, delivery or interim owner playtest is authorized by this document.
