import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from nds_tools.debug import DebugClient

"""Shared TCP client for the ndsrecomp debug protocol (see ../TCP.md).

Speaks line-delimited JSON to either the native runtime (port 19842) or
the melonDS oracle (port 19843). Used by the diff_* probes. Version-
independent of the melonDS-side patch — it only relies on the protocol.
"""

import json
import socket




def parse_event(spec):
    """'vblank9:30' -> ('vblank9', 30)"""
    name, _, count = spec.partition(":")
    return name, int(count or 0)


def first_divergence(a, b):
    """Return (index, a_byte, b_byte) of the first differing byte, or None."""
    n = min(len(a), len(b))
    for i in range(n):
        if a[i] != b[i]:
            return i, a[i], b[i]
    if len(a) != len(b):
        return n, (a[n] if n < len(a) else None), (b[n] if n < len(b) else None)
    return None
