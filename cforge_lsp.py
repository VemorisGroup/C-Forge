"""Servidor LSP 3.17 mínimo de C-Forge mediante JSON-RPC por stdio."""

from __future__ import annotations

import json
import re
import sys
from typing import BinaryIO

from cforge_diagnostics import analyze_source
from cforgev import format_source


KEYWORDS = [
    "sea", "si", "sino", "mientras", "funcion", "retornar", "estructura",
    "clase", "campo", "metodo", "intentar", "capturar", "gpu", "cluster",
    "test", "verdadero", "falso", "nulo", "mostrar", "print", "console.log",
    "System.out.println", "file_read", "file_write", "json_parse", "sys_fetch",
    "forge_hash", "forge_bench", "forge_catalogo", "forge_arena_estado",
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
    pattern = re.compile(rf"^\s*(?:sea\s+|funcion\s+|estructura\s+|clase\s+)?({re.escape(word)})\b")
    for line_number, line in enumerate(source.splitlines()):
        match = pattern.search(line)
        if match:
            return [{"uri": uri, "range": {
                "start": {"line": line_number, "character": match.start(1)},
                "end": {"line": line_number, "character": match.end(1)},
            }}]
    return []


def _symbols(source: str) -> list[dict[str, object]]:
    result = []
    patterns = ((r"^\s*(?:cluster\s+)?funcion\s+([A-Za-z_]\w*)", 12),
                (r"^\s*(?:cluster\s+)?sea\s+([A-Za-z_]\w*)", 13),
                (r"^\s*estructura\s+([A-Za-z_]\w*)", 23),
                (r"^\s*clase\s+([A-Za-z_]\w*)", 5))
    for line_number, line in enumerate(source.splitlines()):
        for pattern, kind in patterns:
            match = re.search(pattern, line)
            if match:
                area = {"start": {"line": line_number, "character": match.start(1)},
                        "end": {"line": line_number, "character": match.end(1)}}
                result.append({"name": match.group(1), "kind": kind, "range": area, "selectionRange": area})
                break
    return result


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
                "result": {"serverInfo": {"name": "C-Forge LSP", "version": "1.5.0"},
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
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": [{"label": word, "kind": 14} for word in KEYWORDS]})
        elif method == "textDocument/hover":
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": {"contents": {"kind": "markdown",
                                                            "value": "**C-Forge 1.5.0** — ForgeValue y sintaxis `.cfv`."}}})
        elif method in {"textDocument/definition", "textDocument/references", "textDocument/rename"}:
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); position = params.get("position", {})
            assert isinstance(document, dict) and isinstance(position, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            word = _word_at(source, position)
            if method.endswith("definition"):
                result = _definitions(uri, source, word)
            elif method.endswith("references"):
                result = _locations(uri, source, word)
            else:
                new_name = str(params.get("newName", ""))
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new_name):
                    _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                           "error": {"code": -32602, "message": "Nombre C-Forge inválido"}})
                    continue
                edits = [{"range": item["range"], "newText": new_name} for item in _locations(uri, source, word)]
                result = {"changes": {uri: edits}}
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
