"""Compilador de bytecode y máquina virtual alojada de C-Forge.

El formato es propio de C-Forge; esta primera versión se aloja en Python para
facilitar su auditoría y portabilidad. No ejecuta código extranjero.
"""

from __future__ import annotations

import json
import hashlib
import concurrent.futures
import math
import os
import platform
import socket
import struct
import subprocess
import queue
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from cforgev import CForgevError, ForgeOption, tokenize
from compilador_nativo import Parser, Program, StaticTypeAnalyzer, resolve_imports

BYTECODE_MAGIC = b"CFBC"
BYTECODE_VERSION = (1, 1)
BYTECODE_HEADER = struct.Struct(">4sBBQ32s")
MAX_BYTECODE_PAYLOAD = 256 * 1024 * 1024
MAX_BYTECODE_NESTING = 256
VALID_OPCODES = {
    "CONST", "LOAD", "STORE", "POP", "PRINT", "BUILD_LIST", "BUILD_TUPLE",
    "BUILD_SET", "BUILD_MAP",
    "UNARY", "BINARY", "INDEX", "FIELD", "METHOD", "CALL", "TRY",
    "JUMP", "JUMP_IF_FALSE", "RETURN", "HALT", "SET_FIELD", "AWAIT",
}


@dataclass(frozen=True)
class Instruction:
    op: str
    arg: Any = None
    line: int | None = None


@dataclass
class Chunk:
    name: str
    parameters: list[str] = field(default_factory=list)
    code: list[Instruction] = field(default_factory=list)
    is_async: bool = False
    current_line: int | None = field(default=None, repr=False)

    def emit(self, op: str, arg: Any = None, line: int | None = None) -> int:
        self.code.append(Instruction(op, arg, self.current_line if line is None else line))
        return len(self.code) - 1

    def patch(self, position: int, target: int) -> None:
        old = self.code[position]
        self.code[position] = Instruction(old.op, target, old.line)


@dataclass
class BytecodeProgram:
    main: Chunk
    functions: dict[str, Chunk]
    structures: dict[str, list[list[str]]] = field(default_factory=dict)
    classes: dict[str, dict[str, Any]] = field(default_factory=dict)


@dataclass
class VMTask:
    future: concurrent.futures.Future[Any]


@dataclass
class VMChannel:
    values: queue.Queue[Any] = field(default_factory=queue.Queue)
    closed: bool = False


def _encode_chunk(chunk: Chunk) -> dict[str, Any]:
    instructions = []
    for instruction in chunk.code:
        argument = instruction.arg
        if instruction.op == "TRY":
            protected, error_name, handler = argument
            argument = {"protected": _encode_chunk(protected), "error": error_name,
                        "handler": _encode_chunk(handler)}
        encoded = {"op": instruction.op, "arg": argument}
        if instruction.line is not None: encoded["line"] = instruction.line
        instructions.append(encoded)
    return {"name": chunk.name, "parameters": chunk.parameters,
            "async": chunk.is_async, "code": instructions}


def _decode_chunk(document: dict[str, Any], depth: int = 0) -> Chunk:
    if depth > MAX_BYTECODE_NESTING:
        raise CForgevError("Bytecode: anidamiento excesivo")
    if not isinstance(document, dict) or not isinstance(document.get("name"), str):
        raise CForgevError("Bytecode: chunk inválido")
    parameters = document.get("parameters", [])
    code = document.get("code", [])
    if not isinstance(parameters, list) or not all(isinstance(x, str) for x in parameters):
        raise CForgevError("Bytecode: parámetros inválidos")
    if not isinstance(code, list) or len(code) > 10_000_000:
        raise CForgevError("Bytecode: tabla de instrucciones inválida")
    asynchronous = document.get("async", False)
    if not isinstance(asynchronous, bool): raise CForgevError("Bytecode: marca async inválida")
    chunk = Chunk(document["name"], parameters, is_async=asynchronous)
    for encoded in code:
        if not isinstance(encoded, dict) or encoded.get("op") not in VALID_OPCODES:
            raise CForgevError("Bytecode: opcode desconocido o inválido")
        operation, argument = encoded["op"], encoded.get("arg")
        if operation == "TRY":
            if not isinstance(argument, dict): raise CForgevError("Bytecode: TRY inválido")
            if not isinstance(argument.get("error"), str):
                raise CForgevError("Bytecode: nombre de error TRY inválido")
            argument = (_decode_chunk(argument.get("protected"), depth + 1), argument["error"],
                        _decode_chunk(argument.get("handler"), depth + 1))
        elif operation in {"CALL", "METHOD"} and isinstance(argument, list):
            argument = tuple(argument)
        line = encoded.get("line")
        if line is not None and (not isinstance(line, int) or line < 1):
            raise CForgevError("Bytecode: línea de depuración inválida")
        chunk.emit(operation, argument, line)
    _verify_chunk(chunk)
    return chunk


def _verify_chunk(chunk: Chunk) -> None:
    """Rechaza operandos estructuralmente inválidos antes de ejecutar bytecode."""
    size = len(chunk.code)
    for offset, instruction in enumerate(chunk.code):
        op, argument = instruction.op, instruction.arg
        if op in {"JUMP", "JUMP_IF_FALSE"}:
            if not isinstance(argument, int) or isinstance(argument, bool) or not 0 <= argument <= size:
                raise CForgevError(
                    f"Bytecode: destino de salto inválido en {chunk.name}:{offset}"
                )
        elif op in {"BUILD_LIST", "BUILD_TUPLE", "BUILD_SET", "BUILD_MAP"}:
            if not isinstance(argument, int) or isinstance(argument, bool) or argument < 0:
                raise CForgevError(f"Bytecode: cantidad inválida para {op}")
        elif op in {"LOAD", "STORE", "SET_FIELD", "FIELD", "UNARY", "BINARY"}:
            if not isinstance(argument, str):
                raise CForgevError(f"Bytecode: operando inválido para {op}")
        elif op in {"CALL", "METHOD"}:
            if (not isinstance(argument, tuple) or len(argument) != 2
                    or not isinstance(argument[0], str)
                    or not isinstance(argument[1], int) or isinstance(argument[1], bool)
                    or argument[1] < 0):
                raise CForgevError(f"Bytecode: invocación inválida para {op}")
        elif op == "TRY":
            if (not isinstance(argument, tuple) or len(argument) != 3
                    or not isinstance(argument[0], Chunk)
                    or not isinstance(argument[1], str)
                    or not isinstance(argument[2], Chunk)):
                raise CForgevError("Bytecode: TRY inválido")


def serialize(program: BytecodeProgram) -> bytes:
    document = {
        "format": "C-Forge Bytecode",
        "version": list(BYTECODE_VERSION),
        "main": _encode_chunk(program.main),
        "functions": {name: _encode_chunk(chunk) for name, chunk in sorted(program.functions.items())},
        "structures": program.structures,
        "classes": {
            name: {"fields": definition["fields"],
                   "methods": {method: _encode_chunk(chunk) for method, chunk in sorted(definition["methods"].items())}}
            for name, definition in sorted(program.classes.items())
        },
    }
    payload = json.dumps(document, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":")).encode("utf-8")
    return BYTECODE_HEADER.pack(BYTECODE_MAGIC, *BYTECODE_VERSION, len(payload),
                                hashlib.sha256(payload).digest()) + payload


def deserialize(data: bytes) -> BytecodeProgram:
    if len(data) < BYTECODE_HEADER.size: raise CForgevError("Bytecode: archivo truncado")
    magic, major, minor, length, expected = BYTECODE_HEADER.unpack_from(data)
    if magic != BYTECODE_MAGIC: raise CForgevError("Bytecode: firma CFBC inválida")
    if major != BYTECODE_VERSION[0] or minor > BYTECODE_VERSION[1]:
        raise CForgevError(f"Bytecode: versión {major}.{minor} incompatible")
    if length > MAX_BYTECODE_PAYLOAD:
        raise CForgevError("Bytecode: carga excede el límite de 256 MiB")
    payload = data[BYTECODE_HEADER.size:]
    if length != len(payload): raise CForgevError("Bytecode: longitud inválida")
    if hashlib.sha256(payload).digest() != expected:
        raise CForgevError("Bytecode: suma SHA-256 inválida")
    try: document = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CForgevError("Bytecode: contenido JSON inválido") from error
    if document.get("format") != "C-Forge Bytecode": raise CForgevError("Bytecode: formato desconocido")
    if document.get("version") != [major, minor]:
        raise CForgevError("Bytecode: versión interna no coincide con la cabecera")
    functions = document.get("functions")
    if not isinstance(functions, dict): raise CForgevError("Bytecode: funciones inválidas")
    structures = document.get("structures", {})
    classes_document = document.get("classes", {})
    if not isinstance(structures, dict) or not isinstance(classes_document, dict):
        raise CForgevError("Bytecode: tabla de tipos inválida")
    classes = {}
    for name, definition in classes_document.items():
        if not isinstance(name, str) or not isinstance(definition, dict):
            raise CForgevError("Bytecode: clase inválida")
        methods = definition.get("methods", {})
        fields = definition.get("fields", [])
        if not isinstance(methods, dict) or not isinstance(fields, list):
            raise CForgevError("Bytecode: definición de clase inválida")
        classes[name] = {"fields": fields,
                         "methods": {method: _decode_chunk(chunk) for method, chunk in methods.items()}}
    return BytecodeProgram(_decode_chunk(document["main"]),
                           {name: _decode_chunk(chunk) for name, chunk in functions.items()},
                           structures, classes)


def save_bytecode(program: BytecodeProgram, path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(serialize(program)); return path


def load_bytecode(path: Path) -> BytecodeProgram:
    try: return deserialize(path.read_bytes())
    except OSError as error: raise CForgevError(f"No se pudo abrir {path}: {error}") from error


class BytecodeCompiler:
    """Baja el AST verificado a instrucciones de pila de C-Forge."""

    def compile(self, program: Program) -> BytecodeProgram:
        self.locations = program.locations
        functions: dict[str, Chunk] = {}
        for node in program.functions:
            chunk = Chunk(node[1], list(node[2]), is_async=bool(node[6]) if len(node) > 6 else False)
            self._statements(chunk, node[3])
            chunk.emit("CONST", None)
            chunk.emit("RETURN")
            functions[node[1]] = chunk
        structures: dict[str, list[list[str]]] = {}
        classes: dict[str, dict[str, Any]] = {}
        for statement in program.statements:
            if statement[0] == "structure":
                structures[statement[1]] = [list(field) for field in statement[2]]
            elif statement[0] == "class":
                methods = {}
                for method in statement[3]:
                    method_chunk = Chunk(f"{statement[1]}.{method[2]}", ["este", *method[3]])
                    self._statements(method_chunk, method[4])
                    method_chunk.emit("CONST", None); method_chunk.emit("RETURN")
                    methods[method[2]] = method_chunk
                classes[statement[1]] = {
                    "fields": [list(field) for field in statement[2]], "methods": methods
                }
        main = Chunk("<main>")
        self._statements(main, program.statements)
        main.emit("HALT")
        return BytecodeProgram(main, functions, structures, classes)

    def _statements(self, chunk: Chunk, statements: list[tuple]) -> None:
        for statement in statements:
            chunk.current_line = self.locations.get(id(statement), chunk.current_line)
            kind = statement[0]
            if kind == "let":
                self._expression(chunk, statement[3]); chunk.emit("STORE", statement[1])
            elif kind == "assign":
                self._expression(chunk, statement[2]); chunk.emit("STORE", statement[1])
            elif kind == "field_assign":
                self._expression(chunk, statement[3]); chunk.emit("SET_FIELD", statement[2])
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
            elif kind in {"region", "unsafe"}:
                self._statements(chunk, statement[1])
            elif kind == "try":
                protected = Chunk(f"{chunk.name}:try")
                handler = Chunk(f"{chunk.name}:catch")
                self._statements(protected, statement[1]); protected.emit("CONST", None); protected.emit("RETURN")
                self._statements(handler, statement[3]); handler.emit("CONST", None); handler.emit("RETURN")
                chunk.emit("TRY", (protected, statement[2], handler))
            elif kind in {"structure", "class", "interface", "import", "universal_import"}:
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
        elif kind == "tuple":
            for item in expression[1]: self._expression(chunk, item)
            chunk.emit("BUILD_TUPLE", len(expression[1]))
        elif kind == "set":
            for item in expression[1]: self._expression(chunk, item)
            chunk.emit("BUILD_SET", len(expression[1]))
        elif kind == "map":
            for key, value in expression[1]:
                self._expression(chunk, key); self._expression(chunk, value)
            chunk.emit("BUILD_MAP", len(expression[1]))
        elif kind == "unary":
            self._expression(chunk, expression[2]); chunk.emit("UNARY", expression[1])
        elif kind == "await":
            self._expression(chunk, expression[1]); chunk.emit("AWAIT")
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
                 trace: Callable[[str, int, Instruction, dict[str, Any]], None] | None = None,
                 base_dir: Path | None = None, permissions: set[str] | None = None) -> None:
        self.program, self.output, self.max_steps = program, output, max_steps
        self.globals: dict[str, Any] = {}
        self.steps = 0
        self._step_lock = threading.Lock()
        self._debug_local = threading.local()
        self._executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=max(2, min(32, os.cpu_count() or 2)),
            thread_name_prefix="cforge-vm",
        )
        self.trace = trace
        self.base_dir = (base_dir or Path.cwd()).resolve()
        self.permissions = permissions if permissions is not None else {
            item.strip() for item in os.environ.get(
                "CFORGE_PERMISSIONS", "fs-read,fs-write"
            ).split(",") if item.strip()
        }
        self.builtins: dict[str, Callable[..., Any]] = {
            "longitud": len, "len": len, "raiz": math.sqrt, "potencia": pow,
            "absoluto": abs, "redondear": round, "a_texto": str,
            "a_numero": lambda value: float(value) if "." in str(value) else int(value),
            "agregar": self._append, "afirmar": self._assert,
            "mover": lambda value: value, "prestar": lambda value: value,
            "prestar_mut": lambda value: value, "soltar_prestamo": lambda value: None,
            "destruir": lambda value: None,
            "algunos": lambda value: ForgeOption(True, value),
            "ninguno": lambda: ForgeOption(False),
            "es_algunos": lambda value: isinstance(value, ForgeOption) and value.has_value,
            "desenvolver": self._unwrap,
            "file_read": self._file_read, "leer_archivo": self._file_read,
            "file_write": self._file_write, "escribir_archivo": self._file_write,
            "file_append": self._file_append, "existe_archivo": self._file_exists,
            "sys_run": self._sys_run, "sys_info": self._sys_info,
            "net_send": self._net_send, "net_listen": self._net_listen,
            "tarea": self._task, "esperar": self._await_task,
            "cancelar": self._cancel_task, "canal": self._channel,
            "enviar": self._send_channel, "recibir": self._receive_channel,
            "cerrar_canal": self._close_channel,
        }

    def run(self) -> Any:
        return self._run_chunk(self.program.main, self.globals)

    def debug_frames(self) -> list[dict[str, Any]]:
        """Instantánea inmutable de la pila del hilo que está ejecutando el trace."""
        frames = getattr(self._debug_local, "frames", [])
        return [{"name": frame["name"], "line": frame["line"],
                 "offset": frame["offset"], "scope": dict(frame["scope"])}
                for frame in frames]

    def _run_chunk(self, chunk: Chunk, scope: dict[str, Any]) -> Any:
        stack: list[Any] = []; ip = 0
        frames = getattr(self._debug_local, "frames", None)
        if frames is None:
            frames = []; self._debug_local.frames = frames
        frame = {"name": chunk.name, "line": 1, "offset": 0, "scope": scope}
        frames.append(frame)
        try:
            while ip < len(chunk.code):
                with self._step_lock:
                    self.steps += 1
                    if self.steps > self.max_steps: raise CForgevError("VM: límite de instrucciones excedido")
                instruction = chunk.code[ip]; ip += 1
                frame["line"], frame["offset"] = instruction.line or frame["line"], ip - 1
                if self.trace is not None:
                    self.trace(chunk.name, ip - 1, instruction, dict(scope))
                op, arg = instruction.op, instruction.arg
                result, returned = self._execute_instruction(op, arg, stack, scope, chunk, ip)
                if returned: return result
                if op == "JUMP": ip = arg
                elif op == "JUMP_IF_FALSE" and result is not None: ip = result
            return None
        finally:
            frames.pop()

    def _execute_instruction(self, op: str, arg: Any, stack: list[Any],
                             scope: dict[str, Any], chunk: Chunk, ip: int) -> tuple[Any, bool]:
            if op == "CONST": stack.append(arg)
            elif op == "LOAD":
                if arg in scope: stack.append(scope[arg])
                elif arg in self.globals: stack.append(self.globals[arg])
                else: raise CForgevError(f"VM: variable desconocida '{arg}'")
            elif op == "STORE": scope[arg] = stack.pop()
            elif op == "SET_FIELD":
                owner = scope.get("este")
                if not isinstance(owner, dict) or arg not in owner:
                    raise CForgevError(f"VM: campo desconocido '{arg}'")
                owner[arg] = stack.pop()
            elif op == "POP": stack.pop()
            elif op == "PRINT": self.output(self._display(stack.pop()))
            elif op == "BUILD_LIST":
                values = stack[-arg:] if arg else []; self._drop(stack, arg); stack.append(values)
            elif op == "BUILD_TUPLE":
                values = stack[-arg:] if arg else []; self._drop(stack, arg); stack.append(tuple(values))
            elif op == "BUILD_SET":
                values = stack[-arg:] if arg else []; self._drop(stack, arg)
                try: stack.append(set(values))
                except TypeError as error: raise CForgevError("VM: los elementos del conjunto deben ser inmutables") from error
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
            elif op == "AWAIT": stack.append(self._await_task(stack.pop()))
            elif op == "TRY":
                protected, error_name, handler = arg
                try:
                    self._run_chunk(protected, scope)
                except Exception as error:
                    scope[error_name] = str(error)
                    self._run_chunk(handler, scope)
            elif op == "JUMP": pass
            elif op == "JUMP_IF_FALSE":
                if not stack.pop(): return arg, False
            elif op == "RETURN": return stack.pop(), True
            elif op == "HALT": return (stack[-1] if stack else None), True
            else: raise CForgevError(f"VM: opcode desconocido '{op}'")
            return None, False

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
        definition = self.program.structures.get(name)
        class_definition = self.program.classes.get(name)
        fields = definition if definition is not None else (
            class_definition["fields"] if class_definition is not None else None
        )
        if fields is not None:
            if len(args) != len(fields):
                raise CForgevError(f"VM: '{name}' requiere {len(fields)} campos")
            value = {field[0]: argument for field, argument in zip(fields, args)}
            if class_definition is not None: value["__clase"] = name
            return value
        if name not in self.program.functions: raise CForgevError(f"VM: función desconocida '{name}'")
        chunk = self.program.functions[name]
        if len(args) != len(chunk.parameters): raise CForgevError(f"VM: '{name}' requiere {len(chunk.parameters)} argumentos")
        scope = dict(zip(chunk.parameters, args))
        if chunk.is_async:
            return VMTask(self._executor.submit(self._run_chunk, chunk, scope))
        return self._run_chunk(chunk, scope)

    @staticmethod
    def _binary(op: str, left: Any, right: Any) -> Any:
        if op == "+": return left + right
        if op == "-": return left - right
        if op == "*": return left * right
        if op == "/":
            if right == 0: raise CForgevError("VM: no se puede dividir por cero")
            return left / right
        if op == "%":
            if right == 0: raise CForgevError("VM: no se puede dividir por cero en módulo")
            return int(left) % int(right)
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

    def _method(self, owner: Any, name: str, args: list[Any]) -> Any:
        if name in {"append", "push", "agregar"} and isinstance(owner, list) and len(args) == 1:
            owner.append(args[0]); return owner
        if name in {"length", "len"} and not args: return len(owner)
        if isinstance(owner, dict) and "__clase" in owner:
            definition = self.program.classes.get(owner["__clase"])
            method = definition["methods"].get(name) if definition else None
            if method is None: raise CForgevError(f"VM: método desconocido '{name}'")
            if len(args) + 1 != len(method.parameters):
                raise CForgevError(f"VM: '{name}' recibió una cantidad incorrecta de argumentos")
            return self._run_chunk(method, dict(zip(method.parameters, [owner, *args])))
        raise CForgevError(f"VM: método incompatible '{name}'")

    @staticmethod
    def _append(owner: list[Any], value: Any) -> list[Any]: owner.append(value); return owner

    @staticmethod
    def _assert(condition: Any, message: str = "afirmación fallida") -> bool:
        if not condition: raise CForgevError(message)
        return True

    @staticmethod
    def _unwrap(value: Any) -> Any:
        if not isinstance(value, ForgeOption): raise CForgevError("VM: desenvolver requiere una opcion")
        if not value.has_value: raise CForgevError("VM: no se puede desenvolver ninguno")
        return value.value

    def _permit(self, permission: str) -> None:
        if permission not in self.permissions:
            raise CForgevError(
                f"VM Sandbox: falta permiso '{permission}'. "
                "Usa CFORGE_PERMISSIONS para concederlo explícitamente"
            )

    def _path(self, raw: Any) -> Path:
        if not isinstance(raw, str): raise CForgevError("VM: la ruta debe ser texto")
        path = (self.base_dir / raw).resolve()
        try: path.relative_to(self.base_dir)
        except ValueError as error: raise CForgevError("VM Sandbox: ruta fuera del proyecto") from error
        return path

    def _file_read(self, raw: Any) -> str:
        self._permit("fs-read")
        try: return self._path(raw).read_text(encoding="utf-8")
        except OSError as error: raise CForgevError(f"VM: no se pudo leer archivo: {error}") from error

    def _file_write(self, raw: Any, content: Any) -> None:
        self._permit("fs-write")
        if not isinstance(content, str): raise CForgevError("VM: el contenido debe ser texto")
        path = self._path(raw); path.parent.mkdir(parents=True, exist_ok=True)
        try: path.write_text(content, encoding="utf-8")
        except OSError as error: raise CForgevError(f"VM: no se pudo escribir archivo: {error}") from error

    def _file_append(self, raw: Any, content: Any) -> None:
        self._permit("fs-write")
        if not isinstance(content, str): raise CForgevError("VM: el contenido debe ser texto")
        path = self._path(raw); path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open("a", encoding="utf-8") as stream: stream.write(content)
        except OSError as error: raise CForgevError(f"VM: no se pudo anexar archivo: {error}") from error

    def _file_exists(self, raw: Any) -> bool:
        self._permit("fs-read"); return self._path(raw).exists()

    def _sys_run(self, command: Any) -> dict[str, Any]:
        self._permit("process")
        if not isinstance(command, str): raise CForgevError("VM: sys_run requiere texto")
        try:
            completed = subprocess.run(command, shell=True, cwd=self.base_dir,
                                       capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError) as error:
            raise CForgevError(f"VM: sys_run falló: {error}") from error
        return {"estado": completed.returncode, "salida": completed.stdout,
                "error": completed.stderr}

    @staticmethod
    def _sys_info() -> dict[str, Any]:
        return {"cpu": platform.machine(), "nucleos": os.cpu_count() or 1,
                "sistema": platform.system()}

    def _net_send(self, host: Any, port: Any, content: Any) -> int:
        self._permit("network")
        if not isinstance(host, str) or not isinstance(content, str):
            raise CForgevError("VM: net_send requiere host, puerto y texto")
        data = content.encode("utf-8")
        try:
            with socket.create_connection((host, int(port)), timeout=5) as connection:
                connection.sendall(data)
        except (OSError, ValueError) as error:
            raise CForgevError(f"VM: net_send falló: {error}") from error
        return len(data)

    def _net_listen(self, port: Any, timeout_ms: Any = 5000) -> str:
        self._permit("network")
        try:
            with socket.socket() as server:
                server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                server.bind(("127.0.0.1", int(port))); server.listen(1)
                server.settimeout(float(timeout_ms) / 1000.0)
                connection, _ = server.accept()
                with connection: return connection.recv(16 * 1024 * 1024).decode("utf-8")
        except (OSError, ValueError, UnicodeError) as error:
            raise CForgevError(f"VM: net_listen falló: {error}") from error

    def _task(self, function_name: Any, arguments: Any = None) -> VMTask:
        if not isinstance(function_name, str) or function_name not in self.program.functions:
            raise CForgevError("VM: tarea requiere el nombre de una función C-Forge")
        if arguments is None: arguments = []
        if not isinstance(arguments, list): raise CForgevError("VM: los argumentos de tarea deben ser una lista")
        chunk = self.program.functions[function_name]
        if len(arguments) != len(chunk.parameters):
            raise CForgevError(f"VM: '{function_name}' requiere {len(chunk.parameters)} argumentos")
        return VMTask(self._executor.submit(
            self._run_chunk, chunk, dict(zip(chunk.parameters, list(arguments)))
        ))

    @staticmethod
    def _await_task(task: Any, timeout_ms: Any = None) -> Any:
        if not isinstance(task, VMTask): raise CForgevError("VM: esperar requiere una tarea")
        timeout = None if timeout_ms is None else float(timeout_ms) / 1000.0
        try: return task.future.result(timeout=timeout)
        except concurrent.futures.TimeoutError as error: raise CForgevError("VM: tiempo de espera agotado") from error
        except concurrent.futures.CancelledError as error: raise CForgevError("VM: tarea cancelada") from error

    @staticmethod
    def _cancel_task(task: Any) -> bool:
        if not isinstance(task, VMTask): raise CForgevError("VM: cancelar requiere una tarea")
        return task.future.cancel()

    @staticmethod
    def _channel(capacity: Any = 0) -> VMChannel:
        size = int(capacity)
        if size < 0: raise CForgevError("VM: capacidad de canal inválida")
        return VMChannel(queue.Queue(maxsize=size))

    @staticmethod
    def _send_channel(channel: Any, value: Any, timeout_ms: Any = None) -> None:
        if not isinstance(channel, VMChannel): raise CForgevError("VM: enviar requiere un canal")
        if channel.closed: raise CForgevError("VM: canal cerrado")
        timeout = None if timeout_ms is None else float(timeout_ms) / 1000.0
        try: channel.values.put(value, timeout=timeout)
        except queue.Full as error: raise CForgevError("VM: canal lleno") from error

    @staticmethod
    def _receive_channel(channel: Any, timeout_ms: Any = None) -> Any:
        if not isinstance(channel, VMChannel): raise CForgevError("VM: recibir requiere un canal")
        timeout = None if timeout_ms is None else float(timeout_ms) / 1000.0
        try: return channel.values.get(timeout=timeout)
        except queue.Empty as error:
            if channel.closed: raise CForgevError("VM: canal cerrado y vacío") from error
            raise CForgevError("VM: tiempo de recepción agotado") from error

    @staticmethod
    def _close_channel(channel: Any) -> None:
        if not isinstance(channel, VMChannel): raise CForgevError("VM: cerrar_canal requiere un canal")
        channel.closed = True

    @staticmethod
    def _display(value: Any) -> str:
        if isinstance(value, ForgeOption):
            return f"algunos({VirtualMachine._display(value.value)})" if value.has_value else "ninguno"
        if value is True: return "verdadero"
        if value is False: return "falso"
        if value is None: return "nulo"
        if isinstance(value, tuple):
            suffix = "," if len(value) == 1 else ""
            return "(" + ", ".join(VirtualMachine._display(item) for item in value) + suffix + ")"
        if isinstance(value, set):
            return "conjunto(" + ", ".join(sorted(VirtualMachine._display(item) for item in value)) + ")"
        return str(value)


def compile_source(source: str) -> BytecodeProgram:
    program = Parser(tokenize(source)).program()
    StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
    return BytecodeCompiler().compile(program)


def compile_file(path: Path) -> BytecodeProgram:
    try: source = path.read_text(encoding="utf-8")
    except OSError as error: raise CForgevError(f"No se pudo abrir {path}: {error.strerror or error}") from error
    program = resolve_imports(Parser(tokenize(source)).program(), path.resolve().parent, set())
    StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
    return BytecodeCompiler().compile(program)


def execute_file(path: Path, output: Callable[[str], None] = print) -> VirtualMachine:
    if path.suffix == ".cfb":
        program = load_bytecode(path)
    else:
        program = compile_file(path)
    vm = VirtualMachine(program, output, base_dir=path.resolve().parent)
    vm.run(); return vm


def disassemble(program: BytecodeProgram) -> str:
    chunks = [program.main, *program.functions.values()]; lines: list[str] = []
    for chunk in chunks:
        lines.append(f"== {chunk.name}({', '.join(chunk.parameters)}) ==")
        lines.extend(f"{index:04d} {item.op:<14} {item.arg!r}" for index, item in enumerate(chunk.code))
    return "\n".join(lines)
