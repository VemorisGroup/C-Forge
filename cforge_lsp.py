"""Servidor LSP 3.17 mínimo de C-Forge mediante JSON-RPC por stdio."""

from __future__ import annotations

import json
import re
import sys
from typing import BinaryIO

from cforge_diagnostics import analyze_source
from cforgev import format_source


KEYWORDS = [
    "sea", "si", "sino", "mientras", "funcion", "async", "await", "retornar", "estructura", "interfaz", "implementa",
    "clase", "campo", "metodo", "intentar", "capturar", "gpu", "cluster",
    "test", "verdadero", "falso", "nulo", "mostrar", "print", "console.log",
    "System.out.println", "file_read", "file_write", "json_parse", "sys_fetch",
    "forge_hash", "forge_bench", "forge_catalogo", "forge_arena_estado",
    "region", "unsafe", "mover", "prestar", "prestar_mut",
    "soltar_prestamo", "destruir", "opcion", "algunos", "ninguno",
    "es_algunos", "desenvolver",
    "numero", "texto", "booleano", "lista", "mapa", "tupla", "conjunto",
    "tarea", "esperar", "cancelar", "canal", "enviar", "recibir",
    "cerrar_canal",
    "extern_c", "segura",
]


def _read(stream: BinaryIO) -> dict[str, object] | None:
    headers: dict[str, str] = {}
    while True:
        line = stream.readline()
        if not line:
            return None
        if line in {b"\r\n", b"\n"}:
            break
        key, _, value = line.decode("ascii").partition(":")
        headers[key.lower()] = value.strip()
    size = int(headers.get("content-length", "0"))
    if size <= 0:
        return None
    return json.loads(stream.read(size).decode("utf-8"))


def _write(stream: BinaryIO, payload: dict[str, object]) -> None:
    raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    stream.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw)
    stream.flush()


def _publish(output: BinaryIO, uri: str, source: str) -> None:
    diagnostics = []
    for item in analyze_source(source):
        line = max(0, item.line - 1)
        column = max(0, item.column - 1)
        diagnostics.append({
            "range": {
                "start": {"line": line, "character": column},
                "end": {"line": line, "character": column + 1},
            },
            "severity": 1 if item.severity == "error" else 2,
            "code": item.code,
            "source": "C-Forge",
            "message": item.message,
        })
    _write(output, {
        "jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
        "params": {"uri": uri, "diagnostics": diagnostics},
    })


def _position_offset(source: str, position: dict[str, object]) -> int:
    lines = source.splitlines(keepends=True)
    line = max(0, int(position.get("line", 0)))
    character = max(0, int(position.get("character", 0)))
    return sum(len(value) for value in lines[:line]) + min(character, len(lines[line]) if line < len(lines) else 0)


def _word_at(source: str, position: dict[str, object]) -> str:
    offset = _position_offset(source, position)
    for match in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", source):
        if match.start() <= offset <= match.end():
            return match.group(0)
    return ""


def _locations(uri: str, source: str, word: str) -> list[dict[str, object]]:
    if not word: return []
    result = []
    for line_number, line in enumerate(source.splitlines()):
        for match in re.finditer(rf"\b{re.escape(word)}\b", line):
            result.append({"uri": uri, "range": {
                "start": {"line": line_number, "character": match.start()},
                "end": {"line": line_number, "character": match.end()},
            }})
    return result


def _definitions(uri: str, source: str, word: str) -> list[dict[str, object]]:
    for declaration in _declarations(source):
        if declaration["name"] == word:
            return [{"uri": uri, "range": declaration["range"]}]
    return []


def _infer_expression_type(expression: str) -> str:
    value = expression.strip()
    if re.match(r'^"', value): return "texto"
    if re.match(r"^-?\d+(?:\.\d+)?\b", value): return "numero"
    if re.match(r"^(?:verdadero|falso)\b", value): return "booleano"
    if value.startswith("["): return "lista"
    if value.startswith("{"): return "mapa"
    if value.startswith("conjunto("): return "conjunto"
    option = re.match(r"algunos\((.*)\)\s*$", value)
    if option: return f"opcion<{_infer_expression_type(option.group(1))}>"
    if value.startswith("ninguno("): return "opcion<cualquiera>"
    return "cualquiera"


def _declarations(source: str) -> list[dict[str, object]]:
    """Extrae símbolos y firmas sin ejecutar código; tolera archivos incompletos."""
    result: list[dict[str, object]] = []
    type_pattern = r"[A-Za-z_][A-Za-z0-9_]*(?:<[^>\n]+>)?"
    function_pattern = re.compile(
        rf"^\s*(?P<prefix>(?:extern_c\s+(?:segura\s+)?)|(?:cluster\s+)?(?:async\s+)?)"
        rf"funcion\s+(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^)]*)\)"
        rf"(?:\s*:\s*(?P<return>{type_pattern}))?"
    )
    variable_pattern = re.compile(
        rf"^\s*(?:cluster\s+)?sea\s+(?P<name>[A-Za-z_]\w*)"
        rf"(?:\s*:\s*(?P<type>{type_pattern}))?\s*=\s*(?P<value>.*)$"
    )
    nominal_pattern = re.compile(r"^\s*(estructura|clase|interfaz)\s+([A-Za-z_]\w*)")
    for line_number, line in enumerate(source.splitlines()):
        function = function_pattern.search(line)
        if function:
            name = function.group("name"); prefix = function.group("prefix").strip()
            returned = function.group("return") or "cualquiera"
            parameters = function.group("params").strip()
            marker = "extern C ABI segura" if prefix.startswith("extern_c segura") else (
                "extern C ABI" if prefix.startswith("extern_c") else "función"
            )
            detail = f"{marker} {name}({parameters}): {returned}"
            start = function.start("name")
            result.append({"name": name, "kind": 12, "detail": detail, "range": {
                "start": {"line": line_number, "character": start},
                "end": {"line": line_number, "character": function.end("name")},
            }})
            continue
        variable = variable_pattern.search(line)
        if variable:
            name = variable.group("name")
            value_type = variable.group("type") or _infer_expression_type(variable.group("value"))
            start = variable.start("name")
            result.append({"name": name, "kind": 13, "detail": f"{name}: {value_type}", "range": {
                "start": {"line": line_number, "character": start},
                "end": {"line": line_number, "character": variable.end("name")},
            }})
            continue
        nominal = nominal_pattern.search(line)
        if nominal:
            name = nominal.group(2); category = nominal.group(1)
            kind = {"estructura": 23, "clase": 5, "interfaz": 11}[category]
            result.append({"name": name, "kind": kind, "detail": f"{category} {name}", "range": {
                "start": {"line": line_number, "character": nominal.start(2)},
                "end": {"line": line_number, "character": nominal.end(2)},
            }})
    return result


def _symbols(source: str) -> list[dict[str, object]]:
    return [{"name": item["name"], "kind": item["kind"], "detail": item["detail"],
             "range": item["range"], "selectionRange": item["range"]}
            for item in _declarations(source)]


def run(input_stream: BinaryIO | None = None, output_stream: BinaryIO | None = None) -> int:
    input_stream = input_stream or sys.stdin.buffer
    output_stream = output_stream or sys.stdout.buffer
    documents: dict[str, str] = {}
    while True:
        message = _read(input_stream)
        if message is None:
            return 0
        method = message.get("method")
        request_id = message.get("id")
        params = message.get("params", {})
        if method == "initialize":
            _write(output_stream, {
                "jsonrpc": "2.0", "id": request_id,
                "result": {"serverInfo": {"name": "C-Forge LSP", "version": "1.6.0"},
                           "capabilities": {"textDocumentSync": 1,
                                            "completionProvider": {"triggerCharacters": ["."]},
                                            "hoverProvider": True,
                                            "definitionProvider": True,
                                            "referencesProvider": True,
                                            "renameProvider": {"prepareProvider": False},
                                            "documentSymbolProvider": True,
                                            "documentFormattingProvider": True}},
            })
        elif method == "shutdown":
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": None})
        elif method == "exit":
            return 0
        elif method in {"textDocument/didOpen", "textDocument/didChange"}:
            assert isinstance(params, dict)
            document = params.get("textDocument", {})
            assert isinstance(document, dict)
            uri = str(document.get("uri", ""))
            if method.endswith("didOpen"):
                source = str(document.get("text", ""))
            else:
                changes = params.get("contentChanges", [])
                source = str(changes[-1].get("text", "")) if isinstance(changes, list) and changes else ""
            documents[uri] = source
            _publish(output_stream, uri, source)
        elif method == "textDocument/completion":
            symbols = {
                str(item["name"]): int(item["kind"])
                for source in documents.values() for item in _declarations(source)
            }
            completion = [{"label": word, "kind": 14} for word in KEYWORDS]
            completion.extend({"label": name, "kind": kind, "detail": "símbolo C-Forge del proyecto"}
                              for name, kind in sorted(symbols.items()))
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": completion})
        elif method == "textDocument/hover":
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); position = params.get("position", {})
            assert isinstance(document, dict) and isinstance(position, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            word = _word_at(source, position)
            detail = next((str(item["detail"]) for text in documents.values()
                           for item in _declarations(text) if item["name"] == word), None)
            value = f"```cforge\n{detail}\n```" if detail else "**C-Forge 1.6.0** — ForgeValue y sintaxis `.cfv`."
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": {"contents": {"kind": "markdown",
                                                            "value": value}}})
        elif method in {"textDocument/definition", "textDocument/references", "textDocument/rename"}:
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); position = params.get("position", {})
            assert isinstance(document, dict) and isinstance(position, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            word = _word_at(source, position)
            if method.endswith("definition"):
                result = next((found for target_uri, text in documents.items()
                               if (found := _definitions(target_uri, text, word))), [])
            elif method.endswith("references"):
                result = [location for target_uri, text in documents.items()
                          for location in _locations(target_uri, text, word)]
            else:
                new_name = str(params.get("newName", ""))
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new_name):
                    _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                           "error": {"code": -32602, "message": "Nombre C-Forge inválido"}})
                    continue
                changes = {}
                for target_uri, text in documents.items():
                    edits = [{"range": item["range"], "newText": new_name}
                             for item in _locations(target_uri, text, word)]
                    if edits: changes[target_uri] = edits
                result = {"changes": changes}
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": result})
        elif method == "textDocument/documentSymbol":
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); assert isinstance(document, dict)
            uri = str(document.get("uri", ""))
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": _symbols(documents.get(uri, ""))})
        elif method == "textDocument/formatting":
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); assert isinstance(document, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            end_line = len(source.splitlines()) + 1
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": [{
                "range": {"start": {"line": 0, "character": 0},
                          "end": {"line": end_line, "character": 0}},
                "newText": format_source(source),
            }]})
        elif request_id is not None:
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": None})


if __name__ == "__main__":
    raise SystemExit(run())
