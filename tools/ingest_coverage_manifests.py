#!/usr/bin/env python3
"""Merge player-submitted Tier-3 coverage manifests into recompiler seeds.

beads-yjp.28. The runner writes one manifest per session (see
runner/src/coverage_manifest.cpp). This turns any number of them -- from any
number of people, across any number of sessions -- into:

  * ROM-ALIAS FINDINGS: captured pages whose bytes are verbatim ROM content
    resident at a load address no bank declares. These are the highest-value
    output by a wide margin and need no dumped bytes at all, only the base.
    The first real submission was 93.5% this: the whole ARM7 module runs from
    two runtime aliases (0x037F7E50, 0x027CFBC4) while the only ARM7 bank is
    compiled at the header address 0x02380000, so all of it was interpreted.
  * a merged entry-point TOML for the call and indirect-branch targets, which
    are the addresses that behave like function entries, and
  * a seed at the START of every contiguous interpreted span, which is the only
    output that can reach code the finder never discovered at all (computed
    branches and jump tables stop it, so the span sits in a hole BETWEEN
    compiled functions and no call-target seed can ever name it).
  * one content-validated bank per captured code generation, with the bytes
    written alongside it, for genuinely runtime-materialized code (patched
    pages, decompressed-in-place regions) that exists nowhere in the ROM.
  * a ranked report of the interpreted spans that carry the most execution, so
    "what should be recompiled next" is answered by the data, not guessed.

Merge the complete desired corpus into a fresh output directory. Existing
nonempty output is rejected so stale seed files cannot survive a new ingest.
Generation-qualified seeds live in seeds/ and images/; entry_points.toml and
interpreted-ranges.json retain address-only legacy diagnostics.

Manifests arrive from other people's machines, so everything in them is
verified before use: each page's SHA-1 is recomputed from its own bytes, and
manifests whose ROM SHA-1 disagrees with the target are rejected outright
rather than blended into the corpus. They are also large -- the first was
202 MB -- so they are read incrementally, never json.load'ed whole.
"""

from __future__ import annotations

import argparse
import base64
import collections
import hashlib
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from coverage_stream import stream_manifest          # noqa: E402
from rom_alias import RomImages, resolve, undeclared  # noqa: E402

# Kinds that behave like function entries. A `root` is where native code fell
# into the interpreter, which sounds like the same thing but is not: the
# interpreter also re-enters at whatever instruction an IRQ or DMA stall
# interrupted, so over a long session roots accumulate at nearly every
# instruction of every hot interpreted function. Measured on the first real
# submission, 92% of root addresses have another root exactly one instruction
# away and the longest unbroken run is 415 instructions.
#
# So roots are an execution map, not a seed list -- but exactly ONE address per
# contiguous run of them IS a seed, and it is the only seed that can reach the
# code the interpreter is actually running (beads-yjp.55). See select_span_seeds.
PROMOTABLE_KINDS = ("call", "indirect")

# The interpreter's own PC while it executed an observed call. By construction
# an address no bank owned, so it is an interpreted-code observation exactly
# like a root -- and a better-attributed one, because it is bound to a concrete
# instruction the manifest saw retire rather than to a bitmap bit. The runner
# folds every caller past kMaxCallersPerEntry into this sentinel.
CALLER_SENTINEL = 0xFFFFFFFF

# Stride of one instruction, per mode. Roots are recorded at this granularity,
# so two roots one stride apart are consecutive instructions of one run.
MODE_STRIDE = {"arm": 4, "thumb": 2}

ARRAY_KEYS = ("entry_points_arm9", "entry_points_arm7", "pages.entries",
              "root_map")

SUPPORTED_SCHEMAS = (2, 3, 4)


def root_addresses(record, base):
    """Yield (addr, mode, hits) from a schema-4 root bitmap record.

    Schema 4 stores roots as two bitmaps over the page -- ARM at word stride,
    Thumb at halfword stride -- plus 16 per-256-byte-block hit counters, rather
    than one JSON record per address. The address set and the mode survive
    exactly; what is approximated is the per-address hit count, which is
    reported at block resolution. Nothing consumed per-address root hits: the
    range report sums them and the overlay seeder only annotates with them.
    """
    blocks = record.get("root_hits") or []
    if (blocks and (len(blocks) != 16 or any(type(h) is not int or h < 0
                                           for h in blocks))):
        raise ValueError("root_hits: expected 16 nonnegative counters")
    per_block = len(blocks)
    found: list[tuple[int, str]] = []
    for key, stride in (("root_arm", 4), ("root_thumb", 2)):
        blob = record.get(key)
        if not blob:
            continue
        raw = base64.b64decode(blob, validate=True)
        if len(raw) != 4096 // stride // 8:
            raise ValueError(f"{key}: wrong bitmap length")
        mode = "arm" if stride == 4 else "thumb"
        for byte_index, byte in enumerate(raw):
            if not byte:
                continue
            for bit in range(8):
                if byte & (1 << bit):
                    found.append(
                        (base + ((byte_index * 8 + bit) * stride), mode))

    if not per_block:
        for addr, mode in found:
            yield addr, mode, 0
        return

    # Spread each block's count across the addresses set inside it, so that
    # summing any set of addresses stays proportional and summing a whole block
    # reproduces its count exactly. Handing every address the full block total
    # instead would inflate a span's cost by its own length.
    def block_of(addr):
        return min(((addr - base) * per_block) // 4096, per_block - 1)

    per = collections.Counter(block_of(addr) for addr, _ in found)
    remainder = {b: blocks[b] % n for b, n in per.items()}
    for addr, mode in sorted(found):
        b = block_of(addr)
        share = blocks[b] // per[b]
        if remainder.get(b):
            share += 1
            remainder[b] -= 1
        yield addr, mode, share


def load_manifest(path: Path, rom_sha1: str | None, problems: list[str]):
    """Stream one manifest, returning (header, entries, pages) or None.

    The header scalars arrive before the arrays, so the ROM-SHA-1 and schema
    gates are applied as soon as they are known: a mismatched manifest costs a
    few hundred bytes of reading rather than a full parse of someone else's
    two-hundred-megabyte dump.
    """
    header: dict = {}
    entries: list[tuple[int, dict]] = []
    pages: list[dict] = []
    root_map: list[dict] = []
    checked = False
    try:
        for kind, key, value in stream_manifest(path, ARRAY_KEYS):
            if kind == "scalar":
                header[key] = value
                continue
            if not checked:
                reason = _reject_reason(header, rom_sha1)
                if reason:
                    problems.append(f"{path.name}: {reason}")
                    return None
                checked = True
            if key == "entry_points_arm9":
                entries.append((9, value))
            elif key == "entry_points_arm7":
                entries.append((7, value))
            elif key == "root_map":
                root_map.append(value)
            else:
                pages.append(value)
    except (OSError, ValueError) as error:
        problems.append(f"{path.name}: unreadable ({error})")
        return None
    if not checked:
        reason = _reject_reason(header, rom_sha1)
        if reason:
            problems.append(f"{path.name}: {reason}")
            return None
    return header, entries, pages, root_map


def _reject_reason(header: dict, rom_sha1: str | None) -> str | None:
    if header.get("kind") != "ndsrecomp-tier3-coverage":
        return "not an ndsrecomp coverage manifest"
    if header.get("schema") not in SUPPORTED_SCHEMAS:
        return (f"schema {header.get('schema')!r}, expected one of "
                f"{', '.join(str(s) for s in SUPPORTED_SCHEMAS)}")
    got = str(header.get("rom_sha1", "")).lower()
    if rom_sha1 and got != rom_sha1:
        return (f"ROM SHA-1 {got or '(none)'} does not match the target "
                f"{rom_sha1}; refusing to merge a different dump")
    return None


def valid_pc(pc, mode, base=None, size=4096):
    return (mode in MODE_STRIDE and 0 <= pc <= 0xffffffff
            and pc % MODE_STRIDE[mode] == 0
            and (base is None or base <= pc < base + size))


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("manifests", nargs="+", type=Path)
    parser.add_argument("--out", type=Path, required=True,
                        help="output directory for the seeds and images")
    parser.add_argument("--images", type=Path, default=None,
                        help="prepared ROM image directory (arm9.bin, "
                             "arm7.bin, overlays/, overlays.json). Supplying "
                             "it enables ROM-alias resolution, which is where "
                             "most of the value in a manifest actually is")
    parser.add_argument("--rom-sha1", default=None,
                        help="required ROM SHA-1; manifests that disagree are "
                             "rejected (defaults to the first manifest's)")
    parser.add_argument("--bank-prefix", default="coverage",
                        help="id prefix for the generated bank configs")
    parser.add_argument("--isa9", default="armv5te")
    parser.add_argument("--isa7", default="armv4t")
    parser.add_argument("--min-hits", type=int, default=1,
                        help="drop entry points seen fewer than this many "
                             "times across all manifests")
    parser.add_argument("--no-promote-ranges", dest="promote_ranges",
                        action="store_false",
                        help="reproduce the pre-yjp.55 policy: report the "
                             "interpreted spans but promote nothing from them, "
                             "so only call/indirect targets become seeds")
    parser.add_argument("--no-promote-callers", dest="promote_callers",
                        action="store_false",
                        help="ignore the caller field of call/indirect records "
                             "instead of treating it as an interpreted-code "
                             "observation")
    parser.add_argument("--min-span-hits", type=int, default=1,
                        help="only promote a span start whose span carries at "
                             "least this many interpreter entries")
    parser.add_argument("--max-span-seeds", type=int, default=0,
                        help="promote at most this many span starts per CPU, "
                             "hottest span first (0 = every qualifying span)")
    parser.add_argument("--owned-rows-arm9", type=Path, action="append",
                        default=[], metavar="PATH",
                        help="a generated *_dispatch.c (or a directory of "
                             "them) whose rows the ARM9 build already owns. "
                             "Repeatable. Interpreted addresses that already "
                             "have a row are removed BEFORE the runs are cut, "
                             "so the seeds are the honest remainder: no seed "
                             "is spent re-declaring covered code, and a run "
                             "that a finder stop left half-covered is split "
                             "and its uncovered part seeded on its own")
    parser.add_argument("--owned-rows-arm7", type=Path, action="append",
                        default=[], metavar="PATH",
                        help="the same, for the ARM7 banks")
    parser.set_defaults(promote_ranges=True, promote_callers=True)
    args = parser.parse_args()
    if args.out.exists() and (not args.out.is_dir() or any(args.out.iterdir())):
        parser.error("--out must be a fresh or empty directory; use a new output "
                     "path to avoid retaining stale generation seeds")

    problems: list[str] = []
    rom_sha1 = args.rom_sha1.lower() if args.rom_sha1 else None

    entries: dict[tuple[int, int, str], dict] = {}
    legacy_entries: dict[tuple[int, int, str], dict] = {}
    # (cpu, addr, sha1) -> {"bytes", "entries": {(pc, mode): slot},
    #                       "roots": {(pc, mode): hits}, "executions"}
    versions: dict[tuple[int, int, str], dict] = {}
    # cpu -> (addr, mode) -> interpreter entries. Mode is carried because a
    # span start becomes a seed and a seed without a mode cannot be compiled;
    # keying on the address alone silently merged ARM and Thumb runs.
    roots: dict[int, dict[tuple[int, str], int]] = {9: {}, 7: {}}
    callers: dict[int, dict[tuple[int, str], int]] = {9: {}, 7: {}}
    rejected: collections.Counter = collections.Counter()
    accepted = 0
    build_ids: set[str] = set()
    truncated = False

    for path in args.manifests:
        loaded = load_manifest(path, rom_sha1, problems)
        if loaded is None:
            continue
        header, manifest_entries, manifest_pages, manifest_roots = loaded
        if rom_sha1 is None:
            rom_sha1 = str(header.get("rom_sha1", "")).lower()
        accepted += 1
        if header.get("build_id"):
            build_ids.add(str(header["build_id"]))

        dropped = int(header.get("pages.dropped", 0))
        if dropped > 0:
            truncated = True
            problems.append(
                f"{path.name}: the runner hit its page-store cap, so this "
                f"manifest is INCOMPLETE ({dropped} pages were refused)")

        # schema 4 carries roots as bitmaps. The session-wide map survives
        # page eviction and part rotation, which the per-address list only did
        # because tier3's own map never forgot an address.
        for record in manifest_roots:
            cpu = int(record["cpu"])
            base = int(record["addr"], 16)
            if cpu not in roots or base % 4096 or not 0 <= base <= 0xfffff000:
                rejected["root map: invalid CPU/page"] += 1
                continue
            try:
                observed_roots = list(root_addresses(record, base))
            except ValueError:
                rejected["root map: invalid bitmap"] += 1
                continue
            for addr, mode, hits in observed_roots:
                key = (addr, mode)
                roots[cpu][key] = roots[cpu].get(key, 0) + hits

        page_size = int(header.get("pages.page_size", 4096))
        for page in manifest_pages:
            raw = base64.b64decode(page["data"])
            if len(raw) != page_size:
                rejected["page: wrong length"] += 1
                continue
            actual = hashlib.sha1(raw).hexdigest()
            if actual != page["sha1"]:
                # Never ingest bytes whose own manifest disagrees about them.
                rejected["page: SHA-1 mismatch"] += 1
                continue
            cpu = int(page["cpu"])
            addr = int(page["addr"], 16)
            if (page_size != 4096 or cpu not in roots or addr % page_size
                    or not 0 <= addr <= 0xfffff000):
                rejected["page: invalid CPU/page size/address"] += 1
                continue
            slot = versions.setdefault(
                (cpu, addr, actual),
                {"bytes": raw, "entries": {}, "roots": {}, "executions": 0,
                 "legacy": False, "modern": False})
            slot["legacy"] |= header["schema"] == 2
            slot["modern"] |= header["schema"] >= 3
            slot["executions"] += int(page.get("executions", 0))
            # Schema 4 repeats the root bitmap per captured page version, which
            # is the only GENERATION-BOUND root attribution in the manifest --
            # the session-wide root_map is keyed by page base alone, so it
            # cannot say which overlay body was resident. A bank assembled from
            # this version needs the former; the span report needs the latter.
            # Nothing read this before, so a bank could only ever be seeded
            # with call targets even when the manifest knew exactly which of
            # its own instructions the interpreter had been entered at.
            try:
                page_roots = list(root_addresses(page, addr))
            except ValueError:
                rejected["page roots: invalid bitmap"] += 1
                page_roots = []
            for root_pc, root_mode, root_hits in page_roots:
                root_key = (root_pc, root_mode)
                slot["roots"][root_key] = (
                    slot["roots"].get(root_key, 0) + root_hits)
            # schema 3 associates each entry with the exact page generation it
            # was observed under. schema 2 carried no such association, which
            # is why the old assembler had to guess by pairing equal version
            # indices across addresses; with schema 3 there is nothing to guess.
            for entry in page.get("entry_points", []):
                kind = entry.get("kind")
                pc = int(entry["addr"], 16)
                hits = int(entry.get("hits", 0))
                mode = entry["mode"]
                if not valid_pc(pc, mode, addr, page_size):
                    rejected["page entry: invalid coordinate/ISA"] += 1
                    continue
                if kind == "root":
                    root_key = (pc, mode)
                    roots[cpu][root_key] = roots[cpu].get(root_key, 0) + hits
                    slot["roots"][root_key] = (
                        slot["roots"].get(root_key, 0) + hits)
                    continue
                if kind not in PROMOTABLE_KINDS:
                    rejected[f"page entry: kind={kind}"] += 1
                    continue
                key = (pc, mode)
                held = slot["entries"].setdefault(
                    key, {"hits": 0, "kinds": set()})
                held["hits"] += hits
                held["kinds"].add(kind)
                note_caller(callers, cpu, entry, hits)

        for cpu, entry in manifest_entries:
            kind = entry.get("kind")
            pc = int(entry["addr"], 16)
            hits = int(entry.get("hits", 0))
            if not valid_pc(pc, entry["mode"]):
                rejected["entry: invalid coordinate/ISA"] += 1
                continue
            if kind == "root":
                # Schema 2/3 carried roots one JSON record per address here;
                # schema 4 moved them to root_map. Either way they now feed the
                # span promoter rather than being counted as a refusal.
                root_key = (pc, entry["mode"])
                roots[cpu][root_key] = max(roots[cpu].get(root_key, 0), hits)
                continue
            if kind not in PROMOTABLE_KINDS:
                rejected[f"entry: kind={kind}"] += 1
                continue
            slot = entries.setdefault(
                (cpu, pc, entry["mode"]), {"hits": 0, "kinds": set()})
            slot["hits"] += hits
            slot["kinds"].add(kind)
            if header["schema"] == 2:
                legacy = legacy_entries.setdefault((cpu, pc, entry["mode"]),
                    {"hits": 0, "kinds": set()})
                legacy["hits"] += hits
                legacy["kinds"].add(kind)
            note_caller(callers, cpu, entry, hits)

    if not accepted:
        for problem in problems:
            print(f"  ! {problem}", file=sys.stderr)
        print("no usable manifests", file=sys.stderr)
        return 1

    dropped_low_hits = 0
    for key in list(entries):
        if entries[key]["hits"] < args.min_hits:
            del entries[key]
            dropped_low_hits += 1

    # ---- interpreted spans -> seeds (beads-yjp.55) ------------------------
    # A caller is an address the interpreter was executing, so it belongs in
    # the same map as the roots: folded in BEFORE the runs are cut, it either
    # extends a run it is adjacent to (costing no extra seed) or forms its own.
    interpreted = {cpu: dict(hits) for cpu, hits in roots.items()}
    if args.promote_callers:
        for cpu, observed in callers.items():
            for key, hits in observed.items():
                interpreted[cpu][key] = interpreted[cpu].get(key, 0) + hits

    # An address the build already owns a dispatch row for is not a seed: the
    # interpreter cannot be entered there, and re-declaring it would split the
    # body that owns it. Removing them first is also what makes the pipeline
    # CONVERGE -- what is left is exactly the code the last build failed to
    # reach, so a run the finder walked halfway into is cut at the stop and its
    # uncovered tail becomes a seed of its own.
    owned = {9: read_dispatch_rows(args.owned_rows_arm9),
             7: read_dispatch_rows(args.owned_rows_arm7)}
    already_owned = 0
    bound_pages = {(cpu, base) for (cpu, base, _), slot in versions.items()
                   if slot["modern"]}
    for cpu, rows in owned.items():
        if not rows:
            continue
        for key in list(interpreted[cpu]):
            addr, mode = key
            if ((cpu, addr & ~0xfff) not in bound_pages
                    and (addr, 1 if mode == "thumb" else 0) in rows):
                del interpreted[cpu][key]
                already_owned += 1

    hot = hot_ranges(interpreted)
    promoted, span_stats = select_span_seeds(
        hot, promote=args.promote_ranges, min_hits=args.min_span_hits,
        limit=args.max_span_seeds)
    for (cpu, addr, mode), hits in sorted(promoted.items()):
        slot = entries.setdefault((cpu, addr, mode),
                                  {"hits": 0, "kinds": set()})
        slot["hits"] += hits
        slot["kinds"].add("span")
    args.out.mkdir(parents=True, exist_ok=True)

    # ---- ROM-alias resolution --------------------------------------------
    alias_findings: list[dict] = []
    aliased_pages = 0
    images = None
    if args.images:
        images = RomImages(args.images)
        aliases, unresolved = resolve(
            ((cpu, addr, slot["bytes"])
             for (cpu, addr, _sha), slot in versions.items()), images)
        aliased_pages = sum(len(v["addrs"]) for v in aliases.values())
        alias_findings = undeclared(aliases, images)
    else:
        problems.append(
            "--images was not supplied, so no ROM-alias resolution ran; on the "
            "first real submission that would have hidden 93.5% of the finding")

    groups, generation_report = generation_seed_groups(versions, legacy_entries, images,
        promote=args.promote_ranges, min_hits=args.min_hits,
        min_span_hits=args.min_span_hits, max_span_seeds=args.max_span_seeds,
        bank_prefix=args.bank_prefix)

    # ---- banks for genuinely runtime-materialized code --------------------
    images_dir = args.out / "images"
    banks_written = 0
    entries_in_banks = 0
    banks = [group for group in groups if group["blob"] is not None]
    if banks:
        images_dir.mkdir(parents=True, exist_ok=True)
    for group in banks:
        cpu, start, blob = group["cpu"], group["base"], group["blob"]
        run_entries = sorted((pc, mode, held)
            for (pc, mode), held in group["entries"].items())
        if not run_entries:
            continue
        entries_in_banks += len(run_entries)
        identity = hashlib.sha1(blob).hexdigest()
        stem = group["name"]
        (images_dir / f"{stem}.bin").write_bytes(blob)
        write_bank_toml(images_dir / f"{stem}.toml", stem, cpu,
                        args.isa9 if cpu == 9 else args.isa7,
                        start, len(blob), identity, run_entries, rom_sha1)
        banks_written += 1

    write_entry_toml(args.out / "entry_points.toml",
                     sorted(entries.items()), rom_sha1, accepted,
                     sorted(build_ids))

    # Only generation-qualified observations feed module/bank seed files.
    # The old flat list above remains an address-only diagnostic/legacy output.
    seeds_dir = args.out / "seeds"
    by_target: dict[str, list] = collections.defaultdict(list)
    for group in groups:
        for (addr, mode), held in sorted(group["entries"].items()):
            by_target[group["name"]].append(((group["cpu"], addr, mode), held))
    if by_target:
        seeds_dir.mkdir(parents=True, exist_ok=True)
    for name, items in sorted(by_target.items()):
        write_entry_toml(seeds_dir / f"{name}.toml", items, rom_sha1,
                         accepted, sorted(build_ids), qualified=True)

    (args.out / "interpreted-ranges.json").write_text(
        json.dumps(hot, indent=2), encoding="utf-8", newline="\n")

    report = {
        "manifests_accepted": accepted,
        "manifests_rejected": len(problems),
        "manifests_incomplete": truncated,
        "rom_sha1": rom_sha1,
        "build_ids": sorted(build_ids),
        "rom_alias_findings": [describe_alias(f) for f in alias_findings],
        "pages_resolved_to_rom": aliased_pages,
        "page_versions_total": len(versions),
        **generation_report,
        "entry_points_merged": len(entries),
        "entry_points_dropped_below_min_hits": dropped_low_hits,
        "entry_points_placed_in_banks": entries_in_banks,
        "entry_points_by_target": {name: len(items)
                                   for name, items in sorted(by_target.items())},
        "banks_written": banks_written,
        "interpreted_ranges": {
            "arm9": len(hot.get("arm9", [])),
            "arm7": len(hot.get("arm7", [])),
        },
        "span_promotion": {
            "enabled": args.promote_ranges,
            "callers_folded_in": args.promote_callers,
            "distinct_callers": sum(len(v) for v in callers.values()),
            "dispatch_rows_read": {f"arm{cpu}": len(rows)
                                   for cpu, rows in sorted(owned.items())},
            "interpreted_addresses_already_owned": already_owned,
            "min_span_hits": args.min_span_hits,
            "max_span_seeds": args.max_span_seeds,
            "seeds": len(promoted),
            "by_cpu": span_stats,
        },
        "rejected": dict(rejected),
        "problems": problems,
    }
    (args.out / "ingest-report.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8", newline="\n")
    print(json.dumps(report, indent=2))
    return 0


def describe_alias(finding: dict) -> dict:
    declared = finding["declared_base"]
    return {
        "cpu": f"arm{finding['cpu']}",
        "module": finding["module"],
        "resident_base": f"0x{finding['base']:08X}",
        "declared_base": (f"0x{declared:08X}" if declared is not None
                          else None),
        "pages": finding["pages"],
        "guest_span": (f"0x{finding['guest_span'][0]:08X}.."
                       f"0x{finding['guest_span'][1]:08X}"),
        "module_span": (f"0x{finding['module_span'][0]:X}.."
                        f"0x{finding['module_span'][1]:X}"),
        "note": "resident at a base no bank declares; recompile the module "
                "at this base, no dumped bytes required",
    }


def merge_observations(target, source):
    for key, held in source.items():
        merged = target.setdefault(key, {"hits": 0, "kinds": set()})
        merged["hits"] += held["hits"]
        merged["kinds"] |= held["kinds"]


def generation_seed_groups(versions, session_entries, images, *, promote,
                           min_hits, min_span_hits, max_span_seeds, bank_prefix):
    """Resolve and promote each body before discarding generation identity.

    Modern page-scoped observations are authoritative. An address-only session
    record (including a caller with unknown source ISA) cannot authorize a seed
    in a modern body. Schema2 retains its historical address-containment fallback
    only where the captured corpus has one version of the page.
    """
    counts = collections.Counter((cpu, base) for cpu, base, _ in versions)
    qualified = {}
    targets = {}
    missing = ambiguous = invalid_placement = legacy_seeds = 0
    for key, slot in sorted(versions.items()):
        cpu, base, _sha = key
        held = {"bytes": slot["bytes"], "entries": {}, "roots": {}}
        merge_observations(held["entries"], slot["entries"])
        if slot.get("modern"):
            held["roots"] = dict(slot["roots"])
        if slot.get("legacy") and counts[(cpu, base)] == 1:
            for (ecpu, pc, mode), observation in session_entries.items():
                if ecpu == cpu and valid_pc(pc, mode, base, len(slot["bytes"])):
                    # A page-scoped record, if present, already includes this
                    # observation. Never double its count from the session map.
                    if (pc, mode) not in held["entries"]:
                        held["entries"][(pc, mode)] = {
                            "hits": observation["hits"],
                            "kinds": set(observation["kinds"]) | {"legacy"}}
                        legacy_seeds += 1
        qualified[key] = held
        matches = images.locate_all(slot["bytes"]) if images else []
        if len(matches) == 1:
            module, offset = matches[0]
            if images.valid_placement(module, base - offset):
                targets[key] = (module, base - offset)
            else:
                invalid_placement += 1
        elif matches:
            ambiguous += 1
        else:
            missing += 1

    modules = {}
    for key, (module, base) in sorted(targets.items()):
        cpu = key[0]
        group = modules.setdefault((cpu, module, base), {
            "cpu": cpu, "module": module, "base": base, "blob": None,
            "name": f"arm{cpu}_{Path(module).stem}_{base:08x}",
            "entries": {}, "roots": {}})
        merge_observations(group["entries"], qualified[key]["entries"])
        for root, hits in qualified[key]["roots"].items():
            group["roots"][root] = group["roots"].get(root, 0) + hits
    groups = list(modules.values())
    # Unique ROM pages can share a whole-image identity across page boundaries.
    # Captured banks retain the existing conservative run assembly: never join
    # neighboring pages across an ambiguous generation.
    unresolved = [key for key in qualified if key not in targets]
    for cpu, base, blob, entries, roots in assemble_runs(
            qualified, unresolved, include_roots=True):
        identity = hashlib.sha1(blob).hexdigest()
        groups.append({"cpu": cpu, "module": None, "base": base, "blob": blob,
            "name": f"{bank_prefix}_arm{cpu}_{base:08x}_{identity[:8]}",
            "entries": {(pc, mode): held for pc, mode, held in entries},
            "roots": roots})

    # Keep existing readable filenames where unique; collision handling must
    # not depend on input order or a case-sensitive developer filesystem.
    names = collections.Counter(g["name"].casefold() for g in groups)
    for group in groups:
        if names[group["name"].casefold()] > 1:
            identity = (group["module"] or hashlib.sha1(group["blob"]).hexdigest())
            group["name"] += "_" + hashlib.sha1(identity.encode()).hexdigest()
        group["entries"] = {key: held for key, held in group["entries"].items()
                            if held["hits"] >= min_hits}

    # Rank qualified spans across ALL bodies for each CPU. A per-body limit
    # would silently multiply the requested budget by the number of overlays.
    candidates = collections.defaultdict(list)
    for group in groups:
        hot = hot_ranges({group["cpu"]: group["roots"]})
        for span in hot[f"arm{group['cpu']}"]:
            if span["entries"] >= min_span_hits:
                candidates[group["cpu"]].append((group, span))
    promoted = collections.Counter()
    for cpu, spans in candidates.items():
        spans.sort(key=lambda item: (-item[1]["entries"], item[0]["name"],
                                    item[1]["start"], item[1]["mode"]))
        chosen = spans[:max_span_seeds] if max_span_seeds > 0 else spans
        if not promote:
            continue
        for group, span in chosen:
            key = (int(span["start"], 16), span["mode"])
            merge_observations(group["entries"], {
                key: {"hits": span["entries"], "kinds": {"span"}}})
            promoted[cpu] += 1
    return sorted(groups, key=lambda g: g["name"]), {
        "page_versions_not_in_rom": missing,
        "page_versions_ambiguous_rom": ambiguous,
        "page_versions_invalid_rom_placement": invalid_placement,
        "page_versions_needing_captured_banks": len(unresolved),
        "legacy_unique_page_seeds": legacy_seeds,
        "generation_span_seeds": {f"arm{cpu}": promoted[cpu] for cpu in (9, 7)},
        "module_seed_policy": "generation-bound; schema2 unique-page fallback only",
        "flat_entry_points_policy": "address-only diagnostic/legacy; not module ownership",
    }


def assemble_runs(versions, unresolved_keys, *, include_roots=False):
    """Group unresolved page versions into contiguous, unambiguous runs.

    A run may only be extended across a page boundary when the next address has
    exactly ONE captured version. With several versions at an address there is
    nothing in the manifest that says which one was resident alongside the
    previous page, and the old assembler's answer -- pair equal indices in
    SHA-1 sort order -- concatenates unrelated overlay generations into one
    image. That is fail-safe at runtime, because dispatch validates each
    function against live bytes, but it silently wastes the coverage. Stopping
    at the ambiguity costs a shorter run and loses nothing.

    Legacy fallback is resolved per page before assembly. Never propagate an
    address-only session observation across a modern or ambiguous page here.
    """
    version_count: collections.Counter = collections.Counter()
    for cpu, addr, _sha in versions:
        version_count[(cpu, addr)] += 1

    by_cpu: dict[int, list] = collections.defaultdict(list)
    for key in unresolved_keys:
        cpu, addr, sha = key
        by_cpu[cpu].append((addr, sha, versions[key]))

    runs = []
    for cpu, items in sorted(by_cpu.items()):
        items.sort(key=lambda item: (item[0], item[1]))
        index = 0
        while index < len(items):
            addr, _sha, slot = items[index]
            page_size = len(slot["bytes"])
            chunks = [slot["bytes"]]
            run_entries = {key: dict(held, kinds=set(held["kinds"]))
                           for key, held in slot["entries"].items()}
            run_roots = dict(slot.get("roots", {}))
            start = addr
            previous = addr
            index += 1
            # Only an address with a single captured version can be appended:
            # anything else would be a guess about what was resident with it.
            while (index < len(items)
                   and version_count[(cpu, previous)] == 1):
                next_addr, _next_sha, next_slot = items[index]
                if next_addr != previous + page_size:
                    break
                if version_count[(cpu, next_addr)] != 1:
                    break
                chunks.append(next_slot["bytes"])
                for key, hits in next_slot.get("roots", {}).items():
                    run_roots[key] = run_roots.get(key, 0) + hits
                for key, held in next_slot["entries"].items():
                    merged = run_entries.setdefault(
                        key, {"hits": 0, "kinds": set()})
                    merged["hits"] += held["hits"]
                    merged["kinds"] |= held["kinds"]
                previous = next_addr
                index += 1
            blob = b"".join(chunks)
            ordered = sorted(
                (pc, mode, held) for (pc, mode), held in run_entries.items()
                if start <= pc < start + len(blob))
            run = (cpu, start, blob, ordered)
            runs.append((*run, run_roots) if include_roots else run)
    return runs


DISPATCH_ROW = re.compile(r"\{0x([0-9A-Fa-f]+)u,\s*(\d+)u,")


def read_dispatch_rows(paths) -> set[tuple[int, int]]:
    """Every (addr, thumb) a generated dispatch table already owns.

    Same row syntax the recompiler's own --preceding-dispatch reader parses.
    A path may be a generated *_dispatch.c or a directory of them, because a
    title's coverage banks number in the hundreds and naming each one on the
    command line is how a caller ends up quietly omitting some.
    """
    files: list[Path] = []
    for path in paths:
        if path.is_dir():
            files.extend(sorted(path.glob("*_dispatch.c")))
        elif path.is_file():
            files.append(path)
    rows: set[tuple[int, int]] = set()
    for path in files:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for line in handle:
                found = DISPATCH_ROW.search(line)
                if found:
                    rows.add((int(found.group(1), 16), int(found.group(2))))
    return rows


def note_caller(callers, cpu: int, entry: dict, hits: int) -> None:
    """Record the interpreted call site behind one call/indirect record.

    The runner stores the interpreter's own PC alongside every transfer it
    observed. That address is by construction code no bank owned -- the
    interpreter was executing it -- so it is an interpreted-code observation of
    exactly the same kind as a root, and a better-attributed one: it names a
    concrete instruction the manifest watched retire instead of a bitmap bit.
    Every promoter in both repos read the target and dropped this field.

    The mode recorded is the TARGET's and can differ for BLX/BX. This historic
    estimate is used only in address-only diagnostics, never in authoritative
    body-qualified seeds: neither caller ISA nor source generation is known.
    """
    raw = entry.get("caller")
    if raw is None:
        return
    addr = int(raw, 16)
    # kMaxCallersPerEntry overflow is folded into one record under this
    # sentinel; its hit count is exact but its address is not an address.
    if addr == CALLER_SENTINEL or addr == 0:
        return
    mode = entry["mode"]
    if addr % MODE_STRIDE.get(mode, 4):
        return
    callers[cpu][(addr, mode)] = callers[cpu].get((addr, mode), 0) + hits


def select_span_seeds(hot, *, promote: bool, min_hits: int, limit: int):
    """Choose one seed per contiguous interpreted span: its START address.

    This is the promotion that reaches code no call-target seed can (yjp.55).
    A call/indirect target is by definition an address some compiled function
    branched to, so the finder had already discovered it; the code the
    interpreter is actually RUNNING sits in the holes between compiled
    functions, where a computed branch or jump table stopped discovery, and the
    only record of it is the root map.

    Exactly one seed per span, not one per root, and the reason is structural
    rather than a size preference: `write_bank_dispatch` emits one dispatch row
    per INSTRUCTION across [fn.addr, fn.end_addr), so a single seed at a span
    start already yields an owning row for every instruction the finder walks
    from there. Seeding the interior roots as well would be strictly harmful --
    the finder gives each seed its own independent Function record and then
    trims the previous one's `end_addr` down to meet it, so every extra seed
    chops the body into a shorter one and suppresses the fallthrough coalescing
    the dispatch cost depends on.

    Ranking is by span cost, so a bounded `limit` spends the seed budget on the
    spans that carry the interpretation rather than on the long tail.
    """
    seeds: dict[tuple[int, int, str], int] = {}
    stats: dict[str, dict] = {}
    for name, spans in sorted(hot.items()):
        cpu = int(name[3:])
        qualifying = [s for s in spans if s["entries"] >= min_hits]
        chosen = qualifying[:limit] if limit > 0 else qualifying
        if promote:
            for span in chosen:
                key = (cpu, int(span["start"], 16), span["mode"])
                seeds[key] = seeds.get(key, 0) + span["entries"]
        stats[name] = {
            "spans": len(spans),
            "spans_above_min_hits": len(qualifying),
            "spans_promoted": len(chosen) if promote else 0,
            "entries_promoted": sum(s["entries"] for s in chosen) if promote
                                else 0,
            "entries_total": sum(s["entries"] for s in spans),
        }
    return seeds, stats


def hot_ranges(interpreted):
    """Merge interpreted addresses into contiguous spans, hottest first.

    Roots are where the interpreter was entered, and callers are where it was
    running. Individually they are poor seeds, but merged they are the honest
    answer to "which code is still being interpreted, and how much does it
    cost", which is the question a coverage submission exists to answer -- and
    each run's START is the one address in it worth seeding.

    Runs are cut per MODE and at instruction stride. Merging an ARM run with a
    Thumb one because their addresses interleave would produce a span whose
    start has no single answer to "which mode do I compile this as".
    """
    out: dict[str, list] = {}
    for cpu, hits in interpreted.items():
        spans = []
        for mode in sorted({mode for _addr, mode in hits}):
            stride = MODE_STRIDE.get(mode, 4)
            run_start = None
            previous = None
            total = 0
            for addr in sorted(a for a, m in hits if m == mode):
                if previous is not None and addr - previous <= stride:
                    total += hits[(addr, mode)]
                    previous = addr
                    continue
                if run_start is not None:
                    spans.append(_span(run_start, previous, total, mode,
                                       stride))
                run_start = addr
                previous = addr
                total = hits[(addr, mode)]
            if run_start is not None:
                spans.append(_span(run_start, previous, total, mode, stride))
        spans.sort(key=lambda span: (-span["entries"], span["start"]))
        out[f"arm{cpu}"] = spans
    return out


def _span(start: int, last: int, entries: int, mode: str,
          stride: int) -> dict:
    return {"start": f"0x{start:08X}", "end": f"0x{last + stride:08X}",
            "bytes": last + stride - start, "mode": mode, "entries": entries}


def write_bank_toml(path: Path, bank_id: str, cpu: int, isa: str,
                    load_address: int, size: int, identity: str,
                    run_entries, rom_sha1: str | None) -> None:
    lines = [
        "# AUTO-GENERATED by tools/ingest_coverage_manifests.py.",
        "# Assembled from player-submitted Tier-3 coverage manifests; every",
        "# page was SHA-1 verified against its own bytes before assembly, and",
        "# the bank is content-validated against live guest bytes at dispatch.",
        "# No unique ROM module/offset matches these bytes. Missing or ambiguous",
        "# matches retain this captured body rather than guessing ownership.",
        "",
        "[program]",
        f'name         = "coverage bank arm{cpu} @ 0x{load_address:08X}"',
        f'id           = "{bank_id}"',
        f'cpu          = "arm{cpu}"',
        f'isa          = "{isa}"',
        f"load_address = 0x{load_address:08X}",
        f"size         = 0x{size:08X}",
        f"entry_pc     = 0x{run_entries[0][0]:08X}",
        "",
        "[identity]",
        f'sha1 = "{identity}"',
    ]
    if rom_sha1:
        lines += [f'rom_sha1 = "{rom_sha1}"']
    lines.append("")
    for addr, mode, slot in run_entries:
        lines += [
            "[[entry_point]]",
            f"addr = 0x{addr:08X}",
            f'mode = "{mode}"',
            'kind = "runtime_observed"',
            f"# hits = {slot['hits']}, seen as "
            f"{'/'.join(sorted(slot['kinds']))}",
            "",
        ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_entry_toml(path: Path, addressed, rom_sha1, manifest_count,
                     build_ids, *, qualified=False) -> None:
    lines = [
        "# AUTO-GENERATED by tools/ingest_coverage_manifests.py.",
        f"# Merged from {manifest_count} player coverage manifest(s).",
        f"# ROM SHA-1: {rom_sha1}",
        f"# Runner build(s): {', '.join(build_ids) if build_ids else 'unknown'}",
        "#",
        *( ["# Generation-qualified seeds for this module/load-base or captured body.",
            "# Targets and span starts retain exact observed page provenance;",
            "# schema2 legacy targets are allowed only at single-version pages."]
           if qualified else
           ["# ADDRESS-ONLY DIAGNOSTIC / LEGACY OUTPUT. This merges generations.",
            "# Caller ISA is estimated from target ISA and can be wrong.",
            "# Do not apply this file to an overlay or bank as ownership proof.",
            "# Use generation-qualified seeds/ and images/ outputs instead."] ),
        "",
    ]
    for (cpu, addr, mode), slot in addressed:
        lines += [
            "[[entry_point]]",
            f"addr = 0x{addr:08X}",
            f'mode = "{mode}"',
            f'cpu  = "arm{cpu}"',
            'kind = "runtime_observed"',
            f"# hits = {slot['hits']}, seen as "
            f"{'/'.join(sorted(slot['kinds']))}",
            "",
        ]
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    raise SystemExit(main())
