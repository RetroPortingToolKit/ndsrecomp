"""Validate and archive an already assembled DS release stage."""
import argparse
from pathlib import Path
from nds_tools.release import archive_stage
if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    print(archive_stage(args.stage, args.out), args.out)
