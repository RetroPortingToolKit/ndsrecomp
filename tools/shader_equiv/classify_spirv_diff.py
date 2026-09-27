#!/usr/bin/env python3
"""Classify the instruction delta in a spirv-diff output file.

spirv-diff (run on canonicalised, -O'd modules) reports a delta in the source
module's id space. Much of it is not a semantic change:
  * OpUndef results are never matched across modules, so every use of an
    undef shows up as a -/+ pair that differs only in the undef id;
  * two's-complement-neutral signedness churn: the same bits produced through
    OpIAdd/OpISub/OpIMul/OpSelect/OpShiftLeftLogical/OpBitwise* on an int vs
    a uint type, plus the OpBitcast that moves to the other side of it.

This module strips undef-id-only pairs and then buckets every remaining
instruction into:
  neutral   - opcode whose result bits do not depend on operand signedness
  SENSITIVE - opcode whose result depends on signedness (signed/unsigned
              compare, arithmetic vs logical right shift, S/U div/rem/mod,
              S/U min/max/clamp, S/U bitfield extract, S/U convert, FindS/UMsb,
              abs/sign)
  other     - anything else (constant/type decls, labels, control flow)

Only SENSITIVE instructions can change results, and only for operand values
where the signed and unsigned interpretations disagree (>= 2^31 as uint /
negative as int). Those are what a human must range-check against the source.
"""
from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path

NEUTRAL = {
    "OpIAdd", "OpISub", "OpIMul", "OpSelect", "OpBitcast", "OpShiftLeftLogical",
    "OpBitwiseAnd", "OpBitwiseOr", "OpBitwiseXor", "OpNot", "OpIEqual", "OpINotEqual",
    "OpSNegate", "OpBitFieldInsert", "OpBitCount", "OpCompositeConstruct",
    "OpCompositeInsert", "OpCompositeExtract", "OpAccessChain", "OpLoad", "OpStore",
    "OpPhi", "OpCopyObject", "OpIAddCarry", "OpUMulExtended", "OpVectorShuffle",
}
SENSITIVE = {
    "OpSLessThan", "OpULessThan", "OpSLessThanEqual", "OpULessThanEqual",
    "OpSGreaterThan", "OpUGreaterThan", "OpSGreaterThanEqual", "OpUGreaterThanEqual",
    "OpShiftRightArithmetic", "OpShiftRightLogical", "OpSDiv", "OpUDiv", "OpSRem",
    "OpSMod", "OpUMod", "OpBitFieldSExtract", "OpBitFieldUExtract", "OpSConvert",
    "OpUConvert", "OpConvertSToF", "OpConvertUToF", "OpConvertFToS", "OpConvertFToU",
    "OpSMulExtended",
}
SENSITIVE_EXT = {"SMin", "UMin", "SMax", "UMax", "SClamp", "UClamp", "SAbs", "SSign",
                 "FindSMsb", "FindUMsb"}

LINE = re.compile(r"^([+-])(.*)$")


def opcode(text: str) -> str:
    m = re.search(r"\b(Op\w+)\b", text)
    op = m.group(1) if m else "?"
    if op == "OpExtInst":
        e = re.search(r"OpExtInst %\w+ %\w+ (\w+)", text)
        if e:
            return "ext:" + e.group(1)
    return op


def classify(diff_text: str) -> dict:
    removed, added = [], []
    undef_ids = set()
    for raw in diff_text.splitlines():
        m = LINE.match(raw)
        if not m:
            continue
        sign, text = m.group(1), m.group(2).strip()
        u = re.match(r"(%\w+) = OpUndef", text)
        if u:
            undef_ids.add(u.group(1))
        (removed if sign == "-" else added).append(text)

    def norm(t: str) -> str:
        return re.sub(r"%\w+", lambda mm: "%UNDEF" if mm.group(0) in undef_ids else mm.group(0), t)

    rc, ac = Counter(norm(t) for t in removed), Counter(norm(t) for t in added)
    common = rc & ac
    rc -= common
    ac -= common
    buckets = {"neutral": [], "SENSITIVE": [], "other": []}
    for sign, cnt in (("-", rc), ("+", ac)):
        for text, n in cnt.items():
            if "OpUndef" in text:
                continue
            op = opcode(text)
            if op.startswith("ext:"):
                b = "SENSITIVE" if op[4:] in SENSITIVE_EXT else "neutral"
            elif op in SENSITIVE:
                b = "SENSITIVE"
            elif op in NEUTRAL:
                b = "neutral"
            else:
                b = "other"
            buckets[b].extend([sign + text] * n)
    return buckets


def main() -> int:
    for p in sys.argv[1:]:
        b = classify(Path(p).read_text(encoding="utf-8"))
        print(f"== {Path(p).name}: neutral={len(b['neutral'])} "
              f"SENSITIVE={len(b['SENSITIVE'])} other={len(b['other'])}")
        for k in ("SENSITIVE", "other"):
            for t in b[k]:
                print(f"   [{k}] {t}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
