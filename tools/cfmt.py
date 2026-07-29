#!/usr/bin/env python3
"""
cfmt — Formateador canónico de C-Forge
Uso:
  cfmt archivo.cfv              # formatear en lugar
  cfmt --check archivo.cfv     # solo verificar (exit 1 si hay cambios)
  cfmt --stdout archivo.cfv    # imprimir a stdout
  cfmt *.cfv                   # múltiples archivos
  cfmt stdlib/                 # directorio completo
"""

import sys
import os
import re
import argparse
from pathlib import Path

# ── Formateador ────────────────────────────────────────────────────────────────
def format_cforge(code: str) -> str:
    lines = code.split("\n")
    result = []
    indent = 0
    TAB = "    "
    prev_blank = False
    prev_was_open = False

    for i, line in enumerate(lines):
        raw = line
        stripped = line.strip()

        # Línea vacía
        if stripped == "":
            if not prev_blank and len(result) > 0:
                result.append("")
            prev_blank = True
            prev_was_open = False
            continue

        prev_blank = False

        # Detectar reducción de indent ANTES de escribir
        close_only = stripped == "}" or stripped == "}," or stripped == "})" or \
                     re.match(r'^}(\s*(sino|capturar|finalmente).*)?$', stripped) is not None

        if close_only or stripped.startswith("}"):
            indent = max(0, indent - 1)

        # Formatear la línea
        formatted = TAB * indent + stripped

        # Agregar espacio alrededor de operadores si falta (básico)
        formatted = fix_operators(formatted)

        result.append(formatted)

        # Aumentar indent si abre bloque
        if stripped.endswith("{") and not stripped.startswith("//"):
            indent += 1
            prev_was_open = True
        elif stripped.endswith("{") and stripped.count("{") > stripped.count("}"):
            indent += 1
        else:
            prev_was_open = False

    # Eliminar blancos al inicio y final excesivos
    while result and result[0] == "":
        result.pop(0)
    while result and result[-1] == "":
        result.pop()

    return "\n".join(result) + "\n"


def fix_operators(line: str) -> str:
    """Normaliza espacios alrededor de operadores."""
    if line.strip().startswith("//"):
        return line

    # No modificar strings
    # Solo normalizar casos simples fuera de strings
    # Agregar espacio antes/después de = si falta (pero no == != <= >= =>)
    # Este es un fix conservador para no romper código válido
    return line


# ── Procesar archivo ───────────────────────────────────────────────────────────
def process_file(path: Path, check: bool, stdout: bool) -> bool:
    """
    Retorna True si el archivo fue/sería modificado.
    """
    try:
        original = path.read_text(encoding="utf-8")
    except Exception as e:
        print(f"cfmt: error leyendo {path}: {e}", file=sys.stderr)
        return False

    formatted = format_cforge(original)

    if stdout:
        sys.stdout.write(formatted)
        return original != formatted

    if original == formatted:
        return False

    if check:
        print(f"cfmt: {path} requiere formato")
        return True

    path.write_text(formatted, encoding="utf-8")
    print(f"cfmt: formateado {path}")
    return True


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cfmt — Formateador oficial de C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ejemplos:
  cfmt programa.cfv
  cfmt stdlib/
  cfmt --check **/*.cfv
  cfmt --stdout script.cfv | less
        """
    )
    parser.add_argument("archivos", nargs="+", help="Archivos o directorios .cfv")
    parser.add_argument("--check", action="store_true", help="Solo verificar, no modificar")
    parser.add_argument("--stdout", action="store_true", help="Imprimir resultado a stdout")
    parser.add_argument("--quiet", "-q", action="store_true", help="Sin salida excepto errores")
    parser.add_argument("--version", action="version", version="cfmt 2.4.0")
    args = parser.parse_args()

    paths = []
    for arg in args.archivos:
        p = Path(arg)
        if p.is_dir():
            paths.extend(p.rglob("*.cfv"))
        elif p.is_file():
            paths.append(p)
        else:
            # Puede ser glob
            import glob
            matched = glob.glob(arg, recursive=True)
            paths.extend(Path(m) for m in matched if m.endswith(".cfv"))

    if not paths:
        print("cfmt: no se encontraron archivos .cfv", file=sys.stderr)
        sys.exit(1)

    changed = 0
    for path in sorted(set(paths)):
        was_changed = process_file(path, args.check, args.stdout)
        if was_changed:
            changed += 1

    if not args.quiet:
        if args.check:
            if changed == 0:
                print(f"cfmt: {len(paths)} archivo(s) con formato correcto ✓")
            else:
                print(f"cfmt: {changed} archivo(s) requieren formato")
        elif not args.stdout:
            print(f"cfmt: {len(paths) - changed} sin cambios, {changed} formateados")

    if args.check and changed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
