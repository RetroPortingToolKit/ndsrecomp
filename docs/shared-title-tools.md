# Shared title tools

`tools/nds_tools` owns the common cartridge preparation, TCP transport and
release-archive contracts. Titles retain original-image identity, coverage,
CPU/bank names, executable-address policy, payload and license requirements.
Set `NDSRECOMP_ROOT` for an explicit framework checkout; otherwise title tools
use their pinned `ndsrecomp` submodule.

`cartridge.arm_images` decompresses ARM9 without shortening raw input.
`extract_overlay` validates size and address overflow before writing its image
and returns the common manifest row. Config writers accept title coverage and
provenance. Exact functions and TWL policies remain in each title.

`debug.DebugClient` retains the oracle host-first API; `CaptureClient` provides
the title port-first API. Both support `cmd`, `command`, fragmented responses
and buffered newline framing. Readiness waits for a listening owned process
or an observed exit. `finish_owned` closes completed owned sessions; transport
timeouts retain running work for inspection.

`cmake/NdsGeneratedBanks.cmake` provides `ndsrecomp_generate_bank` for an output
stamp, directory, byproducts, compiler arguments and dependencies. The companion
`ndsrecomp_target_generated_banks` attaches the pinned ARM C ABI include
contract, C language standard and generated-compile job limits to a library.
Titles retain source CPU definitions, registration order and copied-code
assertions. The standalone fixture in `cmake/tests/generated-bank-consumer`
compiles this facade without a retail image.

`tools/package_release.py stage-runtime` stages the selected SDL/MinGW DLL
closure before title inventories are produced. Missing input DLLs reject the
operation before any copy.

`tools/package_release.py archive --stage <directory> --out <archive.zip>` validates
relative payload names, excludes private images/saves/generated material, and
checks archived content hashes. Supply `--objdump <path>` to check Windows
DLL dependency closure. Title-specific bank, license, dependency and overlay
cache checks run before it. `tools/package_appimage.sh` provides the final
AppImage write primitive; relocation and inventory gates keep their existing
order in each title.

Run `python tools/test_shared_title_tools.py` for the image-free contract tests.
