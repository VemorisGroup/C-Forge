#!/usr/bin/env python3
"""Construye una distribución portable y reproducible de C-Forge."""

from __future__ import annotations

import argparse
import gzip
import shutil
import tarfile
import zipfile
from pathlib import Path


FILES = (
    "cforgev.py",
    "compilador_nativo.py",
    "compilador_wasm.py",
    "compilador_llvm.py",
    "cforge_diagnostics.py",
    "cforge_lsp.py",
    "cforge_dap.py",
    "cforge_packages.py",
    "cforge_vm.py",
    "cforge_memory.py",
    "cforge_parity.py",
    "capabilities.json",
    "README.md",
    "LICENSE",
    "CHANGELOG.md",
    "ESPECIFICACION.md",
    "INTEROPERABILIDAD.md",
    "SECURITY.md",
)
DIRECTORIES = ("include", "ejemplos", "registry", "docs", "bootstrap")
IGNORED = shutil.ignore_patterns("build", "bin", "obj", "__pycache__", "*.pyc", ".DS_Store")


def _write_reproducible_zip(stage: Path, archive: Path) -> None:
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
        for path in sorted(stage.rglob("*")):
            if not path.is_file():
                continue
            info = zipfile.ZipInfo(str(Path(stage.name) / path.relative_to(stage)))
            info.date_time = (1980, 1, 1, 0, 0, 0)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = ((0o755 if path.stat().st_mode & 0o111 else 0o644) & 0xFFFF) << 16
            info.create_system = 3
            bundle.writestr(info, path.read_bytes(), compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def _write_reproducible_tar(stage: Path, archive: Path) -> None:
    with archive.open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as bundle:
                for path in sorted(stage.rglob("*")):
                    if not path.is_file():
                        continue
                    info = bundle.gettarinfo(
                        str(path), str(Path(stage.name) / path.relative_to(stage))
                    )
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
                    with path.open("rb") as source:
                        bundle.addfile(info, source)


def build(version: str, platform_name: str, output: Path) -> Path:
    root = Path(__file__).resolve().parents[1]
    stage = output / f"cforgev-{version}-{platform_name}"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)
    for name in FILES:
        shutil.copy2(root / name, stage / name)
    for name in DIRECTORIES:
        shutil.copytree(root / name, stage / name, ignore=IGNORED)

    if platform_name == "windows":
        (stage / "cforge.cmd").write_text(
            '@echo off\r\npy -3 "%~dp0cforgev.py" %*\r\n', encoding="utf-8"
        )
        archive = output / f"cforgev-{version}-windows-x64.zip"
        _write_reproducible_zip(stage, archive)
    else:
        launcher = stage / "cforge"
        launcher.write_text(
            '#!/bin/sh\nexec python3 "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/cforgev.py" "$@"\n',
            encoding="utf-8",
        )
        launcher.chmod(0o755)
        suffix = "macos-universal" if platform_name == "macos" else "linux-x64"
        archive = output / f"cforgev-{version}-{suffix}.tar.gz"
        _write_reproducible_tar(stage, archive)
    shutil.rmtree(stage)
    return archive


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", choices=("macos", "linux", "windows"), required=True)
    parser.add_argument("--output", type=Path, default=Path("dist"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    print(build(args.version, args.platform, args.output))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
