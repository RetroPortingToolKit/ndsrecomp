# MKDS vector HLE

The integrated MKDS HLE consists of three byte-identical MPH SDK operations:
Q12 scale/add (`0x02147FD8`), cross product (`0x021483A0`) and matrix transform
plus translation (`0x02147288`). `hle_vector_math.h` shares their native kernels;
each game keeps its own ROM/code identity checks and counters.

`NDS_MKDS_IMPLEMENTATION=HLE|LLE` is a developer build-time choice. The generic
framework defaults to LLE; the MarioKartDSRecomp build scripts select HLE and
`NDS_MKDS_HLE_GROUPS=VECTOR` explicitly. VECTOR is the only integrated group.
There is no runtime switch. HLE build identities include `mkds-hle-groups1`;
the read-only `mkds_hle` debug command reports the compiled choice and call counts.

The kernels retain fixed-point results, aliasing, required register/memory
effects, return mode and continuation. Each operation finishes atomically with
one approximate timing charge. Internal guest instructions and intermediate
timing are not reproduced. The original generated routines remain the LLE path.

Only verified AMCE0 SHA-1 `691e00d9a5dd80b04f80cc7559503e8b06848785` binds, at
approved ARM9 ARM-state entries. Cold lookup checks current resident bytes,
write provenance and the complete routine fingerprint. Warm lookup/direct links
retain page-generation and code proofs. Unknown or modified code uses ordinary
dispatch because its identity is not established. No ROM payload is committed.

Two previously authorized 600-frame GCN Luigi Circuit samples measured vector
HLE at 9.264/9.805 ms per frame versus 11.039/11.105 with HLE off on Windows.
The final screenshots matched. This is a bounded local result, not a universal
speedup or a Steam Deck measurement. The title repository retains the full data.

Focused checks:

```sh
cmake --build BUILD --target hle_mkds_test hle_mph_math_test
ctest --test-dir BUILD -R '^(hle_mkds_test|hle_mph_math_test)$' --output-on-failure
BUILD/hle_mkds_test PRIVATE/arm9.bin
```

They cover shared math, overlap, caller return contracts, exact-image bindings,
rejection of modified code and rejection of the draft-only entries. The title's
`tools/test_mkds_hle_integration.py` executes synthetic vector callers through
the real runtime without entering gameplay.

## Deferred experiments and behavior risk

Normalization/division and graphics submission remain in
[draft PR #25](https://github.com/RetroPortingToolKit/ndsrecomp/pull/25), outside
the integrated runtime. Hardware-math measurements were inconsistent; graphics
submission showed no measured gain.

Graphics-enabled samples reproducibly changed the AI driver's trajectory,
item and ranking. Timing/ordering changes could affect collisions, random-event
ordering or other gameplay; the cause and full impact are not established.
Math-only samples matched the baseline screenshots, but atomic math completion
changes observable device/interrupt timing and retains compatibility risk.
Neither experiment is approved for the default build by these measurements.
