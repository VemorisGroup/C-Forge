#!/usr/bin/env python3
"""
cftest — Test runner oficial de C-Forge
Ejecuta archivos de prueba .cfv y reporta resultados.

Uso:
  cftest                          # corre todos los tests/
  cftest tests/mi_test.cfv        # archivo específico
  cftest tests/                   # directorio
  cftest --watch                  # modo watch (re-corre en cambios)
  cftest --tap                    # salida TAP (CI/CD)
  cftest --json                   # salida JSON
  cftest --coverage               # reporte de cobertura básico

Convención de tests (usa stdlib/pruebas.cfv):
  prueba("suma funciona", funcion() {
    afirmar_igual(sumar(2, 3), 5)
  })
"""

import sys
import os
import re
import json
import time
import subprocess
import argparse
import threading
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional

# ── Colores ────────────────────────────────────────────────────────────────────
RESET = "\033[0m"; BOLD = "\033[1m"; DIM = "\033[2m"
RED = "\033[91m"; GREEN = "\033[92m"; YELLOW = "\033[93m"
BLUE = "\033[94m"; CYAN = "\033[96m"; WHITE = "\033[97m"

def c(t, col): return (col + t + RESET) if sys.stdout.isatty() else t


# ── Resultado de test ─────────────────────────────────────────────────────────
@dataclass
class TestResult:
    nombre: str
    archivo: str
    ok: bool
    duracion_ms: float
    mensaje: str = ""
    linea_error: int = 0


@dataclass
class SuiteResult:
    archivo: str
    tests: List[TestResult] = field(default_factory=list)
    duracion_ms: float = 0
    error_sintaxis: str = ""

    @property
    def pasados(self): return sum(1 for t in self.tests if t.ok)
    @property
    def fallados(self): return sum(1 for t in self.tests if not t.ok)
    @property
    def total(self): return len(self.tests)


# ── Ejecutor de tests ──────────────────────────────────────────────────────────
class TestRunner:
    def __init__(self, interpreter: str, timeout: int = 30):
        self.interpreter = interpreter
        self.timeout = timeout

    def ejecutar_archivo(self, ruta: Path) -> SuiteResult:
        suite = SuiteResult(archivo=str(ruta))
        t0 = time.monotonic()

        try:
            # Inyectar colector de resultados antes de ejecutar
            codigo_original = ruta.read_text(encoding="utf-8")
            codigo_instrumentado = self._instrumentar(codigo_original, str(ruta))

            import tempfile
            with tempfile.NamedTemporaryFile(mode='w', suffix='.cfv',
                                              delete=False, encoding='utf-8') as f:
                f.write(codigo_instrumentado)
                tmp = f.name

            result = subprocess.run(
                [self.interpreter, tmp],
                capture_output=True, text=True, timeout=self.timeout
            )
            os.unlink(tmp)

            suite.duracion_ms = (time.monotonic() - t0) * 1000

            if result.returncode != 0 and not result.stdout:
                # Error de sintaxis o runtime antes de cualquier test
                suite.error_sintaxis = result.stderr.strip()
                return suite

            # Parsear output de tests
            suite.tests = self._parsear_resultados(result.stdout, result.stderr, str(ruta))

        except subprocess.TimeoutExpired:
            suite.error_sintaxis = f"Timeout ({self.timeout}s)"
            suite.duracion_ms = self.timeout * 1000
        except FileNotFoundError:
            suite.error_sintaxis = f"Intérprete no encontrado: {self.interpreter}"
        except Exception as e:
            suite.error_sintaxis = str(e)

        if not suite.tests and not suite.error_sintaxis:
            suite.error_sintaxis = "No se encontraron tests (usa prueba() de stdlib/pruebas.cfv)"

        return suite

    def _instrumentar(self, codigo: str, ruta: str) -> str:
        """Envuelve el código con importar pruebas si no lo tiene."""
        tiene_pruebas = "importar" in codigo and "pruebas" in codigo
        preambulo = ""
        if not tiene_pruebas:
            # Agregar stub mínimo de pruebas si no está importado
            preambulo = '''
// cftest: stub de pruebas inyectado
sea __test_resultados = []
sea __test_actual = ""

funcion prueba(nombre, fn) {
    __test_actual = nombre
    sea t0 = tiempo_ms()
    intentar {
        fn()
        agregar(__test_resultados, "PASS|" + nombre + "|" + (tiempo_ms() - t0) + "|")
    } capturar (e) {
        agregar(__test_resultados, "FAIL|" + nombre + "|" + (tiempo_ms() - t0) + "|" + e)
    }
}

funcion afirmar(cond, msg) {
    si (!cond) { lanzar "AssertionError: " + (msg ?? "afirmación falló") }
}

funcion afirmar_igual(a, b) {
    si (a != b) { lanzar "AssertionError: esperado " + b + " pero obtuvo " + a }
}

funcion afirmar_diferente(a, b) {
    si (a == b) { lanzar "AssertionError: se esperaba que " + a + " != " + b }
}

funcion afirmar_verdadero(v) {
    si (!v) { lanzar "AssertionError: se esperaba verdadero" }
}

funcion afirmar_falso(v) {
    si (v) { lanzar "AssertionError: se esperaba falso" }
}

funcion afirmar_lanza(fn, patron) {
    sea lanzó = falso
    intentar { fn() } capturar (e) {
        lanzó = verdadero
        si (patron != nulo y !texto_contiene(e, patron)) {
            lanzar "AssertionError: esperaba error con '" + patron + "' pero obtuvo '" + e + "'"
        }
    }
    si (!lanzó) { lanzar "AssertionError: se esperaba que lanzara una excepción" }
}

funcion afirmar_nulo(v) {
    si (v != nulo) { lanzar "AssertionError: se esperaba nulo" }
}

funcion afirmar_contiene(lista_o_texto, elem) {
    si (tipo_de(lista_o_texto) == "texto") {
        si (!texto_contiene(lista_o_texto, elem)) {
            lanzar "AssertionError: '" + lista_o_texto + "' no contiene '" + elem + "'"
        }
    } sino {
        si (!lista_contiene(lista_o_texto, elem)) {
            lanzar "AssertionError: lista no contiene " + elem
        }
    }
}

'''

        sufijo = '''
// cftest: imprimir resultados
para r en __test_resultados {
    mostrar("__CFTEST__" + r)
}
mostrar("__CFTEST_DONE__" + longitud(__test_resultados))
'''
        return preambulo + "\n" + codigo + "\n" + sufijo

    def _parsear_resultados(self, stdout: str, stderr: str, archivo: str) -> List[TestResult]:
        resultados = []
        for linea in stdout.split("\n"):
            if linea.startswith("__CFTEST__"):
                partes = linea[10:].split("|")
                if len(partes) >= 3:
                    estado = partes[0]
                    nombre = partes[1]
                    dur = float(partes[2]) if partes[2].replace('.','').isdigit() else 0
                    msg = partes[3] if len(partes) > 3 else ""
                    resultados.append(TestResult(
                        nombre=nombre,
                        archivo=archivo,
                        ok=(estado == "PASS"),
                        duracion_ms=dur,
                        mensaje=msg
                    ))
        return resultados


# ── Reportero ──────────────────────────────────────────────────────────────────
class Reportero:
    def imprimir_suite(self, suite: SuiteResult, verbose: bool = False):
        nombre_arch = Path(suite.archivo).name
        if suite.error_sintaxis:
            print(f"\n  {c('✗', RED)} {c(nombre_arch, BOLD)}")
            print(f"    {c(suite.error_sintaxis, RED)}")
            return

        print(f"\n  {c(nombre_arch, BOLD)} {c(f'({suite.duracion_ms:.0f}ms)', DIM)}")
        for t in suite.tests:
            icono = c("✓", GREEN) if t.ok else c("✗", RED)
            dur = c(f"{t.duracion_ms:.1f}ms", DIM)
            print(f"    {icono} {t.nombre} {dur}")
            if not t.ok and t.mensaje:
                print(f"      {c(t.mensaje, RED)}")

    def resumen(self, suites: List[SuiteResult], duracion_total: float):
        total_tests = sum(s.total for s in suites)
        total_pass  = sum(s.pasados for s in suites)
        total_fail  = sum(s.fallados for s in suites)
        total_err   = sum(1 for s in suites if s.error_sintaxis)

        print(f"\n{c('─' * 50, DIM)}")
        print(f"  Suites:  {len(suites)}  ({total_err} con error)")
        print(f"  Tests:   {c(str(total_pass) + ' pasados', GREEN)}  "
              f"{c(str(total_fail) + ' fallados', RED) if total_fail else c('0 fallados', DIM)}")
        print(f"  Tiempo:  {duracion_total*1000:.0f}ms")

        if total_fail == 0 and total_err == 0:
            print(f"\n  {c('✓ Todos los tests pasaron', BOLD + GREEN)}")
        else:
            print(f"\n  {c('✗ Hay tests fallando', BOLD + RED)}")

    def tap(self, suites: List[SuiteResult]):
        n = sum(s.total for s in suites)
        print(f"1..{n}")
        i = 0
        for suite in suites:
            for t in suite.tests:
                i += 1
                estado = "ok" if t.ok else "not ok"
                print(f"{estado} {i} - {t.nombre}")
                if not t.ok and t.mensaje:
                    print(f"  # {t.mensaje}")

    def json_output(self, suites: List[SuiteResult]) -> str:
        return json.dumps([
            {
                "archivo": s.archivo,
                "error": s.error_sintaxis,
                "duracion_ms": s.duracion_ms,
                "tests": [
                    {"nombre": t.nombre, "ok": t.ok,
                     "duracion_ms": t.duracion_ms, "mensaje": t.mensaje}
                    for t in s.tests
                ]
            }
            for s in suites
        ], ensure_ascii=False, indent=2)


# ── Watch mode ────────────────────────────────────────────────────────────────
def watch_mode(paths: List[Path], runner: TestRunner, reportero: Reportero):
    import hashlib
    def digest(p):
        try:
            return hashlib.md5(p.read_bytes()).hexdigest()
        except:
            return ""

    print(c("  Modo watch activado. Ctrl+C para salir.", DIM))
    checksums = {p: digest(p) for p in paths}

    while True:
        time.sleep(0.5)
        cambios = False
        for p in paths:
            d = digest(p)
            if d != checksums.get(p, ""):
                checksums[p] = d
                cambios = True
                print(c(f"\n  ↺ Cambio detectado: {p.name}", CYAN))

        if cambios:
            t0 = time.monotonic()
            suites = [runner.ejecutar_archivo(p) for p in paths]
            for s in suites:
                reportero.imprimir_suite(s)
            reportero.resumen(suites, time.monotonic() - t0)


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cftest — Test runner de C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ejemplos:
  cftest                           # corre tests/
  cftest mi_test.cfv               # archivo específico
  cftest tests/ --watch            # modo watch
  cftest tests/ --tap              # salida TAP para CI
  cftest tests/ --json > reporte.json
        """
    )
    parser.add_argument("archivos", nargs="*", default=["tests/"],
                        help="Archivos o directorios .cfv (default: tests/)")
    parser.add_argument("--interpreter", "-i", default="./cforgev")
    parser.add_argument("--watch", "-w", action="store_true", help="Modo watch")
    parser.add_argument("--tap", action="store_true", help="Salida TAP")
    parser.add_argument("--json", action="store_true", help="Salida JSON")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--timeout", "-t", type=int, default=30, help="Timeout por test (s)")
    parser.add_argument("--filter", "-f", help="Filtrar tests por nombre")
    parser.add_argument("--version", action="version", version="cftest 2.5.0")
    args = parser.parse_args()

    # Recolectar archivos
    paths = []
    for arg in args.archivos:
        p = Path(arg)
        if p.is_dir():
            found = list(p.rglob("*test*.cfv")) + list(p.rglob("*spec*.cfv")) + list(p.rglob("*prueba*.cfv"))
            paths.extend(sorted(set(found)))
        elif p.is_file() and p.suffix == ".cfv":
            paths.append(p)

    if not paths:
        # Buscar en directorio actual
        paths = sorted(Path(".").glob("**/*test*.cfv"))

    if not paths:
        print(c("cftest: no se encontraron archivos de test", YELLOW))
        print(c("  Convención: tests/*.cfv, *test*.cfv, *spec*.cfv", DIM))
        sys.exit(0)

    runner = TestRunner(args.interpreter, args.timeout)
    reportero = Reportero()

    if args.watch:
        try:
            watch_mode(paths, runner, reportero)
        except KeyboardInterrupt:
            print(c("\n  Watch detenido.", DIM))
        return

    # Ejecutar
    print(c(f"\n  cftest — ejecutando {len(paths)} suite(s)...", BOLD))
    t0 = time.monotonic()
    suites = [runner.ejecutar_archivo(p) for p in paths]
    duracion = time.monotonic() - t0

    if args.tap:
        reportero.tap(suites)
    elif args.json:
        print(reportero.json_output(suites))
    else:
        for suite in suites:
            reportero.imprimir_suite(suite, args.verbose)
        reportero.resumen(suites, duracion)

    hay_fallos = any(s.fallados > 0 or s.error_sintaxis for s in suites)
    sys.exit(1 if hay_fallos else 0)


if __name__ == "__main__":
    main()
