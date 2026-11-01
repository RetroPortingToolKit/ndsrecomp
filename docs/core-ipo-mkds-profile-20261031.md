# Target-scoped core IPO: qualified MKDS build profile

`NDS_CORE_IPO` defaults OFF. The selected MKDS Windows/GCC15.2 profile may
enable it with `-DNDS_CORE_IPO=ON`, with runner PGO OFF and the retained O2
title-bank archive. MPH and generic profiles keep it OFF.

The option requires matching GNU C/C++ compiler versions and a successful
`CheckIPOSupported` admission. IPO applies only to the `nds_runner`,
`nds_armv4t` and `nds_support` targets, with final link workers bounded by
`-flto=2`. Imported title-bank/native-SDK archives and other library targets
remain opaque. The option preserves exported runtime interfaces and ordinary
math/ISA semantics, adds compiler-version build identity, and retains the
portable OFF path. It does not qualify another compiler, host or title.

| Qualified experiment | Final campaign choice |
|---|---|
| MKDS Core OFF versus ON, same O2 banks | Retain ON for the measured MKDS profile |
| MKDS Core ON plus bank O3 | Unselect the O3 addition to the chosen profile |
| MPH Core OFF versus ON | Keep OFF; no useful whole-game gain established |
| MPH runner PGO | Separate optional-neutral choice; Core stays OFF |

The MKDS core-only pair used source b0cf0a6, baseline1287789d versus
candidatee013f23d, bankbe071f04 and SDK6a843c42, OriginalCompute4x/AA8,
PIO HLE, native-control OFF and serial presentation. Each binary prepared its
own current-build menu/race state. Both completed180VBlank/180CSV and exited0,
with active native CPUs and a basic nongarbled candidate image.

Observed wall time was1.4808637->1.1338121s and process CPU
1.796875->1.484375s. Different guest seeds/positions, background work and GPU
clocks limit attribution of the23.44%/17.39% reductions. Independent review
selected this bounded useful result without claiming a robust compiler-only
percentage. Raw samples are retained under
`G:/Temp/Codex/ndsrecomp/mkds-input-load-20261027/loading-hitches/gpu-high-resolution/mkds-primitive-runtime-prepared/runs/mkds-core-only/measurements`.

The prior combined Core+O3 package observed higher wall/CPU costs and was
subdivided once. Its O3 addition is unselected for this MKDS profile; this is
not a universal standalone-O3 regression claim. MPH's core-only pair observed
negligible wall benefit and higher CPU cost under unequal clocks/load; its ON
profile is unselected. Complete details and exact identities are in
`beads-yjp.118`. MPH bank O3 remains a separate unfinished candidate.

This PR ports the already-reviewed51-line CMake change onto accepted
integration01ba3b2. Final combined build/runtime and owner practical review
remain before merge/delivery. No PGO+IPO combination or cross-build netplay
compatibility is established by these checks.
