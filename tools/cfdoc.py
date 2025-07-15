#!/usr/bin/env python3
"""
cfdoc — Generador de documentación de C-Forge
Extrae comentarios ///, firmas de funciones y genera HTML/Markdown.

Uso:
  cfdoc stdlib/                    # documentar directorio
  cfdoc stdlib/matematica.cfv      # archivo específico
  cfdoc stdlib/ --format md        # salida Markdown
  cfdoc stdlib/ --out docs/        # directorio de salida
  cfdoc stdlib/ --serve 8080       # servidor HTTP de docs

Formato de doc-comments:
  /// Descripción de la función.
  /// @param nombre: tipo — descripción
  /// @retorna tipo — descripción
  /// @ejemplo
  ///   resultado = sumar(2, 3)  // → 5
  /// @ver matematica_avanzada
  funcion sumar(a: numero, b: numero): numero { ... }
"""

import sys
import re
import os
import json
import argparse
import http.server
import threading
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional, Dict
from datetime import datetime

# ── Estructuras de datos ───────────────────────────────────────────────────────
@dataclass
class Param:
    nombre: str
    tipo: str
    descripcion: str

@dataclass
class DocFuncion:
    nombre: str
    descripcion: str
    params: List[Param]
    retorna: str
    retorna_desc: str
    ejemplo: str
    ver_tambien: List[str]
    linea: int
    firma: str
    es_privada: bool  # empieza con _

@dataclass
class DocVariable:
    nombre: str
    tipo: str
    descripcion: str
    linea: int
    es_privada: bool

@dataclass
class DocModulo:
    nombre: str
    archivo: str
    descripcion: str
    funciones: List[DocFuncion] = field(default_factory=list)
    variables: List[DocVariable] = field(default_factory=list)
    version: str = ""
    autor: str = ""


# ── Parser de doc-comments ────────────────────────────────────────────────────
class DocParser:
    def parsear_archivo(self, ruta: Path) -> DocModulo:
        codigo = ruta.read_text(encoding="utf-8")
        lineas = codigo.split("\n")
        nombre = ruta.stem

        modulo = DocModulo(
            nombre=nombre,
            archivo=str(ruta),
            descripcion=self._extraer_descripcion_modulo(lineas)
        )

        i = 0
        while i < len(lineas):
            linea = lineas[i].strip()

            # Recolectar bloque de doc-comments
            if linea.startswith("///"):
                doc_block = []
                while i < len(lineas) and lineas[i].strip().startswith("///"):
                    doc_block.append(lineas[i].strip()[3:].lstrip())
                    i += 1

                # La siguiente línea que no sea vacía puede ser una declaración
                while i < len(lineas) and not lineas[i].strip():
                    i += 1

                if i < len(lineas):
                    decl = lineas[i].strip()
                    if decl.startswith("funcion "):
                        fn = self._parsear_funcion(decl, doc_block, i + 1)
                        if fn:
                            modulo.funciones.append(fn)
                    elif decl.startswith("sea "):
                        var = self._parsear_variable(decl, doc_block, i + 1)
                        if var:
                            modulo.variables.append(var)
                continue

            # Funciones sin doc-comment
            elif linea.startswith("funcion "):
                fn = self._parsear_funcion(linea, [], i + 1)
                if fn:
                    modulo.funciones.append(fn)

            i += 1

        return modulo

    def _extraer_descripcion_modulo(self, lineas: list) -> str:
        """Extrae el comentario del encabezado del archivo."""
        desc_parts = []
        for linea in lineas[:10]:
            stripped = linea.strip()
            if stripped.startswith("// ") or stripped.startswith("//\t"):
                texto = stripped[3:]
                if texto and not texto.startswith("stdlib/") and not texto.startswith("Uso:"):
                    desc_parts.append(texto)
            elif stripped.startswith("///"):
                desc_parts.append(stripped[3:].lstrip())
            elif stripped and not stripped.startswith("//"):
                break
        return " ".join(desc_parts[:3]) if desc_parts else ""

    def _parsear_funcion(self, decl: str, doc_block: list, linea: int) -> Optional[DocFuncion]:
        m = re.match(r'funcion\s+(\w+)\s*\(([^)]*)\)\s*(?::\s*(\w+))?', decl)
        if not m:
            return None

        nombre = m.group(1)
        params_str = m.group(2)
        tipo_ret = m.group(3) or "nulo"
        es_privada = nombre.startswith("_")

        # Parsear parámetros
        params = []
        for p in params_str.split(","):
            p = p.strip()
            if not p:
                continue
            pm = re.match(r'(\w+)\s*(?::\s*(\w+))?', p)
            if pm:
                params.append(Param(nombre=pm.group(1), tipo=pm.group(2) or "cualquiera", descripcion=""))

        # Parsear doc-block
        descripcion = ""
        retorna_desc = ""
        ejemplo = ""
        ver_tambien = []
        param_docs: Dict[str, str] = {}

        en_ejemplo = False
        ejemplo_lines = []

        for linea_doc in doc_block:
            if linea_doc.startswith("@param"):
                m2 = re.match(r'@param\s+(\w+)(?:\s*:\s*\w+)?\s*[—-]\s*(.*)', linea_doc)
                if m2:
                    param_docs[m2.group(1)] = m2.group(2)
            elif linea_doc.startswith("@retorna") or linea_doc.startswith("@returns"):
                m2 = re.match(r'@ret\w+\s+\w+\s*[—-]\s*(.*)', linea_doc)
                if m2:
                    retorna_desc = m2.group(1)
            elif linea_doc.startswith("@ejemplo") or linea_doc.startswith("@example"):
                en_ejemplo = True
            elif linea_doc.startswith("@ver") or linea_doc.startswith("@see"):
                ver_tambien.extend(w.strip() for w in linea_doc.split()[1:])
            elif en_ejemplo:
                ejemplo_lines.append(linea_doc)
            else:
                if descripcion:
                    descripcion += " " + linea_doc
                else:
                    descripcion = linea_doc

        # Aplicar doc de params
        for p in params:
            if p.nombre in param_docs:
                p.descripcion = param_docs[p.nombre]

        firma = f"funcion {nombre}({params_str})" + (f": {tipo_ret}" if tipo_ret != "nulo" else "")

        return DocFuncion(
            nombre=nombre,
            descripcion=descripcion.strip(),
            params=params,
            retorna=tipo_ret,
            retorna_desc=retorna_desc,
            ejemplo="\n".join(ejemplo_lines),
            ver_tambien=ver_tambien,
            linea=linea,
            firma=firma,
            es_privada=es_privada
        )

    def _parsear_variable(self, decl: str, doc_block: list, linea: int) -> Optional[DocVariable]:
        m = re.match(r'sea\s+(\w+)\s*(?::\s*(\w+))?', decl)
        if not m:
            return None
        nombre = m.group(1)
        tipo = m.group(2) or "cualquiera"
        desc = doc_block[0] if doc_block else ""
        return DocVariable(nombre=nombre, tipo=tipo, descripcion=desc,
                           linea=linea, es_privada=nombre.startswith("_"))


# ── Generador HTML ─────────────────────────────────────────────────────────────
CSS = """
* { box-sizing: border-box; margin: 0; padding: 0; }
body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
       background: #0d1117; color: #e6edf3; line-height: 1.6; }
.layout { display: flex; min-height: 100vh; }
nav { width: 260px; background: #161b22; padding: 20px;
      position: sticky; top: 0; height: 100vh; overflow-y: auto; flex-shrink: 0; }
nav h1 { color: #58a6ff; font-size: 18px; margin-bottom: 20px;
          border-bottom: 1px solid #30363d; padding-bottom: 10px; }
nav ul { list-style: none; }
nav li a { color: #8b949e; text-decoration: none; display: block;
            padding: 4px 8px; border-radius: 4px; font-size: 14px; }
nav li a:hover { color: #e6edf3; background: #21262d; }
nav li a.active { color: #58a6ff; background: #1f2937; }
main { flex: 1; padding: 40px; max-width: 900px; }
.modulo { margin-bottom: 60px; }
.modulo h2 { color: #58a6ff; font-size: 24px; margin-bottom: 8px; }
.modulo-desc { color: #8b949e; margin-bottom: 24px; font-size: 15px; }
.funcion { background: #161b22; border: 1px solid #30363d; border-radius: 8px;
            margin-bottom: 16px; overflow: hidden; }
.funcion-header { padding: 12px 16px; background: #1c2128;
                   cursor: pointer; display: flex; align-items: center; gap: 8px; }
.funcion-nombre { color: #d2a8ff; font-family: monospace; font-size: 15px; font-weight: bold; }
.funcion-privada .funcion-nombre { color: #8b949e; }
.badge { font-size: 11px; padding: 2px 8px; border-radius: 20px;
          background: #1f2937; color: #8b949e; }
.funcion-body { padding: 16px; display: none; }
.funcion-body.visible { display: block; }
.firma { background: #0d1117; padding: 12px; border-radius: 6px;
          font-family: monospace; font-size: 13px; color: #79c0ff;
          margin-bottom: 12px; white-space: pre-wrap; word-break: break-all; }
.desc { color: #c9d1d9; margin-bottom: 12px; }
table { width: 100%; border-collapse: collapse; font-size: 13px; margin-bottom: 12px; }
th { text-align: left; color: #8b949e; padding: 6px 8px;
     border-bottom: 1px solid #30363d; font-weight: normal; }
td { padding: 6px 8px; border-bottom: 1px solid #21262d; vertical-align: top; }
td:first-child { font-family: monospace; color: #79c0ff; white-space: nowrap; }
td:nth-child(2) { color: #a5f3fc; }
.ejemplo { background: #0d1117; padding: 12px; border-radius: 6px;
            font-family: monospace; font-size: 13px; margin-top: 8px;
            border-left: 3px solid #58a6ff; white-space: pre; }
.seccion { color: #8b949e; font-size: 12px; text-transform: uppercase;
            letter-spacing: 1px; margin: 16px 0 8px; }
.privada-section { margin-top: 32px; border-top: 1px solid #30363d; padding-top: 24px; }
.search { width: 100%; padding: 8px 12px; background: #0d1117; border: 1px solid #30363d;
           color: #e6edf3; border-radius: 6px; font-size: 14px; margin-bottom: 16px; }
.search:focus { outline: none; border-color: #58a6ff; }
footer { text-align: center; color: #8b949e; font-size: 12px; padding: 40px;
          border-top: 1px solid #30363d; margin-top: 40px; }
"""

JS = """
document.querySelectorAll('.funcion-header').forEach(h => {
  h.addEventListener('click', () => {
    h.nextElementSibling.classList.toggle('visible');
  });
});
const search = document.getElementById('search');
if (search) {
  search.addEventListener('input', () => {
    const q = search.value.toLowerCase();
    document.querySelectorAll('.funcion').forEach(fn => {
      const nombre = fn.querySelector('.funcion-nombre').textContent.toLowerCase();
      fn.style.display = nombre.includes(q) ? '' : 'none';
    });
  });
}
"""

class GeneradorHTML:
    def generar_indice(self, modulos: List[DocModulo]) -> str:
        mods_html = ""
        for mod in sorted(modulos, key=lambda m: m.nombre):
            publicas = [f for f in mod.funciones if not f.es_privada]
            mods_html += f"""
<div class="modulo-card" onclick="location.href='{mod.nombre}.html'" style="
  background:#161b22;border:1px solid #30363d;border-radius:8px;padding:20px;
  cursor:pointer;margin-bottom:12px;transition:border-color .2s">
  <div style="color:#58a6ff;font-size:16px;font-weight:bold">{mod.nombre}</div>
  <div style="color:#8b949e;font-size:13px;margin-top:4px">{mod.descripcion[:100] if mod.descripcion else ''}</div>
  <div style="color:#30363d;font-size:12px;margin-top:8px">{len(publicas)} funciones públicas</div>
</div>"""

        return f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<title>C-Forge — Documentación</title>
<style>{CSS}
.modulo-card:hover {{ border-color: #58a6ff !important; }}
.grid {{ display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:12px; }}
</style></head><body>
<div class="layout">
<nav><h1>📚 C-Forge Docs</h1>
<input class="search" id="search" placeholder="Buscar módulo...">
<ul>{''.join(f'<li><a href="{m.nombre}.html">{m.nombre}</a></li>' for m in sorted(modulos, key=lambda x: x.nombre))}</ul>
</nav>
<main>
<h1 style="color:#58a6ff;margin-bottom:8px">C-Forge v2.5.0</h1>
<p style="color:#8b949e;margin-bottom:32px">Documentación de la librería estándar — {len(modulos)} módulos</p>
<div class="grid">{mods_html}</div>
<footer>Generado por cfdoc {datetime.now().strftime('%Y-%m-%d %H:%M')} — C-Forge v2.5.0</footer>
</main></div>
<script>
const s=document.getElementById('search');
if(s)s.addEventListener('input',()=>{{
  document.querySelectorAll('.modulo-card').forEach(c=>{{
    c.style.display=c.textContent.toLowerCase().includes(s.value.toLowerCase())?'':'none';
  }});
}});
</script></body></html>"""

    def generar_modulo(self, mod: DocModulo, todos: List[DocModulo]) -> str:
        nav_links = "".join(
            f'<li><a href="{m.nombre}.html"{"class=\"active\"" if m.nombre==mod.nombre else ""}>{m.nombre}</a></li>'
            for m in sorted(todos, key=lambda x: x.nombre)
        )

        publicas = [f for f in mod.funciones if not f.es_privada]
        privadas = [f for f in mod.funciones if f.es_privada]
        vars_pub = [v for v in mod.variables if not v.es_privada]

        fns_html = self._funciones_html(publicas)
        priv_html = ""
        if privadas:
            priv_html = f'<div class="privada-section"><div class="seccion">Internas (_{len(privadas)})</div>{self._funciones_html(privadas)}</div>'

        vars_html = ""
        if vars_pub:
            vars_html = f'<div class="seccion">Constantes y variables</div>'
            for v in vars_pub:
                vars_html += f'<div class="funcion"><div class="funcion-header"><span class="funcion-nombre">{v.nombre}</span><span class="badge">{v.tipo}</span></div></div>'

        return f"""<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<title>{mod.nombre} — C-Forge Docs</title>
<style>{CSS}</style></head><body>
<div class="layout">
<nav><h1><a href="index.html" style="color:#58a6ff;text-decoration:none">📚 C-Forge</a></h1>
<input class="search" id="search" placeholder="Buscar función...">
<ul>{nav_links}</ul></nav>
<main>
<div class="modulo">
<h2>stdlib/{mod.nombre}</h2>
<div class="modulo-desc">{mod.descripcion or f'Módulo {mod.nombre} de C-Forge'}</div>
{vars_html}
<div class="seccion">Funciones públicas ({len(publicas)})</div>
{fns_html}
{priv_html}
</div>
<footer>cfdoc {datetime.now().strftime('%Y-%m-%d')} — C-Forge v2.5.0</footer>
</main></div>
<script>{JS}</script></body></html>"""

    def _funciones_html(self, fns: List[DocFuncion]) -> str:
        html = ""
        for fn in fns:
            params_table = ""
            if fn.params:
                filas = "".join(
                    f"<tr><td>{p.nombre}</td><td>{p.tipo}</td><td style='color:#c9d1d9'>{p.descripcion}</td></tr>"
                    for p in fn.params
                )
                params_table = f"<table><tr><th>Parámetro</th><th>Tipo</th><th>Descripción</th></tr>{filas}</table>"

            ret_html = ""
            if fn.retorna and fn.retorna != "nulo":
                ret_html = f"<div style='color:#8b949e;font-size:13px'>Retorna: <span style='color:#a5f3fc'>{fn.retorna}</span>{' — ' + fn.retorna_desc if fn.retorna_desc else ''}</div>"

            ejemplo_html = f'<div class="ejemplo">{fn.ejemplo}</div>' if fn.ejemplo.strip() else ""

            ver_html = ""
            if fn.ver_tambien:
                links = ", ".join(f'<a href="#{v}" style="color:#58a6ff">{v}</a>' for v in fn.ver_tambien)
                ver_html = f'<div style="color:#8b949e;font-size:13px;margin-top:8px">Ver también: {links}</div>'

            clase_privada = "funcion-privada" if fn.es_privada else ""
            desc_html = f'<div class="desc">{fn.descripcion}</div>' if fn.descripcion else ""

            html += f"""
<div class="funcion {clase_privada}" id="{fn.nombre}">
  <div class="funcion-header">
    <span class="funcion-nombre">{fn.nombre}</span>
    {'<span class="badge">privada</span>' if fn.es_privada else ''}
    {'<span class="badge">' + fn.retorna + '</span>' if fn.retorna != 'nulo' else ''}
  </div>
  <div class="funcion-body">
    <div class="firma">{fn.firma}</div>
    {desc_html}{params_table}{ret_html}{ejemplo_html}{ver_html}
  </div>
</div>"""
        return html


# ── Generador Markdown ─────────────────────────────────────────────────────────
class GeneradorMarkdown:
    def generar_modulo(self, mod: DocModulo) -> str:
        partes = [f"# {mod.nombre}\n\n{mod.descripcion}\n"]
        publicas = [f for f in mod.funciones if not f.es_privada]

        for fn in publicas:
            partes.append(f"## `{fn.nombre}`\n")
            partes.append(f"```cforge\n{fn.firma}\n```\n")
            if fn.descripcion:
                partes.append(f"{fn.descripcion}\n")
            if fn.params:
                partes.append("**Parámetros:**\n")
                for p in fn.params:
                    partes.append(f"- `{p.nombre}` ({p.tipo}): {p.descripcion}")
                partes.append("")
            if fn.retorna != "nulo":
                partes.append(f"**Retorna:** `{fn.retorna}` {fn.retorna_desc}\n")
            if fn.ejemplo.strip():
                partes.append(f"**Ejemplo:**\n```cforge\n{fn.ejemplo}\n```\n")

        return "\n".join(partes)


# ── Servidor de docs ───────────────────────────────────────────────────────────
def servir_docs(directorio: Path, puerto: int):
    os.chdir(directorio)
    handler = http.server.SimpleHTTPRequestHandler
    with http.server.HTTPServer(("", puerto), handler) as httpd:
        print(f"  📚 Docs en http://localhost:{puerto}")
        print(f"  Ctrl+C para detener")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            pass


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="cfdoc — Generador de documentación C-Forge")
    parser.add_argument("archivos", nargs="*", default=["stdlib/"],
                        help="Archivos o directorios .cfv")
    parser.add_argument("--out", "-o", default="docs/api/",
                        help="Directorio de salida (default: docs/api/)")
    parser.add_argument("--format", choices=["html", "md", "json"], default="html")
    parser.add_argument("--serve", type=int, metavar="PUERTO", help="Servir docs en HTTP")
    parser.add_argument("--private", action="store_true", help="Incluir funciones privadas")
    parser.add_argument("--version", action="version", version="cfdoc 2.5.0")
    args = parser.parse_args()

    # Recolectar archivos
    paths = []
    for arg in args.archivos:
        p = Path(arg)
        if p.is_dir():
            paths.extend(sorted(p.glob("*.cfv")))
        elif p.is_file():
            paths.append(p)

    if not paths:
        print("cfdoc: no se encontraron archivos .cfv")
        sys.exit(1)

    print(f"\n  cfdoc — documentando {len(paths)} archivo(s)...")

    parser_doc = DocParser()
    modulos = []
    for p in paths:
        try:
            mod = parser_doc.parsear_archivo(p)
            if mod.funciones or mod.variables:
                modulos.append(mod)
        except Exception as e:
            print(f"  ⚠ Error en {p.name}: {e}")

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    if args.format == "html":
        gen = GeneradorHTML()
        # Índice
        (out_dir / "index.html").write_text(gen.generar_indice(modulos), encoding="utf-8")
        # Un HTML por módulo
        for mod in modulos:
            (out_dir / f"{mod.nombre}.html").write_text(
                gen.generar_modulo(mod, modulos), encoding="utf-8")
        print(f"  ✓ {len(modulos)} módulos → {out_dir}/")
        print(f"  ✓ Abre: {out_dir}/index.html")

    elif args.format == "md":
        gen = GeneradorMarkdown()
        for mod in modulos:
            (out_dir / f"{mod.nombre}.md").write_text(
                gen.generar_modulo(mod), encoding="utf-8")
        print(f"  ✓ {len(modulos)} módulos → {out_dir}/*.md")

    elif args.format == "json":
        data = [
            {
                "nombre": m.nombre,
                "descripcion": m.descripcion,
                "funciones": [
                    {"nombre": f.nombre, "firma": f.firma, "descripcion": f.descripcion,
                     "privada": f.es_privada,
                     "params": [{"nombre": p.nombre, "tipo": p.tipo} for p in f.params],
                     "retorna": f.retorna}
                    for f in m.funciones
                ]
            }
            for m in modulos
        ]
        out_file = out_dir / "api.json"
        out_file.write_text(json.dumps(data, ensure_ascii=False, indent=2))
        print(f"  ✓ → {out_file}")

    if args.serve:
        servir_docs(out_dir, args.serve)


if __name__ == "__main__":
    main()
