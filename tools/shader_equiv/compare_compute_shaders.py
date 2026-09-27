#!/usr/bin/env python3
"""Prove (or disprove) SPIR-V equivalence of the melonDS compute renderer's
shaders between two framework revisions.

For every CompileShader(...) call in runner/vendor/melonds/GPU3D_Compute.cpp
of each revision, this rebuilds the exact source string ComputeRenderer::
CompileShader assembles at runtime:

    <preamble>                      (desktop: "#version 430 core\\n")
    #define <each define>\\n
    #define ScreenWidth N\\n#define ScreenHeight N\\n#define RenderScale N
    \\n#define MaxWorkTiles N           (no trailing newline, as in the C++)
    ComputeRendererShaders::Common
    ComputeRendererShaders::<source>

The shader pieces are taken from each revision's GPU3D_Compute_shaders.h by
compiling a tiny C++ dumper against that header (so C++ string concatenation
and raw-literal handling are exactly the compiler's, not a regex's). The
preamble is parsed from that revision's CompileShader body (the #else /
non-NDS_GLES branch for desktop, the NDS_GLES branch for --gles).

Each (variant x width x scale) source is compiled with glslangValidator -G
(OpenGL SPIR-V), stripped of debug info, optimised with spirv-opt -O, and the
disassembly compared between the two revisions. Unoptimised (strip-only)
disassembly is compared too, as a sanity signal.

It also byte-compares the host shaders (ComputeHost.cpp / TextureUpscale.cpp
R"GLSL(...)GLSL" literals) between the revisions, and, with --gles, compiles
the head revision's GLES variants (#version 320 es + its precision header)
with glslangValidator in strict ES mode, including the host shaders after the
same "#version 430 core" replacement the head revision performs.

Needs glslangValidator / spirv-opt / spirv-diff (Vulkan SDK; override with
VULKAN_SDK) and a host g++ for the header dumper (override with GXX).

Usage (from a checkout that has both revisions):
    py -3 tools/shader_equiv/compare_compute_shaders.py \\
        --repo <ndsrecomp checkout> \\
        --base origin/main --head HEAD --out <dir> [--gles]
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from classify_spirv_diff import classify  # noqa: E402

SHADERS_H = "runner/vendor/melonds/GPU3D_Compute_shaders.h"
COMPUTE_CPP = "runner/vendor/melonds/GPU3D_Compute.cpp"
HOST_FILES = [
    "runner/src/melonds_compute/ComputeHost.cpp",
    "runner/src/melonds_compute/TextureUpscale.cpp",
]
VULKAN_BIN = Path(os.environ.get("VULKAN_SDK", r"C:\VulkanSDK\1.4.341.1")) / "Bin"
GXX = os.environ.get("GXX", r"C:\msys64\mingw64\bin\g++.exe")

# Render configurations. ScreenWidth = RenderWidth * scale, RenderWidth is
# clamp(width, 256, 448) & ~1 (GPU3D_Compute.cpp SetRenderWidth), scale 1..4
# (gpu3d.cpp kMaxInternalScale).
DEFAULT_WIDTHS = [256, 340, 448]
DEFAULT_SCALES = [1, 2, 3, 4]
TILE_SIZE = 8  # GPU3D_Compute.h TileSize


def git_show(repo: Path, ref: str, path: str) -> str:
    return subprocess.run(
        ["git", "-C", str(repo), "show", f"{ref}:{path}"],
        check=True, capture_output=True).stdout.decode("utf-8")


def parse_variants(cpp: str) -> list[tuple[str, str, list[str]]]:
    """(target, source, defines) for every CompileShader call in ShaderCompileStep."""
    out = []
    for m in re.finditer(
            r"CompileShader\(\s*([A-Za-z0-9_\[\]]+)\s*,\s*"
            r"ComputeRendererShaders::(\w+)\s*,\s*\{([^}]*)\}\s*\)", cpp):
        defines = re.findall(r'"([^"]*)"', m.group(3))
        out.append((m.group(1), m.group(2), defines))
    return out


def parse_preambles(cpp: str) -> tuple[str, str | None]:
    """(desktop_preamble, gles_preamble) from ComputeRenderer::CompileShader."""
    body = cpp[cpp.index("bool ComputeRenderer::CompileShader("):]
    body = body[:body.index("for (const char* define : defines)")]

    def lits(block: str) -> str:
        return "".join(
            bytes(s, "utf-8").decode("unicode_escape")
            for s in re.findall(r'shaderSource\s*\+=\s*"((?:[^"\\]|\\.)*)"\s*;', block))

    m = re.search(r"#if defined\(NDS_GLES\)(.*?)#else(.*?)#endif", body, re.S)
    if m:
        return lits(m.group(2)), lits(m.group(1))
    return lits(body), None


DUMPER = r'''
#include <cstdio>
#include <string>
#include "@HEADER@"
using namespace melonDS::ComputeRendererShaders;
static void put(const char* dir, const char* name, const std::string& s) {
    std::string p = std::string(dir) + "/" + name + ".glsl";
    FILE* f = std::fopen(p.c_str(), "wb");
    if (!f) { std::perror(p.c_str()); std::exit(1); }
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
}
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    put(argv[1], "Common", std::string(Common));
@PUTS@
    return 0;
}
'''


def dump_pieces(header_path: Path, sources: list[str], work: Path) -> dict[str, str]:
    work.mkdir(parents=True, exist_ok=True)
    puts = "\n".join(f'    put(argv[1], "{s}", std::string({s}));' for s in sorted(set(sources)))
    cpp = work / "dump.cpp"
    cpp.write_text(DUMPER.replace("@HEADER@", header_path.as_posix()).replace("@PUTS@", puts),
                   encoding="utf-8")
    exe = work / "dump.exe"
    env = dict(os.environ)
    env["PATH"] = str(Path(GXX).parent) + os.pathsep + env.get("PATH", "")
    subprocess.run([GXX, "-std=c++17", "-O0", "-include", "cstdlib", str(cpp), "-o", str(exe)],
                   check=True, env=env)
    outdir = work / "pieces"
    outdir.mkdir(exist_ok=True)
    subprocess.run([str(exe), str(outdir)], check=True, env=env)
    return {p.stem: p.read_bytes().decode("utf-8") for p in outdir.glob("*.glsl")}


def assemble(preamble: str, defines: list[str], width: int, scale: int,
             pieces: dict[str, str], source: str) -> str:
    render_width = max(256, min(448, width)) & ~1
    sw, sh = render_width * scale, 192 * scale
    max_work_tiles = (sw // TILE_SIZE) * (sh // TILE_SIZE) * 16
    s = preamble
    for d in defines:
        s += "#define " + d + "\n"
    s += f"#define ScreenWidth {sw}\n#define ScreenHeight {sh}\n#define RenderScale {scale}"
    s += f"\n#define MaxWorkTiles {max_work_tiles}"
    s += pieces["Common"]
    s += pieces[source]
    return s


def run(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True)


def spirv(src_path: Path, stage: str, out_dir: Path) -> dict:
    """Compile to OpenGL SPIR-V; return disassembly (raw + optimised)."""
    spv = out_dir / (src_path.stem + ".spv")
    r = run([str(VULKAN_BIN / "glslangValidator.exe"), "-G", "-S", stage,
             "-o", str(spv), str(src_path)])
    if r.returncode != 0:
        return {"ok": False, "log": r.stdout + r.stderr}
    strip = out_dir / (src_path.stem + ".strip.spv")
    opt = out_dir / (src_path.stem + ".opt.spv")
    r1 = run([str(VULKAN_BIN / "spirv-opt.exe"), "--strip-debug", str(spv), "-o", str(strip)])
    # -O, then canonicalise ids (spirv-remap equivalent) so id numbering does
    # not depend on incidental instruction order, then drop the debug names
    # that canonicalisation consults.
    r2 = run([str(VULKAN_BIN / "spirv-opt.exe"), "-O", "--canonicalize-ids", "--strip-debug",
              str(spv), "-o", str(opt)])
    if r1.returncode or r2.returncode:
        return {"ok": False, "log": r1.stderr + r2.stderr}
    dis = lambda p: run([str(VULKAN_BIN / "spirv-dis.exe"), "--raw-id", "--no-header",
                         str(p)]).stdout
    val = run([str(VULKAN_BIN / "spirv-val.exe"), "--target-env", "opengl4.5", str(opt)])
    return {"ok": True, "raw": dis(strip), "opt": dis(opt), "opt_spv": opt,
            "val": val.returncode == 0, "val_log": val.stdout + val.stderr}


def gles_check(src_path: Path, stage: str) -> tuple[bool, str]:
    r = run([str(VULKAN_BIN / "glslangValidator.exe"), "-S", stage, str(src_path)])
    return r.returncode == 0, (r.stdout + r.stderr).strip()


def host_literals(text: str) -> list[str]:
    return re.findall(r'R"GLSL\((.*?)\)GLSL"', text, re.S)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", required=True, type=Path)
    ap.add_argument("--base", required=True)
    ap.add_argument("--head", required=True)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--widths", default=",".join(map(str, DEFAULT_WIDTHS)))
    ap.add_argument("--scales", default=",".join(map(str, DEFAULT_SCALES)))
    ap.add_argument("--gles", action="store_true",
                    help="also strict-compile head's GLES variants")
    a = ap.parse_args()
    widths = [int(x) for x in a.widths.split(",")]
    scales = [int(x) for x in a.scales.split(",")]
    out = a.out
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    revs = {}
    for tag, ref in (("base", a.base), ("head", a.head)):
        d = out / tag
        d.mkdir()
        hdr = d / "GPU3D_Compute_shaders.h"
        hdr.write_text(git_show(a.repo, ref, SHADERS_H), encoding="utf-8", newline="")
        cpp = git_show(a.repo, ref, COMPUTE_CPP)
        variants = parse_variants(cpp)
        desk, gles = parse_preambles(cpp)
        pieces = dump_pieces(hdr, [v[1] for v in variants], d / "dump")
        revs[tag] = {"ref": ref, "variants": variants, "desktop": desk, "gles": gles,
                     "pieces": pieces,
                     "host": {f: git_show(a.repo, ref, f) for f in HOST_FILES}}
        print(f"[{tag}] {ref}: {len(variants)} CompileShader variants, "
              f"desktop preamble={desk!r}, gles preamble={'yes' if gles else 'none'}")

    report = {"base": a.base, "head": a.head, "widths": widths, "scales": scales,
              "variant_table_identical": revs["base"]["variants"] == revs["head"]["variants"],
              "desktop_preamble_identical": revs["base"]["desktop"] == revs["head"]["desktop"],
              "piece_identical": {k: revs["base"]["pieces"].get(k) == revs["head"]["pieces"].get(k)
                                  for k in sorted(set(revs["base"]["pieces"]) | set(revs["head"]["pieces"]))},
              "variants": [], "host": [], "gles": []}

    # --- desktop SPIR-V comparison -----------------------------------------
    variants = revs["head"]["variants"]
    by_variant: dict[str, dict] = {}
    for idx, (target, source, defines) in enumerate(variants):
        name = f"{idx:02d}_{source}_{'_'.join(defines[1:]) or 'base'}"
        for w in widths:
            for s in scales:
                cfg = f"w{w}_s{s}"
                res = {}
                for tag in ("base", "head"):
                    r = revs[tag]
                    src = assemble(r["desktop"], defines, w, s, r["pieces"], source)
                    d = out / tag / "desktop" / cfg
                    d.mkdir(parents=True, exist_ok=True)
                    p = d / f"{name}.comp"
                    p.write_text(src, encoding="utf-8", newline="")
                    res[tag] = spirv(p, "comp", d)
                    res[tag]["src_sha"] = hashlib.sha256(src.encode()).hexdigest()[:12]
                entry = {"variant": name, "target": target, "defines": defines, "cfg": cfg,
                         "compiled": res["base"]["ok"] and res["head"]["ok"]}
                if not entry["compiled"]:
                    entry["log"] = {t: res[t].get("log", "") for t in res}
                else:
                    entry["source_identical"] = res["base"]["src_sha"] == res["head"]["src_sha"]
                    entry["raw_identical"] = res["base"]["raw"] == res["head"]["raw"]
                    entry["opt_identical"] = res["base"]["opt"] == res["head"]["opt"]
                    entry["val"] = res["base"]["val"] and res["head"]["val"]
                    if not entry["opt_identical"]:
                        # spirv-diff matches ids structurally, so the output is the
                        # logical instruction delta rather than a renumbering storm.
                        sd = run([str(VULKAN_BIN / "spirv-diff.exe"), "--no-color", "--no-header",
                                  "--no-indent", str(res["base"]["opt_spv"]),
                                  str(res["head"]["opt_spv"])]).stdout
                        dp = out / "diffs" / f"{name}.{cfg}.spirv-diff.txt"
                        dp.parent.mkdir(exist_ok=True)
                        dp.write_text(sd, encoding="utf-8")
                        entry["diff_lines"] = sum(1 for l in sd.splitlines() if l[:1] in "+-")
                        entry["diff_file"] = str(dp)
                        b = classify(sd)
                        entry["neutral"] = len(b["neutral"])
                        entry["sensitive"] = b["SENSITIVE"]
                        entry["other"] = b["other"]
                report["variants"].append(entry)
                agg = by_variant.setdefault(name, {"cfgs": 0, "opt_identical": 0,
                                                   "raw_identical": 0, "failed": 0,
                                                   "only_neutral": 0, "sensitive_max": 0,
                                                   "other_max": 0})
                agg["cfgs"] += 1
                if not entry["compiled"]:
                    agg["failed"] += 1
                else:
                    agg["opt_identical"] += entry["opt_identical"]
                    agg["raw_identical"] += entry["raw_identical"]
                    if not entry["opt_identical"]:
                        ns, no = len(entry["sensitive"]), len(entry["other"])
                        agg["only_neutral"] += (ns == 0 and no == 0)
                        agg["sensitive_max"] = max(agg["sensitive_max"], ns)
                        agg["other_max"] = max(agg["other_max"], no)
    report["by_variant"] = by_variant

    # --- host shaders -------------------------------------------------------
    for f in HOST_FILES:
        lb = host_literals(revs["base"]["host"][f])
        lh = host_literals(revs["head"]["host"][f])
        report["host"].append({"file": f, "count_base": len(lb), "count_head": len(lh),
                               "identical": lb == lh})

    # --- GLES strict compile (head) ----------------------------------------
    if a.gles:
        g = revs["head"]["gles"]
        if g is None:
            print("[gles] head has no NDS_GLES preamble; skipping")
        else:
            for idx, (target, source, defines) in enumerate(variants):
                name = f"{idx:02d}_{source}_{'_'.join(defines[1:]) or 'base'}"
                for w in widths:
                    for s in scales:
                        cfg = f"w{w}_s{s}"
                        src = assemble(g, defines, w, s, revs["head"]["pieces"], source)
                        d = out / "head" / "gles" / cfg
                        d.mkdir(parents=True, exist_ok=True)
                        p = d / f"{name}.comp"
                        p.write_text(src, encoding="utf-8", newline="")
                        ok, log = gles_check(p, "comp")
                        report["gles"].append({"variant": name, "cfg": cfg, "ok": ok,
                                               "log": "" if ok else log})
            # Host shaders: same "#version 430 core" replacement each head file does.
            for f in HOST_FILES:
                text = revs["head"]["host"][f]
                # Accepts replace(pos, ver.size(), ...) and replace(0, pos + ver.size(), ...);
                # the latter also drops everything before #version.
                m = re.search(r'src\.replace\((pos, ver\.size\(\)|0, pos \+ ver\.size\(\)),\s*'
                              r'((?:"(?:[^"\\]|\\.)*"\s*)+)\)', text)
                if not m:
                    report["gles"].append({"variant": f, "cfg": "-", "ok": False,
                                           "log": "no GLES version replacement found"})
                    continue
                repl = "".join(bytes(x, "utf-8").decode("unicode_escape")
                               for x in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(2)))
                from_start = m.group(1).startswith("0")
                lits = re.findall(r'const char\* (\w+) = R"GLSL\((.*?)\)GLSL"', text, re.S)
                for lname, body in lits:
                    stage = "vert" if "Vertex" in lname else "frag" if "Fragment" in lname else "comp"
                    ver = "#version 430 core"
                    src = (repl + body[body.find(ver) + len(ver):] if from_start
                           else body.replace(ver, repl, 1))
                    d = out / "head" / "gles" / "host"
                    d.mkdir(parents=True, exist_ok=True)
                    p = d / f"{Path(f).stem}_{lname}.{stage}"
                    p.write_text(src, encoding="utf-8", newline="")
                    ok, log = gles_check(p, stage)
                    report["gles"].append({"variant": f"{Path(f).name}:{lname}", "cfg": stage,
                                           "ok": ok, "log": "" if ok else log})

    (out / "report.json").write_text(json.dumps(report, indent=1), encoding="utf-8")

    # Distinct signedness-sensitive / unclassified deltas per variant across all
    # configs: this is the list a human has to range-check against the source.
    with (out / "sensitive.txt").open("w", encoding="utf-8") as fh:
        for name in by_variant:
            seen: dict[str, list[str]] = {}
            for e in report["variants"]:
                if e["variant"] != name or not e.get("compiled") or e["opt_identical"]:
                    continue
                for t in [("S " + x) for x in e["sensitive"]] + [("O " + x) for x in e["other"]]:
                    seen.setdefault(re.sub(r"%\d+", "%_", t), []).append(e["cfg"])
            fh.write(f"== {name}\n")
            for t, cfgs in seen.items():
                fh.write(f"   {t}    [{len(cfgs)} cfgs]\n")

    # --- summary --------------------------------------------------------------
    print(f"variant table identical: {report['variant_table_identical']}; "
          f"desktop preamble identical: {report['desktop_preamble_identical']}")
    print("pieces differing:", [k for k, v in report["piece_identical"].items() if not v])
    print(f"{'variant':44s} cfgs  raw==  opt==  neutral-only  max-sensitive  max-other  fail")
    for name, agg in by_variant.items():
        print(f"{name:44s} {agg['cfgs']:4d} {agg['raw_identical']:6d} "
              f"{agg['opt_identical']:6d} {agg['only_neutral']:13d} {agg['sensitive_max']:14d} "
              f"{agg['other_max']:10d} {agg['failed']:5d}")
    for h in report["host"]:
        print(f"host {h['file']}: {h['count_base']}/{h['count_head']} literals, "
              f"identical={h['identical']}")
    if report["gles"]:
        bad = [g for g in report["gles"] if not g["ok"]]
        print(f"gles strict: {len(report['gles']) - len(bad)}/{len(report['gles'])} compiled")
        for b in bad[:20]:
            print("  FAIL", b["variant"], b["cfg"], b["log"][:400])
    nonid = [e for e in report["variants"] if e.get("compiled") and not e["opt_identical"]]
    failed = [e for e in report["variants"] if not e.get("compiled")]
    print(f"desktop: {len(report['variants'])} compiles, {len(failed)} failed, "
          f"{len(nonid)} non-identical after -O")
    for e in failed[:5]:
        print("  FAIL", e["variant"], e["cfg"], json.dumps(e["log"])[:600])
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
