"""Gestor local, reproducible y seguro de paquetes C-Forge."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

from cforgev import CForgevError


MANIFEST = "cforge.json"
LOCKFILE = "cforge.lock"
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,63}$")


def _load(root: Path) -> dict[str, object]:
    path = root / MANIFEST
    if not path.exists():
        raise CForgevError("No existe cforge.json; ejecuta 'cforge pkg init'")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CForgevError(f"Manifiesto inválido: {error}") from error
    if not isinstance(value, dict) or not isinstance(value.get("dependencies", {}), dict):
        raise CForgevError("cforge.json no posee un mapa dependencies válido")
    return value


def init(root: Path, name: str | None = None) -> None:
    project = name or root.name.lower().replace(" ", "-")
    if not NAME_RE.fullmatch(project):
        project = "proyecto-cforge"
    path = root / MANIFEST
    if path.exists():
        raise CForgevError(f"{MANIFEST} ya existe")
    value = {"name": project, "version": "0.1.0", "language": ">=1.5.0", "dependencies": {}}
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    _write_lock(root, value)


def add(root: Path, name: str, source: str) -> None:
    if not NAME_RE.fullmatch(name):
        raise CForgevError("Nombre de paquete inválido")
    manifest = _load(root)
    candidate = Path(source).expanduser()
    if not candidate.is_absolute():
        candidate = (root / candidate).resolve()
    if not candidate.exists():
        raise CForgevError(f"La dependencia local no existe: {candidate}")
    dependencies = manifest.setdefault("dependencies", {})
    assert isinstance(dependencies, dict)
    dependencies[name] = {"path": str(candidate)}
    (root / MANIFEST).write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    _write_lock(root, manifest)


def remove(root: Path, name: str) -> None:
    manifest = _load(root)
    dependencies = manifest.get("dependencies", {})
    assert isinstance(dependencies, dict)
    if name not in dependencies:
        raise CForgevError(f"Paquete no registrado: {name}")
    del dependencies[name]
    (root / MANIFEST).write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    _write_lock(root, manifest)


def list_packages(root: Path) -> list[tuple[str, str]]:
    manifest = _load(root)
    dependencies = manifest.get("dependencies", {})
    assert isinstance(dependencies, dict)
    return sorted(
        (name, str(spec.get("path", "")))
        for name, spec in dependencies.items() if isinstance(spec, dict)
    )


def _write_lock(root: Path, manifest: dict[str, object]) -> None:
    dependencies = manifest.get("dependencies", {})
    assert isinstance(dependencies, dict)
    locked: dict[str, object] = {"format": 1, "dependencies": {}}
    target = locked["dependencies"]
    assert isinstance(target, dict)
    for name, spec in sorted(dependencies.items()):
        if not isinstance(spec, dict):
            continue
        path = Path(str(spec.get("path", "")))
        digest = hashlib.sha256()
        files = sorted(path.rglob("*.cfv")) if path.is_dir() else [path]
        for file in files:
            if file.is_file():
                digest.update(file.read_bytes())
        target[name] = {"path": str(path), "sha256": digest.hexdigest()}
    (root / LOCKFILE).write_text(
        json.dumps(locked, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
