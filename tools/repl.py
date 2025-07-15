#!/usr/bin/env python3
"""
C-Forge REPL — Intérprete interactivo
Uso:
  python3 tools/repl.py
  python3 tools/repl.py --interpreter ./cforgev
  cforgev --repl
"""

import sys
import os
import re
import subprocess
import readline
import tempfile
import argparse
import signal
from pathlib import Path

# ── Colores ANSI ───────────────────────────────────────────────────────────────
RESET   = "\033[0m"
BOLD    = "\033[1m"
DIM     = "\033[2m"
RED     = "\033[91m"
GREEN   = "\033[92m"
YELLOW  = "\033[93m"
BLUE    = "\033[94m"
MAGENTA = "\033[95m"
CYAN    = "\033[96m"
WHITE   = "\033[97m"

def colorear(texto, color):
    if not sys.stdout.isatty():
        return texto
    return color + texto + RESET

# ── Palabras clave para autocompletado ────────────────────────────────────────
KEYWORDS = [
    "sea", "funcion", "retornar", "si", "sino", "esi", "para", "en",
    "mientras", "segun", "caso", "romper", "continuar", "clase", "nuevo",
    "importar", "exportar", "lanzar", "intentar", "capturar", "finalmente",
    "verdadero", "falso", "nulo", "rango", "y", "o", "no"
]

BUILTINS = [
    "mostrar", "tipo_de", "longitud", "agregar", "eliminar", "insertar",
    "ordenar", "invertir", "filtrar", "mapear", "reducir", "texto_a_numero",
    "numero_a_texto", "texto_dividir", "texto_unir", "texto_reemplazar",
    "texto_contiene", "texto_mayusculas", "texto_minusculas", "texto_trim",
    "texto_empieza_con", "texto_termina_con", "json_parsear", "json_texto",
    "piso", "techo", "redondear", "absoluto", "potencia", "raiz",
    "maximo", "minimo", "aleatorio", "tiempo_ms", "timestamp", "dormir",
    "env_obtener", "env_establecer", "leer_archivo", "escribir_archivo",
    "existe_archivo", "http_get", "http_post", "db_query", "db_exec",
    "mapa_claves", "mapa_valores", "mapa_entradas", "mapa_fusionar",
    "tiene_clave", "lista_suma", "lista_max", "lista_min", "lista_unica",
    "texto_formato", "numero_formato", "regex_coincidir", "regex_reemplazar",
    "hash_md5", "hash_sha256", "base64_codificar", "base64_decodificar",
]

STDLIB_MODULES = [
    "matematica", "texto", "lista", "mapa", "fecha", "archivo", "io",
    "json", "csv", "yaml", "http_cliente", "framework", "orm", "db",
    "crypto", "base64", "regex", "log", "config", "cache", "email",
    "eventos", "esquema", "benchmark", "hilos", "concurrencia", "cli",
    "errores", "pruebas", "async", "enum", "interfaz", "tipado",
]

ALL_COMPLETIONS = KEYWORDS + BUILTINS + [f'"{m}"' for m in STDLIB_MODULES]


class CForgeCompleter:
    def __init__(self):
        self.matches = []

    def complete(self, text, state):
        if state == 0:
            self.matches = [c for c in ALL_COMPLETIONS if c.startswith(text)]
        try:
            return self.matches[state]
        except IndexError:
            return None


# ── Historial ──────────────────────────────────────────────────────────────────
HISTFILE = Path.home() / ".cforge_history"

def configurar_readline():
    completer = CForgeCompleter()
    readline.set_completer(completer.complete)
    readline.parse_and_bind("tab: complete")
    readline.set_completer_delims(" \t\n;(){}[]")

    if HISTFILE.exists():
        try:
            readline.read_history_file(str(HISTFILE))
        except Exception:
            pass

    import atexit
    atexit.register(lambda: readline.write_history_file(str(HISTFILE)))


# ── Detector de bloque incompleto ─────────────────────────────────────────────
def es_incompleto(codigo: str) -> bool:
    """Retorna True si el código tiene bloques sin cerrar (necesita más input)."""
    abiertos = 0
    en_string = False
    char_str = None

    for c in codigo:
        if en_string:
            if c == char_str:
                en_string = False
        elif c in ('"', "'"):
            en_string = True
            char_str = c
        elif c == '{':
            abiertos += 1
        elif c == '}':
            abiertos -= 1

    return abiertos > 0


def detectar_linea_continua(linea: str) -> bool:
    """Retorna True si la línea termina con operador o abre bloque."""
    stripped = linea.rstrip()
    if not stripped:
        return False
    terminadores_abiertos = ('{', ',', '(', '[', '+', '-', '*', '/', '\\',
                              'y', 'o', '=', '->', '=>')
    return any(stripped.endswith(t) for t in terminadores_abiertos)


# ── Ejecutor de código ────────────────────────────────────────────────────────
class CForgeRunner:
    def __init__(self, interpreter: str):
        self.interpreter = interpreter
        self.session_vars: list = []   # historial de declaraciones
        self._check_interpreter()

    def _check_interpreter(self):
        if not Path(self.interpreter).exists():
            # Buscar en PATH
            import shutil
            found = shutil.which("cforgev") or shutil.which("cforge")
            if found:
                self.interpreter = found
            else:
                print(colorear(f"  ⚠ Intérprete no encontrado: {self.interpreter}", YELLOW))
                print(colorear("    Modo simulación activado.", DIM))
                self.interpreter = None

    def ejecutar(self, codigo: str) -> tuple[str, str, int]:
        """Ejecuta código y retorna (stdout, stderr, exit_code)."""
        # Construir contexto completo con variables de sesión
        contexto = "\n".join(self.session_vars) + "\n" + codigo

        with tempfile.NamedTemporaryFile(mode='w', suffix='.cfv',
                                         delete=False, encoding='utf-8') as f:
            f.write(contexto)
            tmp = f.name

        try:
            if self.interpreter:
                result = subprocess.run(
                    [self.interpreter, tmp],
                    capture_output=True, text=True, timeout=10
                )
                return result.stdout, result.stderr, result.returncode
            else:
                return self._simular(codigo)
        except subprocess.TimeoutExpired:
            return "", "Error: tiempo de ejecución agotado (10s)", 1
        except Exception as e:
            return "", str(e), 1
        finally:
            os.unlink(tmp)

    def _simular(self, codigo: str) -> tuple[str, str, int]:
        """Simulación básica cuando no hay intérprete."""
        # Evaluar mostrar() básico
        salida = []
        for m in re.finditer(r'mostrar\s*\(([^)]+)\)', codigo):
            arg = m.group(1).strip().strip('"').strip("'")
            salida.append(arg)
        return "\n".join(salida), "", 0

    def registrar_declaracion(self, codigo: str):
        """Guarda declaraciones de variables/funciones para el contexto futuro."""
        lineas = codigo.strip().split("\n")
        for linea in lineas:
            stripped = linea.strip()
            if (stripped.startswith("sea ") or
                stripped.startswith("funcion ") or
                stripped.startswith("clase ")):
                self.session_vars.append(linea)


# ── REPL principal ────────────────────────────────────────────────────────────
BANNER = f"""
{colorear('╔═══════════════════════════════════════════╗', CYAN)}
{colorear('║', CYAN)}  {colorear('C-Forge', BOLD + WHITE)} {colorear('v2.5.0', GREEN)} {colorear('— Intérprete Interactivo', DIM)}  {colorear('║', CYAN)}
{colorear('╚═══════════════════════════════════════════╝', CYAN)}
{colorear('  Escribe código C-Forge y presiona Enter.', DIM)}
{colorear('  Comandos:', DIM)} {colorear('.salir', YELLOW)} {colorear('.limpiar', YELLOW)} {colorear('.vars', YELLOW)} {colorear('.ayuda', YELLOW)} {colorear('.cargar archivo.cfv', YELLOW)}
"""

AYUDA = f"""
{colorear('Comandos del REPL:', BOLD)}
  {colorear('.salir', CYAN)}          Salir del REPL
  {colorear('.limpiar', CYAN)}        Limpiar pantalla y sesión
  {colorear('.vars', CYAN)}           Mostrar variables/funciones de la sesión
  {colorear('.hist', CYAN)}           Ver historial de comandos
  {colorear('.cargar <ruta>', CYAN)}  Ejecutar un archivo .cfv en la sesión
  {colorear('.tiempo', CYAN)}         Activar/desactivar medición de tiempo
  {colorear('.ayuda', CYAN)}          Mostrar esta ayuda
  {colorear('.multi', CYAN)}          Activar modo multi-línea (también: {{ abre bloque)}

{colorear('Atajos:', BOLD)}
  Tab           Autocompletar palabras clave y builtins
  ↑/↓           Navegar historial
  Ctrl+C        Cancelar línea actual
  Ctrl+D        Salir
"""


def repl_main(interpreter: str):
    if sys.stdout.isatty():
        print(BANNER)

    configurar_readline()
    runner = CForgeRunner(interpreter)
    buffer_multilinea = []
    medir_tiempo = False
    linea_num = 0

    def manejar_ctrl_c(sig, frame):
        if buffer_multilinea:
            buffer_multilinea.clear()
            print(colorear("\n  [cancelado]", DIM))
        else:
            print()

    signal.signal(signal.SIGINT, manejar_ctrl_c)

    while True:
        try:
            if buffer_multilinea:
                prompt = colorear("... ", DIM)
            else:
                linea_num += 1
                prompt = colorear(f"[{linea_num}] ", CYAN) + colorear("cforge» ", BOLD + WHITE)

            try:
                entrada = input(prompt)
            except EOFError:
                print(colorear("\n  ¡Hasta luego!", CYAN))
                break

            # ── Comandos especiales ────────────────────────────────────────
            cmd = entrada.strip()

            if cmd in (".salir", ".exit", "salir()", "exit()"):
                print(colorear("  ¡Hasta luego!", CYAN))
                break

            if cmd == ".ayuda":
                print(AYUDA)
                continue

            if cmd == ".limpiar":
                os.system("clear" if os.name != "nt" else "cls")
                runner.session_vars.clear()
                buffer_multilinea.clear()
                print(colorear("  Sesión limpiada.", GREEN))
                continue

            if cmd == ".vars":
                if not runner.session_vars:
                    print(colorear("  (sin variables en sesión)", DIM))
                else:
                    print(colorear(f"  {len(runner.session_vars)} declaración(es) en sesión:", BOLD))
                    for v in runner.session_vars:
                        print(colorear(f"    {v}", WHITE))
                continue

            if cmd == ".hist":
                n = readline.get_current_history_length()
                for i in range(max(0, n - 20), n):
                    item = readline.get_history_item(i + 1)
                    if item:
                        print(colorear(f"  {i+1:3}: {item}", DIM))
                continue

            if cmd == ".tiempo":
                medir_tiempo = not medir_tiempo
                estado = colorear("activado", GREEN) if medir_tiempo else colorear("desactivado", RED)
                print(f"  Medición de tiempo {estado}")
                continue

            if cmd.startswith(".cargar "):
                ruta = cmd[8:].strip()
                try:
                    codigo = Path(ruta).read_text(encoding="utf-8")
                    stdout, stderr, code = runner.ejecutar(codigo)
                    if stdout:
                        print(colorear(stdout.rstrip(), WHITE))
                    if stderr:
                        print(colorear(stderr.rstrip(), RED))
                    if code == 0:
                        runner.registrar_declaracion(codigo)
                        print(colorear(f"  ✓ {ruta} cargado en sesión", GREEN))
                except FileNotFoundError:
                    print(colorear(f"  ✗ Archivo no encontrado: {ruta}", RED))
                continue

            # ── Multi-línea ────────────────────────────────────────────────
            if buffer_multilinea or es_incompleto(entrada) or detectar_linea_continua(entrada):
                buffer_multilinea.append(entrada)

                # Línea vacía termina bloque multi-línea
                if entrada == "" and buffer_multilinea:
                    codigo = "\n".join(buffer_multilinea)
                    buffer_multilinea.clear()
                    if not codigo.strip():
                        continue
                elif es_incompleto("\n".join(buffer_multilinea)):
                    continue
                elif entrada != "" and (es_incompleto("\n".join(buffer_multilinea)) or
                                         detectar_linea_continua(entrada)):
                    continue
                else:
                    codigo = "\n".join(buffer_multilinea)
                    buffer_multilinea.clear()
            else:
                codigo = entrada

            if not codigo.strip():
                continue

            # ── Ejecutar ───────────────────────────────────────────────────
            import time
            t0 = time.monotonic()
            stdout, stderr, code = runner.ejecutar(codigo)
            elapsed = (time.monotonic() - t0) * 1000

            if stdout:
                # Colorear output de resultados
                for linea_out in stdout.rstrip().split("\n"):
                    print(colorear("  " + linea_out, WHITE))

            if stderr:
                for linea_err in stderr.rstrip().split("\n"):
                    if linea_err.strip():
                        print(colorear("  ✗ " + linea_err, RED))

            if medir_tiempo and codigo.strip():
                print(colorear(f"  ⏱ {elapsed:.2f}ms", DIM))

            # Guardar declaraciones exitosas en sesión
            if code == 0:
                runner.registrar_declaracion(codigo)

        except KeyboardInterrupt:
            print()
            continue
        except Exception as e:
            print(colorear(f"  ✗ Error interno REPL: {e}", RED))


def main():
    parser = argparse.ArgumentParser(description="C-Forge REPL interactivo")
    parser.add_argument("--interpreter", "-i", default="./cforgev",
                        help="Ruta al intérprete cforgev")
    parser.add_argument("--no-banner", action="store_true")
    parser.add_argument("--version", action="version", version="cforge-repl 2.5.0")
    args = parser.parse_args()

    repl_main(args.interpreter)


if __name__ == "__main__":
    main()
