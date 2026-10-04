# MPH vector math HLE pilot

This is the first build-selectable HLE replacement for MPH USA revision 0
(ROM SHA-1 `90164d1ac127ee5f9815ea4ae7de798c7b5fc629`). It is development
work after the v0.7.5-alpha coverage release; that release contains no HLE.
MPH v0.7.6-alpha enables these replacements in its release builds.

The developer chooses the implementation when configuring the runner:

```powershell
cmake -S runner -B runner/build-mph-release-0610 "-DNDS_MPH_MATH_IMPLEMENTATION=HLE"
cmake --build runner/build-mph-release-0610 --target nds_runner hle_mph_math_test
```

Configure the same runner with `"-DNDS_MPH_MATH_IMPLEMENTATION=LLE"` to build
the maintained generated-code implementation. This cache variable accepts
only `HLE` or `LLE`. The generic framework configure defaults to LLE; the MPH
Windows and Linux build scripts explicitly select HLE by default. Developers
can pass `-MathImplementation LLE` to either PowerShell build script or
`--math-implementation LLE` to `tools/build-linux.sh` in the game repository.
There is no launcher control, environment variable, runtime mode setter,
mid-operation fallback, or state-conversion mechanism.

## Caller contracts

| ARM9 entry | Inputs | Operation |
|---|---|---|
| `0x02080BD8` | r0: Q12 scale; r1: vector; r2: addend; r3: output | `out = (scale * vector >> 12) + addend` |
| `0x02080DE0` | r0: first vector; r1: second vector; r2: output | Cross product, rounding each component with `+0x800` before shifting by 12 |
| `0x0207FD38` | r0: vector; r1: 4x3 matrix; r2: output | Q12 transform plus the matrix's translation vector |

Vectors have three 32-bit components. Products use signed components and
64-bit modular accumulators; stored results wrap to 32 bits. The transform
uses matrix indices `j`, `j+3`, `j+6`, with translation at `j+9`.

These routines operate on ordinary writable guest RAM. Their contract
includes in-place/overlapping buffers, ARM word-load rotation for unaligned
addresses, and word-aligned stores. MMIO buffers and buffers overlapping the
routine's own temporary stack frame are outside this math interface.

The replacements preserve caller-visible output, stack pointer, callee-saved
registers, LR, and ARM/Thumb return behavior. Retaining the original scratch
registers and final flags is inexpensive here, so this pilot also does so.
That convenience is not a rule that future HLE implementations must reproduce
all internal CPU state. Dead stack-frame bytes, individual instruction
events, internal instruction counts, and intermediate register states are
not reproduced.

## Freedom inside the operation

Native integer arithmetic replaces the instruction stream. The implementation
does not simulate guest pushes/pops, instruction fetches, multiply timing,
per-instruction dispatch, or per-instruction yield checks. The transform
derives a redundant RAM reload from its stored value. Existing guest bus
writes retain memory mapping and code-write invalidation behavior.

Each call completes atomically and publishes its return PC/state before one
timing charge. The initial estimates are 96 ARM9 cycles for scale/add and 160
for each of the other operations. These are provisional coarse budgets, not
cycle-equivalence claims. IRQ delivery and scheduler observation can move to
the operation boundary. Tiny timing differences are acceptable under the
owner's HLE policy. Do not rebuild
an instruction emulator inside these operations merely to match a trace.

An ordinary return uses the existing call-return stack. A tail call uses the
existing interworking dispatch path. An IRQ unwind after completion exposes
the completed return state; it does not restart the HLE operation.

## Integration and LLE maintenance

The HLE build initializes its ROM scope once before guest execution. On a
cold native lookup, the dispatcher recognizes a supported entry and proves
the complete routine's fingerprint against the already validated bank span.
It caches the replacement function using the existing content/page-generation
guards. Warm lookups and direct links call that function without another HLE
selection branch. The scope checks prevent unrelated code at the same address
from being mistaken for this interface.

Only whole-routine entry points are replaced. HLE never creates an interior
resume PC. Other functions and instruction entry points retain their ordinary
execution paths. There is no promise of cross-build savestate compatibility.
The existing interpreter debugging machinery is not the HLE/LLE build setting.

The LLE build compiles out the HLE resolver calls and does not link these math
replacements into the runner. Original generated banks, interpreter support,
and their timing paths remain maintained. No generated source or guest ABI
header is changed by this pilot.

## Checks and limits

`hle_mph_math_test` checks analytic results, in-place operations, call/tail
returns, Thumb interworking, post-operation unwind, and rejection of unrelated
or insufficient code proofs. A local, uncommitted extracted ARM9 image enables
comparison with the original instructions through the independent ARM IR
interpreter:

```powershell
runner/build-mph-release-0610/hle_mph_math_test.exe --arm9 ../metroidprimehuntersrecomp/generated/inputs/arm9.bin
```

The fixture is fingerprint-checked before use. It is never embedded in tests
or committed. The 1,536 deterministic cases include arithmetic extremes,
random inputs, overlapping buffers, unaligned words, and both return modes.
They compare outputs, active registers, flags, and all memory outside the dead
temporary stack area. Timing/internal instruction traces are intentionally
outside this comparison.

Both Windows/MinGW runner builds and their `--help` startup checks passed
during the initial implementation. The LLE executable excludes the HLE startup
marker and replacement source; the HLE executable includes them.

The read-only debug command `mph_math` reports the compiled implementation,
supported-ROM flag, and per-operation call totals. Read it on the emulation
thread. Counts persist across snapshot loads and never change the selected
implementation. Subtract two readings to establish actual HLE execution.

Focused HLE gameplay validation used a fresh boot and same-build checkpoints,
with data-only room/inventory cheats and disk saving disabled. One campaign
session visited Celestial Gateway, Ice Hive, and Gorea phase 2. Screenshots
showed live gameplay, weapon changes, morph form, scanning, and boss attacks.
Native dialog interruptions were dismissed; this was not a full boss victory
or a check of every weapon in every room. Executed call deltas during the
scripted room visits, excluding the subsequent timing window:

| Area | Scale/add | Cross product | Transform + translation |
|---|---:|---:|---:|
| Celestial Gateway (45) | 652 | 62,628 | 33,302 |
| Ice Hive (78) | 0 | 45,971 | 62,964 |
| Gorea phase 2 (92) | 2,882 | 25,833 | 2,648 |

One active Gorea window advanced 600 VBlanks in 3.982 seconds (150.7/s), above
the game's approximately 60 Hz simulation rate on the test PC. This is
unthrottled headless execution with GPU rendering, not presented desktop FPS
or an audio/pacing measurement. Both CPUs continued without terminal states.
Small interpreter counts remained during room loading (58, 58, and 79 ARM9
instructions respectively); these visits are not a claim of zero fallback.

The single offline Battle match used Combat Hall and three bots. Native fire
input spawned the player; movement/jumping, firing, morph movement/bombs, and
return to first-person continued with four active players and no CPU halt.
The gameplay segment executed 71,343 cross products and 2,110 transforms;
scale/add had no calls in that segment. Its 570-frame movement/fire window
took 6.084 seconds (93.7 VBlanks/s). One ARM9 interpreter instruction was
recorded. This validates a combat segment, not completion of the match, online
networking, every arena, or interactive audio/display pacing.

No LLE gameplay comparison or performance matrix was run. The prior profile
attributed 90 of 5,594 self samples (about 1.6%) to these three bodies; shared
dispatch and timing overhead was accounted separately. These are a bounded
first implementation, not evidence of a large overall FPS gain.
The next larger candidate is the DMA/geometry submission family identified in
[the issue 44 assessment](mph-issue44-performance.md).

The governing policy is compatible caller interfaces with freedom inside HLE,
a maintained LLE build, and developer selection at build time. See
[the optimization strategy](host_optimization_strategy.md) and the shared
template's `HLE.md` for the broader policy.
