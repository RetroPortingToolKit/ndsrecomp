#!/usr/bin/env python3
"""Private-image synthetic packet callers; never enters gameplay or writes saves."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--runner', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--native-regions', action='store_true')
    p.add_argument('--packet-hle', action='store_true')
    p.add_argument('--port', type=int, default=19955)
    args = p.parse_args()
    sys.path.insert(0, str(args.game.resolve() / 'tools'))
    from test_mkds_hle_integration import Arm
    from capture_mkds_checkpoints import DebugClient, wait_for_server
    rom = args.game.resolve() / 'Mario Kart DS.nds'
    assert hashlib.sha1(rom.read_bytes()).hexdigest() == '691e00d9a5dd80b04f80cc7559503e8b06848785'
    framework = Path(__file__).resolve().parents[1]
    arm9 = (args.game / 'generated/inputs/arm9.bin').read_bytes()
    itcm = (args.game / 'generated/inputs/copied/mkds_arm9_itcm.bin').read_bytes()
    args.out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ, NDS_HOSTPROF='off')
    if os.name == 'nt':
        env['PATH'] = r'C:\msys64\mingw64\bin;' + env['PATH']
    command = [str(args.runner.resolve()), str(framework / 'bios'), '--serve', '--port', str(args.port),
               '--rom', str(rom), '--config', str(args.game.resolve() / 'game.toml'),
               '--boot', 'direct', '--freebios', '--generated-firmware', '--no-save']
    report = dict(ok=False, command=command, runner_sha256=hashlib.sha256(args.runner.read_bytes()).hexdigest())
    with (args.out/'stdout.log').open('wb') as out, (args.out/'stderr.log').open('wb') as err:
        proc = subprocess.Popen(command, cwd=args.out.resolve(), env=env, stdout=out, stderr=err,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        client = None
        try:
            wait_for_server(args.port, proc)
            client = DebugClient(args.port, timeout=90)
            c = client.command
            def write(address, data):
                for pos in range(0, len(data), 4096):
                    c('write_mem', addr=address+pos, hex=data[pos:pos+4096].hex())
            def words(address, values):
                write(address, struct.pack('<'+'I'*len(values), *values))
            def read(address, n):
                return list(struct.unpack('<'+'I'*n, bytes.fromhex(c('read_mem', addr=address, len=n*4)['hex'])))
            c('deep_trace', on=0)
            write(0x02000000, arm9)
            # The debugger writes canonical main RAM only. Install the ITCM
            # image through real guest stores, also giving it write provenance.
            write(0x02310000, itcm)
            words(0x02380000, [0xEAFFFFFE])
            source = 0x02300000
            words(source, [1, 2, 3, 4, 5])
            buffers = [0x02301000, 0x02302000, 0x02303000]
            for buffer in buffers:
                words(buffer, [0]*32)
            a = Arm(0x02000800)
            a.load(13, 0x02370000)
            a.load(0, 0x20)
            a.emit(0xEE090F31)  # ITCM region: mirrored below main RAM
            a.load(0, 0x42000)
            a.emit(0xEE010F10)  # enable ITCM and high vectors
            a.load(0, 0x02310000)
            a.load(1, 0x01ff8000)
            a.load(2, len(itcm)//4)
            for insn in [0xE4903004, 0xE4813004, 0xE2522001, 0x1AFFFFFB]:
                a.emit(insn)
            for r in range(4, 12):
                a.load(r, 0xF00D0000+r)
            original = struct.unpack_from('<I', itcm, 0x109c)[0]
            for index, buffer in enumerate(buffers):
                # Mutate a warmed native entry, then restore it. The changed
                # store moves the command to +8; the subsequent copy replaces
                # it. Stale native/HLE execution would leave the command at +4.
                if index == 1:
                    a.store(0x01ff909c, (original & ~0xfff) | 8)
                elif index == 2:
                    a.store(0x01ff909c, original)
                a.store(0x021731e4, buffer)
                a.store(0x021731e8, 1)
                for _ in range(2):
                    a.call(0x01ff9048, 0x10101010, source, 5)
            stop, code = a.finish()
            write(a.base, code)
            report['run'] = c('run_rounds', count=20000)
            report['regs'] = c('regs')
            report['links'] = c('direct_link')
            report['hle'] = c('mkds_hle')
            report['buffers'] = [read(b, 13) for b in buffers]
            assert report['regs']['r'][15] == stop, report
            normal = [12, 0x10101010, 1, 2, 3, 4, 5, 0x10101010, 1, 2, 3, 4, 5]
            changed = [12, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5]
            assert report['buffers'] == [normal, changed, normal], report
            assert report['regs']['r'][4:12] == [0xF00D0000+r for r in range(4, 12)], report
            if args.native_regions:
                assert report['links']['native_region_entries'] > 0, report
            if args.packet_hle:
                assert report['hle']['packet_buffered'] == 4, report
            report['ok'] = True
        finally:
            (args.out/'result.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
            if client:
                client.close()
            if proc.poll() is None:
                proc.terminate()
            proc.wait(timeout=10)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
