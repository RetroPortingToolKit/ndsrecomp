#!/usr/bin/env python3
"""A/B two runner BINARIES against the always-on presented-frame digest ring.

tools/gpu2d_digest_ab.py compares environment legs of ONE binary. A framework
change (e.g. a shader rewrite) needs the same comparison across two builds.
This runs gpu2d_digest_ab.py once per binary with an identical recipe and a
single leg, then compares the two digest streams frame for frame.

Nothing is armed: NDS_FRAME_HASH=1 makes each runner record every presented
frame from process start; the driver only reads a window out of the ring.

Use a reset boot (--slot 0, no --rtc-host) or a pinned savestate (--slot N)
so both processes start from identical state with no host input.

To exercise the melonDS compute renderer's readback path (3D composited into
the CPU-side top/bottom surfaces that the digest hashes), force
NDS_3D_RENDERER=compute and keep the top surface off the direct-GL presenter:
--internal-resolution 1 and --adaptive-widescreen off.

Example:
  py -3 tools/shader_equiv/render_ab.py --title sm64ds \\
      --exe-a base=<base build>/nds_runner.exe --exe-b pr=<pr build>/nds_runner.exe \\
      --out-root <dir> --frames 2400 --slot 0 -- \\
      --rom <path to ROM> --config <game.toml> --cwd <dir> \\
      --bios <bios dir> --no-preset-args \\
      --runner-arg=--boot --runner-arg=direct ...
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DRIVER = HERE.parent / "gpu2d_digest_ab.py"
KEYS = ("top", "bottom", "hd", "tw", "bw", "flags")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--title", required=True)
    ap.add_argument("--exe-a", required=True, help="LABEL=PATH")
    ap.add_argument("--exe-b", required=True, help="LABEL=PATH")
    ap.add_argument("--out-root", required=True, type=Path)
    ap.add_argument("--frames", type=int, default=1800)
    ap.add_argument("--slot", type=int, default=0)
    ap.add_argument("--env", action="append", default=[],
                    help="K=V applied to both legs (e.g. NDS_3D_RENDERER=compute)")
    ap.add_argument("rest", nargs=argparse.REMAINDER,
                    help="after --: passed verbatim to gpu2d_digest_ab.py")
    a = ap.parse_args()
    rest = a.rest[1:] if a.rest[:1] == ["--"] else a.rest

    legs = []
    for spec in (a.exe_a, a.exe_b):
        label, _, path = spec.partition("=")
        out = a.out_root / label
        env_spec = label + "=" + ",".join(a.env)
        cmd = [sys.executable, str(DRIVER), "--title", a.title, "--exe", path,
               "--out-root", str(out), "--frames", str(a.frames),
               "--slot", str(a.slot), "--leg", env_spec] + rest
        print("+", " ".join(cmd), flush=True)
        rc = subprocess.run(cmd).returncode
        data = json.loads((out / "digest-ab.json").read_text(encoding="utf-8"))
        leg = data[0]
        legs.append((label, leg, rc))
        if leg.get("error"):
            print(f"leg {label} failed: {leg['error']}")
            return 2

    (la, da, _), (lb, db, _) = legs
    xa, xb = da.get("digests") or [], db.get("digests") or []
    n = min(len(xa), len(xb))
    mism = [i for i in range(n) if any(xa[i].get(k) != xb[i].get(k) for k in KEYS)]
    flags = sorted({int(x.get("flags", 0)) for x in xa[:n]})
    distinct_top = len({x.get("top") for x in xa[:n]})
    summary = {"a": la, "b": lb, "frames_a": len(xa), "frames_b": len(xb),
               "compared": n, "mismatches": len(mism), "first_mismatches": [
                   {"index": i, la: xa[i], lb: xb[i]} for i in mism[:20]],
               "flags_seen": flags, "distinct_top_hashes": distinct_top,
               "stderr_a": str(Path(da["run_dir"]) / "stderr.log"),
               "stderr_b": str(Path(db["run_dir"]) / "stderr.log")}
    (a.out_root / "render-ab-summary.json").write_text(json.dumps(summary, indent=2),
                                                       encoding="utf-8")
    print(json.dumps({k: v for k, v in summary.items() if k != "first_mismatches"},
                     indent=2))
    for m in summary["first_mismatches"][:5]:
        print("  mismatch", json.dumps(m))
    return 0 if (n and not mism) else 1


if __name__ == "__main__":
    sys.exit(main())
