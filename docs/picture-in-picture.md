# Nintendo DS picture in picture

The launcher's Display layout choices include picture in picture in each of
the four corners. Existing Stacked window and Separate windows choices keep
their saved indices and defaults. The first PiP choice places the bottom
screen at the bottom right, with the top screen as the main view.

The runner also accepts `--screen-layout pip`, `--pip-corner bottom-right`
and `--pip-main-screen top`. The corresponding `[display]` configuration
keys are `screen_layout`, `pip_corner` and `pip_main_screen`. All four
hyphenated corner names are supported. Generic hosts may make the bottom
screen the main view; Mario Kart DS and Metroid Prime Hunters disable that
capability in their launchers and reject that request in the runner.

Both LCDs preserve their aspect within the main view. The inset stays inside
the visible main content when the window is resized. Touch input maps to the
native lower-screen area, excluding adaptive margins and an occluding upper
inset. A captured FPS mouse retains its capture behavior. The virtual stylus
uses the same lower-screen rectangle.

The native display service draws both LCDs through its existing GPU shader
in one GL window. SDL/reference presentation uses the same layout rectangles.
The layout changes only host presentation; guest engine-to-LCD routing and
native save data remain unchanged.
