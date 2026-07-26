#!/usr/bin/env python3
"""Impide publicar capacidades sin estado y evidencia verificable."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "capabilities.json"
README = ROOT / "README.md"
ALLOWED = {
    "verified-preview", "experimental", "partial", "planned", "not-certified",
}
README_LABELS = {
    "Verificado en Developer Preview", "Experimental", "Parcial",
    "Planeado", "No certificado",
}


def fail(message: str) -> None:
    raise ValueError(message)


def verify_manifest() -> dict:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if data.get("schema") != 1:
        fail("capabilities.json usa un schema desconocido")
    if data.get("overall_status") != "developer-preview":
        fail("el estado global debe seguir siendo developer-preview")
    if set(data.get("statuses", [])) != ALLOWED:
        fail("la lista de estados oficiales no coincide con la política")
    seen: set[str] = set()
    for capability in data.get("capabilities", []):
        identifier = capability.get("id", "")
        if not identifier or identifier in seen:
            fail(f"identificador de capacidad inválido o duplicado: {identifier!r}")
        seen.add(identifier)
        if capability.get("status") not in ALLOWED:
            fail(f"{identifier}: estado desconocido")
        if not capability.get("scope"):
            fail(f"{identifier}: falta alcance explícito")
        evidence = capability.get("evidence", [])
        if not evidence:
            fail(f"{identifier}: falta evidencia o documento de planificación")
        for relative in evidence:
            if not (ROOT / relative).exists():
                fail(f"{identifier}: evidencia inexistente: {relative}")
    if not seen:
        fail("el manifiesto no contiene capacidades")
    return data


def verify_readme() -> None:
    text = README.read_text(encoding="utf-8")
    if "`1.6.0-developer-preview`" not in text:
        fail("README no declara la versión Developer Preview")
    if "[`capabilities.json`](capabilities.json)" not in text:
        fail("README no enlaza la fuente de verdad de capacidades")
    match = re.search(r"## Características\n(?P<body>.*?)(?=\n## )", text, re.S)
    if not match:
        fail("README no contiene una sección Características verificable")
    bullets = [
        line for line in match.group("body").splitlines() if line.startswith("- ")
    ]
    if not bullets:
        fail("README no enumera capacidades")
    label_pattern = re.compile(
        r"^- \*\*\[(" + "|".join(re.escape(item) for item in README_LABELS) + r")\]\*\*"
    )
    for bullet in bullets:
        if not label_pattern.match(bullet):
            fail(f"capacidad pública sin estado oficial: {bullet}")

    for target in re.findall(r"\]\(([^)]+)\)", text):
        if target.startswith(("http://", "https://", "#", "mailto:")):
            continue
        path = target.split("#", 1)[0]
        if path and not (ROOT / path).exists():
            fail(f"README enlaza un archivo inexistente: {path}")


def main() -> int:
    try:
        data = verify_manifest()
        verify_readme()
    except (OSError, json.JSONDecodeError, ValueError) as error:
        print(f"[C-Forge Capability Gate] ERROR: {error}", file=sys.stderr)
        return 1
    counts: dict[str, int] = {status: 0 for status in ALLOWED}
    for capability in data["capabilities"]:
        counts[capability["status"]] += 1
    summary = ", ".join(f"{status}={counts[status]}" for status in sorted(counts))
    print(f"[C-Forge Capability Gate] OK: {summary}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
