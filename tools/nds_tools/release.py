"""Common portable release payload checks and verified ZIP writing."""
import hashlib
import re
import shutil
import subprocess
from pathlib import Path, PurePosixPath
import zipfile


def sha256(path):
    with Path(path).open("rb") as source:
        digest = hashlib.sha256()
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
        return digest.hexdigest()


def validate_name(name):
    path = PurePosixPath(name)
    if (not name or "\\" in name or path.is_absolute() or ".." in path.parts
            or ":" in name or name.endswith("/")):
        raise ValueError(f"Unsafe archive path: {name}")
    if (path.suffix.lower() in {".nds", ".rom", ".sav", ".ndss", ".bin", ".gpr"}
            or "generated" in {part.casefold() for part in path.parts}
            or "psx.enhancement.cd-speed" in name or "psx.enhancement.fast-loading" in name):
        raise ValueError(f"Private or derived payload: {name}")


def write_archive(out, files, extra=None):
    extra = extra or {}
    if set(files) & set(extra):
        raise ValueError("Duplicate release entries")
    names = list(files) + list(extra)
    for name in names:
        validate_name(name)
    for path in files.values():
        if not Path(path).is_file():
            raise ValueError(f"Missing release input: {path}")
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(out, "x", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, path in sorted(files.items()):
            archive.write(path, name)
        for name, data in sorted(extra.items()):
            archive.writestr(name, data)
    with zipfile.ZipFile(out) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(names):
            raise ValueError("Package integrity check failed")
        for name, path in files.items():
            if hashlib.sha256(archive.read(name)).hexdigest() != sha256(path):
                raise ValueError(f"Package content mismatch: {name}")
    return sha256(out)


def runtime_dll_files(runtime_bin, backends):
    """Select the common MinGW/SDL runtime closure before copying any file."""
    backends = tuple(dict.fromkeys(backends))
    if not backends or any(name not in {"SDL2", "SDL3"} for name in backends):
        raise ValueError("Select SDL2 and/or SDL3 for the release runtime")
    names = [name + ".dll" for name in backends]
    names += ["libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll"]
    if "SDL3" in backends:
        names.append("libiconv-2.dll")
    files = {name: Path(runtime_bin) / name for name in names}
    for source in files.values():
        if not source.is_file():
            raise ValueError(f"Required runtime DLL missing: {source}")
    return files


def stage_runtime_dlls(stage, runtime_bin, backends):
    stage = Path(stage)
    if not stage.is_dir() or stage.is_symlink():
        raise ValueError("Runtime stage must be an existing real directory")
    files = runtime_dll_files(runtime_bin, backends)
    for name in files:
        if (stage / name).exists() or (stage / name).is_symlink():
            raise ValueError(f"Runtime stage already contains {name}")
    for name, source in files.items():
        shutil.copy2(source, stage / name)
    return list(files)


def archive_stage(stage, out, objdump=None, embedded_python=False):
    stage = Path(stage).resolve()
    files = {}
    for path in stage.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Release links are not allowed: {path}")
        if path.is_file():
            path.resolve().relative_to(stage)
            files[path.relative_to(stage).as_posix()] = path
    if objdump is not None:
        check_pe_dependencies(files, objdump, embedded_python)
    return write_archive(out, files)


WINDOWS_SYSTEM_DLLS = {'userenv.dll', 'setupapi.dll', 'oleaut32.dll', 'dwrite.dll', 'opengl32.dll', 'msvcrt.dll', 'shlwapi.dll', 'psapi.dll', 'netapi32.dll', 'shell32.dll', 'normaliz.dll', 'ole32.dll', 'winmm.dll', 'version.dll', 'propsys.dll', 'ncrypt.dll', 'ws2_32.dll', 'rpcrt4.dll', 'imm32.dll', 'gdi32.dll', 'dbghelp.dll', 'wsock32.dll', 'secur32.dll', 'iphlpapi.dll', 'kernel32.dll', 'advapi32.dll', 'winhttp.dll', 'bcrypt.dll', 'wldap32.dll', 'd2d1.dll', 'user32.dll', 'crypt32.dll', 'comdlg32.dll'}


def check_pe_dependencies(files, objdump, embedded_python=False):
    """Check the existing Windows loader contract against staged paths only.

    A title with a separately validated embedded Python toolchain may opt into
    that payload's import exemption. Other releases must stage Python too.
    """
    available = {name.casefold() for name in files}
    unresolved = []
    for name, path in files.items():
        if Path(name).suffix.lower() not in {".exe", ".dll", ".pyd"}:
            continue
        report = subprocess.check_output([str(objdump), "-p", str(path)], text=True, stderr=subprocess.STDOUT)
        for dependency in re.findall(r"DLL Name:\s*(\S+)", report, re.IGNORECASE):
            lower = dependency.casefold()
            if lower in WINDOWS_SYSTEM_DLLS or lower.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            if embedded_python and lower.startswith("python3"):
                continue
            beside = (PurePosixPath(name).parent / lower).as_posix().casefold()
            if lower not in available and beside not in available:
                unresolved.append(f"{name} needs {dependency}")
    if unresolved:
        raise ValueError("DLL import closure is broken: " + "; ".join(sorted(set(unresolved))))
