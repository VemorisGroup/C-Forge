#!/usr/bin/env python3
"""
C-Forge Language Server Protocol (LSP) — JSON-RPC 2.0
Compatible con VSCode, Neovim, Helix, Zed y cualquier editor con soporte LSP.

Uso:
  ./cforgev --lsp              (modo stdio, para editores)
  python3 tools/lsp_server.py  (directo)

Instalar en VSCode: configurar cforge.interpreterPath y habilitar LSP en extension.
"""

import sys
import json
import re
import os
import threading
from typing import Any, Optional

# ── Constantes de completado ──────────────────────────────────────────────────
KEYWORDS = [
    "funcion", "sea", "constante", "si", "sino", "mientras", "para", "en",
    "retornar", "romper", "continuar", "segun", "caso", "defecto",
    "intentar", "capturar", "lanzar", "finalmente", "importar", "exportar",
    "clase", "extiende", "implementa", "interfaz", "this", "super", "nuevo",
    "enum", "tipo", "async", "esperar", "verdadero", "falso", "nulo",
    "y", "o", "no", "cualquiera"
]

TYPES = [
    "numero", "texto", "booleano", "lista", "mapa", "nulo", "tupla",
    "conjunto", "funcion", "cualquiera", "matriz", "canal"
]

BUILTINS = {
    "mostrar": "(valor: cualquiera): nulo",
    "leer": "(): texto",
    "longitud": "(coleccion): numero",
    "rango": "(n: numero): lista",
    "rango_paso": "(desde, hasta, paso): lista",
    "tipo_de": "(valor): texto",
    "tiempo_ms": "(): numero",
    "dormir": "(ms: numero): nulo",
    "agregar": "(lista, elemento): nulo",
    "eliminar_indice": "(lista, indice): nulo",
    "filtrar": "(lista, fn): lista",
    "mapear": "(lista, fn): lista",
    "reducir": "(lista, inicial, fn): cualquiera",
    "ordenar": "(lista): lista",
    "invertir": "(lista): lista",
    "tiene_clave": "(mapa, clave): booleano",
    "mapa_claves": "(mapa): lista",
    "mapa_valores": "(mapa): lista",
    "mapa_entradas": "(mapa): lista",
    "mapa_fusionar": "(a, b): mapa",
    "json_parsear": "(texto): cualquiera",
    "json_texto": "(valor): texto",
    "json_bonito": "(valor): texto",
    "texto_dividir": "(texto, sep): lista",
    "texto_unir": "(lista, sep): texto",
    "texto_contiene": "(texto, sub): booleano",
    "texto_empieza_con": "(texto, pre): booleano",
    "texto_termina_con": "(texto, suf): booleano",
    "texto_reemplazar": "(texto, de, a): texto",
    "texto_mayusculas": "(texto): texto",
    "texto_minusculas": "(texto): texto",
    "texto_recortar": "(texto): texto",
    "texto_es_numero": "(texto): booleano",
    "subcadena": "(texto, desde, hasta): texto",
    "texto_indice": "(texto, sub): numero",
    "regex_buscar": "(texto, patron): texto|nulo",
    "regex_buscar_todos": "(texto, patron): lista",
    "regex_reemplazar": "(texto, patron, reem): texto",
    "regex_coincide": "(texto, patron): booleano",
    "leer_archivo": "(ruta): texto",
    "escribir_archivo": "(ruta, contenido): nulo",
    "existe_archivo": "(ruta): booleano",
    "ruta_unir": "(lista): texto",
    "ruta_nombre": "(ruta): texto",
    "ruta_extension": "(ruta): texto",
    "directorio_listar": "(dir): lista",
    "fecha_ahora": "(): mapa",
    "http_get": "(url, headers?): mapa",
    "http_post": "(url, body, headers?): mapa",
    "sha256": "(texto): texto",
    "base64_encode": "(texto): texto",
    "base64_decode": "(texto): texto",
    "web_escuchar": "(puerto): cualquiera",
    "web_solicitud": "(srv): mapa",
    "web_responder": "(srv, codigo, cuerpo, tipo): nulo",
    "pg_conectar": "(conn_string): numero",
    "pg_query": "(id, sql): mapa",
    "pg_exec": "(id, sql): mapa",
    "mysql_conectar": "(config: mapa): numero",
    "mysql_query": "(id, sql): mapa",
    "ws_escuchar": "(puerto): numero",
    "ws_aceptar": "(srv): numero",
    "ws_recibir": "(cli): texto",
    "ws_enviar": "(cli, msg): booleano",
    "ws_broadcast": "(srv, msg): numero",
    "lista_slice": "(lista, desde, hasta): lista",
    "lista_buscar": "(lista, valor): numero",
    "lista_contiene": "(lista, valor): booleano",
    "lista_max": "(lista): numero",
    "lista_min": "(lista): numero",
    "lista_suma": "(lista): numero",
    "lista_promedio": "(lista): numero",
    "numero_formato": "(n, decimales): texto",
    "abs": "(n): numero",
    "max": "(a, b): numero",
    "min": "(a, b): numero",
    "piso": "(n): numero",
    "techo": "(n): numero",
    "raiz": "(n): numero",
    "potencia": "(base, exp): numero",
    "log_info": "(msg): nulo",
    "log_error": "(msg): nulo",
    "log_advertencia": "(msg): nulo",
}

STDLIB_MODULES = [
    "aleatorio", "archivo", "base64", "cache", "cli", "colecciones",
    "concurrencia", "config", "crypto", "csv", "db", "email", "errores",
    "esquema", "eventos", "fecha", "framework", "gl", "http_cliente",
    "io", "json", "lista", "log", "mapa", "matematica", "matematica_avanzada",
    "orm", "pkgmgr", "plantilla", "pruebas", "regex", "router", "sdl",
    "texto", "tipos", "validar", "web", "yaml", "benchmark"
]

# ── Transporte JSON-RPC ────────────────────────────────────────────────────────
def send_message(msg: dict):
    body = json.dumps(msg, ensure_ascii=False)
    header = f"Content-Length: {len(body.encode())}\r\n\r\n"
    sys.stdout.buffer.write(header.encode() + body.encode())
    sys.stdout.buffer.flush()

def read_message() -> Optional[dict]:
    headers = {}
    while True:
        line = sys.stdin.buffer.readline().decode("utf-8", errors="replace")
        if not line or line == "\r\n":
            break
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip()] = v.strip()
    length = int(headers.get("Content-Length", 0))
    if length == 0:
        return None
    body = sys.stdin.buffer.read(length).decode("utf-8", errors="replace")
    try:
        return json.loads(body)
    except:
        return None

# ── Análisis de código ─────────────────────────────────────────────────────────
def extract_symbols(code: str) -> dict:
    """Extrae funciones, variables y clases del código."""
    symbols = {"funciones": [], "variables": [], "clases": []}
    for line_no, line in enumerate(code.split("\n")):
        m = re.match(r'\s*(?:async\s+)?funcion\s+(\w+)\s*\(([^)]*)\)', line)
        if m:
            symbols["funciones"].append({
                "nombre": m.group(1),
                "params": m.group(2),
                "linea": line_no
            })
        m = re.match(r'\s*(?:sea|constante)\s+(\w+)', line)
        if m:
            symbols["variables"].append({"nombre": m.group(1), "linea": line_no})
        m = re.match(r'\s*clase\s+(\w+)', line)
        if m:
            symbols["clases"].append({"nombre": m.group(1), "linea": line_no})
    return symbols

def check_diagnostics(code: str, uri: str) -> list:
    """Genera diagnósticos básicos (errores y advertencias)."""
    diags = []
    lines = code.split("\n")
    # Balanceo de llaves
    depth = 0
    for i, line in enumerate(lines):
        clean = re.sub(r'"[^"]*"', '""', line)
        clean = re.sub(r'//.*$', '', clean)
        depth += clean.count("{") - clean.count("}")
        if depth < 0:
            diags.append({
                "range": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": len(line)}},
                "severity": 1,
                "source": "cforge",
                "message": "Llave de cierre '}' sin apertura correspondiente"
            })
            depth = 0
    # Detectar 'var' (debería ser 'sea')
    for i, line in enumerate(lines):
        if re.match(r'\s*var\s+', line):
            col = len(line) - len(line.lstrip())
            diags.append({
                "range": {"start": {"line": i, "character": col}, "end": {"line": i, "character": col+3}},
                "severity": 2,
                "source": "cforge",
                "message": "Usar 'sea' en lugar de 'var'"
            })
    # Detectar console.log (no es C-Forge)
    for i, line in enumerate(lines):
        if 'console.log' in line:
            col = line.index('console.log')
            diags.append({
                "range": {"start": {"line": i, "character": col}, "end": {"line": i, "character": col+11}},
                "severity": 2,
                "source": "cforge",
                "message": "Usar 'mostrar()' en lugar de 'console.log()'"
            })
    return diags

# ── Servidor LSP ────────────────────────────────────────────────────────────────
class CForgeLSP:
    def __init__(self):
        self.docs: dict[str, str] = {}
        self.initialized = False

    def handle(self, msg: dict) -> Optional[dict]:
        method = msg.get("method", "")
        mid = msg.get("id")
        params = msg.get("params", {})

        if method == "initialize":
            self.initialized = True
            return {
                "jsonrpc": "2.0", "id": mid,
                "result": {
                    "capabilities": {
                        "textDocumentSync": 1,
                        "completionProvider": {
                            "triggerCharacters": [".", "(", "\"", "/"],
                            "resolveProvider": False
                        },
                        "hoverProvider": True,
                        "definitionProvider": True,
                        "documentSymbolProvider": True,
                        "workspaceSymbolProvider": True,
                        "referencesProvider": True,
                        "documentFormattingProvider": True,
                        "diagnosticProvider": {"interFileDependencies": False, "workspaceDiagnostics": False}
                    },
                    "serverInfo": {"name": "cforge-lsp", "version": "2.4.0"}
                }
            }

        if method == "initialized":
            return None

        if method == "textDocument/didOpen":
            uri = params["textDocument"]["uri"]
            text = params["textDocument"]["text"]
            self.docs[uri] = text
            self._publish_diagnostics(uri, text)
            return None

        if method == "textDocument/didChange":
            uri = params["textDocument"]["uri"]
            changes = params.get("contentChanges", [])
            if changes:
                self.docs[uri] = changes[-1]["text"]
                self._publish_diagnostics(uri, self.docs[uri])
            return None

        if method == "textDocument/didClose":
            uri = params["textDocument"]["uri"]
            self.docs.pop(uri, None)
            return None

        if method == "textDocument/completion":
            return {"jsonrpc": "2.0", "id": mid, "result": self._completion(params)}

        if method == "textDocument/hover":
            return {"jsonrpc": "2.0", "id": mid, "result": self._hover(params)}

        if method == "textDocument/documentSymbol":
            return {"jsonrpc": "2.0", "id": mid, "result": self._doc_symbols(params)}

        if method == "textDocument/formatting":
            return {"jsonrpc": "2.0", "id": mid, "result": self._format(params)}

        if method == "textDocument/definition":
            return {"jsonrpc": "2.0", "id": mid, "result": self._definition(params)}

        if method == "shutdown":
            return {"jsonrpc": "2.0", "id": mid, "result": None}

        if method == "exit":
            sys.exit(0)

        if mid is not None:
            return {"jsonrpc": "2.0", "id": mid, "result": None}
        return None

    def _publish_diagnostics(self, uri: str, text: str):
        diags = check_diagnostics(text, uri)
        send_message({
            "jsonrpc": "2.0",
            "method": "textDocument/publishDiagnostics",
            "params": {"uri": uri, "diagnostics": diags}
        })

    def _completion(self, params: dict) -> dict:
        uri = params.get("textDocument", {}).get("uri", "")
        pos = params.get("position", {})
        line_no = pos.get("line", 0)
        char = pos.get("character", 0)
        text = self.docs.get(uri, "")
        lines = text.split("\n")
        current_line = lines[line_no] if line_no < len(lines) else ""
        prefix = current_line[:char]

        items = []

        # Completado de importar "stdlib/..."
        if re.search(r'importar\s+"stdlib/', prefix):
            for mod in STDLIB_MODULES:
                items.append({
                    "label": mod + ".cfv",
                    "kind": 9,  # Module
                    "insertText": mod + '.cfv"',
                    "detail": f"Módulo stdlib C-Forge"
                })
            return {"isIncomplete": False, "items": items}

        # Completado de builtins
        word_match = re.search(r'[\w_]+$', prefix)
        word = word_match.group() if word_match else ""

        for kw in KEYWORDS:
            if kw.startswith(word):
                items.append({"label": kw, "kind": 14, "detail": "Palabra clave"})

        for t in TYPES:
            if t.startswith(word):
                items.append({"label": t, "kind": 25, "detail": "Tipo C-Forge"})

        for name, sig in BUILTINS.items():
            if name.startswith(word):
                items.append({
                    "label": name,
                    "kind": 3,  # Function
                    "detail": name + sig,
                    "insertText": name + "(",
                    "documentation": f"Builtin C-Forge: `{name}{sig}`"
                })

        # Símbolos del documento actual
        if text:
            syms = extract_symbols(text)
            for fn in syms["funciones"]:
                if fn["nombre"].startswith(word):
                    items.append({
                        "label": fn["nombre"],
                        "kind": 3,
                        "detail": f"funcion {fn['nombre']}({fn['params']})",
                        "insertText": fn["nombre"] + "("
                    })
            for v in syms["variables"]:
                if v["nombre"].startswith(word):
                    items.append({"label": v["nombre"], "kind": 6, "detail": "Variable local"})
            for c in syms["clases"]:
                if c["nombre"].startswith(word):
                    items.append({"label": c["nombre"], "kind": 7, "detail": "Clase"})

        return {"isIncomplete": False, "items": items[:100]}

    def _hover(self, params: dict) -> Optional[dict]:
        uri = params.get("textDocument", {}).get("uri", "")
        pos = params.get("position", {})
        line_no = pos.get("line", 0)
        char = pos.get("character", 0)
        text = self.docs.get(uri, "")
        lines = text.split("\n")
        if line_no >= len(lines):
            return None
        line = lines[line_no]
        # Encontrar palabra bajo cursor
        start = char
        while start > 0 and re.match(r'\w', line[start-1]):
            start -= 1
        end = char
        while end < len(line) and re.match(r'\w', line[end]):
            end += 1
        word = line[start:end]
        if word in BUILTINS:
            return {
                "contents": {
                    "kind": "markdown",
                    "value": f"```cforge\n{word}{BUILTINS[word]}\n```\n\nBuiltin de C-Forge"
                }
            }
        if word in KEYWORDS:
            return {"contents": {"kind": "markdown", "value": f"**{word}** — Palabra clave de C-Forge"}}
        if word in TYPES:
            return {"contents": {"kind": "markdown", "value": f"**{word}** — Tipo de C-Forge"}}
        return None

    def _doc_symbols(self, params: dict) -> list:
        uri = params.get("textDocument", {}).get("uri", "")
        text = self.docs.get(uri, "")
        if not text:
            return []
        syms = extract_symbols(text)
        result = []
        for fn in syms["funciones"]:
            result.append({
                "name": fn["nombre"],
                "kind": 12,  # Function
                "location": {"uri": uri, "range": {
                    "start": {"line": fn["linea"], "character": 0},
                    "end": {"line": fn["linea"], "character": 100}
                }}
            })
        for c in syms["clases"]:
            result.append({
                "name": c["nombre"],
                "kind": 5,  # Class
                "location": {"uri": uri, "range": {
                    "start": {"line": c["linea"], "character": 0},
                    "end": {"line": c["linea"], "character": 100}
                }}
            })
        return result

    def _format(self, params: dict) -> list:
        uri = params.get("textDocument", {}).get("uri", "")
        text = self.docs.get(uri, "")
        if not text:
            return []
        formatted = format_cforge(text)
        lines = text.split("\n")
        return [{
            "range": {
                "start": {"line": 0, "character": 0},
                "end": {"line": len(lines), "character": 0}
            },
            "newText": formatted
        }]

    def _definition(self, params: dict) -> Optional[dict]:
        uri = params.get("textDocument", {}).get("uri", "")
        pos = params.get("position", {})
        text = self.docs.get(uri, "")
        lines = text.split("\n") if text else []
        line_no = pos.get("line", 0)
        char = pos.get("character", 0)
        if line_no >= len(lines):
            return None
        line = lines[line_no]
        start = char
        while start > 0 and re.match(r'\w', line[start-1]):
            start -= 1
        end = char
        while end < len(line) and re.match(r'\w', line[end]):
            end += 1
        word = line[start:end]
        # Buscar definición en el documento
        for i, l in enumerate(lines):
            if re.match(rf'\s*(?:async\s+)?funcion\s+{re.escape(word)}\s*\(', l):
                return {"uri": uri, "range": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": len(l)}}}
            if re.match(rf'\s*(?:sea|constante)\s+{re.escape(word)}\s*=', l):
                return {"uri": uri, "range": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": len(l)}}}
            if re.match(rf'\s*clase\s+{re.escape(word)}\s*', l):
                return {"uri": uri, "range": {"start": {"line": i, "character": 0}, "end": {"line": i, "character": len(l)}}}
        return None

# ── Formateador ────────────────────────────────────────────────────────────────
def format_cforge(code: str) -> str:
    """Formateador canónico de C-Forge."""
    lines = code.split("\n")
    result = []
    indent = 0
    TAB = "    "
    prev_blank = False

    for line in lines:
        stripped = line.strip()

        # Reducir indent por líneas que cierran bloque
        if stripped.startswith("}"):
            indent = max(0, indent - 1)

        if stripped == "":
            if not prev_blank:
                result.append("")
            prev_blank = True
            continue

        prev_blank = False

        # Conservar comentarios al nivel correcto
        if stripped.startswith("//"):
            result.append(TAB * indent + stripped)
        else:
            result.append(TAB * indent + stripped)

        # Aumentar indent después de abrir bloque
        if stripped.endswith("{") and not stripped.startswith("//"):
            indent += 1

    # Eliminar blancos al final
    while result and result[-1] == "":
        result.pop()

    return "\n".join(result) + "\n"

# ── Main ────────────────────────────────────────────────────────────────────────
def main():
    lsp = CForgeLSP()
    while True:
        try:
            msg = read_message()
            if msg is None:
                break
            response = lsp.handle(msg)
            if response is not None:
                send_message(response)
        except EOFError:
            break
        except Exception as e:
            sys.stderr.write(f"[cforge-lsp error] {e}\n")
            sys.stderr.flush()

if __name__ == "__main__":
    main()
