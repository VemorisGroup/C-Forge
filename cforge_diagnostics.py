"""Diagnósticos estructurados de C-Forge para CLI, editores y CI."""

from __future__ import annotations

import re
from dataclasses import asdict, dataclass
from pathlib import Path

from cforgev import CForgevError, tokenize
from compilador_nativo import Parser, StaticTypeAnalyzer


@dataclass(frozen=True)
class Diagnostic:
    line: int
    column: int
    severity: str
    code: str
    message: str

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def _from_error(error: Exception, code: str) -> Diagnostic:
    message = str(error)
    found = re.search(r"Línea\s+(\d+)", message)
    line = int(found.group(1)) if found else 1
    return Diagnostic(line, 1, "error", code, message)


def analyze_source(source: str) -> list[Diagnostic]:
    try:
        tokens = tokenize(source)
    except CForgevError as error:
        return [_from_error(error, "CF1001")]
    try:
        program = Parser(tokens).program()
    except CForgevError as error:
        return [_from_error(error, "CF1002")]
    try:
        StaticTypeAnalyzer().analyze(program)
    except CForgevError as error:
        return [_from_error(error, "CF2001")]
    return []


def analyze_file(path: Path) -> list[Diagnostic]:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        return [Diagnostic(1, 1, "error", "CF0001", f"No se pudo abrir {path}: {error}")]
    return analyze_source(source)
