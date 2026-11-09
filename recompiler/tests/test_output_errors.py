"""The real compiler must report emission failures through its exit status."""

import argparse
import hashlib
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--recompiler", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    args = parser.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.work) as temporary:
        root = Path(temporary)
        image = bytes.fromhex("1e ff 2f e1")  # bx lr
        (root / "image.bin").write_bytes(image)
        (root / "image.toml").write_text(
            '[program]\nname="output fixture"\nid="fixture"\n'
            'cpu="arm9"\nisa="armv5te"\nload_address=0x02000000\n'
            'entry_pc=0x02000000\nsize=4\n[identity]\n'
            f'sha1="{hashlib.sha1(image).hexdigest()}"\n'
        )
        command = [str(args.recompiler.resolve()), "--config",
                   str(root / "image.toml"), "--bin", str(root / "image.bin"),
                   "--bank", "fixture"]

        def run(output, *extra):
            return subprocess.run(command + ["--out", str(output), *extra],
                                  capture_output=True, text=True, timeout=20)

        output = root / "success"
        output.mkdir()
        result = run(output)
        assert result.returncode == 0, result.stderr
        names = {"fixture.h", "fixture.c", "fixture_dispatch.c"}
        assert {p.name for p in output.iterdir()} == names
        assert all(p.stat().st_size for p in output.iterdir())

        def failed(result):
            assert result.returncode != 0, result.stdout + result.stderr
            assert "cannot " in result.stderr, result.stderr
            assert "[emit] bank " not in result.stdout, result.stdout

        failed(run(root / "missing"))
        cases = [(name, []) for name in sorted(names)]
        cases += [("fixture_dispatch.c", ["--dispatch-only"]),
                  ("fixture_01.c", ["--shards", "2", "--stable-address-shards"])]
        for index, (name, extra) in enumerate(cases):
            output = root / f"blocked-{index}"
            output.mkdir()
            (output / name).mkdir()  # fopen fails even when tests run as root
            failed(run(output, *extra))

        # A successful fopen followed by failed buffered output/close.
        if Path("/dev/full").exists():
            for index, name in enumerate(sorted(names)):
                output = root / f"full-{index}"
                output.mkdir()
                (output / name).symlink_to("/dev/full")
                failed(run(output))
    print("output failures return nonzero; successful emission remains intact")


if __name__ == "__main__":
    main()
