"""Backend WebAssembly inicial de C-Forgev: subconjunto numérico -> WAT válido."""

from __future__ import annotations

from pathlib import Path

from cforgev import CForgevError, tokenize
from compilador_nativo import Parser, Program, resolve_imports, safe


class WasmGenerator:
    def __init__(self, program: Program) -> None:
        self.program = program
        self.locals: set[str] = set()

    def generate(self) -> str:
        for statement in self.program.statements:
            if statement[0] == "let":
                self.locals.add(statement[1])
        declarations = "\n".join(
            f"    (local ${safe(name)} f64)" for name in sorted(self.locals)
        )
        body = "\n".join(self.statement(statement) for statement in self.program.statements)
        return (
            ";; C-Forgev WebAssembly 0.9 — módulo WAT completo\n"
            "(module\n"
            '  (import "env" "cfv_print_f64" (func $cfv_print_f64 (param f64)))\n'
            '  (func (export "_start")\n'
            f"{declarations}\n{body}\n"
            "  )\n"
            ")\n"
        )

    def statement(self, statement: tuple) -> str:
        kind = statement[0]
        if kind == "let":
            return self.expr(statement[3]) + f"\n    local.set ${safe(statement[1])}"
        if kind == "assign":
            if statement[1] not in self.locals:
                raise CForgevError(f"Wasm: variable no declarada '{statement[1]}'")
            return self.expr(statement[2]) + f"\n    local.set ${safe(statement[1])}"
        if kind == "print":
            return self.expr(statement[1]) + "\n    call $cfv_print_f64"
        if kind in {"universal_import", "structure", "class"}:
            return "    ;; declaración sin código Wasm"
        raise CForgevError(
            f"Wasm 0.9 todavía no admite la instrucción '{kind}'; usa números, variables y mostrar"
        )

    def expr(self, expression: tuple) -> str:
        kind = expression[0]
        if kind == "number":
            return f"    f64.const {float(expression[1])}"
        if kind == "variable":
            if expression[1] not in self.locals:
                raise CForgevError(f"Wasm: variable desconocida '{expression[1]}'")
            return f"    local.get ${safe(expression[1])}"
        if kind == "unary" and expression[1] == "-":
            return "    f64.const -1\n" + self.expr(expression[2]) + "\n    f64.mul"
        if kind == "binary" and expression[1] in {"+", "-", "*", "/"}:
            opcode = {"+": "add", "-": "sub", "*": "mul", "/": "div"}[expression[1]]
            return self.expr(expression[2]) + "\n" + self.expr(expression[3]) + f"\n    f64.{opcode}"
        raise CForgevError(f"Wasm 0.9 todavía no puede traducir la expresión '{kind}'")


def compile_wasm(source_path: Path, output_path: Path) -> Path:
    if output_path.suffix not in {".wat", ".wast"}:
        raise CForgevError("El backend Wasm 0.9 genera texto WebAssembly; usa extensión .wat")
    try:
        source = source_path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {source_path}: {error}") from error
    program = resolve_imports(Parser(tokenize(source)).program(), source_path.resolve().parent, set())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(WasmGenerator(program).generate(), encoding="utf-8")
    return output_path
