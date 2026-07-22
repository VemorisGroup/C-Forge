"""Gestor local, reproducible y seguro de paquetes C-Forge."""

from __future__ import annotations

import hashlib
import json
import re
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from cforgev import CForgevError


MANIFEST = "cforge.json"
LOCKFILE = "cforge.lock"
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,63}$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$")
DEFAULT_REGISTRY = "https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/registry/index.json"
MAX_PACKAGE_BYTES = 32 * 1024 * 1024


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


def fetch_registry(url: str = DEFAULT_REGISTRY) -> dict[str, object]:
    if not url.startswith("https://"):
        raise CForgevError("El registro debe usar HTTPS")
    try:
        with urllib.request.urlopen(url, timeout=15) as response:
            raw = response.read(MAX_PACKAGE_BYTES + 1)
    except Exception as error:
        raise CForgevError(f"No se pudo consultar el registro: {error}") from error
    if len(raw) > MAX_PACKAGE_BYTES:
        raise CForgevError("Respuesta del registro demasiado grande")
    try: value = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise CForgevError(f"Índice del registro inválido: {error}") from error
    if not isinstance(value, dict) or value.get("format") != 1 or not isinstance(value.get("packages"), dict):
        raise CForgevError("Formato de registro C-Forge incompatible")
    return value


def search_registry(query: str, url: str = DEFAULT_REGISTRY) -> list[tuple[str, str, str]]:
    packages = fetch_registry(url)["packages"]
    assert isinstance(packages, dict)
    lowered = query.lower()
    result = []
    for name, metadata in sorted(packages.items()):
        if not isinstance(metadata, dict): continue
        description = str(metadata.get("description", ""))
        if lowered in name.lower() or lowered in description.lower():
            versions = metadata.get("versions", {})
            latest = sorted(versions, reverse=True)[0] if isinstance(versions, dict) and versions else ""
            result.append((name, latest, description))
    return result


def install_registry(root: Path, name: str, version: str | None = None,
                     url: str = DEFAULT_REGISTRY) -> Path:
    if not NAME_RE.fullmatch(name): raise CForgevError("Nombre de paquete inválido")
    index = fetch_registry(url); packages = index["packages"]; assert isinstance(packages, dict)
    metadata = packages.get(name)
    if not isinstance(metadata, dict): raise CForgevError(f"Paquete no encontrado: {name}")
    versions = metadata.get("versions", {})
    if not isinstance(versions, dict) or not versions: raise CForgevError(f"Paquete sin versiones: {name}")
    selected = version or max(versions, key=lambda item: tuple(int(part) for part in item.split("-")[0].split(".")))
    release = versions.get(selected)
    if not isinstance(release, dict): raise CForgevError(f"Versión no encontrada: {name}@{selected}")
    download_url, expected = str(release.get("url", "")), str(release.get("sha256", "")).lower()
    if not download_url.startswith("https://") or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise CForgevError("Metadatos de descarga inseguros")
    try:
        with urllib.request.urlopen(download_url, timeout=30) as response:
            payload = response.read(MAX_PACKAGE_BYTES + 1)
    except Exception as error:
        raise CForgevError(f"No se pudo descargar {name}: {error}") from error
    if len(payload) > MAX_PACKAGE_BYTES or hashlib.sha256(payload).hexdigest() != expected:
        raise CForgevError("El paquete excede el límite o su SHA-256 no coincide")
    destination = root / ".cforge" / "packages" / name / selected
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".tar.gz") as temporary:
        temporary.write(payload); temporary.flush()
        with tarfile.open(temporary.name, "r:gz") as archive:
            base = destination.resolve()
            for member in archive.getmembers():
                target = (destination / member.name).resolve()
                if base != target and base not in target.parents:
                    raise CForgevError("Paquete rechazado: ruta fuera del destino")
                if member.issym() or member.islnk():
                    raise CForgevError("Paquete rechazado: enlaces no permitidos")
            archive.extractall(destination)
    add(root, name, str(destination))
    return destination


def build_package(root: Path, output: Path) -> tuple[Path, str]:
    manifest = _load(root)
    name, version = str(manifest.get("name", "")), str(manifest.get("version", ""))
    if not NAME_RE.fullmatch(name) or not VERSION_RE.fullmatch(version):
        raise CForgevError("El manifiesto requiere name y version semántica válidos")
    output.mkdir(parents=True, exist_ok=True)
    target = output / f"{name}-{version}.tar.gz"
    files = [path for path in sorted(root.rglob("*")) if path.is_file() and ".cforge" not in path.parts and ".git" not in path.parts]
    with tarfile.open(target, "w:gz") as archive:
        for path in files: archive.add(path, arcname=Path(name) / path.relative_to(root))
    return target, hashlib.sha256(target.read_bytes()).hexdigest()


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
