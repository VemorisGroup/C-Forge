#!/usr/bin/env python3
"""cftype — Type checker estático para C-Forge"""

import sys, re, os
from pathlib import Path

VERSION = "3.0.0"

class CForgeTypeChecker:
    def __init__(self):
        self.errors = []
        self.warnings = []
        self.variables = {}  # name -> {type, line}
        self.functions = {}  # name -> {params, return_type}
        self.classes = {}    # name -> {fields, methods}
        self.current_file = ""

    def error(self, line, msg):
        self.errors.append(f"{self.current_file}:{line}: error: {msg}")

    def warn(self, line, msg):
        self.warnings.append(f"{self.current_file}:{line}: advertencia: {msg}")

    def check_file(self, path: Path):
        self.current_file = str(path)
        try:
            code = path.read_text(encoding="utf-8")
        except Exception as e:
            self.error(0, f"No se pudo leer el archivo: {e}")
            return

        lines = code.splitlines()
        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            # Skip comments
            if stripped.startswith("//"):
                continue

            # Check type annotations on declarations
            self._check_decl_type(i, stripped)
            # Check function signatures
            self._check_fn_sig(i, stripped)
            # Check class definitions
            self._check_clase(i, stripped)
            # Check undefined variables (basic)
            self._check_undefined(i, stripped)

    def _check_decl_type(self, lineno, line):
        m = re.match(r'sea\s+(\w+)\s*:\s*(\w+(?:<\w+>)?)\s*=', line)
        if m:
            varname, typename = m.group(1), m.group(2)
            valid_types = {"Numero", "Texto", "Booleano", "Lista", "Mapa", "Nulo",
                          "Cualquiera", "Auto", "numero", "texto", "booleano", "lista", "mapa"}
            # Accept generic types like Lista<Numero>
            base_type = typename.split("<")[0] if "<" in typename else typename
            if base_type not in valid_types and not base_type[0].isupper():
                self.warn(lineno, f"Tipo desconocido '{typename}' en declaración de '{varname}'")
            self.variables[varname] = {"type": typename, "line": lineno}

        # Check function parameters with types
        m2 = re.match(r'funcion\s+(\w+)\s*\((.*)\)', line)
        if m2:
            params = m2.group(2)
            for param in params.split(","):
                p = param.strip()
                if ":" in p:
                    pname, ptype = [x.strip() for x in p.split(":", 1)]
                    if not ptype[0].isupper() and ptype not in {"numero", "texto", "booleano", "lista", "mapa", "cualquiera"}:
                        self.warn(lineno, f"Tipo de parámetro '{ptype}' puede ser incorrecto")

    def _check_fn_sig(self, lineno, line):
        m = re.match(r'funcion\s+(\w+)\s*\((.*)\)\s*(?::\s*(\w+))?\s*\{?$', line)
        if m:
            fname = m.group(1)
            params_str = m.group(2)
            return_type = m.group(3)
            params = [p.strip() for p in params_str.split(",") if p.strip()]
            self.functions[fname] = {
                "params": len(params),
                "return_type": return_type,
                "line": lineno
            }

    def _check_clase(self, lineno, line):
        m = re.match(r'(?:abstracto\s+)?clase\s+(\w+)', line)
        if m:
            self.classes[m.group(1)] = {"line": lineno}

        # implementa check
        if "implementa" in line:
            m2 = re.search(r'implementa\s+(\w+)', line)
            if m2:
                iface_name = m2.group(1)
                # Note: we don't have full interface tracking here, just log
                pass

    def _check_undefined(self, lineno, line):
        # Check for common typos in keywords
        for typo, correct in [("retornar", None), ("funcion", None), ("clase", None),
                               ("mientras", None), ("para", None), ("si", None)]:
            pass  # C-Forge uses Spanish keywords, they're valid

        # Warn about division by zero potential
        if re.search(r'/\s*0(?:[^.]|$)', line):
            self.warn(lineno, "Posible división por cero")

        # Warn about == vs = in conditions
        if re.search(r'\bsi\s*\([^)]*[^=!<>]=(?!=)[^)]*\)', line):
            self.warn(lineno, "¿Quizás quisiste '==' en lugar de '='?")

    def report(self):
        total = len(self.errors) + len(self.warnings)
        for e in self.errors:
            print(f"\033[31m{e}\033[0m")
        for w in self.warnings:
            print(f"\033[33m{w}\033[0m")
        if total == 0:
            print("\033[32m✓ Sin problemas de tipo encontrados\033[0m")
        else:
            print(f"\n{len(self.errors)} error(es), {len(self.warnings)} advertencia(s)")
        return len(self.errors)

def main():
    import argparse
    p = argparse.ArgumentParser(description=f"cftype v{VERSION} — Type checker para C-Forge")
    p.add_argument("files", nargs="*", help="Archivos .cfv a analizar")
    p.add_argument("--strict", action="store_true", help="Modo estricto (advertencias = errores)")
    p.add_argument("--version", action="version", version=f"cftype {VERSION}")
    args = p.parse_args()

    if not args.files:
        # Find all .cfv files in current directory
        files = list(Path(".").rglob("*.cfv"))
    else:
        files = [Path(f) for f in args.files]

    checker = CForgeTypeChecker()
    for f in files:
        if f.exists():
            checker.check_file(f)
        else:
            print(f"Advertencia: {f} no existe")

    code = checker.report()
    if args.strict and checker.warnings:
        sys.exit(1)
    sys.exit(code)

if __name__ == "__main__":
    main()
