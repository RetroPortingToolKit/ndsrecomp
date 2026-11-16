# Native 3D service

`NDS_3D_GEOMETRY=NATIVE` selects a host geometry service in place of the
cycle-scheduled geometry engine. The default remains `REFERENCE`; select that
option for the separate reference build. The raster backends and their licenses
remain unchanged.

The native service consumes complete command-memory DMA submissions as jobs,
direct command-port writes, or native mesh operations. It decodes complete
packed lists once, reuses their immutable operations, and executes them against
live matrix, lighting, material and texture state. It produces clipped geometry
for the existing raster interfaces. Its three frame banks reuse storage.

Command DMA has no FIFO refill quota or per-word cycle walk in this path.
Geometry no longer runs in each scheduler round and does not create FIFO stall
debt. Card-ready DMA retains its request-driven behavior. Command-port writes
can continue in native ARM9 execution until an actual interrupt, DMA, halt,
debug or execution-budget boundary requires a handoff. Status/test reads flush
the operations needed for that observation. Pending swaps retain their frame
boundary and preserve subsequent commands for the next frame.

Guest-visible matrix/test results, GX status, stack-error acknowledgement,
overflow acknowledgement and level IRQ behavior remain available. Current-build
states include native geometry, partial command input, pending swaps and frame
banks. Load validates the stream before replacing live state; host command/mesh
caches are cleared rather than serialized. States from different geometry
implementations are intentionally incompatible.

Windows SDL3 Native/Performance builds automatically select GPU-resident display
when the renderer supports Direct3D/OpenGL sharing. CPU scanline work produces
compact OBJ/window metadata or general 2D survivors independently for each row;
the GPU performs the final 3D blend for either LCD. Native and adaptive views
retain their own HUD/window selection. Owned GPU images preserve the observed
frame across subsequent rendering, and shared bottom targets detach before SDL
renderer resets. Actual capture, explicit observations and snapshot export can
obtain canonical native images on demand. CPU 2D rasterization remains.

`NDS_3D_SURVIVORS=0` selects the previous display path. Automatic selection leaves
Reference/Faithful builds on that path, and unsupported sharing capabilities
retain the existing renderer fallback.

## Native model input

For the supported MKDS image, `NDS_MKDS_MODEL_IMPLEMENTATION=HLE` also replaces
the complete one-material/one-shape model draw operation. It requires native
geometry and MKDS HLE; `LLE` retains the guest caller. Code and dependent helper
identities are validated before binding the replacement.

The operation accepts a model resource, material ID, shape ID and material-send
flag. It applies resource scaling, live material and native texture transforms,
then submits a native mesh packet. It avoids constructing packed material words
or submitting mesh command DMA on this path. General scene/SBC traversal remains
guest code and can use the native command-input service normally.

Mesh caching uses physical storage identity and mapped page generations. Writes
on a shared material page trigger an exact mesh-content check; a real mesh
change recompiles the packet. Mapping changes, reset and load invalidate the
cache. The 32,768-word mesh limit exceeds the largest shape observed in the
supported retail resource inventory (6,371 words); modified resources need their
own qualification.

## Evidence and implementation provenance

Focused tests cover command continuation, matrix stacks and arithmetic,
clipping/strip reuse, live material replay, culling tests, status/IRQ behavior,
capacity limits, state validation, model-resource mapping/invalidation and
raster-interface fog/depth/texture behavior. The core and model arithmetic also
compile and pass tests on Linux using the portable C++ implementation. A
separate reference runner compiles without the native geometry/model objects.

Bounded hidden Windows frontend samples retained audio emulation with output
discarded and diagnostic counters enabled equally. Against the same composition
baseline, the native model package used 8.304 to 7.257 ms of process CPU per
frontend frame in MKDS; the native geometry package used 7.283 to 6.224 ms in
MPH. These are sampled shared-host results, not general throughput guarantees.
The samples reported zero geometry-stall rounds in both native paths. Final
combined integration and practical review remain separate gates.

A subsequent matched sample compared GPU-resident display against the retained
native geometry/model baseline. MKDS process CPU was neutral within variation
(8.873 to 8.925 ms/frame), with wall time 7.069 to 6.466 ms/frame. MPH used
6.276 to 5.458 ms of process CPU and 4.993 to 4.402 ms of wall time per frame.
Both candidates recorded zero ordinary 3D readback calls. These bounded hidden,
unpaced samples are shared-host observations, not general throughput guarantees.

`native_3d.*`, `native_model.*` and `hle_mkds_model.*` are original MIT code.
The geometry implementation was derived from the public
[GBATEK hardware contract](https://problemkaputt.de/gbatek.htm#ds3dvideo), with
focused comparisons against public device outputs. Model resource/caller
semantics were derived from the supported image and resource observations.
No SDK implementation or vendored geometry implementation was copied. The
adapter retains the existing raster ABI and does not replace its licensed code.
