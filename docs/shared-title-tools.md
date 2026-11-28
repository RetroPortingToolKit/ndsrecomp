# Shared title tools

`tools/nds_tools` owns the common cartridge preparation, TCP transport and
release-archive contracts. A title keeps its original-image identity, coverage,
CPU/bank names, executable address policy, payload and license requirements.
Set `NDSRECOMP_ROOT` when running a title tool against an explicit framework
checkout. The default is the title's pinned `ndsrecomp` submodule.

`cartridge.arm_images` decompresses the ARM9 image without shortening raw
inputs. `extract_overlay` validates size and address overflow before writing
its image and returns the common manifest record. Config writers accept title
coverage and provenance; title-specific exact functions or TWL policies remain
in the consumer.

`debug.DebugClient` retains the oracle host-first API and `CaptureClient`
provides the title port-first API. Both support `cmd`, `command`, fragmented
responses and buffered newline framing. Readiness waits for a listening owned
process or an observed process exit, without an elapsed startup deadline.
`finish_owned` closes completed owned sessions; a transport timeout leaves
running work available for inspection.

`cmake/NdsGeneratedBanks.cmake` provides `ndsrecomp_generate_bank`: an output
stamp, directory, byproducts, compiler arguments and dependencies. Titles keep
registration order, CPU profiles and copied-code bank assertions.

`tools/package_release.py --stage <directory> --out <archive.zip>` validates
relative payload names, excludes private images/saves/generated material, and
checks archived content hashes. It runs after title-specific staging and bank,
license, dependency or overlay-cache checks. `tools/package_appimage.sh`
provides only the final AppImage write primitive; platform-specific relocation
and inventory checks retain their existing order in each consumer.

Run `python tools/test_shared_title_tools.py` for the image-free contract tests.
