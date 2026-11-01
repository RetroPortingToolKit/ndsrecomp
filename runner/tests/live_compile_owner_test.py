#!/usr/bin/env python3
"""Exercise real provider ownership/publication with controlled build boundaries.

Native generation/linking are deterministic stand-ins here, not gameplay or
native-code qualification. Actual main/index/queue/compile_page/publication and
OS locks run in separate processes. An unlocked control must duplicate work.
"""
from __future__ import annotations

import base64
import contextlib
import errno
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import compile_live_shards as tool

ROM = '1' * 40
LIBRARY = b'controlled library publication'


def wait_for(predicate, description, seconds=15):
    end = time.monotonic() + seconds
    while not predicate():
        if time.monotonic() >= end:
            raise AssertionError('timeout: ' + description)
        time.sleep(.01)


def worker(cache, role, unlocked=False):
    events = cache / 'events'
    real_lock = tool.exclusive_file_lock

    def lock(path, *args, **kwargs):
        if path.name == 'live-compile.lock':
            (events / (role + '-waiting')).touch()
            if unlocked:
                return contextlib.nullcontext()
        return real_lock(path, *args, **kwargs)

    tool.exclusive_file_lock = lock
    # Only external tool discovery/execution is replaced. All cache ownership,
    # actual config/wrapper emission, identity and publication code remains real.
    tool.provider_identity = lambda args: 'controlled-provider'
    tool.recompiler_codegen_version = lambda args: 3
    tool.find_import_lib = lambda path: path / 'unused.a'

    def generate(command):
        if role == 'retry':
            return subprocess.CompletedProcess(command, 1)
        src = Path(command[command.index('--out') + 1])
        bank = command[command.index('--bank') + 1]
        for suffix in ('.h', '.c', '_dispatch.c'):
            (src / (bank + suffix)).write_text('/* controlled source */\n', encoding='utf-8')
        return subprocess.CompletedProcess(command, 0)

    def compile_dll(args, sources, src, stage, cpu):
        marker = events / (role + '-compile.json')
        marker.write_text(json.dumps(dict(work=str(src.parent), stage=str(stage))),encoding='utf-8')
        if role == 'killed':
            # Model the actual failure mode: provider death need not kill its
            # recompiler/compiler descendants. This child writes very late.
            subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
                '--orphan', str(stage), str(events)],
                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if role in ('first', 'killed'):
            wait_for(lambda: (events / 'release').exists(), 'release first provider', 20)
        stage.write_bytes(LIBRARY)
        return True

    tool.run = generate
    tool.compile_shard_dll = compile_dll
    manifest = cache / 'snapshots' / ('manifest-0000dead-000001.json'
        if role in ('first', 'killed') else 'manifest-0000beef-000001.json')
    dummy = cache / 'recompiler'; dummy.touch(exist_ok=True)
    sys.argv = ['compile_live_shards.py', '--cache', str(cache), '--manifest', str(manifest),
        '--rom-sha1', ROM, '--recompiler', str(dummy), '--runtime-include', str(cache),
        '--runner-build', str(cache)]
    return tool.main()


def fixture(cache):
    (cache / 'events').mkdir(parents=True)
    (cache / 'snapshots').mkdir()
    blob = bytes(4096)
    page = dict(cpu=9, addr='0x02000000', sha1=hashlib.sha1(blob).hexdigest(),
        data=base64.b64encode(blob).decode(), executions=10,
        entry_points=[dict(addr='0x02000000', mode='arm', kind='call', hits=10)])
    manifest = dict(schema=3, kind='ndsrecomp-tier3-coverage', rom_sha1=ROM,
        pages=dict(entries=[page]))
    for owner in ('0000dead', '0000beef'):
        (cache / 'snapshots' / ('manifest-' + owner + '-000001.json')).write_text(
            json.dumps(manifest), encoding='utf-8')


def start(cache, role, unlocked=False):
    return subprocess.Popen([sys.executable, str(Path(__file__).resolve()),
        '--worker', str(cache), role] + (['--unlocked'] if unlocked else []),
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8')


def finish(child, check=True):
    output = child.communicate(timeout=20)[0]
    if check:
        assert child.returncode == 0, output
    return output


def interleaving(cache, unlocked):
    fixture(cache)
    events = cache / 'events'
    a = start(cache, 'first', unlocked)
    b = None
    try:
        wait_for(lambda: (events / 'first-compile.json').exists(), 'first enters native boundary')
        b = start(cache, 'second', unlocked)
        if unlocked:
            wait_for(lambda: (events / 'second-compile.json').exists(), 'control duplicates compilation')
        else:
            wait_for(lambda: (events / 'second-waiting').exists() or
                (events / 'second-compile.json').exists(), 'second reaches ownership boundary')
        (events / 'release').touch()
        finish(a, not unlocked); output = finish(b, not unlocked)
        duplicated = (events / 'second-compile.json').exists()
        assert duplicated == unlocked, 'shared key must compile once; control must duplicate it'
        if not unlocked:
            assert 'ok=0 failed=0 skipped=1' in output, output
            assert (cache / 'snapshots/manifest-0000beef-000001.json').is_file()
            index = json.loads((cache / 'live-index.json').read_text(encoding='utf-8'))
            assert len(index['captures']) == 1
            assert Path(next(iter(index['captures'].values()))['dll']).read_bytes() == LIBRARY
            completed = list((cache / 'work').glob('*/completed'))
            assert len(completed) == 1 and not list((cache / 'work').glob('*/attempt-*'))
        print('ok: ' + ('unlocked control duplicates work' if unlocked else
            'overlapping providers compile once, reuse publication, preserve waiting input'), flush=True)
    finally:
        (events / 'release').touch()
        for child in (a, b):
            if child and child.poll() is None:
                child.kill(); child.communicate()


def killed_owner(cache):
    fixture(cache)
    events = cache / 'events'
    a = start(cache, 'killed')
    b = None
    try:
        wait_for(lambda: (events / 'killed-compile.json').exists(), 'owner reaches build')
        wait_for(lambda: (events / 'orphan-ready').exists(), 'child started before owner death')
        old = json.loads((events / 'killed-compile.json').read_text(encoding='utf-8'))
        a.kill(); a.communicate()
        b = start(cache, 'recovery'); finish(b)
        new = json.loads((events / 'recovery-compile.json').read_text(encoding='utf-8'))
        assert old['work'] != new['work'] and old['stage'] != new['stage']
        index = json.loads((cache / 'live-index.json').read_text(encoding='utf-8'))
        library = Path(next(iter(index['captures'].values()))['dll'])
        (events / 'release-orphan').touch()
        wait_for(lambda: (events / 'orphan-done').exists(), 'orphan late write finished')
        assert Path(old['stage']).read_bytes() == b'late orphan output'
        assert library.read_bytes() == LIBRARY
        print('ok: killed owner releases lock; late orphan write cannot corrupt recovery publication', flush=True)
    finally:
        (events / 'release').touch(); (events / 'release-orphan').touch()
        for child in (a,b):
            if child and child.poll() is None:
                child.kill(); child.communicate()


def lock_failure(cache):
    if os.name == 'nt':
        import msvcrt as api
        name = 'locking'
    else:
        import fcntl as api
        name = 'flock'
    real = getattr(api, name)
    def fail(*args): raise OSError(errno.EIO, 'injected permanent lock failure')
    setattr(api, name, fail)
    try:
        try:
            with tool.exclusive_file_lock(cache / 'fault.lock', timeout=None):
                raise AssertionError('permanent lock failure entered critical section')
        except OSError as error:
            assert error.errno == errno.EIO
        else:
            raise AssertionError('permanent lock error must surface')
    finally:
        setattr(api, name, real)
    print('ok: permanent lock error surfaces without retrying forever', flush=True)


def retry_retention(cache):
    fixture(cache)
    for _ in range(3):
        child = start(cache, 'retry')
        output = finish(child, check=False)
        assert child.returncode == 2 and 'failed=1' in output, output
    assert len(list((cache / 'work').glob('*/completed'))) == 1
    assert not list((cache / 'work').glob('*/attempt-*'))
    print('ok: transient retries retain one completed diagnostic attempt per identity', flush=True)


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--worker':
        return worker(Path(sys.argv[2]),sys.argv[3],'--unlocked' in sys.argv)
    if len(sys.argv) > 1 and sys.argv[1] == '--orphan':
        stage, events = Path(sys.argv[2]), Path(sys.argv[3])
        (events / 'orphan-ready').touch()
        wait_for(lambda: (events / 'release-orphan').exists(), 'orphan release', 20)
        stage.write_bytes(b'late orphan output')
        (events / 'orphan-done').touch()
        return 0
    with tempfile.TemporaryDirectory(prefix='nds-compile-owner-') as temp:
        work = Path(temp)
        interleaving(work/'locked', False)
        interleaving(work/'control', True)
        killed_owner(work/'killed')
        retry_retention(work/'retry')
        lock_failure(work)
    print('shared provider ownership: all assertions hold')
    return 0


if __name__ == '__main__': raise SystemExit(main())
