#!/usr/bin/env python3
"""
cfpkg registry — Servidor HTTP del registro de paquetes C-Forge
Funciona como npm registry minimalista pero para paquetes .cfv

Uso (servidor):
  python3 pkg_registry.py serve --port 7373 --dir ./packages

Cliente (cfpkg):
  cfpkg publish mi_paquete/
  cfpkg install nombre@version
  cfpkg search texto
  cfpkg info nombre
  cfpkg list
"""

import sys
import os
import re
import json
import hashlib
import tarfile
import tempfile
import argparse
import shutil
from pathlib import Path
from datetime import datetime
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs, quote
import urllib.request

# ── Utilidades ─────────────────────────────────────────────────────────────────
def ahora() -> str:
    return datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")

def sha256_archivo(ruta: Path) -> str:
    h = hashlib.sha256()
    with open(ruta, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()

def leer_cforge_toml(directorio: Path) -> dict:
    """Lee cforge.toml o cforge.json del paquete."""
    for nombre in ("cforge.toml", "cforge.json", "paquete.json"):
        p = directorio / nombre
        if p.exists():
            texto = p.read_text(encoding="utf-8")
            if nombre.endswith(".json"):
                return json.loads(texto)
            # TOML simple (solo claves planas y secciones)
            resultado = {}
            seccion = resultado
            for linea in texto.split("\n"):
                linea = linea.strip()
                if not linea or linea.startswith("#"):
                    continue
                if linea.startswith("[") and linea.endswith("]"):
                    nombre_sec = linea[1:-1]
                    resultado[nombre_sec] = {}
                    seccion = resultado[nombre_sec]
                    continue
                if "=" in linea:
                    k, _, v = linea.partition("=")
                    k = k.strip()
                    v = v.strip().strip('"').strip("'")
                    seccion[k] = v
            return resultado
    raise FileNotFoundError("No se encontró cforge.toml ni cforge.json en el directorio")


# ── Almacenamiento del registro ────────────────────────────────────────────────
class Registry:
    def __init__(self, directorio: Path):
        self.dir = directorio
        self.dir.mkdir(parents=True, exist_ok=True)
        self.meta_file = self.dir / "index.json"
        self.pkgs_dir = self.dir / "packages"
        self.pkgs_dir.mkdir(exist_ok=True)
        self._cargar_index()

    def _cargar_index(self):
        if self.meta_file.exists():
            self.index = json.loads(self.meta_file.read_text())
        else:
            self.index = {"paquetes": {}, "total": 0}

    def _guardar_index(self):
        self.meta_file.write_text(json.dumps(self.index, indent=2, ensure_ascii=False))

    def publicar(self, nombre: str, version: str, descripcion: str,
                 autor: str, tarball: bytes, keywords: list = None) -> dict:
        """Publica una versión de un paquete."""
        sha = hashlib.sha256(tarball).hexdigest()
        nombre_archivo = f"{nombre}-{version}.cfpkg"
        ruta_tarball = self.pkgs_dir / nombre_archivo
        ruta_tarball.write_bytes(tarball)

        if nombre not in self.index["paquetes"]:
            self.index["paquetes"][nombre] = {
                "nombre": nombre,
                "descripcion": descripcion,
                "autor": autor,
                "keywords": keywords or [],
                "versiones": {},
                "dist-tags": {"latest": version},
                "creado": ahora(),
                "modificado": ahora(),
                "descargas": 0
            }

        pkg = self.index["paquetes"][nombre]
        pkg["versiones"][version] = {
            "version": version,
            "descripcion": descripcion,
            "autor": autor,
            "keywords": keywords or [],
            "publicado": ahora(),
            "archivo": nombre_archivo,
            "sha256": sha,
            "tamaño": len(tarball)
        }
        pkg["dist-tags"]["latest"] = version
        pkg["modificado"] = ahora()
        self.index["total"] = len(self.index["paquetes"])
        self._guardar_index()

        return {"ok": True, "nombre": nombre, "version": version, "sha256": sha}

    def obtener_paquete(self, nombre: str) -> dict:
        if nombre not in self.index["paquetes"]:
            return None
        return self.index["paquetes"][nombre]

    def obtener_version(self, nombre: str, version: str = None) -> dict:
        pkg = self.obtener_paquete(nombre)
        if not pkg:
            return None
        if version is None:
            version = pkg["dist-tags"]["latest"]
        if version == "latest":
            version = pkg["dist-tags"]["latest"]
        return pkg["versiones"].get(version)

    def descargar_tarball(self, nombre_archivo: str) -> bytes:
        ruta = self.pkgs_dir / nombre_archivo
        if not ruta.exists():
            return None
        # Incrementar contador
        for nombre, pkg in self.index["paquetes"].items():
            for ver, info in pkg["versiones"].items():
                if info["archivo"] == nombre_archivo:
                    pkg["descargas"] = pkg.get("descargas", 0) + 1
        self._guardar_index()
        return ruta.read_bytes()

    def buscar(self, query: str) -> list:
        query = query.lower()
        resultados = []
        for nombre, pkg in self.index["paquetes"].items():
            score = 0
            if query in nombre.lower(): score += 10
            if query in pkg.get("descripcion", "").lower(): score += 5
            for kw in pkg.get("keywords", []):
                if query in kw.lower(): score += 3
            if score > 0:
                resultados.append({
                    "nombre": nombre,
                    "descripcion": pkg.get("descripcion", ""),
                    "version": pkg["dist-tags"]["latest"],
                    "autor": pkg.get("autor", ""),
                    "descargas": pkg.get("descargas", 0),
                    "score": score
                })
        return sorted(resultados, key=lambda x: -x["score"])

    def listar(self) -> list:
        resultado = []
        for nombre, pkg in self.index["paquetes"].items():
            resultado.append({
                "nombre": nombre,
                "version": pkg["dist-tags"]["latest"],
                "descripcion": pkg.get("descripcion", ""),
                "autor": pkg.get("autor", ""),
                "descargas": pkg.get("descargas", 0),
                "modificado": pkg.get("modificado", "")
            })
        return sorted(resultado, key=lambda x: -x["descargas"])


# ── Servidor HTTP ──────────────────────────────────────────────────────────────
_registry: Registry = None

class RegistryHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print(f"[{ahora()}] {format % args}")

    def send_json(self, data, status=200):
        body = json.dumps(data, ensure_ascii=False, indent=2).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", len(body))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def send_bytes(self, data, content_type="application/octet-stream", status=200):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", len(data))
        self.end_headers()
        self.wfile.write(data)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization")
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")
        qs = parse_qs(parsed.query)

        # GET / — info del registry
        if path == "" or path == "/":
            return self.send_json({
                "nombre": "C-Forge Package Registry",
                "version": "2.4.0",
                "total_paquetes": _registry.index["total"],
                "endpoints": {
                    "listar": "GET /paquetes",
                    "buscar": "GET /buscar?q=texto",
                    "info": "GET /paquetes/:nombre",
                    "version": "GET /paquetes/:nombre/:version",
                    "descargar": "GET /descargar/:archivo",
                    "publicar": "PUT /paquetes/:nombre/:version"
                }
            })

        # GET /paquetes — lista todos
        if path == "/paquetes":
            return self.send_json(_registry.listar())

        # GET /buscar?q=texto
        if path == "/buscar":
            q = qs.get("q", [""])[0]
            return self.send_json(_registry.buscar(q))

        # GET /paquetes/:nombre
        m = re.match(r'^/paquetes/([^/]+)$', path)
        if m:
            nombre = m.group(1)
            pkg = _registry.obtener_paquete(nombre)
            if not pkg:
                return self.send_json({"error": f"Paquete '{nombre}' no encontrado"}, 404)
            return self.send_json(pkg)

        # GET /paquetes/:nombre/:version
        m = re.match(r'^/paquetes/([^/]+)/([^/]+)$', path)
        if m:
            nombre, version = m.group(1), m.group(2)
            ver = _registry.obtener_version(nombre, version)
            if not ver:
                return self.send_json({"error": "Versión no encontrada"}, 404)
            return self.send_json(ver)

        # GET /descargar/:archivo
        m = re.match(r'^/descargar/(.+\.cfpkg)$', path)
        if m:
            nombre_arch = m.group(1)
            data = _registry.descargar_tarball(nombre_arch)
            if not data:
                return self.send_json({"error": "Archivo no encontrado"}, 404)
            return self.send_bytes(data)

        self.send_json({"error": "Ruta no encontrada"}, 404)

    def do_PUT(self):
        parsed = urlparse(self.path)
        path = parsed.path.rstrip("/")
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)

        # PUT /paquetes/:nombre/:version — publicar
        m = re.match(r'^/paquetes/([^/]+)/([^/]+)$', path)
        if m:
            nombre, version = m.group(1), m.group(2)
            try:
                if self.headers.get("Content-Type", "").startswith("application/json"):
                    meta = json.loads(body)
                    tarball_b64 = meta.pop("tarball", None)
                    if tarball_b64:
                        import base64
                        tarball = base64.b64decode(tarball_b64)
                    else:
                        return self.send_json({"error": "Falta campo 'tarball'"}, 400)
                else:
                    tarball = body
                    meta = {}

                resultado = _registry.publicar(
                    nombre=nombre,
                    version=version,
                    descripcion=meta.get("descripcion", ""),
                    autor=meta.get("autor", "anónimo"),
                    tarball=tarball,
                    keywords=meta.get("keywords", [])
                )
                return self.send_json(resultado, 201)
            except Exception as e:
                return self.send_json({"error": str(e)}, 500)

        self.send_json({"error": "Ruta no encontrada"}, 404)


# ── Cliente cfpkg ──────────────────────────────────────────────────────────────
DEFAULT_REGISTRY = "http://localhost:7373"

def cmd_publish(args):
    """cfpkg publish ./mi-paquete"""
    directorio = Path(args.directorio)
    try:
        meta = leer_cforge_toml(directorio)
    except FileNotFoundError as e:
        print(f"cfpkg: {e}", file=sys.stderr)
        sys.exit(1)

    nombre = meta.get("nombre") or meta.get("name")
    version = meta.get("version", "1.0.0")
    descripcion = meta.get("descripcion") or meta.get("description", "")
    autor = meta.get("autor") or meta.get("author", "anónimo")
    keywords = meta.get("keywords", [])
    if isinstance(keywords, str):
        keywords = [k.strip() for k in keywords.split(",")]

    if not nombre:
        print("cfpkg: cforge.toml debe tener campo 'nombre'", file=sys.stderr)
        sys.exit(1)

    # Crear tarball
    with tempfile.NamedTemporaryFile(suffix=".cfpkg", delete=False) as tmp:
        with tarfile.open(tmp.name, "w:gz") as tar:
            tar.add(str(directorio), arcname=nombre)
        tarball = Path(tmp.name).read_bytes()

    # Publicar via HTTP
    import base64
    payload = json.dumps({
        "descripcion": descripcion,
        "autor": autor,
        "keywords": keywords,
        "tarball": base64.b64encode(tarball).decode()
    }).encode()

    registry = args.registry or DEFAULT_REGISTRY
    url = f"{registry}/paquetes/{nombre}/{version}"
    req = urllib.request.Request(url, data=payload,
        headers={"Content-Type": "application/json"}, method="PUT")
    try:
        with urllib.request.urlopen(req) as resp:
            resultado = json.loads(resp.read())
            print(f"cfpkg: ✓ publicado {nombre}@{version}")
            print(f"       sha256: {resultado['sha256'][:16]}...")
    except Exception as e:
        print(f"cfpkg: error publicando: {e}", file=sys.stderr)
        sys.exit(1)

def cmd_install(args):
    """cfpkg install nombre@version"""
    spec = args.paquete
    if "@" in spec:
        nombre, version = spec.rsplit("@", 1)
    else:
        nombre = spec
        version = "latest"

    registry = args.registry or DEFAULT_REGISTRY
    # Obtener info
    try:
        url = f"{registry}/paquetes/{nombre}/{version}"
        with urllib.request.urlopen(url) as resp:
            info = json.loads(resp.read())
    except Exception as e:
        print(f"cfpkg: error obteniendo info de '{nombre}': {e}", file=sys.stderr)
        sys.exit(1)

    # Descargar tarball
    try:
        dl_url = f"{registry}/descargar/{info['archivo']}"
        with urllib.request.urlopen(dl_url) as resp:
            tarball = resp.read()
    except Exception as e:
        print(f"cfpkg: error descargando: {e}", file=sys.stderr)
        sys.exit(1)

    # Extraer en ./cforge_modules/
    modules_dir = Path("cforge_modules")
    modules_dir.mkdir(exist_ok=True)
    pkg_dir = modules_dir / nombre
    if pkg_dir.exists():
        shutil.rmtree(pkg_dir)

    with tempfile.NamedTemporaryFile(suffix=".cfpkg", delete=False) as tmp:
        tmp.write(tarball)
        tmp_path = tmp.name

    with tarfile.open(tmp_path, "r:gz") as tar:
        tar.extractall(str(modules_dir))
    os.unlink(tmp_path)

    print(f"cfpkg: ✓ instalado {nombre}@{info['version']} → cforge_modules/{nombre}/")

def cmd_search(args):
    """cfpkg search texto"""
    registry = args.registry or DEFAULT_REGISTRY
    q = quote(args.query)
    try:
        with urllib.request.urlopen(f"{registry}/buscar?q={q}") as resp:
            resultados = json.loads(resp.read())
    except Exception as e:
        print(f"cfpkg: error buscando: {e}", file=sys.stderr)
        sys.exit(1)

    if not resultados:
        print("cfpkg: sin resultados")
        return

    print(f"\n{'NOMBRE':<25} {'VERSIÓN':<10} {'DESCARGAS':<10} DESCRIPCIÓN")
    print("─" * 80)
    for r in resultados[:20]:
        desc = r.get("descripcion", "")[:40]
        print(f"{r['nombre']:<25} {r['version']:<10} {r['descargas']:<10} {desc}")

def cmd_info(args):
    """cfpkg info nombre"""
    registry = args.registry or DEFAULT_REGISTRY
    try:
        with urllib.request.urlopen(f"{registry}/paquetes/{args.nombre}") as resp:
            pkg = json.loads(resp.read())
    except Exception as e:
        print(f"cfpkg: error: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"\n{'─'*50}")
    print(f"  📦 {pkg['nombre']}")
    print(f"  Descripción: {pkg.get('descripcion','')}")
    print(f"  Autor: {pkg.get('autor','')}")
    print(f"  Última versión: {pkg['dist-tags']['latest']}")
    print(f"  Versiones: {', '.join(pkg['versiones'].keys())}")
    print(f"  Keywords: {', '.join(pkg.get('keywords',[]))}")
    print(f"  Descargas: {pkg.get('descargas',0)}")
    print(f"  Modificado: {pkg.get('modificado','')}")
    print(f"{'─'*50}\n")

def cmd_list(args):
    """cfpkg list"""
    registry = args.registry or DEFAULT_REGISTRY
    try:
        with urllib.request.urlopen(f"{registry}/paquetes") as resp:
            pkgs = json.loads(resp.read())
    except Exception as e:
        print(f"cfpkg: error: {e}", file=sys.stderr)
        sys.exit(1)

    print(f"\n  Registro: {registry}")
    print(f"  Total: {len(pkgs)} paquetes\n")
    for p in pkgs:
        print(f"  📦 {p['nombre']:<25} {p['version']:<10} {p.get('descripcion','')[:50]}")

def cmd_serve(args):
    """Iniciar servidor del registro."""
    global _registry
    _registry = Registry(Path(args.dir))
    servidor = HTTPServer(("0.0.0.0", args.port), RegistryHandler)
    print(f"cfpkg registry: escuchando en http://0.0.0.0:{args.port}")
    print(f"cfpkg registry: almacenamiento en {args.dir}")
    print(f"cfpkg registry: {_registry.index['total']} paquete(s) registrados")
    try:
        servidor.serve_forever()
    except KeyboardInterrupt:
        print("\ncfpkg registry: detenido")


# ── CLI principal ──────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cfpkg — Gestor de paquetes C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Comandos:
  serve     Iniciar el servidor del registro
  publish   Publicar un paquete
  install   Instalar un paquete
  search    Buscar paquetes
  info      Ver información de un paquete
  list      Listar todos los paquetes
        """
    )
    parser.add_argument("--version", action="version", version="cfpkg 2.4.0")
    parser.add_argument("--registry", default=DEFAULT_REGISTRY,
                        help=f"URL del registry (default: {DEFAULT_REGISTRY})")
    sub = parser.add_subparsers(dest="cmd")

    # serve
    p_serve = sub.add_parser("serve", help="Iniciar servidor del registro")
    p_serve.add_argument("--port", type=int, default=7373)
    p_serve.add_argument("--dir", default="./pkg_store")

    # publish
    p_pub = sub.add_parser("publish", help="Publicar paquete")
    p_pub.add_argument("directorio", default=".", nargs="?")

    # install
    p_inst = sub.add_parser("install", help="Instalar paquete")
    p_inst.add_argument("paquete", help="nombre o nombre@version")

    # search
    p_search = sub.add_parser("search", help="Buscar paquetes")
    p_search.add_argument("query")

    # info
    p_info = sub.add_parser("info", help="Info de un paquete")
    p_info.add_argument("nombre")

    # list
    sub.add_parser("list", help="Listar paquetes")

    args = parser.parse_args()

    if args.cmd == "serve":    cmd_serve(args)
    elif args.cmd == "publish": cmd_publish(args)
    elif args.cmd == "install": cmd_install(args)
    elif args.cmd == "search":  cmd_search(args)
    elif args.cmd == "info":    cmd_info(args)
    elif args.cmd == "list":    cmd_list(args)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
