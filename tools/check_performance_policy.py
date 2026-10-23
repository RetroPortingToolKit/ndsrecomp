"""Focused startup/debug-hook check; never advances a gameplay route."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import time

def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', type=Path, required=True)
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--lean', action='store_true')
    p.add_argument('--arm-profiling', action='store_true')
    p.add_argument('--port', type=int, default=19984)
    args = p.parse_args()
    out = args.out.resolve()
    if out.drive.upper() == 'C:':
        p.error('use non-C: scratch')
    out.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ)
    for name in ('NDS_PROFILE_EMU', 'NDS_PROFILE_EMU_BUS', 'NDS_PROFILE_DISPATCH',
                 'NDS_PC_PROFILE', 'NDS_HOSTPROF'):
        env.pop(name, None)
    env.update(TEMP=str(out), TMP=str(out), SDL_AUDIO_DRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NDS_DEEP_TRACE='0', NDS_3D_RENDERER='soft',
               __GL_SHADER_DISK_CACHE='0', PYTHONPYCACHEPREFIX=str(out / 'pycache'))
    if args.arm_profiling:
        env.update(NDS_PROFILE_EMU='31', NDS_PROFILE_EMU_BUS='1009',
                   NDS_PROFILE_DISPATCH='1009', NDS_PC_PROFILE='1', NDS_HOSTPROF='on')
    command = [str(args.runner.resolve()), str(args.bios.resolve()), '--serve',
               '--port', str(args.port), '--no-save', '--diagnostics', 'off', '--no-coverage-manifest']
    with (out / 'stdout.log').open('wb') as stdout, (out / 'stderr.log').open('wb') as stderr:
        process = subprocess.Popen(command, cwd=out, env=env, stdout=stdout, stderr=stderr,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    sock = None
    observations = {}
    try:
        deadline = time.monotonic() + 30
        while sock is None:
            if process.poll() is not None:
                raise RuntimeError(f'runner exited {process.returncode}')
            try:
                sock = socket.create_connection(('127.0.0.1', args.port), timeout=3)
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(.1)
        stream = sock.makefile('rwb')
        def cmd(name: str, **data: object) -> dict:
            stream.write((json.dumps(dict(cmd=name, **data)) + '\n').encode()); stream.flush()
            result = json.loads(stream.readline())
            if 'error' in result:
                raise RuntimeError(result)
            return result
        observations['policy'] = policy = cmd('performance_policy')
        enabled = args.arm_profiling or not args.lean
        assert policy['lean_diagnostics'] == args.lean, policy
        assert policy['host_sampler'] == enabled, policy
        assert policy['pc_sampling'] == enabled, policy
        assert bool(policy['dispatch_modulus']) == enabled, policy
        assert bool(policy['emu_modulus']) == enabled, policy
        cmd('deep_trace', on=0)
        count = cmd('event_counts')
        observations['vblank_anchor'] = cmd('run_to_event', event='vblank9',
            count=count['vblank9'] + 1, max_rounds=1)
        assert observations['vblank_anchor']['instruction_hook'] is False
        count = cmd('event_counts')
        observations['instruction_anchor'] = cmd('run_to_event', event='insn9',
            count=count['insn9'] + 64, max_rounds=1)
        assert observations['instruction_anchor']['instruction_hook'] is True
        cmd('deep_trace', on=1)
        count = cmd('event_counts')
        observations['traced_vblank_anchor'] = cmd('run_to_event', event='vblank9',
            count=count['vblank9'] + 1, max_rounds=1)
        assert observations['traced_vblank_anchor']['instruction_hook'] is True
        observations['passed'] = True
        print(json.dumps(observations, indent=2))
    finally:
        if sock:
            sock.close()
        process.terminate()
        process.wait(timeout=15)
        (out / 'result.json').write_text(json.dumps(observations, indent=2) + '\n')

if __name__ == '__main__':
    main()
