#!/usr/bin/env python3
"""Construye una distribución portable y reproducible de C-Forge."""

from __future__ import annotations

import argparse
import shutil
import tarfile
import zipfile
from pathlib import Path


FILES = (
    "cforgev.py",
    "compilador_nativo.py",
    "compilador_wasm.py",
    "cforge_diagnostics.py",
    "cforge_lsp.py",
    "cforge_packages.py",
    "cforge_vm.py",
    "README.md",
    "LICENSE",
    "CHANGELOG.md",
    "ESPECIFICACION.md",
    "INTEROPERABILIDAD.md",
)
DIRECTORIES = ("include", "ejemplos")
IGNORED = shutil.ignore_patterns("build", "bin", "obj", "__pycache__", "*.pyc", ".DS_Store")


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
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
            for path in sorted(stage.rglob("*")):
                if path.is_file():
                    bundle.write(path, Path(stage.name) / path.relative_to(stage))
    else:
        launcher = stage / "cforge"
        launcher.write_text(
            '#!/bin/sh\nexec python3 "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/cforgev.py" "$@"\n',
            encoding="utf-8",
        )
        launcher.chmod(0o755)
        suffix = "macos-universal" if platform_name == "macos" else "linux-x64"
        archive = output / f"cforgev-{version}-{suffix}.tar.gz"
        with tarfile.open(archive, "w:gz") as bundle:
            bundle.add(stage, arcname=stage.name)
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
