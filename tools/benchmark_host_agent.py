"""Portable headroom probe and OS-backed capacity leases (stdlib only).

Executed locally or sent through an existing strict SSH connection. Lease files
are private host-local metadata; each live agent holds its own OS file lock.
The short ledger lock protects accounting, not the entire benchmark interval.
"""
import argparse
import base64
import csv
import ctypes
import io
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import uuid

FORMAT = "codex-benchmark-capacity-v1"


class FileLock:
    def __init__(self, path, create=True):
        self.file = open(path, "a+b" if create else "r+b")
        self.file.seek(0, 2)
        if not self.file.tell():
            self.file.write(b"0")
            self.file.flush()
        self.locked = False

    def acquire(self, timeout=0):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.file.seek(0)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(self.file.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB)
                self.locked = True
                return self
            except OSError:
                if time.monotonic() >= deadline:
                    self.close()
                    raise
                time.sleep(.025)

    def close(self):
        if self.file.closed:
            return
        if self.locked:
            self.file.seek(0)
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(self.file.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(self.file, fcntl.LOCK_UN)
        self.file.close()

    def __enter__(self):
        return self.acquire(5)

    def __exit__(self, *_):
        self.close()


def command(argv):
    return subprocess.check_output(argv, text=True, timeout=8,
        creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0).strip()


def cpu_ticks():
    if os.name == "nt":
        idle, kernel, user = (ctypes.c_uint64() for _ in range(3))
        if not ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user)):
            raise ctypes.WinError()
        return idle.value, kernel.value + user.value
    values = [int(v) for v in Path('/proc/stat').read_text().splitlines()[0].split()[1:9]]
    return values[3] + values[4], sum(values)


def probe(storage):
    first = cpu_ticks()
    time.sleep(1)
    last = cpu_ticks()
    busy = 1 - (last[0] - first[0]) / max(1, last[1] - first[1])
    if os.name == "nt":
        script = "$c=@(Get-CimInstance Win32_Processor);$m=Get-CimInstance Win32_OperatingSystem;@{physical=($c|Measure-Object NumberOfCores -Sum).Sum;available=$m.FreePhysicalMemory*1024;total=$m.TotalVisibleMemorySize*1024}|ConvertTo-Json -Compress"
        info = json.loads(command([str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'), '-NoProfile', '-Command', script]))
    else:
        pairs = set()
        for entry in Path('/proc/cpuinfo').read_text().split('\n\n'):
            fields = dict(line.split(':', 1) for line in entry.splitlines() if ':' in line)
            fields = {k.strip(): v.strip() for k, v in fields.items()}
            if 'core id' in fields:
                pairs.add((fields.get('physical id', '0'), fields['core id']))
        memory = {line.split(':')[0]: int(line.split()[1])*1024 for line in Path('/proc/meminfo').read_text().splitlines() if line.startswith(('MemTotal:', 'MemAvailable:'))}
        info = dict(physical=len(pairs) or os.cpu_count(), available=memory['MemAvailable'], total=memory['MemTotal'])
    gpu, gpu_source = None, None
    nvidia = shutil.which('nvidia-smi')
    if not nvidia and os.name == 'nt':
        path = Path(os.environ['SystemRoot'])/'System32/nvidia-smi.exe'
        if path.is_file():
            nvidia = str(path)
    if nvidia:
        try:
            values = list(csv.reader(io.StringIO(command([nvidia, '--query-gpu=utilization.gpu', '--format=csv,noheader,nounits']))))
            # Conservative on multi-GPU hosts; no unverified device ownership.
            gpu = max(float(row[0]) for row in values)
            gpu_source = 'nvidia-smi utilization.gpu'
        except (OSError, ValueError, subprocess.SubprocessError):
            pass
    if gpu is None and os.name == 'nt':
        try:
            script = '@(Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine -ErrorAction Stop | Select-Object Name,UtilizationPercentage)|ConvertTo-Json -Compress'
            rows = json.loads(command([str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'), '-NoProfile','-Command',script]) or '[]')
            if isinstance(rows,dict):
                rows=[rows]
            engines={}
            for row in rows:
                engine=re.sub(r'^pid_\d+_', '', row['Name'])
                engines[engine]=engines.get(engine,0)+float(row['UtilizationPercentage'])
            if engines:
                gpu=min(100,max(engines.values()))
                gpu_source='WDDM per-engine aggregate maximum'
        except (OSError,ValueError,subprocess.SubprocessError):
            pass
    if gpu is None and os.name != 'nt':
        values = []
        for path in Path('/sys/class/drm').glob('card*/device/gpu_busy_percent'):
            try:
                values.append(float(path.read_text()))
            except (OSError, ValueError):
                pass
        if values:
            gpu, gpu_source = max(values), 'drm gpu_busy_percent'
    storage = Path(storage).expanduser()
    disk = shutil.disk_usage(storage)
    return dict(host=platform.node(), os=platform.system().lower(), timestamp=time.time(),
        physical_cores=int(info['physical']), logical_cpus=os.cpu_count(), cpu_busy_percent=100*busy,
        cpu_free_cores=int(info['physical'])*(1-busy), memory_available_gib=info['available']/2**30,
        memory_total_gib=info['total']/2**30, gpu_busy_percent=gpu, gpu_source=gpu_source,
        storage=str(storage), storage_free_gib=disk.free/2**30)


def assess(sample, requested, claims=()):
    reasons = []
    if any(not math.isfinite(requested[k]) for k in ('cpu','memory','disk','gpu')):
        reasons.append('nonfinite resource budget')
    for key in ('cpu', 'memory', 'disk'):
        if requested[key] <= 0:
            reasons.append('invalid '+key+' budget')
    if not 0 <= requested['gpu'] <= 100:
        reasons.append('invalid GPU budget')
    pending = [c['requested'] for c in claims if c['phase'] == 'pending']
    all_requests = [c['requested'] for c in claims]
    free_cpu = sample['cpu_free_cores'] - sum(c['cpu'] for c in pending)
    free_memory = sample['memory_available_gib'] - sum(c['memory'] for c in pending)
    free_gpu = None if sample['gpu_busy_percent'] is None else 100-sample['gpu_busy_percent']-sum(c['gpu'] for c in pending)
    if free_cpu < requested['cpu']+.25 or sum(c['cpu'] for c in all_requests)+requested['cpu'] > sample['physical_cores']-.25:
        reasons.append('insufficient CPU headroom/budget')
    if free_memory < requested['memory']+.5 or sum(c['memory'] for c in all_requests)+requested['memory'] > sample['memory_total_gib']-.5:
        reasons.append('insufficient RAM headroom/budget')
    if requested['gpu'] and (free_gpu is None or free_gpu < requested['gpu']+5 or sum(c['gpu'] for c in all_requests)+requested['gpu'] > 95):
        reasons.append('insufficient or unknown GPU headroom/budget')
    if sample['storage_free_gib'] < requested['disk']+1:
        reasons.append('insufficient workspace storage')
    return dict(eligible=not reasons, reasons=reasons, requested=requested,
        remaining=dict(cpu_cores=free_cpu, memory_gib=free_memory, gpu_percent=free_gpu),
        sample=sample, active_claims=len(claims), policy=FORMAT)


def write_record(path, data):
    # A killed writer must not leave a truncated accounting record that blocks
    # every other client. Only the completed JSON replaces the current record.
    temporary=path.with_suffix('.tmp')
    try:
        with temporary.open('w') as file:
            json.dump(data,file)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary,path)
    finally:
        temporary.unlink(missing_ok=True)


def active_claims(root):
    claims = []
    for path in root.glob('lease-*.json'):
        token = path.stem.removeprefix('lease-')
        if len(token) != 32 or any(c not in '0123456789abcdef' for c in token) or path.is_symlink():
            raise RuntimeError('unexpected reservation metadata: '+str(path))
        data = json.loads(path.read_text())
        if data.get('format') != FORMAT:
            raise RuntimeError('unrecognized reservation metadata')
        lock_path = path.with_suffix('.lock')
        if lock_path.is_symlink():
            raise RuntimeError('reservation lock is a symlink')
        try:
            lock = FileLock(lock_path, create=False).acquire()
        except OSError:
            claims.append(data)
        else:
            lock.close()
            # An unlocked, protocol-owned record has no surviving lease agent.
            path.unlink()
            lock_path.unlink()
    return claims


def emit(value):
    print(json.dumps(value), flush=True)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--mode', choices=('probe','reserve'), required=True)
    p.add_argument('--root', required=True)
    p.add_argument('--storage', required=True)
    p.add_argument('--requested', required=True)
    p.add_argument('--owner', default='codex')
    a = p.parse_args()
    requested = json.loads(base64.b64decode(a.requested))
    sample = probe(a.storage)
    root = Path(a.root).expanduser()
    root.mkdir(parents=True, exist_ok=True)
    lock = None
    record = None
    try:
        with FileLock(root/'ledger.lock'):
            decision = assess(sample, requested, active_claims(root))
            if a.mode == 'probe' or not decision['eligible']:
                emit(decision)
                return 0 if decision['eligible'] else 2
            token = uuid.uuid4().hex
            record = root/('lease-'+token+'.json')
            lock = FileLock(record.with_suffix('.lock')).acquire()
            data = dict(format=FORMAT, owner=a.owner, pid=os.getpid(), token=token,
                requested=requested, phase='pending', created=time.time(), host=sample['host'])
            write_record(record,data)
        emit(dict(**decision, token=token, phase='pending'))
        for line in sys.stdin:
            request = json.loads(line)
            if request['cmd'] == 'release':
                break
            if request['cmd'] == 'running':
                with FileLock(root/'ledger.lock'):
                    data['phase'] = 'running'
                    write_record(record,data)
                emit(dict(ok=True, phase='running'))
            elif request['cmd'] == 'probe':
                emit(probe(a.storage))
            else:
                raise ValueError('unknown lease request')
    finally:
        if record:
            with FileLock(root/'ledger.lock'):
                if lock:
                    lock.close()
                record.unlink(missing_ok=True)
                record.with_suffix('.lock').unlink(missing_ok=True)


if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception as error:
        emit(dict(eligible=False, error=str(error)))
        sys.exit(1)
