#!/usr/bin/env python3
"""
cfbuild — Build system de C-Forge
Gestiona proyectos multi-archivo, resuelve imports, genera bundles.

Uso:
  cfbuild                   # build desde cforge.toml en directorio actual
  cfbuild --dev             # modo desarrollo (sin optimizaciones)
  cfbuild --prod            # modo producción (bundle optimizado)
  cfbuild --run             # build y ejecutar
  cfbuild init              # crear proyecto nuevo
  cfbuild clean             # limpiar build/

Estructura de proyecto:
  mi-proyecto/
  ├── cforge.toml           # configuración
  ├── src/
  │   └── main.cfv          # punto de entrada
  ├── stdlib/               # módulos locales
  ├── cforge_modules/       # dependencias (cfpkg install)
  ├── tests/                # tests
  └── build/                # salida del build
"""

import sys
import os
import re
import json
import shutil
import subprocess
import argparse
import hashlib
from pathlib import Path
from typing import List, Dict, Set, Optional
from datetime import datetime

# ── TOML mínimo ───────────────────────────────────────────────────────────────
def leer_toml(ruta: Path) -> dict:
    if not ruta.exists():
        return {}
    resultado = {}
    seccion = resultado
    for linea in ruta.read_text(encoding="utf-8").split("\n"):
        linea = linea.strip()
        if not linea or linea.startswith("#"):
            continue
        if linea.startswith("[") and linea.endswith("]"):
            nombre_sec = linea[1:-1].strip()
            resultado[nombre_sec] = {}
            seccion = resultado[nombre_sec]
            continue
        if "=" in linea:
            k, _, v = linea.partition("=")
            k = k.strip()
            v = v.strip()
            # Parsear tipos básicos
            if v.startswith('"') or v.startswith("'"):
                v = v.strip('"').strip("'")
            elif v == "true":   v = True
            elif v == "false":  v = False
            elif v.lstrip('-').isdigit(): v = int(v)
            elif v.startswith("["):
                v = [x.strip().strip('"').strip("'")
                     for x in v.strip("[]").split(",") if x.strip()]
            seccion[k] = v
    return resultado

def escribir_toml(ruta: Path, datos: dict):
    lineas = []
    # Claves de nivel raíz primero
    for k, v in datos.items():
        if not isinstance(v, dict):
            if isinstance(v, bool): lineas.append(f'{k} = {"true" if v else "false"}')
            elif isinstance(v, str): lineas.append(f'{k} = "{v}"')
            elif isinstance(v, list): lineas.append(f'{k} = [{", ".join(repr(x) for x in v)}]')
            else: lineas.append(f'{k} = {v}')
    # Secciones
    for k, v in datos.items():
        if isinstance(v, dict):
            lineas.append(f'\n[{k}]')
            for sk, sv in v.items():
                if isinstance(sv, bool): lineas.append(f'{sk} = {"true" if sv else "false"}')
                elif isinstance(sv, str): lineas.append(f'{sk} = "{sv}"')
                elif isinstance(sv, list): lineas.append(f'{sk} = [{", ".join(repr(x) for x in sv)}]')
                else: lineas.append(f'{sk} = {sv}')
    ruta.write_text("\n".join(lineas) + "\n", encoding="utf-8")


# ── Resolver dependencias (grafo de imports) ──────────────────────────────────
class Resolver:
    def __init__(self, raiz: Path, stdlib_dir: Optional[Path] = None,
                 modules_dir: Optional[Path] = None):
        self.raiz = raiz
        self.stdlib_dir = stdlib_dir or raiz / "stdlib"
        self.modules_dir = modules_dir or raiz / "cforge_modules"
        self._cache: Dict[str, str] = {}
        self._en_proceso: Set[str] = set()

    def resolver(self, punto_entrada: Path) -> List[Path]:
        """Retorna lista de archivos en orden topológico (deps primero)."""
        orden = []
        visitados = set()
        self._visitar(punto_entrada.resolve(), orden, visitados)
        return orden

    def _visitar(self, ruta: Path, orden: list, visitados: set):
        clave = str(ruta)
        if clave in visitados:
            return
        if clave in self._en_proceso:
            raise RecursionError(f"Dependencia circular detectada: {ruta}")

        self._en_proceso.add(clave)
        visitados.add(clave)

        try:
            codigo = ruta.read_text(encoding="utf-8")
        except FileNotFoundError:
            raise FileNotFoundError(f"Archivo no encontrado: {ruta}")

        # Encontrar imports
        for m in re.finditer(r'\bimportar\s+"([^"]+)"', codigo):
            modulo = m.group(1)
            dep = self._encontrar_modulo(modulo, ruta.parent)
            if dep:
                self._visitar(dep, orden, visitados)

        self._en_proceso.discard(clave)
        orden.append(ruta)

    def _encontrar_modulo(self, nombre: str, directorio_actual: Path) -> Optional[Path]:
        """Busca un módulo en múltiples ubicaciones."""
        buscar_en = [
            directorio_actual / f"{nombre}.cfv",
            directorio_actual / nombre / "index.cfv",
            self.raiz / "src" / f"{nombre}.cfv",
            self.stdlib_dir / f"{nombre}.cfv",
            self.modules_dir / nombre / "index.cfv",
            self.modules_dir / f"{nombre}.cfv",
        ]
        for p in buscar_en:
            if p.exists():
                return p.resolve()
        return None


# ── Bundler ───────────────────────────────────────────────────────────────────
class Bundler:
    def __init__(self, config: dict, raiz: Path):
        self.config = config
        self.raiz = raiz

    def bundle(self, archivos: List[Path], modo: str = "dev") -> str:
        """Genera un archivo bundle con todos los módulos."""
        partes = [
            f"// C-Forge Bundle — generado por cfbuild {datetime.now().isoformat()}",
            f"// Proyecto: {self.config.get('nombre', 'sin nombre')} v{self.config.get('version', '0.0.0')}",
            f"// Modo: {modo}",
            "",
        ]

        modulos_vistos = set()
        for archivo in archivos:
            clave = str(archivo)
            if clave in modulos_vistos:
                continue
            modulos_vistos.add(clave)

            codigo = archivo.read_text(encoding="utf-8")

            # En modo prod: eliminar comentarios y líneas vacías duplicadas
            if modo == "prod":
                codigo = self._minificar(codigo)

            partes.append(f"\n// ── Módulo: {archivo.relative_to(self.raiz)} ──")
            # Eliminar líneas de importar (ya están incluidos en orden)
            codigo_sin_imports = re.sub(r'\bimportar\s+"[^"]+"\s*\n?', '', codigo)
            partes.append(codigo_sin_imports)

        return "\n".join(partes)

    def _minificar(self, codigo: str) -> str:
        """Minificación básica: quitar comentarios y espacios excesivos."""
        lineas = []
        prev_vacia = False
        for linea in codigo.split("\n"):
            stripped = linea.rstrip()
            # Quitar comentarios de una línea
            if stripped.lstrip().startswith("//"):
                continue
            # Quitar comentarios inline
            stripped = re.sub(r'\s*//(?!["\']).*$', '', stripped)
            if not stripped:
                if not prev_vacia:
                    lineas.append("")
                prev_vacia = True
            else:
                lineas.append(stripped)
                prev_vacia = False
        return "\n".join(lineas)


# ── Caché de build ────────────────────────────────────────────────────────────
class CacheBuild:
    def __init__(self, build_dir: Path):
        self.cache_file = build_dir / ".build_cache.json"
        self._cache = self._cargar()

    def _cargar(self) -> dict:
        if self.cache_file.exists():
            try:
                return json.loads(self.cache_file.read_text())
            except Exception:
                pass
        return {}

    def guardar(self):
        self.cache_file.write_text(json.dumps(self._cache, indent=2))

    def sha_archivo(self, ruta: Path) -> str:
        try:
            return hashlib.md5(ruta.read_bytes()).hexdigest()
        except:
            return ""

    def necesita_rebuild(self, archivos: List[Path]) -> bool:
        for f in archivos:
            sha = self.sha_archivo(f)
            if self._cache.get(str(f)) != sha:
                return True
        return False

    def actualizar(self, archivos: List[Path]):
        for f in archivos:
            self._cache[str(f)] = self.sha_archivo(f)
        self.guardar()


# ── Build principal ───────────────────────────────────────────────────────────
class Builder:
    def __init__(self, raiz: Path):
        self.raiz = raiz
        self.config_file = raiz / "cforge.toml"
        self.config = leer_toml(self.config_file)
        self.build_dir = raiz / "build"
        self.cache = None

    def build(self, modo: str = "dev", ejecutar: bool = False,
              verbose: bool = False) -> int:
        print(f"\n  cfbuild — {self.config.get('nombre', 'proyecto')} v{self.config.get('version', '0.0.0')}")
        print(f"  Modo: {modo}\n")

        self.build_dir.mkdir(exist_ok=True)
        self.cache = CacheBuild(self.build_dir)

        # Punto de entrada
        entrada_cfg = self.config.get("build", {}).get("entrada", "src/main.cfv")
        entrada = self.raiz / entrada_cfg
        if not entrada.exists():
            entrada = self.raiz / "main.cfv"
        if not entrada.exists():
            print(f"  ✗ Punto de entrada no encontrado: {entrada_cfg}")
            return 1

        if verbose:
            print(f"  Entrada: {entrada}")

        # Resolver dependencias
        t0 = __import__("time").monotonic()
        try:
            resolver = Resolver(self.raiz)
            archivos = resolver.resolver(entrada)
        except (FileNotFoundError, RecursionError) as e:
            print(f"  ✗ Error resolviendo dependencias: {e}")
            return 1

        if verbose:
            print(f"  Resueltos {len(archivos)} archivos:")
            for f in archivos:
                print(f"    {f.relative_to(self.raiz)}")

        # Verificar si necesita rebuild
        if not self.cache.necesita_rebuild(archivos):
            print(f"  ✓ Sin cambios — build actualizado")
            if ejecutar:
                return self._ejecutar(modo)
            return 0

        # Bundle
        bundler = Bundler(self.config, self.raiz)
        bundle_code = bundler.bundle(archivos, modo)

        # Escribir salida
        nombre_salida = self.config.get("build", {}).get("salida", "programa")
        salida = self.build_dir / f"{nombre_salida}.cfv"
        salida.write_text(bundle_code, encoding="utf-8")

        duracion = (__import__("time").monotonic() - t0) * 1000
        self.cache.actualizar(archivos)

        print(f"  ✓ Build completado: {salida} ({duracion:.0f}ms)")
        print(f"  ✓ {len(archivos)} módulos → {len(bundle_code)} bytes")

        if ejecutar:
            return self._ejecutar(modo)

        return 0

    def _ejecutar(self, modo: str) -> int:
        interpreter = self.config.get("build", {}).get("interpreter", "./cforgev")
        nombre_salida = self.config.get("build", {}).get("salida", "programa")
        salida = self.build_dir / f"{nombre_salida}.cfv"

        print(f"\n  Ejecutando: {salida.name}")
        print("  " + "─" * 40)
        result = subprocess.run([interpreter, str(salida)])
        return result.returncode


# ── Comando init ──────────────────────────────────────────────────────────────
def cmd_init(args):
    nombre = args.nombre or Path.cwd().name
    raiz = Path(args.directorio) if args.directorio else Path.cwd() / nombre

    if raiz.exists() and list(raiz.iterdir()):
        print(f"  ✗ El directorio '{raiz}' ya existe y no está vacío")
        sys.exit(1)

    raiz.mkdir(parents=True, exist_ok=True)
    (raiz / "src").mkdir()
    (raiz / "tests").mkdir()
    (raiz / "stdlib").mkdir()
    (raiz / "build").mkdir()

    # cforge.toml
    escribir_toml(raiz / "cforge.toml", {
        "nombre": nombre,
        "version": "0.1.0",
        "descripcion": f"Proyecto C-Forge: {nombre}",
        "autor": os.environ.get("USER", "desconocido"),
        "licencia": "MIT",
        "build": {
            "entrada": "src/main.cfv",
            "salida": nombre,
            "interpreter": "./cforgev",
        },
        "dependencias": {},
        "dev-dependencias": {},
    })

    # src/main.cfv
    (raiz / "src" / "main.cfv").write_text(f'''\
// {nombre} — Punto de entrada principal

mostrar("¡Hola desde {nombre}!")
''', encoding="utf-8")

    # tests/main_test.cfv
    (raiz / "tests" / "main_test.cfv").write_text(f'''\
// Tests de {nombre}

prueba("ejemplo pasa", funcion() {{
    afirmar_igual(1 + 1, 2)
}})

prueba("texto funciona", funcion() {{
    afirmar_contiene("hola mundo", "mundo")
}})
''', encoding="utf-8")

    # .gitignore
    (raiz / ".gitignore").write_text("build/\ncforge_modules/\n.build_cache.json\n*.cfpkg\n")

    print(f"\n  ✓ Proyecto '{nombre}' creado en {raiz}/")
    print(f"\n  Para empezar:")
    print(f"    cd {raiz.name}")
    print(f"    cfbuild --run")


def cmd_clean(raiz: Path):
    build_dir = raiz / "build"
    if build_dir.exists():
        shutil.rmtree(build_dir)
        print(f"  ✓ Limpiado: {build_dir}")
    else:
        print(f"  (build/ no existe)")


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cfbuild — Build system de C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--dev",  action="store_true", help="Modo desarrollo (default)")
    parser.add_argument("--prod", action="store_true", help="Modo producción (minificado)")
    parser.add_argument("--run",  action="store_true", help="Build y ejecutar")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--dir", default=".", help="Directorio del proyecto")
    parser.add_argument("--version", action="version", version="cfbuild 2.5.0")

    sub = parser.add_subparsers(dest="cmd")

    p_init = sub.add_parser("init", help="Crear nuevo proyecto")
    p_init.add_argument("nombre", nargs="?", help="Nombre del proyecto")
    p_init.add_argument("--directorio", help="Directorio destino")

    sub.add_parser("clean", help="Limpiar build/")
    sub.add_parser("info",  help="Info del proyecto")

    args = parser.parse_args()

    raiz = Path(args.dir).resolve()

    if args.cmd == "init":
        cmd_init(args)
        return

    if args.cmd == "clean":
        cmd_clean(raiz)
        return

    if args.cmd == "info":
        config = leer_toml(raiz / "cforge.toml")
        print(json.dumps(config, indent=2, ensure_ascii=False, default=str))
        return

    # Build por defecto
    modo = "prod" if args.prod else "dev"
    builder = Builder(raiz)
    sys.exit(builder.build(modo=modo, ejecutar=args.run, verbose=args.verbose))


if __name__ == "__main__":
    main()
