"""One explicitly selected MPH native-service preparation or timing interval.

Reuse the title's existing data-assisted fixture. Each execution model prepares
its own state; ordinary build/model identity checks remain enabled. All output
and settings live under --out. This tool never selects a workload matrix.
"""
import argparse
import ctypes
import hashlib
import json
import msvcrt
import os
from pathlib import Path
import subprocess
import time

import measure_host_work as host


def dump(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('action', choices=('prepare', 'sample', 'inspect', 'stop'))
    p.add_argument('--fixture-module', type=Path, required=True)
    p.add_argument('--runner', type=Path, required=True)
    p.add_argument('--game', type=Path, required=True)
    p.add_argument('--bios', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--model', choices=('faithful', 'performance'), required=True)
    p.add_argument('--port', type=int, default=20036)
    p.add_argument('--frames', type=int, default=600)
    p.add_argument('--checkpoint', type=Path)
    p.add_argument('--diagnostic', action='store_true')
    p.add_argument('--measurement-lock', type=Path,
                   default=Path('G:/Temp/Codex/performance-measurement.lock'))
    a = p.parse_args()
    a.out = a.out.resolve()
    if not 1 <= a.frames <= 3600:
        p.error('choose one bounded interval of 1..3600 VBlanks')
    fixture = host.load_module(a.fixture_module.resolve())
    fixture.GAME = a.game.resolve()
    fixture.FRAMEWORK = a.bios.resolve().parent
    a.exe = a.runner.resolve()
    if a.action == 'prepare':
        spawn = subprocess.Popen
        def launch(command, **kwargs):
            env = dict(kwargs['env'])
            env.update(NDS_3D_RENDERER='native-gl', NDS_HOSTPROF='on' if a.diagnostic else 'off',
                       NDS_ARM9_DIRECT_READS='1', NDS_ARM9_BATCH_WRITES='1',
                       TEMP=str(a.out), TMP=str(a.out),
                       APPDATA=str(a.out/'appdata'), LOCALAPPDATA=str(a.out/'localappdata'),
                       NDS_LIVE_OVERLAY_CACHE=str(a.out/'live-cache'),
                       __GL_SHADER_DISK_CACHE='0',
                       PATH=str(a.exe.parent)+';'+str(a.exe.parent.parent/'runtime')+';'+env.get('PATH',''))
            for key in ('NDS_ARM9_LONG_BLOCKS', 'NDS_ARM9_DATA_HANDOFFS', 'NDS_ARM9_GX_BATCH',
                        'NDS_ARM9_EVENT_WINDOWS', 'NDS_PC_PROFILE', 'NDS_PROFILE_EMU',
                        'NDS_PROFILE_DISPATCH', 'NDS_PROFILE_GPU', 'NDS_PROFILE_SCHED'):
                env.pop(key, None)
            if a.diagnostic:
                env.update(NDS_PROFILE_EMU='31',NDS_PROFILE_EMU_BUS='1009')
            command += ['--execution-model', a.model, '--diagnostics', 'off',
                        '--runtime-settings-path', str(a.out/'settings.json'),
                        '--no-coverage-manifest', '--network', 'off', '--wfc', 'off',
                        '--adaptive-widescreen', 'none', '--widescreen-width', '256',
                        '--internal-resolution', '1', '--texture-upscale', '1']
            return spawn(command, **dict(kwargs, env=env, cwd=a.out,
                creationflags=subprocess.CREATE_NO_WINDOW | subprocess.NORMAL_PRIORITY_CLASS))
        subprocess.Popen = launch
        # Existing input-only setup sometimes requests thousands of frames.
        # Bound individual debug commands, checking actual progress every time.
        def advance(client, frames):
            while frames:
                count = min(frames, 600)
                result = client.cmd('run_vblanks', count=count)
                if result['vblanks'] != count or result['terminal']:
                    raise RuntimeError(f'preparation did not advance: {result}')
                frames -= count
        fixture.DebugClient.advance = advance
        try:
            fixture.prepare(a)
        finally:
            subprocess.Popen = spawn
        metadata = json.loads((a.out/'process.json').read_text())
        from ctypes import wintypes
        api = ctypes.WinDLL('kernel32', use_last_error=True)
        api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        api.OpenProcess.restype = wintypes.HANDLE
        api.GetPriorityClass.argtypes = [wintypes.HANDLE]
        api.CloseHandle.argtypes = [wintypes.HANDLE]
        handle = api.OpenProcess(0x1000, False, metadata['pid'])
        if not handle:
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            priority = api.GetPriorityClass(handle)
            assert priority == 0x20, f'runner priority must be Normal: {priority}'
        finally:
            api.CloseHandle(handle)
        metadata.update(model=a.model, renderer='native-gl', profile=a.diagnostic,
                        priority_class=priority, source_fixture=str(a.fixture_module.resolve()))
        dump(a.out/'process.json', metadata)
        return

    metadata = json.loads((a.out/'process.json').read_text())
    if metadata['exe_sha256'] != hashlib.sha256(a.exe.read_bytes()).hexdigest() or metadata['model'] != a.model:
        raise RuntimeError('runner/model differs from the owned prepared process')
    lock = None
    mutex = None
    if a.action == 'sample':
        from ctypes import wintypes
        mutex_api = ctypes.WinDLL('kernel32', use_last_error=True)
        mutex_api.CreateMutexW.argtypes = [ctypes.c_void_p, wintypes.BOOL, wintypes.LPCWSTR]
        mutex_api.CreateMutexW.restype = wintypes.HANDLE
        mutex_api.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
        mutex_api.ReleaseMutex.argtypes = [wintypes.HANDLE]
        mutex_api.CloseHandle.argtypes = [wintypes.HANDLE]
        mutex = mutex_api.CreateMutexW(None, False, 'Global\\CodexPerformanceMeasurement')
        if not mutex or mutex_api.WaitForSingleObject(mutex, 0) not in (0, 0x80):
            raise RuntimeError('another measurement owns the shared NDS mutex')
        a.measurement_lock.parent.mkdir(parents=True, exist_ok=True)
        lock = a.measurement_lock.open('a+b')
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError:
            lock.close()
            raise RuntimeError('another workload owns the shared measurement file lock')
    client = fixture.DebugClient(metadata['port'])
    try:
        if a.action == 'stop':
            client.sock.close()
            from ctypes import wintypes
            api = ctypes.WinDLL('kernel32', use_last_error=True)
            api.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            api.OpenProcess.restype = wintypes.HANDLE
            api.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
            api.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
            api.CloseHandle.argtypes = [wintypes.HANDLE]
            handle = api.OpenProcess(0x1001, False, metadata['pid'])
            if not handle:
                raise ctypes.WinError(ctypes.get_last_error())
            try:
                image, size = ctypes.create_unicode_buffer(32768), wintypes.DWORD(32768)
                if not api.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(size)):
                    raise ctypes.WinError(ctypes.get_last_error())
                if Path(image.value).resolve() != a.exe:
                    raise RuntimeError('owned PID no longer belongs to the prepared executable')
                if not api.TerminateProcess(handle, 0):
                    raise ctypes.WinError(ctypes.get_last_error())
            finally:
                api.CloseHandle(handle)
            return
        client.cmd('state_load', path=(a.out/'active.state').as_posix())
        client.cmd('deep_trace', on=0)
        policy = client.cmd('performance_policy')
        assert not policy['instruction_hook_armed'] and not policy['deep_trace'], policy
        warm = client.cmd('run_vblanks', count=120)
        assert warm['vblanks'] == 120 and not warm['terminal'], warm
        # Continue from the equal warm-up, rather than restoring again and
        # discarding the native code cache immediately before timing.
        client.cmd('keys', mask=fixture.RELEASED & ~(1 << fixture.KEY_BITS['l']))
        if a.diagnostic:
            client.cmd('arm9_native_census',on=True,reset=True)
            assert client.cmd('hostprof_status')['running'], 'diagnostic sampler is not running'
        harvest = fixture.Harvest(client, a.out)
        try:
            def observe():
                value=dict(scene=harvest.status(), native=client.cmd('arm9_native'),
                            math=client.cmd('mph_math'), submission=client.cmd('mph_submit'),
                            events=client.cmd('event_counts'), coverage=client.cmd('static_coverage'))
                if a.diagnostic:
                    value.update(emu_attrib=client.cmd('emu_attrib'),profile=client.cmd('profile'),
                                 pc9=client.cmd('pc_hot',cpu=9,kind='exec',top=64),
                                 pc7=client.cmd('pc_hot',cpu=7,kind='exec',top=64),
                                 mem_timing=client.cmd('mem_timing_profile'))
                return value
            before = observe()
            if a.action == 'sample':
                host.quiet_gate(fixture.process_cpu, (metadata['pid'], os.getpid()), a.out/'quiet-gate.json')
            host_before = host.census(fixture.process_cpu)
            cpu_start, cycles_start, start = fixture.process_cpu(metadata['pid']), host.process_cycles(metadata['pid']), time.perf_counter()
            result = client.cmd('run_vblanks', count=a.frames)
            wall = time.perf_counter() - start
            if a.diagnostic:
                sampled=client.cmd('hostprof_dump',path=(a.out/'cpu.hostprof').as_posix(),window_ms=int(wall*1000))
                dump(a.out/'hostprof-receipt.json',sampled)
                assert sampled.get('ok'),sampled
            cpu = fixture.process_cpu(metadata['pid']) - cpu_start
            cycles = host.process_cycles(metadata['pid']) - cycles_start
            host_after = host.census(fixture.process_cpu)
            assert result['vblanks'] == a.frames and not result['terminal'], result
            after = observe()
            if a.diagnostic:
                client.cmd('arm9_native_census',on=False)
            assert after['scene']['room'] == 92 and after['scene']['health'] > 0, after['scene']
            if a.model == 'performance':
                assert after['native']['instructions'] > before['native']['instructions'], 'native CPU did not execute'
            assert after['submission']['submit_calls'] > before['submission']['submit_calls'], 'submission HLE did not execute'
            client.cmd('state_save', path=(a.out/'after.state').as_posix())
            client.cmd('state_load', path=(a.out/'after.state').as_posix())
            assert harvest.status() == after['scene'], 'current-build restore changed scene'
            for engine in ('A', 'B'):
                host.write_png(client.cmd('framebuffer', engine=engine), a.out/f'after-{engine}.png')
            old = {row['pid']: row for row in host_before['processes']}
            background = [dict(name=row['name'], pid=row['pid'], cpu=max(0,row['cpu']-old[row['pid']]['cpu']))
                          for row in host_after['processes'] if row['pid'] in old and row['pid'] not in (metadata['pid'], os.getpid())]
            record = dict(identity=metadata, policy=policy, before=before, after=after, result=result,
                          frames=a.frames, wall_seconds=wall, cpu_seconds=cpu, process_cycles=cycles,
                          background_cores=sum(r['cpu'] for r in background)/(host_after['time']-host_before['time']),
                          background_top=sorted(background,key=lambda r:-r['cpu'])[:8],
                          current_build_restore=True, performance_eligible=a.action=='sample' and not a.diagnostic)
            record['performance_eligible'] &= record['background_cores'] <= .75
            dump(a.out/('measurement.json' if a.action=='sample' else 'inspection.json'),record)
            print(json.dumps({k:record[k] for k in ('frames','wall_seconds','cpu_seconds','background_cores','performance_eligible')}),flush=True)
        finally:
            harvest.log.close()
    finally:
        client.sock.close()
        if lock:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            lock.close()
        if mutex:
            mutex_api.ReleaseMutex(mutex)
            mutex_api.CloseHandle(mutex)


if __name__ == '__main__':
    main()
