# Local loading measurements, 2026-10-28

Owner: beads-d8um / beads-04cb; campaign beads-2wy8.1.

One bounded construction sample per executable uses fresh menus, Pinball 150cc,
compute 4x/AA8, normal pacing, hidden frontend and dummy audio on Ryzen 7 9800X3D /
RTX 3080 Ti. All 180 selected frames are retained without trimming. No checkpoint
is loaded during timing. A real isolated frontend save/load follows each sample.

| Package | Mean whole frame | Worst whole frame | Frames >33ms |
| --- | ---: | ---: | ---: |
| Exact delivered v0.0.10 | 16.7442ms | 37.0667ms | 1 |
| New loading package, capture split OFF | 16.7667ms | 24.4210ms | 0 |
| Same new executable, capture split ON | 16.6093ms | 24.1838ms | 0 |

Delivered runner SHA-256: 9bd380ba5dd52886e20e5581c659b0d630cc847d2d68a86b4623c35cac4b02b2.
New runner SHA-256: 35c9d4ffc158dcca419d9e4ecd241ba51a17e972f237b83b1ba220732a4456ad.
The retained development control is a different executable; its 48.9ms worst
frame must not be described as the shipped v0.0.10 baseline.

This supports practical review of the loading-only package. Average paced wall
time is neutral in this sample; it is not uncapped throughput. Background CPU
load differs (delivered 5.63 cores / new OFF 10.89 / ON 9.46), and GPU clock
variation prevents a clean causal average-CPU comparison. The old quiet-host
and adoption gate values remain false. Useful ambient observations do not
require pretending the host was quiet or that GPU clocks matched.

All 11,550 native preparation hints complete before construction in the new
build. Both old and new real frontend save/load checks pass. Focused existing
correctness evidence covers publication, ISA widths/alignment and portable
fallback compilation; no full campaign or multiplayer validation is repeated.

Keep native capture-first OFF: its OFF/ON pair shows no material whole-frame
benefit. Exclude the private CPU service-clock prototype. Finish release metadata,
matching source/runtime artifact checks and owner practical observation before
replacing the one current delivery ZIP. The broad subsystem scopes remain open.

Exact raw samples, executable/source/settings identities, QPC/UTC mappings,
telemetry and per-frame data are retained outside delivery in
G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/:
local-loading-package-measurements.json, delivered-reference-local-v3/,
capture-first-local-pair/, and their job/GPU telemetry files.
