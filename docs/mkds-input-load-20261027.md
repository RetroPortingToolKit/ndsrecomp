# MKDS input, display sizing and slowdown report

Tracking: `beads-04cb`, loading follow-up `beads-d8um`, shared UI
integration `beads-n12g`.

The MKDS launcher enabled host-owned binding rows but supplied zeroed gameplay
arrays, did not persist their edits, and did not pass them to the runner. The
runner used a fixed keyboard layout. Upstream recomp-ui commit `a5a7822` also
fixes capture incorrectly writing to the legacy store instead of these arrays;
the title now pins a version containing that fix.

`--keyboard-bindings` and `--gamepad-bindings` accept twelve validated integer
controls in NDS profile order: Up, Down, Left, Right, A, B, X, Y, L, R, Start,
Select. Zero explicitly unbinds a row. Standalone defaults remain the existing
DS layout. Host shortcuts and MPH Prime Controls keep their existing paths.

`--window-scale 1..8` requests initial DPI-aware sizing and fits both windows
together within the display's usable bounds. The default standalone sizing is
retained when this argument is absent. The conversion uses window display scale
divided by pixel density so platforms with logical window coordinates are not
scaled twice; see [SDL high-DPI guidance](https://wiki.libsdl.org/SDL3/README-highdpi).
Debug touch input converts through the current renderer rather than assuming 2x.

Focused checks cover parsing/rejection, actual DS defaults, unbinding, saved
settings, signed pad axes, and 225%/small/stacked sizing. Actual launcher capture
and close persistence were observed. A dummy-display run verified saved N maps
to A, the former Z binding no longer drives A, and B/directional keys remain
correct. No physical 225% monitor or sustained CPU-contention claim is made.

The reported speed difference is against offline AI on delivered v0.0.9,
probably keyboard. One held-acceleration 120-VBlank check took 0.852 seconds
normally versus 4.225 with delayed host command service, with identical complete
4 MiB main RAM and race observations. This does not reproduce physical input
timing or genuine host CPU contention, so the original report remains open.

One 600-VBlank loading transition and one 180-VBlank host profile observed cold
native compilation work. Later slow frames did not coincide with explicit ARM9
whole-cache invalidations. ARM7 invalidations were not exposed by that query.
An exact fast-provenance probe preserved all main RAM but gave essentially flat
CPU cost (7.359375 versus 7.296875 seconds) and retained hitches. It is not shipped.
No change to simulation timing or physics was adopted.

For unattended tests, `NDS_FRONTEND_HIDDEN=1` creates hidden SDL windows without
showing/activating them. `NDS_FRONTEND_LATENCY_TRACE=<path>` remains opt-in and
bounded, and writes only on exit. Its frame CSV now also records keyboard bits,
the published DS key mask, and virtual system time to correlate an acceleration
report with a real stall. Normal owner playtests use visible windows and audio.
