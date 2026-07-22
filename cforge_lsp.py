"""Servidor LSP 3.17 mínimo de C-Forge mediante JSON-RPC por stdio."""

from __future__ import annotations

import json
import sys
from typing import BinaryIO

from cforge_diagnostics import analyze_source


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
                                            "hoverProvider": True}},
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
        elif request_id is not None:
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": None})


if __name__ == "__main__":
    raise SystemExit(run())
