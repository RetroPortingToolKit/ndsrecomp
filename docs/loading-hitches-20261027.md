# Native loading hitches

Tracking: `beads-d8um`, MKDS `beads-04cb`, campaign `beads-2wy8.1`.

A bounded Pinball 150cc load census compiled 40,265 ARM9 blocks and 3,420 ARM7
blocks in 180 VBlanks, with no repeated block translations or dropped census
entries. Only the expected restore-start invalidations occurred. A later 48 ms
step compiled 5,849 new ARM9 blocks. This identifies first-use native compilation
in this workload; a larger code cache is not justified by these samples.

The SDK's x64 register allocator built temporary singleton vectors and copied
candidate lists to heap storage for each register selection. The patch uses
borrowed spans and a 32-entry local scratch array, preserving both partition
operations and their order. Unusually long lists retain a heap fallback.
Register choices, IR validation, instruction boundaries, simulated cycle timing,
backend selection and CPU capability checks remain unchanged.

Apply both patches to the pinned Dynarmic a41c380 SDK before configuring:

```powershell
git -C <isolated-sdk> apply <framework>/runner/cmake/dynarmic-readonly-pages.patch
git -C <isolated-sdk> apply <framework>/runner/cmake/dynarmic-cold-register-allocation.patch
```

The existing optional Windows/x64 SDK gate remains. Other hosts/builds retain
portable execution. Native build identity includes both patch digests; this
does not establish compatibility with older builds or relax netplay checks.

The differential check compiles the actual candidate selector against the
pinned original algorithm: 39,681 selections/failures match, normal scratch
allocation falls from one heap allocation to zero, and oversized fallback is
retained. It also covers reversed/permuted candidate orders, which matter
because a first-match search would change the original partition's choice.
ARM9 and ARM7 native probes pass.

Two matched 600-VBlank MKDS diagnostic pairs used compute 1x, adaptive 448,
VECTOR HLE, native ARM9/ARM7, no SSAA, governor/sampler off, dummy audio and
identical inputs. The 180-VBlank loading window excludes the first restored
frame, whose cold-cache cost is a snapshot artifact.

| Order | Loading CPU before/after | Largest loading step before/after | Steps above 33 ms |
| --- | --- | --- | --- |
| Before / after | 1.703125 / 1.500000 s | 42.55 / 39.50 ms | 2 / 1 |
| After / before | 1.843750 / 1.593750 s | 54.31 / 42.08 ms | 2 / 1 |

Start/end complete 4 MiB main RAM and race observations match in both pairs;
each compiles 49,352 blocks. Loading-window CPU reductions are 11.9% and 13.6%
in these local samples. Whole 600-VBlank CPU varies from 5.000 to 4.703125 s and
4.609375 to 4.593750 s; later racing windows are mixed. No general gameplay FPS
win or elimination of all first-use compilation is claimed.

These are diagnostic timings: a subsequent MPH check's quiet-host gate found
other running games and CPU/GPU activity, so qualification remains pending.
No MPH timed sample was collected in that rejected attempt. Preserve that
failure and do not manufacture a passing performance gate. The MPH control
reconstructs the old heap allocations with a common span ABI, identical engine
objects and all other SDK objects, rather than comparing a delivered release.
Both MPH variants have prepared their own fresh states. The candidate remains
on the local `fix/loading-hitches-20261027` branch; it has not been promoted.

A separate 229 ms presentation sample waited inside the NVIDIA driver. A callback
probe removed ordinary error-query overhead but retained the long loading
stalls. It is excluded, consistent with the earlier boundary investigation.
The qualified GL error checks/order are retained. Debug contexts and error
injection are not part of this fix.

`NDS_NATIVE_COMPILE_CENSUS=1` enables a bounded, default-off per-CPU census.
The JSON exposes compiled/recompiled blocks, dropped entries and ARM7
invalidations once the lazy native instance exists. Assert actual native entry
counts before interpreting it. A headless deep-trace-on harness attempt that
bypassed native execution is explicitly invalid, not zero-compilation evidence.

Raw source/binary identities, patches, samples, full-RAM comparisons and captures
are retained under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches`.
No complete campaign or multiplayer run was repeated. Native saves/settings and
the desktop are preserved. Delivery stays at MKDS 0.0.10 until the remaining
qualification and candidate packaging are complete.

## Remaining release work

- Obtain one eligible matched MPH sample from the prepared exact-build states;
  the rejected quiet-host attempt is not a regression result. Confirm the MKDS
  loading result under a recorded quiet host before calling it qualified.
- Complete the final runner build, title engine pin/version and source bundle,
  then check banks, DLL closure, licenses, private payloads and archive contents.
- Prepare a short loading check for the owner to launch. Current review notes
  have no observations; no owner playtest is claimed.
- Promote only a verified replacement archive, retaining one current MKDS ZIP
  in Nextcloud and preserving native saves/settings.

Keyboard persistence/capture and initial DPI/window fitting are already in
0.0.10 with focused checks. The reported loss of player speed against offline AI
under real CPU load remains unreproduced. It is a separate open correctness
report, not established as fixed by lower compilation cost.
