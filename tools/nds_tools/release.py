"""Common portable release payload checks and verified ZIP writing."""
import hashlib
from pathlib import Path, PurePosixPath
import zipfile


def sha256(path):
    with Path(path).open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def validate_name(name):
    path = PurePosixPath(name)
    if (not name or "\\" in name or path.is_absolute() or ".." in path.parts
            or ":" in name or name.endswith("/")):
        raise ValueError(f"Unsafe archive path: {name}")
    if (path.suffix.lower() in {".nds", ".rom", ".sav", ".bin", ".gpr"}
            or "generated" in path.parts
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


def archive_stage(stage, out):
    stage = Path(stage).resolve()
    files = {}
    for path in stage.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Release links are not allowed: {path}")
        if path.is_file():
            path.resolve().relative_to(stage)
            files[path.relative_to(stage).as_posix()] = path
    return write_archive(out, files)
