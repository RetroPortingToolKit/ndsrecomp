# MKDS adaptive visibility

Owner report and issue: `beads-q4q.14`. The final native optimization builds
were accepted; disappearing shells and other karts in the adaptive margins
are a separate correctness fix. Original optimization measurements remain in
`native-burndown-20261027.json`; they do not measure this added visibility fix.

Two missing paths caused early rejection before rendering:

- `SubmitVertex` already widened clip X for an expanded full viewport, but
  hardware `BOX_TEST` still used the original clip volume. Apply the same
  horizontal transformation to the bounding box. Preserve guest matrices,
  command timing, vertical/depth clipping, native and partial viewports, and
  the existing guest-wide projection exemption.
- The earlier map-object plane fix `d001c06` existed on a separate branch and
  was absent from engine main `88ca461`. Reuse it, but select the active
  render camera (`race_getCamera`, AMCE0 `0217AA4C`) rather than the player
  camera (`0217AC74`). The prepared race uses different cameras. Use actual
  output width, bounded by the configured maximum. The ROM identity, code,
  camera and normalized-plane guards remain in place.

Focused verification: real geometry FIFO BOX_TEST first fails the adaptive
right-margin case before the fix, then passes left/right margins and resizing;
outside-band, vertical, near/far, native, subviewport and guest-wide cases
retain their clipping behavior. Title-patch tests cover distinct player/render
cameras, changed FOV, current-width calculation, idempotence after state load,
code guards and preservation of unrelated RAM. Built-game evidence is retained
under `G:/Temp/Codex/ndsrecomp/native-services-20261026/followup/culling`.

Validation is a focused scene check, not another performance/gameplay matrix.
Native save files and the already accepted owner observations are preserved.
