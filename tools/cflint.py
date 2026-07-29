#!/usr/bin/env python3
"""
cflint — Linter estático de C-Forge
Detecta errores comunes, malas prácticas y advertencias de calidad.

Uso:
  cflint archivo.cfv
  cflint --strict archivo.cfv     # warnings son errores
  cflint --json archivo.cfv       # output JSON
  cflint stdlib/                  # directorio completo
"""

import sys
import re
import json
import argparse
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional

# ── Estructura de diagnóstico ──────────────────────────────────────────────────
@dataclass
class Diag:
    archivo: str
    linea: int
    col: int
    nivel: str  # "error" | "warning" | "info"
    codigo: str
    mensaje: str

    def __str__(self):
        emoji = {"error": "✗", "warning": "⚠", "info": "ℹ"}.get(self.nivel, "?")
        return f"  {self.archivo}:{self.linea}:{self.col}  {emoji} [{self.codigo}] {self.mensaje}"


# ── Reglas de linting ──────────────────────────────────────────────────────────
class Linter:
    def __init__(self, strict: bool = False):
        self.strict = strict

    def lint(self, codigo: str, nombre_archivo: str) -> List[Diag]:
        lineas = codigo.split("\n")
        diags: List[Diag] = []
        self._check_variables(lineas, nombre_archivo, diags)
        self._check_funciones(lineas, nombre_archivo, diags)
        self._check_errores_sintaxis(lineas, nombre_archivo, diags)
        self._check_estilo(lineas, nombre_archivo, diags)
        self._check_complejidad(lineas, nombre_archivo, diags)
        self._check_builtins(lineas, nombre_archivo, diags)
        self._check_seguridad(lineas, nombre_archivo, diags)
        return diags

    def _check_variables(self, lineas, arch, diags):
        """CF001-CF009: Variables y scoping."""
        declaradas = {}   # nombre → línea
        usadas = set()

        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            # Detectar declaraciones: sea x = ...
            m = re.match(r'\bsea\s+(\w+)\s*(?::\s*\w+)?\s*=', stripped)
            if m:
                nombre = m.group(1)
                if nombre in declaradas:
                    diags.append(Diag(arch, i, linea.index("sea")+1, "warning",
                        "CF002", f"Variable '{nombre}' redeclarada (sombrea declaración anterior en línea {declaradas[nombre]})"))
                declaradas[nombre] = i

            # Detectar usos de variables
            for nombre in list(declaradas.keys()):
                # Buscar uso después de la declaración
                patron = r'\b' + re.escape(nombre) + r'\b'
                if re.search(patron, stripped):
                    # No contar la línea de declaración como "uso"
                    if i > declaradas[nombre]:
                        usadas.add(nombre)

        # Variables declaradas pero nunca usadas
        for nombre, linea_decl in declaradas.items():
            if nombre not in usadas and not nombre.startswith("_"):
                diags.append(Diag(arch, linea_decl, 1, "warning",
                    "CF001", f"Variable '{nombre}' declarada pero nunca usada (prefija con _ para suprimir)"))

    def _check_funciones(self, lineas, arch, diags):
        """CF010-CF019: Funciones."""
        funciones_def = {}   # nombre → {linea, params, lineas_cuerpo}
        funciones_llamadas = set()
        en_funcion = None
        cuerpo_inicio = 0
        prof = 0

        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            # Declaración de función
            m = re.match(r'\bfuncion\s+(\w+)\s*\(([^)]*)\)', stripped)
            if m:
                nombre = m.group(1)
                params_str = m.group(2).strip()
                params = [p.strip().split(":")[0].strip() for p in params_str.split(",") if p.strip()]
                funciones_def[nombre] = {"linea": i, "params": params, "n_lineas": 0}
                en_funcion = nombre
                cuerpo_inicio = i
                prof = stripped.count("{") - stripped.count("}")

                # Función sin parámetros con nombre largo (posible bug)
                if len(nombre) < 3 and nombre not in ("fn",):
                    diags.append(Diag(arch, i, 1, "warning",
                        "CF011", f"Función '{nombre}' tiene nombre muy corto"))

                continue

            # Contar llaves para saber cuándo termina la función
            if en_funcion:
                prof += stripped.count("{") - stripped.count("}")
                funciones_def[en_funcion]["n_lineas"] = i - cuerpo_inicio
                if prof <= 0:
                    n = funciones_def[en_funcion]["n_lineas"]
                    if n > 80:
                        diags.append(Diag(arch, funciones_def[en_funcion]["linea"], 1, "warning",
                            "CF013", f"Función '{en_funcion}' tiene {n} líneas (recomendado: ≤80). Considera dividirla."))
                    en_funcion = None

            # Llamadas a funciones
            for m in re.finditer(r'\b(\w+)\s*\(', stripped):
                funciones_llamadas.add(m.group(1))

        # Funciones declaradas pero nunca llamadas (excluir main, arrancar, etc.)
        RESERVADAS = {"main", "arrancar", "inicio", "test", "setup", "teardown", "configurar"}
        for nombre, info in funciones_def.items():
            if nombre not in funciones_llamadas and nombre not in RESERVADAS and not nombre.startswith("_"):
                diags.append(Diag(arch, info["linea"], 1, "info",
                    "CF012", f"Función '{nombre}' declarada pero nunca llamada en este archivo"))

        # Funciones con demasiados parámetros
        for nombre, info in funciones_def.items():
            if len(info["params"]) > 7:
                diags.append(Diag(arch, info["linea"], 1, "warning",
                    "CF014", f"Función '{nombre}' tiene {len(info['params'])} parámetros (recomendado: ≤7)"))

    def _check_errores_sintaxis(self, lineas, arch, diags):
        """CF020-CF029: Errores de sintaxis comunes."""
        apertura = 0
        apertura_linea = []

        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            # Contar llaves (ignora strings simples)
            en_string = False
            char_string = None
            for c in linea:
                if en_string:
                    if c == char_string:
                        en_string = False
                elif c in ('"', "'"):
                    en_string = True
                    char_string = c
                elif c == "{":
                    apertura += 1
                    apertura_linea.append(i)
                elif c == "}":
                    apertura -= 1
                    if apertura_linea:
                        apertura_linea.pop()
                    if apertura < 0:
                        diags.append(Diag(arch, i, linea.index("}")+1, "error",
                            "CF020", "Llave de cierre '}' sin apertura correspondiente"))
                        apertura = 0

            # var en lugar de sea (JavaScript leak)
            if re.search(r'\bvar\s+\w+', stripped):
                diags.append(Diag(arch, i, 1, "error",
                    "CF021", "Usa 'sea' en lugar de 'var' (C-Forge no es JavaScript)"))

            # let en lugar de sea
            if re.search(r'\blet\s+\w+', stripped):
                diags.append(Diag(arch, i, 1, "error",
                    "CF022", "Usa 'sea' en lugar de 'let'"))

            # const en lugar de sea
            if re.search(r'\bconst\s+\w+', stripped):
                diags.append(Diag(arch, i, 1, "error",
                    "CF023", "Usa 'sea' en lugar de 'const'"))

            # console.log (JavaScript)
            if "console.log" in stripped:
                diags.append(Diag(arch, i, 1, "warning",
                    "CF024", "Usa 'mostrar()' en lugar de 'console.log()'"))

            # print( (Python)
            if re.search(r'\bprint\s*\(', stripped) and "mostrar" not in stripped:
                diags.append(Diag(arch, i, 1, "warning",
                    "CF025", "Usa 'mostrar()' en lugar de 'print()'"))

            # return en lugar de retornar
            if re.match(r'\s*return\b', linea) and "retornar" not in linea:
                diags.append(Diag(arch, i, 1, "error",
                    "CF026", "Usa 'retornar' en lugar de 'return'"))

            # == para asignación (confusión con comparación)
            if re.search(r'^\s*\w+\s*==\s*[^=]', linea) and not re.search(r'\bsi\b|\bpara\b|\bmientras\b|\besi\b', linea):
                diags.append(Diag(arch, i, 1, "warning",
                    "CF027", "¿Intentabas asignar con '='? El operador '==' es comparación"))

        if apertura > 0:
            linea_apertura = apertura_linea[-1] if apertura_linea else "?"
            diags.append(Diag(arch, len(lineas), 1, "error",
                "CF028", f"Llave de apertura '{{' sin cierre (abierta en línea {linea_apertura})"))

    def _check_estilo(self, lineas, arch, diags):
        """CF030-CF039: Estilo y convenciones."""
        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            # Líneas muy largas
            if len(linea) > 120:
                diags.append(Diag(arch, i, 121, "warning",
                    "CF030", f"Línea demasiado larga ({len(linea)} chars, recomendado: ≤120)"))

            # Tabs mezclados con espacios
            if "\t" in linea and "    " in linea:
                diags.append(Diag(arch, i, 1, "warning",
                    "CF031", "Mezcla de tabs y espacios para indentación"))

            # Múltiples ; en la misma línea
            if linea.count(";") > 2:
                diags.append(Diag(arch, i, 1, "info",
                    "CF032", "Múltiples punto y coma en una línea — considera separar en líneas"))

            # Nombres de variable en mayúsculas (no es constante en C-Forge)
            m = re.match(r'\bsea\s+([A-Z][A-Z_0-9]+)\s*=', stripped)
            if m:
                diags.append(Diag(arch, i, 1, "info",
                    "CF033", f"'{m.group(1)}' está en MAYÚSCULAS — C-Forge usa camelCase para variables"))

            # Comentario TODO/FIXME/HACK
            if re.search(r'\b(TODO|FIXME|HACK|XXX)\b', stripped, re.IGNORECASE):
                tag = re.search(r'\b(TODO|FIXME|HACK|XXX)\b', stripped, re.IGNORECASE).group(1).upper()
                diags.append(Diag(arch, i, 1, "info",
                    "CF034", f"Comentario {tag} pendiente"))

    def _check_complejidad(self, lineas, arch, diags):
        """CF040-CF049: Complejidad ciclomática."""
        anidamiento_max = 0
        anidamiento_actual = 0

        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            # Contar estructuras de control que aumentan anidamiento
            aumenta = len(re.findall(r'\b(si|para|mientras|segun)\b', stripped))
            cierra = stripped.count("}")

            anidamiento_actual += aumenta
            anidamiento_actual = max(0, anidamiento_actual - cierra)
            anidamiento_max = max(anidamiento_max, anidamiento_actual)

            if anidamiento_actual > 5:
                diags.append(Diag(arch, i, 1, "warning",
                    "CF040", f"Anidamiento profundo ({anidamiento_actual} niveles). Considera refactorizar."))

    def _check_builtins(self, lineas, arch, diags):
        """CF050-CF059: Uso incorrecto de builtins."""
        DEPRECATED = {
            "lista_agregar": "Usa agregar() directamente",
            "mapa_obtener": "Usa mapa['clave'] directamente",
        }

        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            for dep, alternativa in DEPRECATED.items():
                if dep + "(" in stripped:
                    diags.append(Diag(arch, i, 1, "warning",
                        "CF050", f"'{dep}' está deprecado. {alternativa}"))

            # dividir() vs texto_dividir()
            if re.search(r'\bdividir\s*\(', stripped):
                diags.append(Diag(arch, i, 1, "info",
                    "CF051", "¿Quisiste decir 'texto_dividir()'?"))

    def _check_seguridad(self, lineas, arch, diags):
        """CF060-CF069: Problemas de seguridad."""
        for i, linea in enumerate(lineas, 1):
            stripped = linea.strip()
            if stripped.startswith("//"):
                continue

            # Credenciales hardcodeadas (básico)
            if re.search(r'(password|passwd|secret|api_key|token)\s*=\s*"[^"]+"',
                         stripped, re.IGNORECASE):
                diags.append(Diag(arch, i, 1, "error",
                    "CF060", "Posible credencial hardcodeada. Usa variables de entorno con env_obtener()"))

            # eval-like patterns
            if re.search(r'\bejecutar_codigo\s*\(', stripped):
                diags.append(Diag(arch, i, 1, "warning",
                    "CF061", "ejecutar_codigo() puede ser peligroso con input no validado"))

            # SQL injection hint
            if re.search(r'(db_query|pg_query|mysql_query)\s*\(.*\+', stripped):
                diags.append(Diag(arch, i, 1, "warning",
                    "CF062", "Posible concatenación en SQL. Usa parámetros preparados o el ORM"))


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cflint — Linter oficial de C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Códigos:
  CF001-CF009  Variables
  CF010-CF019  Funciones
  CF020-CF029  Sintaxis
  CF030-CF039  Estilo
  CF040-CF049  Complejidad
  CF050-CF059  Builtins
  CF060-CF069  Seguridad
        """
    )
    parser.add_argument("archivos", nargs="+", help="Archivos o directorios .cfv")
    parser.add_argument("--strict", action="store_true", help="Tratar warnings como errores")
    parser.add_argument("--json", action="store_true", help="Salida en formato JSON")
    parser.add_argument("--only", help="Solo mostrar nivel: error|warning|info")
    parser.add_argument("--ignore", help="Ignorar códigos separados por coma (CF001,CF012)")
    parser.add_argument("--version", action="version", version="cflint 2.4.0")
    args = parser.parse_args()

    ignorar = set(args.ignore.split(",")) if args.ignore else set()

    import glob as glob_mod
    paths = []
    for arg in args.archivos:
        p = Path(arg)
        if p.is_dir():
            paths.extend(p.rglob("*.cfv"))
        elif p.is_file():
            paths.append(p)
        else:
            matched = glob_mod.glob(arg, recursive=True)
            paths.extend(Path(m) for m in matched if m.endswith(".cfv"))

    if not paths:
        print("cflint: no se encontraron archivos .cfv", file=sys.stderr)
        sys.exit(1)

    linter = Linter(strict=args.strict)
    todos_diags = []

    for path in sorted(set(paths)):
        try:
            codigo = path.read_text(encoding="utf-8")
        except Exception as e:
            print(f"cflint: error leyendo {path}: {e}", file=sys.stderr)
            continue

        diags = linter.lint(codigo, str(path))

        # Filtrar
        if args.only:
            diags = [d for d in diags if d.nivel == args.only]
        if ignorar:
            diags = [d for d in diags if d.codigo not in ignorar]
        if args.strict:
            for d in diags:
                if d.nivel == "warning":
                    d.nivel = "error"

        todos_diags.extend(diags)

    if args.json:
        output = [
            {"archivo": d.archivo, "linea": d.linea, "col": d.col,
             "nivel": d.nivel, "codigo": d.codigo, "mensaje": d.mensaje}
            for d in todos_diags
        ]
        print(json.dumps(output, ensure_ascii=False, indent=2))
    else:
        archivos_con_diag = {}
        for d in todos_diags:
            archivos_con_diag.setdefault(d.archivo, []).append(d)

        errores = sum(1 for d in todos_diags if d.nivel == "error")
        warnings = sum(1 for d in todos_diags if d.nivel == "warning")
        infos = sum(1 for d in todos_diags if d.nivel == "info")

        for arch, diags in archivos_con_diag.items():
            print(f"\n{arch}:")
            for d in sorted(diags, key=lambda x: x.linea):
                print(str(d))

        print(f"\ncflint: {len(paths)} archivo(s) — {errores} errores, {warnings} warnings, {infos} info")

    hay_errores = any(d.nivel == "error" for d in todos_diags)
    if hay_errores:
        sys.exit(1)


if __name__ == "__main__":
    main()
