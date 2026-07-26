"""Adaptador Debug Adapter Protocol para la VM de C-Forge."""
from __future__ import annotations

import json
import ast
import re
import sys
import threading
from pathlib import Path
from typing import Any, BinaryIO

from cforgev import CForgevError
from cforge_vm import BytecodeProgram, Instruction, VirtualMachine, compile_file


class DAPSession:
    def __init__(self, reader: BinaryIO, writer: BinaryIO) -> None:
        self.reader, self.writer = reader, writer
        self.sequence = 1
        self.send_lock = threading.Lock()
        self.program: BytecodeProgram | None = None
        self.source: Path | None = None
        self.breakpoints: dict[int, dict[str, Any]] = {}
        self.breakpoint_hits: dict[int, int] = {}
        self.condition = threading.Condition()
        self.paused = False
        self.stepping = False
        self.terminated = False
        self.snapshot: dict[str, Any] = {}
        self.chunk = "<main>"; self.line = 1
        self.frames: list[dict[str, Any]] = []
        self.references: dict[int, Any] = {}
        self.frame_references: dict[int, int] = {}
        self.next_reference = 1
        self.vm: VirtualMachine | None = None
        self.last_location: tuple[int, int] | None = None
        self.resume_location: tuple[int, int] | None = None
        self.step_mode: str | None = None
        self.step_depth = 0
        self.worker: threading.Thread | None = None

    def send(self, message: dict[str, Any]) -> None:
        with self.send_lock:
            message.setdefault("seq", self.sequence); self.sequence += 1
            body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
            self.writer.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
            self.writer.flush()

    def response(self, request: dict[str, Any], body: dict[str, Any] | None = None,
                 success: bool = True, message: str | None = None) -> None:
        result: dict[str, Any] = {"type": "response", "request_seq": request.get("seq", 0),
                                  "command": request.get("command", ""), "success": success}
        if body is not None: result["body"] = body
        if message: result["message"] = message
        self.send(result)

    def event(self, name: str, body: dict[str, Any] | None = None) -> None:
        message: dict[str, Any] = {"type": "event", "event": name}
        if body is not None: message["body"] = body
        self.send(message)

    def _all_lines(self) -> set[int]:
        if self.program is None: return set()
        chunks = [self.program.main, *self.program.functions.values()]
        for definition in self.program.classes.values():
            chunks.extend(definition["methods"].values())
        return {instruction.line for chunk in chunks for instruction in chunk.code
                if instruction.line is not None}

    def trace(self, chunk: str, offset: int, instruction: Instruction,
              scope: dict[str, Any]) -> None:
        line = instruction.line or self.line
        frames = self.vm.debug_frames() if self.vm is not None else [
            {"name": chunk, "line": line, "offset": offset, "scope": scope}
        ]
        location = (len(frames), line)
        changed = location != self.resume_location
        if self.resume_location is not None and changed:
            self.resume_location = None
        breakpoint = self.breakpoints.get(line)
        should_pause = bool(breakpoint and changed)
        if breakpoint and changed:
            self.breakpoint_hits[line] = self.breakpoint_hits.get(line, 0) + 1
            condition = str(breakpoint.get("condition") or "").strip()
            if condition:
                try: should_pause = bool(self._evaluate_safe(condition, scope))
                except Exception as error:
                    should_pause = True
                    self.event("output", {"category": "stderr",
                        "output": f"[C-Forge Debug] Condición inválida en línea {line}: {error}\n"})
            hit_condition = str(breakpoint.get("hitCondition") or "").strip()
            if should_pause and hit_condition:
                try: should_pause = self._hit_matches(hit_condition, self.breakpoint_hits[line])
                except Exception as error:
                    should_pause = True
                    self.event("output", {"category": "stderr",
                        "output": f"[C-Forge Debug] Hit condition inválida en línea {line}: {error}\n"})
            log_message = str(breakpoint.get("logMessage") or "")
            if should_pause and log_message:
                try: rendered = self._format_logpoint(log_message, scope)
                except Exception as error: rendered = f"[C-Forge Debug] Logpoint inválido: {error}"
                self.event("output", {"category": "console", "output": rendered + "\n"})
                should_pause = False
        if self.step_mode == "in" and changed: should_pause = True
        elif self.step_mode == "over" and len(frames) <= self.step_depth and changed: should_pause = True
        elif self.step_mode == "out" and len(frames) < self.step_depth: should_pause = True
        if not should_pause: return
        with self.condition:
            self.chunk, self.line, self.snapshot = chunk, line, dict(scope)
            self.frames = frames; self._rebuild_references()
            self.paused = True; self.stepping = False; self.step_mode = None
            self.last_location = location
            self.event("stopped", {"reason": "breakpoint" if line in self.breakpoints else "step",
                                   "threadId": 1, "allThreadsStopped": True})
            self.condition.wait_for(lambda: not self.paused or self.terminated)

    def start(self) -> None:
        if self.program is None or self.worker is not None: return
        def execute() -> None:
            try:
                vm = VirtualMachine(self.program,
                    lambda text: self.event("output", {"category": "stdout", "output": text + "\n"}),
                    base_dir=(self.source.parent if self.source else Path.cwd()), trace=self.trace)
                self.vm = vm; vm.run()
            except Exception as error:
                self.event("output", {"category": "stderr", "output": f"[C-Forge Debug] {error}\n"})
            finally:
                self.terminated = True; self.event("terminated")
                with self.condition:
                    self.paused = False; self.condition.notify_all()
        self.worker = threading.Thread(target=execute, name="cforge-dap-vm", daemon=True)
        self.worker.start()

    @staticmethod
    def value(value: Any) -> str:
        if value is None: return "nulo"
        if isinstance(value, bool): return "verdadero" if value else "falso"
        return repr(value)

    def _reference(self, value: Any) -> int:
        if not isinstance(value, (dict, list, tuple, set)) and not hasattr(value, "__dict__"):
            return 0
        for reference, existing in self.references.items():
            if existing is value: return reference
        reference = self.next_reference; self.next_reference += 1
        self.references[reference] = value
        return reference

    def _rebuild_references(self) -> None:
        self.references = {}; self.frame_references = {}; self.next_reference = 1
        for frame_id, frame in enumerate(reversed(self.frames), 1):
            self.frame_references[frame_id] = self._reference(frame["scope"])

    def _children(self, value: Any) -> list[tuple[str, Any]]:
        if isinstance(value, dict): return [(str(key), item) for key, item in value.items()]
        if isinstance(value, (list, tuple)): return [(f"[{index}]", item) for index, item in enumerate(value)]
        if isinstance(value, set): return [(f"[{index}]", item) for index, item in enumerate(sorted(value, key=repr))]
        if hasattr(value, "__dict__"): return list(vars(value).items())
        return []

    def _variable(self, name: str, value: Any) -> dict[str, Any]:
        return {"name": name, "value": self.value(value), "type": type(value).__name__,
                "variablesReference": self._reference(value)}

    def _inspect(self, expression: str) -> Any:
        match = re.match(r"^[A-Za-z_]\w*", expression)
        if not match: raise CForgevError("Solo se permiten variables, campos e índices")
        name = match.group(); value = self.snapshot.get(name, self.vm.globals.get(name) if self.vm else None)
        if name not in self.snapshot and (self.vm is None or name not in self.vm.globals):
            raise CForgevError(f"Variable desconocida '{name}'")
        cursor = match.end()
        while cursor < len(expression):
            if expression[cursor] == ".":
                field = re.match(r"[A-Za-z_]\w*", expression[cursor + 1:])
                if not field: raise CForgevError("Campo inválido")
                key = field.group(); cursor += 1 + len(key)
                if not isinstance(value, dict) or key not in value: raise CForgevError(f"Campo desconocido '{key}'")
                value = value[key]
            elif expression[cursor] == "[":
                end = expression.find("]", cursor)
                if end < 0: raise CForgevError("Índice sin cerrar")
                raw = expression[cursor + 1:end].strip()
                try: key = json.loads(raw) if raw.startswith('"') else int(raw)
                except Exception as error: raise CForgevError("Índice inválido") from error
                value = value[key]; cursor = end + 1
            else: raise CForgevError("La evaluación no permite llamadas ni operadores")
        return value

    @classmethod
    def _evaluate_safe(cls, expression: str, scope: dict[str, Any]) -> Any:
        """Evalúa condiciones DAP sin llamadas, asignaciones ni acceso arbitrario."""
        operators = {
            ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b,
            ast.Mult: lambda a, b: a * b, ast.Div: lambda a, b: a / b,
            ast.Mod: lambda a, b: a % b, ast.Eq: lambda a, b: a == b,
            ast.NotEq: lambda a, b: a != b, ast.Lt: lambda a, b: a < b,
            ast.LtE: lambda a, b: a <= b, ast.Gt: lambda a, b: a > b,
            ast.GtE: lambda a, b: a >= b,
        }
        def visit(node: ast.AST) -> Any:
            if isinstance(node, ast.Expression): return visit(node.body)
            if isinstance(node, ast.Constant) and isinstance(node.value, (str, int, float, bool, type(None))):
                return node.value
            if isinstance(node, ast.Name):
                if node.id not in scope: raise CForgevError(f"variable desconocida '{node.id}'")
                return scope[node.id]
            if isinstance(node, ast.BinOp) and type(node.op) in operators:
                return operators[type(node.op)](visit(node.left), visit(node.right))
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.Not, ast.USub, ast.UAdd)):
                value = visit(node.operand)
                return not value if isinstance(node.op, ast.Not) else (-value if isinstance(node.op, ast.USub) else +value)
            if isinstance(node, ast.BoolOp) and isinstance(node.op, (ast.And, ast.Or)):
                values = [bool(visit(value)) for value in node.values]
                return all(values) if isinstance(node.op, ast.And) else any(values)
            if isinstance(node, ast.Compare):
                left = visit(node.left)
                for operator, comparator in zip(node.ops, node.comparators):
                    if type(operator) not in operators: raise CForgevError("operador de comparación no permitido")
                    right = visit(comparator)
                    if not operators[type(operator)](left, right): return False
                    left = right
                return True
            if isinstance(node, ast.Subscript):
                container = visit(node.value); key = visit(node.slice)
                return container[key]
            if isinstance(node, ast.Attribute):
                container = visit(node.value)
                if not isinstance(container, dict) or node.attr not in container:
                    raise CForgevError(f"campo desconocido '{node.attr}'")
                return container[node.attr]
            raise CForgevError("la condición contiene una operación no permitida")
        expression = re.sub(r"\bverdadero\b", "True", expression)
        expression = re.sub(r"\bfalso\b", "False", expression)
        expression = re.sub(r"\bnulo\b", "None", expression)
        expression = re.sub(r"\by\b", "and", expression)
        expression = re.sub(r"\bo\b", "or", expression)
        expression = re.sub(r"\bno\b", "not", expression)
        try: tree = ast.parse(expression, mode="eval")
        except SyntaxError as error: raise CForgevError("sintaxis de condición inválida") from error
        return visit(tree)

    @staticmethod
    def _hit_matches(expression: str, count: int) -> bool:
        if expression.isdigit(): return count == int(expression)
        match = re.fullmatch(r"(>=|>|==|%)(\d+)", expression.replace(" ", ""))
        if not match: raise CForgevError("hitCondition debe ser N, >=N, >N, ==N o %N")
        operator, raw = match.groups(); target = int(raw)
        if operator == ">=": return count >= target
        if operator == ">": return count > target
        if operator == "==": return count == target
        return target > 0 and count % target == 0

    @classmethod
    def _format_logpoint(cls, template: str, scope: dict[str, Any]) -> str:
        def replace(match: re.Match[str]) -> str:
            return cls.value(cls._evaluate_safe(match.group(1), scope))
        return re.sub(r"\{([^{}]+)\}", replace, template)

    def handle(self, request: dict[str, Any]) -> bool:
        command = request.get("command"); arguments = request.get("arguments") or {}
        try:
            if command == "initialize":
                self.response(request, {"supportsConfigurationDoneRequest": True,
                    "supportsTerminateRequest": True, "supportsStepBack": False,
                    "supportsEvaluateForHovers": True,
                    "supportsConditionalBreakpoints": True,
                    "supportsHitConditionalBreakpoints": True,
                    "supportsLogPoints": True})
                self.event("initialized")
            elif command == "launch":
                path = Path(str(arguments.get("program", ""))).resolve()
                if path.suffix != ".cfv": raise CForgevError("DAP requiere un archivo .cfv")
                self.program, self.source = compile_file(path), path
                self.response(request)
            elif command == "setBreakpoints":
                specifications = list(arguments.get("breakpoints", []))
                requested = [int(item.get("line", 0)) for item in specifications]
                available = self._all_lines()
                self.breakpoints = {int(item.get("line", 0)): dict(item) for item in specifications
                                    if int(item.get("line", 0)) in available}
                self.breakpoint_hits = {}
                self.response(request, {"breakpoints": [
                    {"verified": line in available, "line": line,
                     **({} if line in available else {"message": "No hay instrucción ejecutable en esta línea"})}
                    for line in requested]})
            elif command == "configurationDone": self.response(request); self.start()
            elif command == "threads":
                self.response(request, {"threads": [{"id": 1, "name": "C-Forge VM"}]})
            elif command == "stackTrace":
                frames = list(reversed(self.frames)) or [{"name": self.chunk, "line": self.line}]
                self.response(request, {"stackFrames": [{"id": index, "name": frame["name"],
                    "line": frame["line"], "column": 1,
                    "source": {"name": self.source.name if self.source else "programa.cfv",
                               "path": str(self.source) if self.source else ""}}
                    for index, frame in enumerate(frames, 1)], "totalFrames": len(frames)})
            elif command == "scopes":
                self.response(request, {"scopes": [{"name": "Variables C-Forge",
                    "variablesReference": self.frame_references.get(int(arguments.get("frameId", 1)), 0),
                    "expensive": False}]})
            elif command == "variables":
                value = self.references.get(int(arguments.get("variablesReference", 0)))
                self.response(request, {"variables": [self._variable(name, item)
                    for name, item in self._children(value)]})
            elif command == "evaluate":
                value = self._inspect(str(arguments.get("expression", "")))
                self.response(request, {"result": self.value(value),
                    "type": type(value).__name__, "variablesReference": self._reference(value)})
            elif command in {"continue", "next", "stepIn", "stepOut"}:
                with self.condition:
                    self.resume_location = self.last_location
                    self.step_mode = {"next": "over", "stepIn": "in", "stepOut": "out"}.get(command)
                    self.step_depth = len(self.frames)
                    self.stepping = command != "continue"; self.paused = False; self.condition.notify_all()
                self.response(request, {"allThreadsContinued": True})
            elif command in {"disconnect", "terminate"}:
                self.terminated = True
                with self.condition: self.paused = False; self.condition.notify_all()
                self.response(request); return False
            else: self.response(request, success=False, message=f"Comando DAP no soportado: {command}")
        except Exception as error:
            self.response(request, success=False, message=str(error))
        return True


def _read_message(reader: BinaryIO) -> dict[str, Any] | None:
    length = None
    while True:
        line = reader.readline()
        if not line: return None
        if line in {b"\r\n", b"\n"}: break
        key, _, value = line.decode("ascii").partition(":")
        if key.lower() == "content-length": length = int(value.strip())
    if length is None: raise CForgevError("DAP: falta Content-Length")
    payload = reader.read(length)
    if len(payload) != length: raise CForgevError("DAP: mensaje truncado")
    value = json.loads(payload)
    if not isinstance(value, dict): raise CForgevError("DAP: mensaje inválido")
    return value


def run(reader: BinaryIO | None = None, writer: BinaryIO | None = None) -> int:
    session = DAPSession(reader or sys.stdin.buffer, writer or sys.stdout.buffer)
    while True:
        message = _read_message(session.reader)
        if message is None or not session.handle(message): break
    return 0


if __name__ == "__main__": raise SystemExit(run())
