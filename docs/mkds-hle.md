# MKDS native math and graphics submission

`NDS_MKDS_IMPLEMENTATION=HLE|LLE` is a developer CMake choice. The generic runner
defaults to LLE; the MarioKartDSRecomp title scripts default to HLE. HLE builds
compile `hle_mkds.cpp` and carry a `mkds-hle` build identity. There is no runtime
selector. The original generated banks and device timing remain usable in LLE.

For bounded developer measurements, `NDS_MKDS_HLE_GROUPS` selects a semicolon
list of `VECTOR`, `MATH`, and `SUBMIT` at build time (default: all three). Omitted
groups retain ordinary generated execution. Partial selections receive a distinct
build identity and expose a read-only `group_mask` in `mkds_hle` diagnostics:
vector=1, math=2, submission=4. This does not add runtime switches.

HLE covers three byte-identical MPH SDK kernels (scale/add, cross product and
transform/translate), four-component normalization, rounded division-result
completion, and fixed-destination word submission. `hle_vector_math.h` contains
the shared arithmetic; each game retains its own ROM/code binding and counters.

## Boundary and shortcuts

Math preserves fixed-point arithmetic, required register/memory effects,
aliasing, return mode and continuation. Each call uses one approximate timing
charge after publishing its completed state. Native division and square root
publish observable device registers immediately and remove guest polling loops.
The original device start/poll paths remain unchanged for LLE.

Word submission batches up to 32 ordinary RAM words into packed geometry FIFO
ports. Other sources or destinations use ordered native bus access. Accepted
prefixes update guest registers before yielding; the explicit loop entry resumes
without host-only state or duplicate writes. Backpressure ends the current slice
using `nds_reschedule_slice`, allowing device drain instead of repeated stalled
dispatch. The geometry device retains command order, results and DMA/IRQ behavior.
The batch path bypasses the generic bus lookup and its per-word diagnostic ring.

These replacements do not require original internal instruction timing.
Larger queues or wider packet boundaries can replace these internals later if
they preserve the same caller-visible contract.

## Identity and dispatch

Only MKDS USA revision 0 SHA-1 `691e00d9a5dd80b04f80cc7559503e8b06848785`
and approved ARM9 ARM-state entries bind. `mkds_hle::resolve` reads current
resident code, checks write provenance and a full-operation SHA-1, and supplies
a stable `NdsStaticValidation` proof. Code bytes are never embedded in this source.

Generated entry proofs can cover only a short prefix. HLE expands the cached
proof to the complete operation and literals, for both ordinary lookup and
direct links. Even a rejected binding adds its complete page range to the cached
answer: repairing a distant literal must invalidate that rejection too.
An unknown/modified routine uses ordinary dispatch; it is not an established
instance of the supported operation. No operation switches implementation midway.

The read-only `mkds_hle` debug command reports the compiled choice, ROM admission,
per-operation calls, submitted words, batches and yields.

## Focused checks

```sh
cmake --build BUILD --target hle_mkds_test hle_mkds_io_test hle_mph_math_test video_savestate_test
ctest --test-dir BUILD -R '^(hle_mkds_test|hle_mkds_io_test|hle_mph_math_test|video_savestate_test)$' --output-on-failure
BUILD/hle_mkds_test PRIVATE/arm9.bin PRIVATE/copied/mkds_arm9_itcm.bin
```

The first test checks caller and transfer contracts; the second checks the real
math device implementation. The geometry test includes real packed-command
position results and FIFO pressure/drain/resume. The MPH regression test covers
the extracted shared kernels. Optional private images check the exact bindings.

MarioKartDSRecomp's `tools/test_mkds_hle_integration.py` additionally exercises
all replacements through real runtime dispatch, including mutation/restoration
of a normalization literal on its second page and a stalled FIFO continuation.
It executes synthetic callers without entering gameplay. Windows MinGW HLE/LLE
builds and these focused tests passed on October 4, 2026. They establish no FPS
gain, new gameplay validation or additional platform result.
