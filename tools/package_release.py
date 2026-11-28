"""Stage shared DS runtime files or validate and archive a release payload."""
import argparse
from pathlib import Path
from nds_tools.release import archive_stage, stage_runtime_dlls
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    stage = commands.add_parser("stage-runtime")
    stage.add_argument("--stage", type=Path, required=True)
    stage.add_argument("--runtime-bin", type=Path, required=True)
    stage.add_argument("--sdl-backend", action="append", choices=("SDL2", "SDL3"), required=True)
    archive = commands.add_parser("archive")
    archive.add_argument("--stage", type=Path, required=True)
    archive.add_argument("--out", type=Path, required=True)
    archive.add_argument("--objdump", type=Path)
    archive.add_argument("--embedded-python", action="store_true")
    args = parser.parse_args()
    if args.command == "stage-runtime":
        print("Staged:", ", ".join(stage_runtime_dlls(args.stage, args.runtime_bin, args.sdl_backend)))
    else:
        print(archive_stage(args.stage, args.out, args.objdump, args.embedded_python), args.out)
