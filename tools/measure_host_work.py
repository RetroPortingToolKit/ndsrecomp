"""Bounded Windows host-work sample using an explicitly supplied game fixture.

Preparation is untimed and creates a checkpoint in this exact build. Measurement
uses scheduler VBlank boundaries, never instruction hooks or identity overrides.
No unrelated process is stopped or suspended. This is not WFC qualification.
"""
from __future__ import annotations
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time
import zlib


def dump(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n')


def memory_usage(pid):
    class Memory(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('faults', wintypes.DWORD)] + [
            (name, ctypes.c_size_t) for name in ('peak_working', 'working', 'peak_paged',
                'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private')]
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    psapi.GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(Memory), wintypes.DWORD]
    handle = api.OpenProcess(0x410, False, pid)
    if not handle:
        raise ctypes.WinError()
    try:
        info = Memory()
        info.cb = ctypes.sizeof(info)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(info), info.cb):
            raise ctypes.WinError()
        return dict(private_bytes=info.private, working_bytes=info.working,
                    peak_working_bytes=info.peak_working, page_faults=info.faults)
    finally:
        api.CloseHandle(handle)


def process_cycles(pid):
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    api.OpenProcess.restype = wintypes.HANDLE
    api.QueryProcessCycleTime.argtypes = [wintypes.HANDLE, ctypes.POINTER(ctypes.c_uint64)]
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = api.OpenProcess(0x1000, False, pid)
    if not handle:
        raise ctypes.WinError()
    try:
        value = ctypes.c_uint64()
        if not api.QueryProcessCycleTime(handle, ctypes.byref(value)):
            raise ctypes.WinError()
        return value.value
    finally:
        api.CloseHandle(handle)


def census(cpu_seconds):
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    class Entry(ctypes.Structure):
        _fields_ = [('dwSize', wintypes.DWORD), ('cntUsage', wintypes.DWORD),
                    ('pid', wintypes.DWORD), ('heap', ctypes.c_size_t),
                    ('module', wintypes.DWORD), ('threads', wintypes.DWORD),
                    ('parent', wintypes.DWORD), ('priority', wintypes.LONG),
                    ('flags', wintypes.DWORD), ('name', wintypes.WCHAR * 260)]
    api.CreateToolhelp32Snapshot.argtypes = [wintypes.DWORD, wintypes.DWORD]
    api.CreateToolhelp32Snapshot.restype = wintypes.HANDLE
    api.Process32FirstW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.Process32NextW.argtypes = [wintypes.HANDLE, ctypes.POINTER(Entry)]
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = api.CreateToolhelp32Snapshot(2, 0)
    if handle == wintypes.HANDLE(-1).value:
        raise ctypes.WinError()
    rows, entry = [], Entry()
    entry.dwSize = ctypes.sizeof(entry)
    try:
        more = api.Process32FirstW(handle, ctypes.byref(entry))
        while more:
            try:
                rows.append(dict(pid=entry.pid, name=entry.name, cpu=cpu_seconds(entry.pid)))
            except OSError:
                pass
            more = api.Process32NextW(handle, ctypes.byref(entry))
    finally:
        api.CloseHandle(handle)
    return dict(time=time.perf_counter(), processes=rows)


def quiet_gate(cpu_seconds, excluded, path):
    previous, stable, gate = census(cpu_seconds), 0, []
    for _ in range(18):
        time.sleep(2)
        current = census(cpu_seconds)
        old = {r['pid']: r for r in previous['processes']}
        rows = [dict(name=r['name'], pid=r['pid'], cpu=max(0, r['cpu'] - old[r['pid']]['cpu']))
                for r in current['processes'] if r['pid'] in old and r['pid'] not in excluded]
        elapsed = current['time'] - previous['time']
        cores = sum(r['cpu'] for r in rows) / elapsed
        competitors = [r for r in current['processes'] if r['pid'] not in excluded and re.search(
            r'(recomp|nds_runner|smw|cc1|clang|ninja|cmake|gcc|g\+\+)', r['name'], re.I)]
        stable = stable + 1 if cores <= .75 and not competitors else 0
        gate.append(dict(cores=cores, competitors=competitors, stable=stable,
                         top=sorted(rows, key=lambda r: -r['cpu'])[:8]))
        dump(path, gate)
        previous = current
        if stable >= 3:
            return
    raise RuntimeError('host busy; no timed sample collected')


def load_module(path):
    sys.path.insert(0, str(path.parent))
    spec = importlib.util.spec_from_file_location('explicit_fixture', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def optional_query(client, name):
    try:
        return client.cmd(name)
    except RuntimeError as error:
        if 'unknown' not in str(error):
            raise
        return dict(supported=False)


def state_sections(path):
    data = Path(path).read_bytes()
    assert data[:8] == b'NDSSTATE'
    version, count, directory, reserved = struct.unpack_from('<IIII', data, 8)
    assert version == 4 and directory == count * 32 and not reserved
    result = {}
    for i in range(count):
        tag, revision, offset, size, crc, flags = struct.unpack_from('<IIQQII', data, 24 + i * 32)
        name = tag.to_bytes(4, 'little').decode('ascii')
        payload = data[offset:offset + size]
        assert len(payload) == size and zlib.crc32(payload) == crc and not flags
        result[name] = dict(bytes=size, revision=revision, sha256=hashlib.sha256(payload).hexdigest())
    return result


def write_png(frame, path):
    width, height = frame['w'], frame['h']
    data = bytes.fromhex(frame['rgb'])
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    rows = b''.join(b'\0' + data[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def adapt_mph(fixture, args):
    """Reuse the existing data-assisted campaign fixture with explicit paths."""
    original_prepare = fixture.prepare
    original_popen = subprocess.Popen
    fixture.GAME = args.game.resolve()
    fixture.FRAMEWORK = args.bios.resolve().parent
    class Client(fixture.DebugClient):
        def scene(self):
            harvest = object.__new__(fixture.Harvest)
            harvest.c = self
            return harvest.status()
        def close(self):
            self.sock.close()
    fixture.Client = Client
    fixture.cpu_seconds = fixture.process_cpu
    def advance(self, frames):
        while frames:
            # Boot needs more than one 100M-round command budget. Keep each
            # preparation command small; never mistake exhaustion for arrival.
            count = min(frames, 600)
            result = self.cmd('run_vblanks', count=count)
            assert result['vblanks'] == count and not result['terminal'], result
            frames -= count
    fixture.DebugClient.advance = advance
    def prepare(options):
        options.exe = options.runner
        def launch(command, **kwargs):
            env = dict(kwargs['env'])
            env.update(NDS_3D_RENDERER='native-gl', NDS_HOSTPROF=options.hostprof,
                       PATH=str(options.runner.resolve().parent) + ';' + env.get('PATH', ''))
            kwargs.update(env=env, cwd=options.out)
            command += ['--diagnostics', 'off', '--no-coverage-manifest', '--network', 'off',
                        '--wfc', 'off', '--adaptive-widescreen', 'none', '--widescreen-width', '256',
                        '--internal-resolution', '1', '--texture-upscale', '1']
            return original_popen(command, **kwargs)
        subprocess.Popen = launch
        try:
            original_prepare(options)
            metadata = json.loads((options.out / 'process.json').read_text())
            metadata['renderer'] = 'native-gl'
            metadata['fixture_adapter'] = 'explicit native GL paths and matched settings'
            dump(options.out / 'process.json', metadata)
        finally:
            subprocess.Popen = original_popen
    fixture.prepare = prepare


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture-module', type=Path, required=True)
    p.add_argument('--runner', type=Path, required=True)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--checkpoint', type=Path)
    p.add_argument('--title', choices=['mkds', 'mph'], default='mkds')
    p.add_argument('--bios', type=Path)
    p.add_argument('--qualify-only', action='store_true', help='collect state evidence without claiming a performance sample')
    p.add_argument('--local-spans', choices=['0', '1'])
    p.add_argument('--profile-gpu', action='store_true', help='focused MKDS discovery; excluded from performance verdicts')
    p.add_argument('--lean', action='store_true')
    p.add_argument('--port', type=int, default=19987)
    p.add_argument('--frames', type=int, default=600)
    args = p.parse_args()
    if args.profile_gpu and args.title != 'mkds':
        p.error('--profile-gpu currently supports the MKDS fixture')
    args.out = args.out.resolve()
    if args.out.drive.upper() == 'C:' or not 60 <= args.frames <= 600:
        p.error('non-C: output and 60..600 frames required')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    os.environ.update(TEMP=str(args.out.parent), TMP=str(args.out.parent),
        PYTHONPYCACHEPREFIX=str(args.out.parent / 'pycache'),
        NDS_LIVE_OVERLAY_CACHE=str(args.out.parent / 'live-cache'),
        __GL_SHADER_DISK_CACHE='0', MESA_SHADER_CACHE_DIR=str(args.out.parent / 'shader-cache'))
    for name in ('NDS_PROFILE_EMU', 'NDS_PROFILE_EMU_BUS', 'NDS_PROFILE_DISPATCH',
                 'NDS_PC_PROFILE', 'NDS_LOCAL_REGISTER_SPANS'):
        os.environ.pop(name, None)
    if args.local_spans is not None:
        os.environ['NDS_LOCAL_REGISTER_SPANS'] = args.local_spans
    fixture = load_module(args.fixture_module.resolve())
    if args.title == 'mph':
        if not args.bios:
            p.error('MPH fixture requires --bios')
        adapt_mph(fixture, args)
    # Use production sampling defaults, including the baseline host sampler.
    args.renderer, args.expected_mask = 'compute', 1
    args.hostprof = 'off' if args.lean else 'on'
    api = ctypes.WinDLL('kernel32', use_last_error=True)
    api.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
    api.CreateMutexW.restype = wintypes.HANDLE
    api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    api.ReleaseMutex.argtypes = [wintypes.HANDLE]
    api.CloseHandle.argtypes = [wintypes.HANDLE]
    lock = api.CreateMutexW(None, False, 'Global\\CodexPerformanceMeasurement')
    if not lock or api.WaitForSingleObject(lock, 0) not in (0, 0x80):
        raise RuntimeError('another measurement owns the shared mutex')
    pid = None
    client = None
    try:
        if not args.qualify_only:
            quiet_gate(fixture.cpu_seconds, (os.getpid(),),
                       args.out.with_name(args.out.name + '-launch-gate.json'))
        fixture.Client.advance = lambda self, frames: self.cmd('run_vblanks', count=frames)
        spawn = subprocess.Popen
        def launch_profiled(*command, **kwargs):
            kwargs['env'] = dict(kwargs['env'], NDS_PROFILE_GPU='1')
            return spawn(*command, **kwargs)
        if args.profile_gpu:
            subprocess.Popen = launch_profiled
        try:
            fixture.prepare(args)
        finally:
            subprocess.Popen = spawn
        metadata = json.loads((args.out / 'process.json').read_text())
        pid = metadata['pid']
        if not args.qualify_only:
            quiet_gate(fixture.cpu_seconds, (pid, os.getpid()), args.out / 'idle-gate.json')
        client = fixture.Client(args.port)
        if args.title == 'mph':
            client.cmd('state_load', path=(args.out / 'active.state').as_posix())
            client.cmd('deep_trace', on=0)
            warm = client.cmd('run_vblanks', count=120)
            assert warm['vblanks'] == 120 and not warm['terminal'], warm
            client.cmd('state_load', path=(args.out / 'active.state').as_posix())
            client.cmd('keys', mask=fixture.RELEASED & ~(1 << fixture.KEY_BITS['l']))
        policy = client.cmd('performance_policy')
        assert not policy['instruction_hook_armed'] and not policy['deep_trace'], policy
        assert policy['lean_diagnostics'] == args.lean and policy['host_sampler'] != args.lean, policy
        before = dict(events=client.cmd('event_counts'), scene=client.scene(),
                      coverage=client.cmd('static_coverage'), spans=optional_query(client, 'local_spans'),
                      capture_snapshot=optional_query(client, 'capture_snapshot'), profile=client.cmd('profile'))
        before['host_memory'] = memory_usage(pid)
        host_before = census(fixture.cpu_seconds)
        cpu_start, cycles_start, begin = fixture.cpu_seconds(pid), process_cycles(pid), time.perf_counter()
        result = client.cmd('run_vblanks', count=args.frames)
        elapsed, cpu = time.perf_counter() - begin, fixture.cpu_seconds(pid) - cpu_start
        cycles = process_cycles(pid) - cycles_start
        host_after = census(fixture.cpu_seconds)
        assert result['vblanks'] == args.frames and not result['terminal'], result
        after = dict(events=client.cmd('event_counts'), scene=client.scene(),
                     coverage=client.cmd('static_coverage'), spans=optional_query(client, 'local_spans'),
                     dispatch=client.cmd('dispatch_stats'), gpu=client.cmd('gpu3d_backend'),
                     capture_snapshot=optional_query(client, 'capture_snapshot'), profile=client.cmd('profile'))
        after['host_memory'] = memory_usage(pid)
        if args.title == 'mkds':
            assert after['scene']['race_frames'] - before['scene']['race_frames'] == args.frames
            assert after['scene']['course'] == 14 and not after['scene']['finished']
        else:
            assert after['scene']['room'] == before['scene']['room'] == 92
            assert after['scene']['frontend_state'] == 14 and after['scene']['health'] > 0
        client.cmd('state_save', path=(args.out / 'after.state').as_posix())
        frames = {}
        for engine in ('A', 'B'):
            frame = client.cmd('framebuffer', engine=engine)
            frames[engine] = dict(w=frame['w'], h=frame['h'],
                sha256=hashlib.sha256(bytes.fromhex(frame['rgb'])).hexdigest())
            write_png(frame, args.out / f'after-{engine}.png')
        audio = client.cmd('audio_samples', start=1 << 62, count=0)
        audio = client.cmd('audio_samples', start=max(audio['oldest'], audio['produced'] - 4096), count=4096)
        audio['sha256'] = hashlib.sha256(bytes.fromhex(audio.pop('pcm_s16le'))).hexdigest()
        record = dict(identity=metadata, policy=policy, frames=args.frames, wall_seconds=elapsed,
            cpu_seconds=cpu, process_cycles=cycles, vblanks_per_second=args.frames / elapsed, before=before, after=after,
            host_before=host_before, host_after=host_after, result=result,
            sections=state_sections(args.out / 'after.state'), pixels=frames, audio=audio)
        old = {r['pid']: r for r in host_before['processes']}
        background = [dict(name=r['name'], pid=r['pid'], cpu=max(0, r['cpu'] - old[r['pid']]['cpu']))
            for r in host_after['processes'] if r['pid'] in old and r['pid'] not in (pid, os.getpid())]
        record['background_cores'] = sum(r['cpu'] for r in background) / (host_after['time'] - host_before['time'])
        record['background_top'] = sorted(background, key=lambda r: -r['cpu'])[:8]
        record['competing_processes'] = [r for r in host_after['processes'] if r['pid'] != pid and re.search(
            r'(recomp|nds_runner|smw|cc1|clang|ninja|cmake|gcc|g\+\+)', r['name'], re.I)]
        record['gpu_profile_enabled'] = args.profile_gpu
        record['performance_eligible'] = (not args.qualify_only and not args.profile_gpu and record['background_cores'] <= .75
                                          and not record['competing_processes'])
        filename = 'diagnostic.json' if args.profile_gpu else ('qualification.json' if args.qualify_only else 'measurement.json')
        dump(args.out / filename, record)
        print(json.dumps({k: record[k] for k in ('wall_seconds', 'cpu_seconds', 'vblanks_per_second')}), flush=True)
    finally:
        if client:
            client.close()
        if pid:
            subprocess.run(['taskkill.exe', '/PID', str(pid), '/T', '/F'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
        api.ReleaseMutex(lock)
        api.CloseHandle(lock)


if __name__ == '__main__':
    main()
