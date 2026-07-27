#!/usr/bin/env python3
"""Extrae el runtime bootstrap estable como una fuente C-Forge."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0 = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
OUTPUT = ROOT / "bootstrap/core_runtime.cfv"


def main() -> int:
    source = STAGE0.read_text(encoding="utf-8")
    anchor = "    static std::string runtime() {"
    start = source.index('return R"CPP(', source.index(anchor)) + len('return R"CPP(')
    end = source.index(')CPP";', start)
    runtime = source[start:end]
    literal = json.dumps(runtime, ensure_ascii=False)
    generated = (
        "// Runtime nativo Core 0.5 generado desde el contrato Stage 0.\n"
        "// Es una fuente C-Forge; no se lee Stage 0 durante la compilación.\n\n"
        "funcion runtime_cpp_core(): texto {\n"
        f"    retornar {literal}\n"
        "}\n"
    )
    OUTPUT.write_text(generated, encoding="utf-8")
    print(f"Runtime C-Forge generado: {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
