"""Build pinned, independently selectable performance artifacts (no gameplay)."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--banks', type=Path, required=True)
    p.add_argument('--workspace', type=Path, required=True)
    p.add_argument('--title', choices=['mkds', 'mph'], required=True)
    p.add_argument('--framework-root', type=Path, help='explicit isolated source worktree')
    p.add_argument('--retained', nargs='+', choices=['reference', 'cpu', 'deadline', 'lean', 'capture'],
                   help='build only the explicitly chosen combination')
    args = p.parse_args()
    out, root = args.out.resolve(), Path(__file__).resolve().parents[1]
    if args.framework_root:
        root = args.framework_root.resolve()
    if out.drive.upper() == 'C:':
        p.error('generated data must use non-C: storage')
    out.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ)
    env.update(TEMP=str(out), TMP=str(out), CCACHE_DIR=str(out / 'ccache'),
               PYTHONPYCACHEPREFIX=str(out / 'pycache'),
               PATH='C:/msys64/mingw64/bin;' + env.get('PATH', ''))
    bins = Path('C:/msys64/mingw64/bin')
    cmake = str(bins / 'cmake.exe')
    game = args.workspace / ('mariokartdsrecomp' if args.title == 'mkds' else 'metroidprimehuntersrecomp')
    flags = dict(CMAKE_MAKE_PROGRAM=str(bins / 'ninja.exe'),
                 CMAKE_C_COMPILER=str(bins / 'gcc.exe'), CMAKE_CXX_COMPILER=str(bins / 'g++.exe'),
                 CMAKE_BUILD_TYPE='Release', NDSRECOMP_COMPILER_CACHE='OFF',
                 NDSRECOMP_COMPILE_JOBS='2', NDSRECOMP_LINK_JOBS='1',
                 NDS_BOOTSTRAP_FIRMWARE='ON', NDS_SDL_BACKEND='SDL3',
                 NDS_GENERATED_DIR=str(args.workspace / 'ndsrecomp/generated'),
                 NDS_TITLE_BANK_DIR=str(game / 'generated/recomp'),
                 NDS_RECOMP_UI_ROOT='F:/Projects/recomp-ui', NDS_BUILD_NATIVE_GL_TESTS='ON')
    if args.title == 'mkds':
        banks = json.loads(args.banks.read_text())['variants']
        flags.update(NDS_MKDS_IMPLEMENTATION='HLE', NDS_MKDS_HLE_GROUPS='VECTOR',
                     NDS_GX_COMMAND_CACHE='ON', NDS_ACCELERATED_RENDERER='COMPUTE',
                     NDS_TITLE_ROM_SHA1='691e00d9a5dd80b04f80cc7559503e8b06848785')
        specs = [('reference', 'reference', []),
                 ('deadline', 'reference', ['NDS_PERIODIC_DEADLINE_CACHE']),
                 ('lean', 'reference', ['NDS_LEAN_DIAGNOSTICS']),
                 ('cpu', 'local', []),
                 ('bundle', 'local', ['NDS_PERIODIC_DEADLINE_CACHE', 'NDS_LEAN_DIAGNOSTICS'])]
    else:
        archive = args.banks.resolve()
        banks = {'reference': dict(archive=str(archive), sha256=sha(archive))}
        flags.update(NDS_MPH_MATH_IMPLEMENTATION='HLE', NDS_MPH_SUBMIT_IMPLEMENTATION='HLE',
                     NDS_ACCELERATED_RENDERER='OPENGL',
                     NDS_TITLE_ROM_SHA1='90164d1ac127ee5f9815ea4ae7de798c7b5fc629')
        specs = [('reference', 'reference', []),
                 ('capture', 'reference', ['NDS_CAPTURE_VIEW_REUSE']),
                 ('bundle', 'reference', ['NDS_PERIODIC_DEADLINE_CACHE', 'NDS_LEAN_DIAGNOSTICS',
                                           'NDS_CAPTURE_VIEW_REUSE'])]
    if args.retained:
        if 'reference' in args.retained and args.retained != ['reference']:
            p.error('reference cannot be combined with experiments')
        if 'cpu' in args.retained and not (root / 'recompiler/armv4t/local_spans.h').exists():
            p.error('selected worktree does not contain the CPU experiment')
        if args.title == 'mph' and 'cpu' in args.retained:
            p.error('MPH span banks are not qualified or supplied')
        selections = {'deadline': 'NDS_PERIODIC_DEADLINE_CACHE', 'lean': 'NDS_LEAN_DIAGNOSTICS',
                      'capture': 'NDS_CAPTURE_VIEW_REUSE'}
        specs = [('retained', 'local' if 'cpu' in args.retained else 'reference',
                  [selections[key] for key in args.retained if key not in ('cpu', 'reference')])]
    manifest_file = out / 'artifacts.json'
    manifest = json.loads(manifest_file.read_text()) if manifest_file.exists() else {
        'title': args.title, 'source_root': str(root), 'git_base': subprocess.check_output(
            ['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
        'compiler': subprocess.check_output([str(bins / 'gcc.exe'), '--version'], text=True).splitlines()[0],
        'artifacts': {}}
    for name, bank, options in specs:
        artifact = out / name
        exe = artifact / 'nds_runner.exe'
        if name in manifest['artifacts']:
            if sha(exe) != manifest['artifacts'][name]['sha256']:
                raise RuntimeError('existing pinned artifact changed: ' + name)
            print('already built', args.title, name, flush=True)
            continue
        selected = dict(flags, NDS_PREBUILT_BANK_ARCHIVE=banks[bank]['archive'],
                        NDS_PREBUILT_BANK_ARCHIVE_SHA256=banks[bank]['sha256'])
        for key in ('NDS_PERIODIC_DEADLINE_CACHE', 'NDS_CAPTURE_VIEW_REUSE', 'NDS_LEAN_DIAGNOSTICS'):
            selected[key] = 'ON' if key in options else 'OFF'
        if 'option(NDS_PERIODIC_DEADLINE_CACHE' not in (root / 'runner/CMakeLists.txt').read_text():
            if selected['NDS_PERIODIC_DEADLINE_CACHE'] == 'ON':
                p.error('selected worktree excludes the rejected deadline experiment')
            del selected['NDS_PERIODIC_DEADLINE_CACHE']
        build = out / 'build'
        with (out / f'{name}-configure.log').open('w') as log:
            subprocess.run([cmake, '-S', str(root / 'runner'), '-B', str(build), '-G', 'Ninja',
                            *[f'-D{k}={v}' for k, v in selected.items()]],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        with (out / f'{name}-build.log').open('w') as log:
            subprocess.run([cmake, '--build', str(build), '--target', 'nds_runner', '--parallel', '2'],
                           env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
        artifact.mkdir(exist_ok=False)
        shutil.copy2(build / 'nds_runner.exe', exe)
        manifest['artifacts'][name] = dict(sha256=sha(exe), bytes=exe.stat().st_size,
            flags=selected, bank_sha256=banks[bank]['sha256'], bank_profile=bank)
        manifest_file.write_text(json.dumps(manifest, indent=2) + '\n')
        print('built', args.title, name, manifest['artifacts'][name]['sha256'], flush=True)


if __name__ == '__main__':
    main()
