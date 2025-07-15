#!/usr/bin/env python3
"""
cfwatch — Hot reload para C-Forge
Vigila cambios en archivos .cfv y reinicia el proceso automáticamente.

Uso:
  cfwatch archivo.cfv
  cfwatch src/main.cfv --watch src/ stdlib/
  cfwatch main.cfv --delay 200
  cfwatch main.cfv --clear              # limpiar pantalla en cada reload
  cfwatch main.cfv --on-change "cftest" # comando extra al detectar cambio
"""

import sys
import os
import re
import time
import subprocess
import argparse
import hashlib
import signal
import threading
from pathlib import Path
from typing import List, Optional, Set

# ── Colores ────────────────────────────────────────────────────────────────────
RESET = "\033[0m"; BOLD = "\033[1m"; DIM = "\033[2m"
RED = "\033[91m"; GREEN = "\033[92m"; YELLOW = "\033[93m"
CYAN = "\033[96m"; WHITE = "\033[97m"
def c(t, col): return (col + t + RESET) if sys.stdout.isatty() else t

# ── Watcher ────────────────────────────────────────────────────────────────────
class FileWatcher:
    def __init__(self, paths: List[Path], extensiones: Set[str] = None):
        self.paths = paths
        self.extensiones = extensiones or {".cfv", ".toml", ".json"}
        self._checksums: dict = {}
        self._actualizar_checksums()

    def _sha(self, ruta: Path) -> str:
        try:
            return hashlib.md5(ruta.read_bytes()).hexdigest()
        except:
            return ""

    def _todos_los_archivos(self) -> List[Path]:
        archivos = []
        for p in self.paths:
            if p.is_file():
                archivos.append(p)
            elif p.is_dir():
                for ext in self.extensiones:
                    archivos.extend(p.rglob(f"*{ext}"))
        return archivos

    def _actualizar_checksums(self):
        for f in self._todos_los_archivos():
            self._checksums[str(f)] = self._sha(f)

    def esperar_cambio(self, delay_ms: int = 100) -> List[Path]:
        """Bloquea hasta detectar un cambio. Retorna lista de archivos cambiados."""
        while True:
            time.sleep(delay_ms / 1000)
            archivos = self._todos_los_archivos()
            cambiados = []

            for f in archivos:
                clave = str(f)
                sha_nuevo = self._sha(f)
                if sha_nuevo != self._checksums.get(clave, ""):
                    cambiados.append(f)
                    self._checksums[clave] = sha_nuevo

            # Detectar archivos eliminados
            for clave in list(self._checksums.keys()):
                if not Path(clave).exists():
                    del self._checksums[clave]
                    cambiados.append(Path(clave))

            if cambiados:
                return cambiados


class ProcessManager:
    """Gestiona el proceso del intérprete C-Forge."""
    def __init__(self, interpreter: str, archivo: str):
        self.interpreter = interpreter
        self.archivo = archivo
        self.proceso: Optional[subprocess.Popen] = None
        self._lock = threading.Lock()

    def iniciar(self):
        """Inicia el proceso."""
        with self._lock:
            if self.proceso and self.proceso.poll() is None:
                self.detener()

            self.proceso = subprocess.Popen(
                [self.interpreter, self.archivo],
                preexec_fn=os.setsid if os.name != "nt" else None
            )

    def detener(self):
        """Detiene el proceso y todos sus hijos."""
        if self.proceso and self.proceso.poll() is None:
            try:
                if os.name != "nt":
                    os.killpg(os.getpgid(self.proceso.pid), signal.SIGTERM)
                else:
                    self.proceso.terminate()
                self.proceso.wait(timeout=2)
            except Exception:
                try:
                    self.proceso.kill()
                except:
                    pass

    def reiniciar(self):
        self.detener()
        time.sleep(0.05)
        self.iniciar()

    @property
    def esta_corriendo(self) -> bool:
        return self.proceso is not None and self.proceso.poll() is None


# ── Formato de log ─────────────────────────────────────────────────────────────
def log_cambio(archivos: List[Path], reinicio_num: int):
    ts = time.strftime("%H:%M:%S")
    print(f"\n{c('─' * 50, DIM)}")
    print(f"{c(ts, DIM)} {c(f'[#{reinicio_num}]', CYAN)} {c('cambio detectado:', YELLOW)}")
    for f in archivos[:5]:
        print(f"  {c('↺', GREEN)} {f.name}")
    if len(archivos) > 5:
        print(f"  {c(f'+ {len(archivos)-5} más...', DIM)}")
    print(c('─' * 50, DIM))


# ── Main watch loop ────────────────────────────────────────────────────────────
def watch_loop(args):
    archivo_principal = Path(args.archivo)
    if not archivo_principal.exists():
        print(c(f"  ✗ Archivo no encontrado: {archivo_principal}", RED))
        sys.exit(1)

    # Directorios a vigilar
    watch_dirs = [Path(d) for d in args.watch] if args.watch else [archivo_principal.parent]
    if archivo_principal not in watch_dirs:
        pass  # el archivo ya está cubierto por su directorio

    interpreter = args.interpreter
    if not Path(interpreter).exists():
        import shutil
        found = shutil.which("cforgev") or shutil.which("cforge")
        if found:
            interpreter = found
        else:
            print(c(f"  ✗ Intérprete no encontrado: {interpreter}", RED))
            sys.exit(1)

    # Banner
    print(f"\n{c('cfwatch', BOLD + CYAN)} {c('v2.5.0', DIM)}")
    print(f"  Archivo:  {c(str(archivo_principal), WHITE)}")
    print(f"  Vigila:   {c(', '.join(str(d) for d in watch_dirs), DIM)}")
    print(f"  Delay:    {args.delay}ms")
    print(c("  Ctrl+C para salir\n", DIM))

    watcher = FileWatcher(watch_dirs)
    manager = ProcessManager(interpreter, str(archivo_principal))
    reinicio_num = 0

    def arrancar():
        nonlocal reinicio_num
        reinicio_num += 1
        if args.clear:
            os.system("clear" if os.name != "nt" else "cls")
        print(f"{c('[cfwatch]', CYAN)} {c(f'#{reinicio_num}', DIM)} iniciando {archivo_principal.name}...")
        manager.reiniciar()

    # Primer arranque
    arrancar()

    try:
        while True:
            cambiados = watcher.esperar_cambio(args.delay)
            log_cambio(cambiados, reinicio_num + 1)

            # Ejecutar comando extra si hay
            if args.on_change:
                print(c(f"  → {args.on_change}", DIM))
                subprocess.run(args.on_change, shell=True)

            arrancar()

    except KeyboardInterrupt:
        print(c("\n\n  cfwatch detenido.", DIM))
        manager.detener()


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cfwatch — Hot reload para C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ejemplos:
  cfwatch main.cfv
  cfwatch src/main.cfv --watch src/ stdlib/
  cfwatch servidor.cfv --clear --delay 300
  cfwatch main.cfv --on-change "cftest tests/"
        """
    )
    parser.add_argument("archivo", help="Archivo .cfv principal")
    parser.add_argument("--interpreter", "-i", default="./cforgev")
    parser.add_argument("--watch", "-w", nargs="+", metavar="DIR",
                        help="Directorios adicionales a vigilar")
    parser.add_argument("--delay", "-d", type=int, default=100,
                        help="Delay entre checks en ms (default: 100)")
    parser.add_argument("--clear", "-c", action="store_true",
                        help="Limpiar pantalla en cada reload")
    parser.add_argument("--on-change", metavar="CMD",
                        help="Comando extra a ejecutar al detectar cambio")
    parser.add_argument("--version", action="version", version="cfwatch 2.5.0")
    args = parser.parse_args()

    watch_loop(args)


if __name__ == "__main__":
    main()
