# Optional HD graphics and supersample resolve

The accelerated top-screen presenter supports 1x–4x internal 3D resolution.
Its anti-aliasing option now resolves 2, 4 or 8 subpixel samples from that
actual higher-resolution raster, rather than reconstructing a native image.
Use 4x internal resolution with 8-sample AA for the quality preview.

This is supersample resolve, not hardware MSAA. Its benefit depends on the
ratio between rendered resolution and window size: a very large window can
map several taps to the same source texel. It cannot add texture detail that
the original game does not contain. The presenter's native 2D/HUD coordinate
is held fixed across taps; DS blending and brightness run before averaging.

AA defaults to off, and native/software output and guest-visible framebuffer
and capture surfaces retain their existing paths. The legacy presentation
reconstruction filter remains available on presentation paths without an HD
accelerated surface. Changing graphics mods requires restarting the game.
The governor may lower internal resolution when enabled; qualification and
quality previews keep it off to make the selected quality stable.

Focused actual-GL checks cover RGB6 compositor equivalence with AA off,
red/blue subpixel integration at all sample settings, opaque HUD preservation
and invariance at native resolution. Launcher provider checks cover supported
choices, persistence, bounds and providing an HD source when enabling AA.
Built-game screenshots must use `presented_framebuffer` and HD-consumption
counters; the native framebuffer alone cannot demonstrate higher resolution.

The initial quality preview exposed two existing presentation gaps: window
masks that hide BG0 disabled HD for the entire adaptive frame, and native
aspect skipped HD layer preparation. HD metadata now carries per-pixel BG0
visibility, and native aspect also prepares the layer stack while retaining
the original native framebuffer for fallback. Both fixes preserve DS window
masking and keep guest-visible captures on their original surfaces.

Tracking: `beads-yjp.101`, with prior internal-resolution work `beads-yjp.21`.
