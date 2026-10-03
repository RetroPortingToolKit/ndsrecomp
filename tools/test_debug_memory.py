#!/usr/bin/env python3
"""Check debug RAM bounds and checkpoint restoration without running gameplay.

Connect to a disposable --serve --no-save runner before any execution. The
caller owns runner launch/cleanup and supplies a private output directory.
"""
import argparse
from pathlib import Path

from title_smoke import DebugClient


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    client = DebugClient(args.port)
    addr = 0x023F0000
    initial = client.cmd("read_mem", addr=addr, len=4)["hex"]
    checks = 0
    try:
        for invalid in (
            dict(addr=0x01FFFFFF, hex="00"),
            dict(addr=0x02400000, hex="00"),
            dict(addr=0x023FFFFF, hex="0000"),
            dict(addr=0x04000000, hex="00"),
            dict(addr=addr, cpu=8, hex="00"),
            dict(addr=addr, hex=""), dict(addr=addr, hex="0"),
            dict(addr=addr, hex="gg"), dict(addr=addr, hex="00" * 4097),
        ):
            try:
                client.cmd("write_mem", **invalid)
            except RuntimeError:
                checks += 1
            else:
                raise AssertionError(f"accepted invalid write: {invalid.keys()}")
        assert client.cmd("read_mem", addr=addr, len=4)["hex"] == initial
        for cpu, value in ((7, "aabbccdd"), (9, "12345678")):
            assert client.cmd("write_mem", cpu=cpu, addr=addr, hex=value)["written"] == 4
            assert client.cmd("read_mem", cpu=cpu, addr=addr, len=4)["hex"] == value
            checks += 1
        state = (args.out / "roundtrip.state").resolve().as_posix()
        client.cmd("state_save", path=state)
        client.cmd("write_mem", addr=addr, hex="00112233")
        client.cmd("state_load", path=state)
        assert client.cmd("read_mem", addr=addr, len=4)["hex"] == "12345678"
        checks += 1
        try:
            client.cmd("state_load", path=(args.out / "absent.state").resolve().as_posix())
        except RuntimeError:
            checks += 1
        else:
            raise AssertionError("loaded absent state")
        assert client.cmd("read_mem", addr=addr, len=4)["hex"] == "12345678"
        print(f"PASS: {checks} debug memory/checkpoint checks; no guest frames run")
    finally:
        client.cmd("write_mem", addr=addr, hex=initial)
        client.sock.close()


if __name__ == "__main__":
    main()
