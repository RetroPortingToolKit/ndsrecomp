# MKDS whole-packet HLE experiment

2026-10-04; `beads-yjp.94`. **Park as a draft. No measured benefit; gameplay
differs.** Main and title release defaults remain vector HLE only.

This replaces the complete SDK operation at ARM9 `0x01FF9048`: append a command
and parameters to its 192-word buffer, wait for a genuine preceding asynchronous
transfer, flush existing buffered commands, and submit the new packet in order.
It removes guest packet bookkeeping, the RAM-copy call and the inner word-send
loop. The existing geometry decoder and renderer still do the graphics work.

Enable with `NDS_MKDS_IMPLEMENTATION=HLE` and
`NDS_EXPERIMENT_MKDS_PACKET_HLE=ON`; the latter defaults OFF. The separate LLE
build uses `NDS_MKDS_IMPLEMENTATION=LLE` and the experiment OFF. There is no live
implementation switch. Build identity adds `packet-hle`; `mkds_hle` reports
group mask 9 (vector plus packet) and separate packet counters.

The caller contract preserves command order, buffer contents/count, required
completion and callee-saved registers. Native copying preserves the SDK's
eight-word load-before-store behavior for overlapping inputs. Timing is an
estimate per native batch. A full FIFO yields an HLE operation whose continuation
is represented by guest registers and a 16-byte caller frame; no host-only queue
or LLE handoff is used. Entry `0x01FF9130` is reserved for that build's HLE
continuation, not a promise to resume an arbitrary LLE interior state. Cross-build
savestate compatibility is not promised; save/load of a pending packet was not
separately exercised.

Admission proves the complete outer function/literals, flush/wait helpers and
copy/send helper range. Both successful and rejected resolutions watch the
helper page, so changing and repairing it does not leave a stale cached answer.

## Bounded result

One fresh 600-frame GCN Luigi Circuit native-AI window, Windows Ryzen 7 9800X3D /
RTX 3080 Ti, OpenGL compute, unpaced headless runner, no saves, vector HLE enabled
in both builds. Shared baseline: **11.167 ms/frame**. Packet: **12.426 ms/frame**
(11.3% more elapsed time in this sample). Process CPU time also increased,
13.021 to 14.844 ms/frame. Host load was not controlled; no statistical or Deck
claim is made. No additional packet gameplay repetitions were run.

The replacement handled 140,905 whole calls, 942,874 FIFO words, 281,210 adapter
iterations and 14,035 yields/waits. Every call used the direct path; buffer
construction did not contribute to this racing window. ARM9 instructions fell
106,030,566 to 98,516,495, yet elapsed time increased. Interpreter execution was
zero. Removing guest instructions alone did not establish a useful improvement.

**Behavior risk:** the final AI position, item and ranking differ. Baseline is
seventh with a Boo item; this candidate is eighth with an empty item box at race
frame 755. Neither is a completed race. Geometry remains visually coherent, but
72,853 pixels differ in the final combined screenshot. Timing/order changes are
a possible cause, not an established diagnosis. Scene work is consequently not
identical across the timing samples. Do not merge this candidate as a default.

## Focused validation

Windows HLE and LLE runners build. MKDS vector math/private-image bindings and
dispatch lookup unit checks pass. `tools/test_mkds_structural.py --packet-hle`
uses private prepared images and synthetic callers, with no gameplay: buffered
output, callee-saved registers, warmed code mutation/repair, helper-page-only
mutation/repair, buffered flush, and a 405-word FIFO stress case all pass. The
stress case produces the real position-test result `[4096,8192,12288,4096]`, with
337 adapter iterations and 334 yields/waits. The separate LLE runner passes the
synthetic buffered/mutation caller checks with packet HLE absent.

Detailed numeric evidence and hashes are in the title's
`docs/hle-structural-measurements-2026-10-04.json`; private logs, binaries and
captures are under `captures/hle-structural-20261004/`. The measured binary
matches implementation commit `902b750`; it was built immediately before that
commit, so the report records its exact SHA-256 and dirty build provenance.
