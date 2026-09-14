# Steam Deck dispatch-cache investigation — 2026-09-13

Tracking: `beads-yjp.88`, discovered during MKDS work in `beads-q4q.9`.
The cache-layout experiment described here was **rejected and reverted**.
No framework runtime performance improvement is claimed by this report.

## Measured lead

An owner-driven GCN Luigi Circuit profile of MKDS's copied-code build attributed
6.168% of main-thread CPU samples to `lookup_static_cached_impl` self time and
6.067% to `runtime_dispatch_impl` self time. The exact runner ELF build ID was
`36f06b83321c81054decda2faf33a7d3dc60e637`. Its framework checkout was
`d117ecfe295b199a0d20aa575700b5401f0cb091`, not the later framework main branch
on which this report is filed.

The highest individual lookup offset, `+0x6f`, accounted for 148 samples. Exact
ELF disassembly places it immediately after the occupied-byte comparison at
entry offset `0x62`. The 128-byte, 64-byte-aligned cache record put `pc` in its
first line and occupancy, epoch and function pointer in its second line.
Thus even a simple immutable hit accessed both lines. Sampling at that offset
suggested a memory-access lead; CPU-clock sampling alone does not prove a cache
miss, quantify memory stalls, or establish why the instruction was sampled.

## Experiment and correctness checks

The experiment grouped lookup identity fields and the first two validation-page
records into the first 64 bytes. It retained the 128-byte stride, 65,536 slots
per CPU, table hash, CPU/mode/epoch checks, validation bytes, generation checks,
cache invalidation, direct-link guards and scheduler behavior. A compile-time
layout assertion checked the first-line boundary. No renderer, generated bank,
power setting or diagnostics toggle changed.

A focused production-cache test used the real write bus and `nds_has_bank` to
check one through four dependency pages for ARM9 main RAM and ARM7 WRAM. It
covered positive hits, mutation rejection, restoration after cached rejection,
CPU/mode separation, and registration/unregistration epoch changes. This test,
the existing dispatch-lookup test, machinery guards and attribution guards all
passed. The rebuilt runner and AppImage packaging/integrity checks passed.
The rejected patch and test source are retained with the private evidence;
neither is part of the surviving framework implementation.

## One authorized Deck check

GCN Luigi Circuit, Mario/B Dasher, 50cc VS, normal opponents, teams off. One
15-second acceleration segment ended in grass-side barrier contact and was then
paused. An eight-second 499 Hz `cpu-clock:u` profile used 8,192-byte DWARF
stacks and matching symbols. This was not a complete lap or an identical-state
replay of the owner's drive.

| Measurement | Result |
| --- | ---: |
| Real frames / elapsed counter time | 770 / 14.960020 s |
| Average FPS | 51.471 |
| Lowest brief counter window | 41.830 FPS / 0.334689 s |
| Emulation / presentation | 17.997 / 1.168 ms per frame |
| Audio underrun increments | 473 |
| Separately queried guest VBlanks / synthetic presents | 770 / 0 |
| Lookup self samples | 243 / 3,789 main samples = 6.413% |
| Dispatch self samples | 243 / 3,789 main samples = 6.413% |

The direct dispatcher-lookup timer also failed to show a useful saving:

| Mean nanoseconds per sampled lookup | Prior scripted build | Prior owner drive | Layout experiment |
| --- | ---: | ---: | ---: |
| ARM9 positive cache hit | 67.319 | 62.226 | 67.474 |
| ARM7 positive cache hit | 81.824 | 86.790 | 90.985 |
| ARM9 slow lookup | 1,416.211 | 1,394.301 | 1,683.115 |

These are the logger's actual dispatcher-only timer populations, not estimates
obtained by dividing total CPU time by all cache counters. The latter also count
interpreter takeover polling. The candidate's complete interior logger intervals
cover 725 frames; its ARM9 positive-hit mean has 4,691 samples and ARM7 has 2,021.

Selected host observations all report AC online, 15 W caps and `powersave`.
The fastest sampled core ranged from 2.425 to 3.500 GHz. Scene progression,
profiling coverage and sampled clocks differ from the historical measurements.
Consequently this is not proof of a precise FPS regression or percentage
speedup. It is insufficient evidence to retain the layout change as a fix:
neither the intended lookup saving nor a meaningful gameplay gain was measured.

## Disposition and evidence

The experiment was reverted after verifying that no intervening source edits
would be overwritten. MKDS's pinned framework checkout is clean at the original
revision. The earlier owner-confirmed copied-code AppImage was restored to the
Deck. The successful MKDS change compiles verified ROM-backed ARM9 ITCM and
ARM7 WRAM code; it belongs to the title repository and does not require this
cache experiment.

Private evidence is retained under the sibling MKDS repository's
`generated/deck-dispatch-layout/`: `framework.patch`, `dispatch_cache_test.cpp`,
build/package logs, exact ELF symbols, `measurement-summary.json`,
`analyze_candidate.py`, and raw recordings/screenshots in `evidence/`.

- Rejected AppImage SHA-256:
  `59b3d2a73b6fe2604f7653d6153bf56360dcbf58d7f94d560a785074614b2e99`.
- Rejected ELF build ID: `b7cce1bfb3182e909d02d157bfb4dd27c9fc3d2a`.
- Evidence archive SHA-256:
  `7a334494f3603e4a8536c6161743896303216a189e1d38cdaefa847a6eedb0af`.
- All 4,832 native sample headers parsed; local archive and raw-profile hashes
  agree with the Deck's retained files.

Remaining native execution cost and sustained 60 FPS are unresolved. Future
work should first obtain stronger evidence about the initial cache access and
transition costs; repeating this layout change or inferring a fix from sample
counts alone is not justified. The owner requested committing the validated work
and ending the current investigation. No further gameplay is authorized by
this report.
