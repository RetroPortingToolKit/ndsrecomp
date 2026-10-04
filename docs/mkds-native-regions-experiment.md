# Connected ARM9 native-region experiment

2026-10-04; `beads-yjp.94`. **Park as a draft: no measured end-to-end gain.**
Main and title release defaults remain unchanged.

Build with `NDS_EXPERIMENT_NATIVE_REGIONS=ON` to connect warm, validated ARM9
link targets without passing each entry through the full dispatcher. Native
branches become iterative tails; calls own a nested continuation. Page-generation
and code-identity guards remain active. Cold/changed targets and pending service
use the authoritative dispatcher. There is no runtime implementation selector.

The default OFF build retains the original routing. ARM7 is outside this
experiment because its direct C calls have different tail ownership. Per-guest-
instruction timing, IRQ, debug and code-write checks remain; this is a first
connected-body experiment, not a new whole-function compiler or local register
allocator. NDS already shares its CPU state between compiled bodies, so Xbox's
Unicorn register-import/export savings are not present here.

Fast entries do not produce the intermediate dispatch trace/PC-profile events.
Deep tracing keeps authoritative routing; generated instruction diagnostics
continue to run. Read-only `direct_link` output adds `native_regions`,
`native_region_entries` and `native_region_tails`. Consequently a reduced
`dispatch_total` is not evidence of deleted guest instructions; fast entries
are accounted separately. Build identity adds `native-regions`.

## Bounded result

One fresh 600-frame GCN Luigi Circuit native-AI window per valid build, Windows
Ryzen 7 9800X3D / RTX 3080 Ti, OpenGL compute, unpaced headless runner, no save
writes. Vector HLE is enabled in both:

| | Shared baseline | Native regions |
| --- | ---: | ---: |
| Elapsed ms/frame | 11.167 | 12.053 |
| Process CPU ms/frame | 13.021 | 14.323 |
| Full ARM9 dispatch entries | 15,220,328 | 5,079,045 |
| Connected native entries | 0 | 10,141,283 |
| ARM9 instructions | 106,030,566 | 106,030,566 |
| ARM7 instructions | 28,879,344 | 28,879,344 |

Final PNGs, positions, scheduler rounds and FIFO-stall rounds match. Both
windows have zero interpreter execution. Elapsed time increased 7.9% in this
sample despite avoiding two thirds of full ARM9 dispatch entries. Remaining
guard/continuation/per-instruction costs still exist; their exact contribution
to the result is not established. Host load was not controlled, so this is
screening evidence rather than a statistical regression estimate or Deck claim.

The first attempt admitted zero native entries because the diagnostic gate
also rejected the always-armed instruction hook. It took 12.956 ms/frame and
is preserved as an invalid activation attempt. Removing that unnecessary gate
keeps generated instruction hooks effective. The corrected synthetic caller
test proves 15 native entries before the second and final native gameplay run.
No more native gameplay repetitions were run or are planned for this experiment.

Windows ON and OFF/LLE builds pass. The synthetic private-image caller test
checks buffered command output, callee-saved registers and mutation/repair of
a warmed code entry; both the candidate and separate LLE build pass. This is
not comprehensive title validation. The single valid gameplay sample has no
observed behavior change, but broader compatibility remains unproven.

Implementation commit: `4bf49c5`. The measured binary was built immediately
before committing that source; exact executable SHA-256 and dirty build
provenance are recorded in the title's
`docs/hle-structural-measurements-2026-10-04.json`. Private binaries, logs and
screenshots are in `captures/hle-structural-20261004/`. Keep this branch separate
from the whole-packet HLE experiment and do not promote it to a shipping default.
