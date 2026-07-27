#!/usr/bin/env python3
"""Genera la unidad .cfv autocontenida del compilador C-Forge Stage 1."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PARTS = (
    ROOT / "bootstrap/core_lexer.cfv",
    ROOT / "bootstrap/core_ast.cfv",
    ROOT / "bootstrap/core_parser.cfv",
    ROOT / "bootstrap/core_semantics.cfv",
    ROOT / "bootstrap/core_emitter.cfv",
    ROOT / "bootstrap/core_driver.cfv",
)
OUTPUT = ROOT / "bootstrap/stage1/cforge_stage1.cfv"


def source_without_embedded_test(path: Path) -> str:
    source = path.read_text(encoding="utf-8")
    marker = 'test "lexer core reconoce el programa mínimo"'
    if marker in source:
        source = source.split(marker, 1)[0]
    return source.rstrip()


def main() -> int:
    sections = [
        "// C-Forge Stage 1 Bootstrap B4.",
        "// Archivo generado únicamente a partir de componentes escritos en .cfv.",
    ]
    for path in PARTS:
        sections.extend(
            (
                "",
                f"// ===== {path.relative_to(ROOT)} =====",
                source_without_embedded_test(path),
            )
        )
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text("\n".join(sections) + "\n", encoding="utf-8")
    print(f"Stage 1 C-Forge generado: {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
