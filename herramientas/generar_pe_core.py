#!/usr/bin/env python3
"""Genera el backend PE Core autocontenido escrito en C-Forge."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "bootstrap/direct/cforge_pe_x64_core.cfv"
PARTS = (
    ROOT / "bootstrap/core_lexer.cfv",
    ROOT / "bootstrap/core_ast.cfv",
    ROOT / "bootstrap/core_parser.cfv",
)
PE_LIBRARY = ROOT / "bootstrap/direct/cforge_pe_x64.cfv"
BACKEND = ROOT / "bootstrap/direct/cforge_pe_x64_core_backend.cfv"


def main() -> int:
    sources = [path.read_text(encoding="utf-8") for path in PARTS]
    lexer_demo = "sea muestra: texto ="
    if lexer_demo not in sources[0]:
        raise RuntimeError("no se encontró la prueba incrustada del lexer Core")
    sources[0] = sources[0].split(lexer_demo, 1)[0]

    pe = PE_LIBRARY.read_text(encoding="utf-8")
    marker = "sea argumentos_pe: lista = argumentos_programa()"
    if marker not in pe:
        raise RuntimeError("no se encontró el controlador B6.5")
    sources.append(pe.split(marker, 1)[0])
    sources.append(BACKEND.read_text(encoding="utf-8"))
    OUTPUT.write_text("\n\n".join(sources), encoding="utf-8")
    print(f"Backend PE Core generado: {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
