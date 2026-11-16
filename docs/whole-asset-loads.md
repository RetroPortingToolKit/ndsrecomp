# Whole compressed-asset loads

Enhanced offline MKDS and MPH builds can complete a compressed file or archive
load from immutable cartridge bytes to decompressed RAM as one native operation.
The service removes the guest card-polling and decompression-token loops while
retaining guest allocation, archive mounting, heap queries and caller returns.
It requires native ARM9 observation and offline ARM7 services. Reference builds,
custom filesystems, unsupported input and observable asynchronous operations
continue through the original implementation.

Admission proves the complete owned caller and its dependencies, ordinary ROM
filesystem identity/tables, input bounds and complete compressed stream before
allocating output. A 32-byte guest continuation retains ownership across guest
allocation and current-build state load. The real internal callers use both
eight- and four-byte-aligned stacks; all frame accesses are words, and the
original stack alignment is restored. Invalid alignment, stale code, malformed
streams and physical aliases of continuation/ownership metadata reject safely.

One complete decode publishes through canonical RAM/TCM bounds, settles old PCM
before mutation and invalidates affected translated code. The MKDS output
pointer is published after the complete data; MPH retains its real archive
mount/list/count operation. No native-save storage path is replaced.

## Windows loading results

One corrected matched pair per game compared the qualified offline package
with the complete asset service. These are hidden, muted **serve-mode** loads,
measured from the final transition trigger to the same scene-ready fields,
with ten-VBlank completion polling. Wall time includes the trigger and TCP
observations. They are loading measurements, not presentation or FPS results.

| Load | Process CPU seconds | Observed wall seconds | Native work during measured load |
| --- | --- | --- | --- |
| MKDS course 33 | 5.3125 to 3.0313 (-42.9%) | 4.5068 to 2.3300 (-48.3%) | 13 files, 1,531,604 output bytes |
| MPH room 92 | 5.0313 to 4.4688 (-11.2%) | 4.4147 to 3.9697 (-10.1%) | 7 archives, 718,112 output bytes |

Both legs use the same host and settings; each package uses its own
current-build fixture. Completion has
up to ten guest frames of polling overshoot; timing-seeded trajectories can
differ. These single shared-host samples do not establish a general speedup
for every course, room or machine.

The complete Main2D (40,648 bytes) and common archive (84,288 bytes) outputs
match independent ROM decompression at their actual caller return, before
relocation. The later MainRace (50,036 bytes) and Samus (57,696 bytes) consumed
spans match their reference builds exactly. A raw ROM comparison after gameplay
is inappropriate because the caller has already modified those archives.
Existing TCP breakpoints now stop before native service invocation as well as
generated instructions, allowing this observation without timing instrumentation.

Focused components pass 68 MKDS checks and 113 MPH assertions, including the
observed DTCM stack layouts, continuation restore, single publication, malformed
input and physical alias rejection. Six focused runtime/component tests per
title pass. Both measured scenes continue for 120 VBlanks, current-build state
and pixels restore, and basic images are coherent.

The engineering result remains separate from final combined consumer defaults,
human review, integration and release. The original ARM7/geometry/CPU reference
build remains available as a separate executable; older snapshots need no
migration.
