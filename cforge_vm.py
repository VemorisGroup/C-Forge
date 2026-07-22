"""Compilador de bytecode y máquina virtual alojada de C-Forge.

El formato es propio de C-Forge; esta primera versión se aloja en Python para
facilitar su auditoría y portabilidad. No ejecuta código extranjero.
"""

from __future__ import annotations

import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from cforgev import CForgevError, tokenize
from compilador_nativo import Parser, Program, StaticTypeAnalyzer


@dataclass(frozen=True)
class Instruction:
    op: str
    arg: Any = None


@dataclass
class Chunk:
    name: str
    parameters: list[str] = field(default_factory=list)
    code: list[Instruction] = field(default_factory=list)

    def emit(self, op: str, arg: Any = None) -> int:
        self.code.append(Instruction(op, arg))
        return len(self.code) - 1

    def patch(self, position: int, target: int) -> None:
        old = self.code[position]
        self.code[position] = Instruction(old.op, target)


@dataclass
class BytecodeProgram:
    main: Chunk
    functions: dict[str, Chunk]


class BytecodeCompiler:
    """Baja el AST verificado a instrucciones de pila de C-Forge."""

    def compile(self, program: Program) -> BytecodeProgram:
        functions: dict[str, Chunk] = {}
        for node in program.functions:
            chunk = Chunk(node[1], list(node[2]))
            self._statements(chunk, node[3])
            chunk.emit("CONST", None)
            chunk.emit("RETURN")
            functions[node[1]] = chunk
        main = Chunk("<main>")
        self._statements(main, program.statements)
        main.emit("HALT")
        return BytecodeProgram(main, functions)

    def _statements(self, chunk: Chunk, statements: list[tuple]) -> None:
        for statement in statements:
            kind = statement[0]
            if kind == "let":
                self._expression(chunk, statement[3]); chunk.emit("STORE", statement[1])
            elif kind == "assign":
                self._expression(chunk, statement[2]); chunk.emit("STORE", statement[1])
            elif kind == "print":
                self._expression(chunk, statement[1]); chunk.emit("PRINT")
            elif kind == "expression":
                self._expression(chunk, statement[1]); chunk.emit("POP")
            elif kind == "return":
                self._expression(chunk, statement[1]); chunk.emit("RETURN")
            elif kind == "if":
                self._expression(chunk, statement[1])
                false_jump = chunk.emit("JUMP_IF_FALSE", -1)
                self._statements(chunk, statement[2])
                end_jump = chunk.emit("JUMP", -1)
                chunk.patch(false_jump, len(chunk.code))
                self._statements(chunk, statement[3])
                chunk.patch(end_jump, len(chunk.code))
            elif kind == "while":
                start = len(chunk.code)
                self._expression(chunk, statement[1])
                done = chunk.emit("JUMP_IF_FALSE", -1)
                self._statements(chunk, statement[2])
                chunk.emit("JUMP", start)
                chunk.patch(done, len(chunk.code))
            elif kind in {"gpu", "test"}:
                self._statements(chunk, statement[-1])
            elif kind == "try":
                protected = Chunk(f"{chunk.name}:try")
                handler = Chunk(f"{chunk.name}:catch")
                self._statements(protected, statement[1]); protected.emit("CONST", None); protected.emit("RETURN")
                self._statements(handler, statement[3]); handler.emit("CONST", None); handler.emit("RETURN")
                chunk.emit("TRY", (protected, statement[2], handler))
            elif kind in {"structure", "class", "import", "universal_import"}:
                continue
            else:
                raise CForgevError(f"Bytecode 1.0 todavía no admite la sentencia '{kind}'")

    def _expression(self, chunk: Chunk, expression: tuple) -> None:
        kind = expression[0]
        if kind == "number":
            raw = expression[1]; chunk.emit("CONST", float(raw) if "." in raw else int(raw))
        elif kind == "string": chunk.emit("CONST", json.loads(expression[1]))
        elif kind == "bool": chunk.emit("CONST", expression[1])
        elif kind == "null": chunk.emit("CONST", None)
        elif kind == "variable": chunk.emit("LOAD", expression[1])
        elif kind == "list":
            for item in expression[1]: self._expression(chunk, item)
            chunk.emit("BUILD_LIST", len(expression[1]))
        elif kind == "map":
            for key, value in expression[1]:
                self._expression(chunk, key); self._expression(chunk, value)
            chunk.emit("BUILD_MAP", len(expression[1]))
        elif kind == "unary":
            self._expression(chunk, expression[2]); chunk.emit("UNARY", expression[1])
        elif kind == "binary":
            self._expression(chunk, expression[2]); self._expression(chunk, expression[3])
            chunk.emit("BINARY", expression[1])
        elif kind == "index":
            self._expression(chunk, expression[1]); self._expression(chunk, expression[2]); chunk.emit("INDEX")
        elif kind == "field":
            self._expression(chunk, expression[1]); chunk.emit("FIELD", expression[2])
        elif kind == "method_call":
            self._expression(chunk, expression[1])
            for argument in expression[3]: self._expression(chunk, argument)
            chunk.emit("METHOD", (expression[2], len(expression[3])))
        elif kind == "call":
            for argument in expression[2]: self._expression(chunk, argument)
            chunk.emit("CALL", (expression[1], len(expression[2])))
        else:
            raise CForgevError(f"Bytecode 1.0 todavía no admite la expresión '{kind}'")


class VirtualMachine:
    """VM de pila determinista, con límite de instrucciones y ámbitos aislados."""

    def __init__(self, program: BytecodeProgram, output: Callable[[str], None] = print,
                 max_steps: int = 10_000_000,
                 trace: Callable[[str, int, Instruction, dict[str, Any]], None] | None = None) -> None:
        self.program, self.output, self.max_steps = program, output, max_steps
        self.globals: dict[str, Any] = {}
        self.steps = 0
        self.trace = trace
        self.builtins: dict[str, Callable[..., Any]] = {
            "longitud": len, "len": len, "raiz": math.sqrt, "potencia": pow,
            "absoluto": abs, "redondear": round, "a_texto": str,
            "a_numero": lambda value: float(value) if "." in str(value) else int(value),
            "agregar": self._append, "afirmar": self._assert,
        }

    def run(self) -> Any:
        return self._run_chunk(self.program.main, self.globals)

    def _run_chunk(self, chunk: Chunk, scope: dict[str, Any]) -> Any:
        stack: list[Any] = []; ip = 0
        while ip < len(chunk.code):
            self.steps += 1
            if self.steps > self.max_steps: raise CForgevError("VM: límite de instrucciones excedido")
            instruction = chunk.code[ip]; ip += 1
            if self.trace is not None:
                self.trace(chunk.name, ip - 1, instruction, dict(scope))
            op, arg = instruction.op, instruction.arg
            if op == "CONST": stack.append(arg)
            elif op == "LOAD":
                if arg in scope: stack.append(scope[arg])
                elif arg in self.globals: stack.append(self.globals[arg])
                else: raise CForgevError(f"VM: variable desconocida '{arg}'")
            elif op == "STORE": scope[arg] = stack.pop()
            elif op == "POP": stack.pop()
            elif op == "PRINT": self.output(self._display(stack.pop()))
            elif op == "BUILD_LIST":
                values = stack[-arg:] if arg else []; self._drop(stack, arg); stack.append(values)
            elif op == "BUILD_MAP":
                values = stack[-2 * arg:] if arg else []; self._drop(stack, 2 * arg)
                stack.append({values[i]: values[i + 1] for i in range(0, len(values), 2)})
            elif op == "UNARY": stack.append((not stack.pop()) if arg == "no" else -stack.pop())
            elif op == "BINARY":
                right, left = stack.pop(), stack.pop(); stack.append(self._binary(arg, left, right))
            elif op == "INDEX":
                key, owner = stack.pop(), stack.pop(); stack.append(owner[key])
            elif op == "FIELD": stack.append(self._field(stack.pop(), arg))
            elif op == "METHOD":
                name, count = arg; args = stack[-count:] if count else []; self._drop(stack, count)
                owner = stack.pop(); stack.append(self._method(owner, name, args))
            elif op == "CALL":
                name, count = arg; args = stack[-count:] if count else []; self._drop(stack, count)
                stack.append(self._call(name, args))
            elif op == "TRY":
                protected, error_name, handler = arg
                try:
                    self._run_chunk(protected, scope)
                except Exception as error:
                    scope[error_name] = str(error)
                    self._run_chunk(handler, scope)
            elif op == "JUMP": ip = arg
            elif op == "JUMP_IF_FALSE":
                if not stack.pop(): ip = arg
            elif op == "RETURN": return stack.pop()
            elif op == "HALT": return stack[-1] if stack else None
            else: raise CForgevError(f"VM: opcode desconocido '{op}'")
        return None

    @staticmethod
    def _drop(stack: list[Any], count: int, keep: bool = False) -> None:
        if count:
            if keep:
                value = stack[-count:]
                del stack[-count:]
                stack.append(value)
            else: del stack[-count:]

    def _call(self, name: str, args: list[Any]) -> Any:
        if name in self.builtins: return self.builtins[name](*args)
        if name not in self.program.functions: raise CForgevError(f"VM: función desconocida '{name}'")
        chunk = self.program.functions[name]
        if len(args) != len(chunk.parameters): raise CForgevError(f"VM: '{name}' requiere {len(chunk.parameters)} argumentos")
        return self._run_chunk(chunk, dict(zip(chunk.parameters, args)))

    @staticmethod
    def _binary(op: str, left: Any, right: Any) -> Any:
        if op == "+": return left + right
        if op == "-": return left - right
        if op == "*": return left * right
        if op == "/":
            if right == 0: raise CForgevError("VM: no se puede dividir por cero")
            return left / right
        if op == "==": return left == right
        if op == "!=": return left != right
        if op == ">": return left > right
        if op == ">=": return left >= right
        if op == "<": return left < right
        if op == "<=": return left <= right
        if op == "y": return bool(left and right)
        if op == "o": return bool(left or right)
        raise CForgevError(f"VM: operador desconocido '{op}'")

    @staticmethod
    def _field(owner: Any, name: str) -> Any:
        if name in {"length", "len"}: return len(owner)
        if isinstance(owner, dict) and name in owner: return owner[name]
        raise CForgevError(f"VM: miembro desconocido '{name}'")

    @staticmethod
    def _method(owner: Any, name: str, args: list[Any]) -> Any:
        if name in {"append", "push", "agregar"} and isinstance(owner, list) and len(args) == 1:
            owner.append(args[0]); return owner
        if name in {"length", "len"} and not args: return len(owner)
        raise CForgevError(f"VM: método incompatible '{name}'")

    @staticmethod
    def _append(owner: list[Any], value: Any) -> list[Any]: owner.append(value); return owner

    @staticmethod
    def _assert(condition: Any, message: str = "afirmación fallida") -> bool:
        if not condition: raise CForgevError(message)
        return True

    @staticmethod
    def _display(value: Any) -> str:
        if value is True: return "verdadero"
        if value is False: return "falso"
        if value is None: return "nulo"
        return str(value)


def compile_source(source: str) -> BytecodeProgram:
    program = Parser(tokenize(source)).program()
    StaticTypeAnalyzer().analyze(program)
    return BytecodeCompiler().compile(program)


def execute_file(path: Path, output: Callable[[str], None] = print) -> VirtualMachine:
    try: source = path.read_text(encoding="utf-8")
    except OSError as error: raise CForgevError(f"No se pudo abrir {path}: {error.strerror or error}") from error
    vm = VirtualMachine(compile_source(source), output)
    vm.run(); return vm


def disassemble(program: BytecodeProgram) -> str:
    chunks = [program.main, *program.functions.values()]; lines: list[str] = []
    for chunk in chunks:
        lines.append(f"== {chunk.name}({', '.join(chunk.parameters)}) ==")
        lines.extend(f"{index:04d} {item.op:<14} {item.arg!r}" for index, item in enumerate(chunk.code))
    return "\n".join(lines)
