# Native capture before enhanced display: private prototype

Owner: `beads-yjp.72`, related loading work `beads-d8um`, campaign
`beads-2wy8.1`. Coupled inventory: 01/16/17/18/19/20/28. These broad items
remain open. No release archive or production default changed.

The measured compute readback cost is predominantly synchronization. The
existing full native-GL alternative had mixed costs and different GPU power
states; it remains unadopted. This prototype changes the shared frame job's
completion boundaries while retaining its renderer and prepared resources.

One geometry, texture-cache and binning job supplies both consumers. At scales
3–4, it can rasterize/resolve the exact scaled pixel positions used by native
readback first. Edge marking includes the four neighbouring depth/attribute
samples. It queues the original native pixel pack and fence at that point,
then rasterizes the complementary samples and completes the enhanced display.
The continuation is flushed too, so it can overlap CPU scanline work.

This keeps the existing fractional coordinates, integer colour/alpha/ID
transport and current-frame capture. It does not substitute a 1× redraw,
sample presentation pixels for guest capture, or draw a second complete scene.
The second final pass reads the now-complete neighbour data and writes the
enhanced display; it does not rewrite the already-owned native output.

The callback runs on the existing GL owner thread. It may bind the native
texture/PBO and queue its pack/fence, but cannot mutate geometry, variants,
SSBO bindings or the texture cache. Existing single-context command ordering
protects the shared immutable inputs. The bridge recognizes an already queued
readback so frame completion/restore do not queue it twice. Identical-frame
reuse and jobs without a native consumer retain the ordinary complete path.
Scales 1–2 retain unsplit shaders. Native GL and faithful builds remain valid.

`NDS_COMPUTE_NATIVE_CAPTURE_FIRST=1` enables private qualification; its default
is off. It is not exposed in player settings. No netplay or cross-build
compatibility claim is made from compilation alone.

The real GL contract test compares the early PBO pixels and completed enhanced
texture against the full scaled job. It passes locally and in its earlier
version on the GTX 760 lab host. Cases cover clears, fractional geometry,
edge/fog/AA shader combinations, W-buffering, textured translucency, ordinary
no-consumer/identical-frame jobs and the low-scale fallback. The latest local
test also requires nonempty geometry, active fog and odd-scale neighbour
spacing. These are focused correctness checks, not performance samples.

Private MKDS executable SHA-256 starts `134b9cda…`; exact source/object/SDK
identities are retained. Its hidden, muted fresh-menu functional run reaches
Pinball 150cc, presents plausible output and restores its real frontend slot
from race frame 179 to 153. A live observation interval contains 152 actual
native-first jobs. RPC observations allow frontend progress; the interval's
249 readbacks must not be described as exactly 180 VBlanks.

The intended matched off/on pair was rejected before a timed game launch:
other lab processes were using substantial CPU and GPU capacity. Local
functional checking ran under about 10 background CPU cores. **Performance
qualification is still false.** Do not compare its phase timings with those
from another GPU, or call an earlier fence a whole-frame speedup. Additional
dispatch work and presentation waits remain real costs to measure.

Retain the one matched pair budget for an eligible host, at delivered 4×/8×
settings, including whole-frame and presentation costs and GPU operating
state. No blind repetitions or benchmark matrix. Adoption still requires
per-game neutral-or-positive behavior and the required owner handoff.

Machine-readable identities, verified rejected-preflight receipts, source
copies, image, isolated state slot and raw functional output are under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/` in
`capture-first-evidence.json`, `nas-capture-first-receipts/` and
`capture-first-functional-local/`. The native CPU clock/transaction prototype
is excluded; the candidate uses the original three-patch SDK package.


## Completed local measurement and disposition, 2026-10-28

The committed implementation at `926e473` builds through supported CMake;
runner SHA-256 is `35c9d4ffc158dcca419d9e4ecd241ba51a17e972f237b83b1ba220732a4456ad`.
The exact original three-patch SDK and validated title-bank archive are retained.
The final real-GL contract test passes (1/1), as do representative faithful and
native-GL bridge compile checks.

One local OFF/ON pair uses the same executable, fresh Pinball 150cc construction,
compute 4x/AA8, normal pacing, hidden frontend and dummy audio. It retains all
180 selected frames without trimming. OFF/ON mean whole frames are 16.7667 /
16.6093 ms; worst whole frames are 24.4210 / 24.1838 ms, with zero frames over
33 ms on either side. Both real frontend save/load checks pass after timing.
All 11,550 preparation hints finish before course construction on both sides.

This does not demonstrate a material whole-frame improvement. Background CPU
load differs (10.89 / 9.46 cores) and median GPU graphics clocks differ (855 /
1965 MHz), although both sampled P0. Readback means fall 0.1081 -> 0.0624 ms;
that cannot explain or establish causation for the much larger emulation-time
difference. Normal pacing also prevents treating average wall time as uncapped
throughput. The private selector stays OFF; no further repetitions are needed
for this decision. Do not present this experiment as a release performance win.

Local measurements are useful under recorded ambient conditions. The earlier
absolute quiet-host gate remains false; it is not a reason to claim this host
cannot measure, nor permission to fabricate a qualified result. Raw samples,
QPC/UTC mapping, CPU/GPU telemetry and executable identities are retained in
`loading-hitches/capture-first-local-pair/`, `capture-first-local-pair-job.json`,
`capture-first-local-gpu.csv` and `capture-first-local-pair-summary.json`.
