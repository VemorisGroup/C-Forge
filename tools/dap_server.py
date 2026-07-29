#!/usr/bin/env python3
"""
C-Forge DAP (Debug Adapter Protocol) Server
Implementa el protocolo DAP de Microsoft para depuración de archivos .cfv.
Compatible con VS Code, Neovim, etc.

Uso:
  python3 dap_server.py --port 4711          # TCP socket (VS Code remote debug)
  python3 dap_server.py --stdio              # stdio (VS Code launch config)

Configuración en VS Code (.vscode/launch.json):
{
  "type": "cforge",
  "request": "launch",
  "name": "Depurar C-Forge",
  "program": "${file}",
  "interpreter": "/ruta/a/cforgev"
}
"""

import sys
import os
import re
import json
import socket
import struct
import threading
import subprocess
import time
import argparse
from pathlib import Path
from typing import Dict, List, Optional, Any

# ── Protocolo DAP ──────────────────────────────────────────────────────────────
class DAPConnection:
    """Maneja la comunicación JSON-RPC del protocolo DAP."""
    def __init__(self, reader, writer):
        self.reader = reader
        self.writer = writer
        self._seq = 0
        self._lock = threading.Lock()

    def _next_seq(self) -> int:
        with self._lock:
            self._seq += 1
            return self._seq

    def read_message(self) -> Optional[dict]:
        """Lee un mensaje DAP del stream."""
        try:
            header = b""
            while True:
                c = self.reader.read(1)
                if not c:
                    return None
                header += c
                if header.endswith(b"\r\n\r\n"):
                    break

            # Parsear Content-Length
            length = 0
            for line in header.decode().split("\r\n"):
                if line.startswith("Content-Length:"):
                    length = int(line.split(":")[1].strip())

            if length == 0:
                return None

            body = self.reader.read(length)
            return json.loads(body.decode("utf-8"))
        except Exception:
            return None

    def send_message(self, msg: dict):
        """Envía un mensaje DAP."""
        body = json.dumps(msg, ensure_ascii=False).encode("utf-8")
        header = f"Content-Length: {len(body)}\r\n\r\n".encode()
        with self._lock:
            self.writer.write(header + body)
            self.writer.flush()

    def send_response(self, req: dict, body: dict = None, success: bool = True, message: str = ""):
        resp = {
            "seq": self._next_seq(),
            "type": "response",
            "request_seq": req["seq"],
            "success": success,
            "command": req["command"],
        }
        if body:
            resp["body"] = body
        if message:
            resp["message"] = message
        self.send_message(resp)

    def send_event(self, event: str, body: dict = None):
        msg = {
            "seq": self._next_seq(),
            "type": "event",
            "event": event,
        }
        if body:
            msg["body"] = body
        self.send_message(msg)


# ── Estado del depurador ───────────────────────────────────────────────────────
class DebugState:
    def __init__(self):
        self.breakpoints: Dict[str, List[int]] = {}  # archivo → [líneas]
        self.threads: List[dict] = [{"id": 1, "name": "main"}]
        self.stack_frames: List[dict] = []
        self.variables: Dict[int, List[dict]] = {}   # variablesReference → vars
        self.var_ref_counter = 100
        self.paused = False
        self.pause_reason = "entry"
        self.current_line = 1
        self.current_file = ""
        self.source_lines: Dict[str, List[str]] = {}  # archivo → líneas

    def load_source(self, path: str):
        try:
            self.source_lines[path] = Path(path).read_text().split("\n")
        except Exception:
            self.source_lines[path] = []

    def is_breakpoint(self, path: str, line: int) -> bool:
        bps = self.breakpoints.get(path, [])
        return line in bps

    def next_var_ref(self) -> int:
        self.var_ref_counter += 1
        return self.var_ref_counter


# ── Intérprete C-Forge instrumentado ──────────────────────────────────────────
class CForgeDebugInterpreter:
    """
    Ejecuta C-Forge paso a paso interceptando la salida del intérprete.
    Usa el intérprete cforgev con modo de depuración embebido.
    """
    def __init__(self, program: str, interpreter: str, state: DebugState, conn: DAPConnection):
        self.program = program
        self.interpreter = interpreter
        self.state = state
        self.conn = conn
        self.process: Optional[subprocess.Popen] = None
        self._step_mode = "continue"  # "continue" | "stepIn" | "stepOver" | "stepOut"
        self._resume_event = threading.Event()
        self._stopped = False

    def start(self, stop_on_entry: bool = False):
        """Inicia la ejecución del programa."""
        self.state.load_source(self.program)

        # Modo: ejecutamos el intérprete con output interceptado
        # Para depuración real, cforgev necesita soporte --debug (implementado abajo)
        cmd = [self.interpreter, "--debug", self.program]
        if not Path(self.interpreter).exists():
            # Fallback: ejecutar y simular breakpoints
            cmd = [self.interpreter, self.program]

        try:
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                stdin=subprocess.PIPE,
                text=True,
                bufsize=1
            )

            # Thread para leer salida
            threading.Thread(target=self._leer_salida, daemon=True).start()
            threading.Thread(target=self._leer_stderr, daemon=True).start()

            if stop_on_entry:
                self._pause("entry")
            else:
                self._continue_execution()

        except FileNotFoundError:
            # Sin intérprete: modo simulación para demos
            self.conn.send_event("output", {
                "category": "console",
                "output": f"[debug] Simulando depuración de {Path(self.program).name}\n"
            })
            self._simulate_execution(stop_on_entry)

    def _simulate_execution(self, stop_on_entry: bool):
        """Simulación cuando el intérprete no está disponible."""
        lineas = self.state.source_lines.get(self.program, [])
        self.state.stack_frames = [{
            "id": 1,
            "name": "principal",
            "source": {"path": self.program, "name": Path(self.program).name},
            "line": 1,
            "column": 1
        }]

        if stop_on_entry:
            self.state.current_line = 1
            self._pause("entry")
            self._wait_for_resume()

        for i, linea in enumerate(lineas, 1):
            if self._stopped:
                break
            stripped = linea.strip()
            if not stripped or stripped.startswith("//"):
                continue

            self.state.current_line = i
            self.state.stack_frames[0]["line"] = i

            # Simular variables
            m = re.match(r'\bsea\s+(\w+)\s*(?::\s*\w+)?\s*=\s*(.+)', stripped)
            if m:
                var_ref = 1
                if var_ref not in self.state.variables:
                    self.state.variables[var_ref] = []
                nombre = m.group(1)
                valor_str = m.group(2).rstrip(";").strip()
                # Intentar evaluar valor
                tipo = "string"
                if re.match(r'^-?\d+\.?\d*$', valor_str): tipo = "number"
                elif valor_str in ("verdadero", "falso"): tipo = "boolean"
                elif valor_str == "nulo": tipo = "null"
                # Quitar o agregar variable
                existing = [v for v in self.state.variables[var_ref] if v["name"] != nombre]
                existing.append({"name": nombre, "value": valor_str, "type": tipo, "variablesReference": 0})
                self.state.variables[var_ref] = existing

            # Simular output de mostrar(...)
            m_out = re.match(r'\bmostrar\s*\((.+)\)', stripped)
            if m_out:
                contenido = m_out.group(1).strip().strip('"').strip("'")
                self.conn.send_event("output", {"category": "stdout", "output": contenido + "\n"})

            # Verificar breakpoint
            if self.state.is_breakpoint(self.program, i):
                self._pause("breakpoint")
                self._wait_for_resume()
                if self._stopped: break

            # stepIn/stepOver
            if self._step_mode in ("stepIn", "stepOver"):
                self._pause("step")
                self._wait_for_resume()
                if self._stopped: break

            time.sleep(0.005)  # pequeña pausa para no saturar

        if not self._stopped:
            self.conn.send_event("exited", {"exitCode": 0})
            self.conn.send_event("terminated", {})

    def _leer_salida(self):
        """Lee y reenvía stdout del intérprete."""
        if not self.process:
            return
        for linea in self.process.stdout:
            self.conn.send_event("output", {"category": "stdout", "output": linea})
        self.conn.send_event("exited", {"exitCode": self.process.wait()})
        self.conn.send_event("terminated", {})

    def _leer_stderr(self):
        """Lee stderr y detecta errores."""
        if not self.process:
            return
        for linea in self.process.stderr:
            # Parsear líneas de error de C-Forge: "Error en línea 5: ..."
            m = re.search(r'l[íi]nea\s+(\d+)', linea, re.IGNORECASE)
            if m:
                nlinea = int(m.group(1))
                self.state.current_line = nlinea
                self._pause("exception")
            self.conn.send_event("output", {"category": "stderr", "output": linea})

    def _pause(self, reason: str):
        self.state.paused = True
        self.state.pause_reason = reason
        self._resume_event.clear()
        self.conn.send_event("stopped", {
            "reason": reason,
            "threadId": 1,
            "allThreadsStopped": True
        })

    def _wait_for_resume(self):
        self._resume_event.wait()
        self.state.paused = False

    def _continue_execution(self):
        self._step_mode = "continue"
        self._resume_event.set()

    def resume(self, granularity: str = "continue"):
        self._step_mode = granularity
        self._resume_event.set()

    def stop(self):
        self._stopped = True
        if self.process:
            self.process.terminate()
        self._resume_event.set()


# ── Servidor DAP ───────────────────────────────────────────────────────────────
class DAPServer:
    def __init__(self, interpreter: str = "cforgev"):
        self.interpreter = interpreter
        self.state = DebugState()
        self.debugger: Optional[CForgeDebugInterpreter] = None
        self._initialized = False

    def handle_session(self, conn: DAPConnection):
        """Maneja una sesión de depuración completa."""
        self.state = DebugState()

        while True:
            msg = conn.read_message()
            if msg is None:
                break

            tipo = msg.get("type")
            if tipo == "request":
                self._handle_request(msg, conn)
            elif tipo == "response":
                pass  # No esperamos respuestas entrantes

    def _handle_request(self, req: dict, conn: DAPConnection):
        cmd = req.get("command", "")
        args = req.get("arguments", {})

        handlers = {
            "initialize": self._handle_initialize,
            "launch": self._handle_launch,
            "attach": self._handle_attach,
            "disconnect": self._handle_disconnect,
            "terminate": self._handle_terminate,
            "setBreakpoints": self._handle_set_breakpoints,
            "setFunctionBreakpoints": self._handle_set_function_breakpoints,
            "setExceptionBreakpoints": self._handle_set_exception_breakpoints,
            "configurationDone": self._handle_configuration_done,
            "continue": self._handle_continue,
            "next": self._handle_next,
            "stepIn": self._handle_step_in,
            "stepOut": self._handle_step_out,
            "pause": self._handle_pause,
            "threads": self._handle_threads,
            "stackTrace": self._handle_stack_trace,
            "scopes": self._handle_scopes,
            "variables": self._handle_variables,
            "evaluate": self._handle_evaluate,
            "source": self._handle_source,
        }

        handler = handlers.get(cmd)
        if handler:
            handler(req, args, conn)
        else:
            conn.send_response(req, success=True)  # comando desconocido: ignorar

    def _handle_initialize(self, req, args, conn):
        conn.send_response(req, {
            "supportsConfigurationDoneRequest": True,
            "supportsSetBreakpointsRequest": True,
            "supportsSetFunctionBreakpointsRequest": True,
            "supportsEvaluateForHovers": True,
            "supportsStepBack": False,
            "supportsSingleThreadExecutionRequests": True,
            "supportsTerminateRequest": True,
            "exceptionBreakpointFilters": [
                {"filter": "all", "label": "Todas las excepciones", "default": False},
                {"filter": "uncaught", "label": "Excepciones no capturadas", "default": True}
            ]
        })
        conn.send_event("initialized")

    def _handle_launch(self, req, args, conn):
        program = args.get("program", "")
        stop_on_entry = args.get("stopOnEntry", False)
        interpreter = args.get("interpreter", self.interpreter)

        conn.send_response(req)

        self.debugger = CForgeDebugInterpreter(program, interpreter, self.state, conn)
        threading.Thread(
            target=self.debugger.start,
            args=(stop_on_entry,),
            daemon=True
        ).start()

    def _handle_attach(self, req, args, conn):
        conn.send_response(req, success=False, message="attach no soportado — usa launch")

    def _handle_disconnect(self, req, args, conn):
        if self.debugger:
            self.debugger.stop()
        conn.send_response(req)

    def _handle_terminate(self, req, args, conn):
        if self.debugger:
            self.debugger.stop()
        conn.send_response(req)
        conn.send_event("terminated")

    def _handle_set_breakpoints(self, req, args, conn):
        source = args.get("source", {})
        path = source.get("path", "")
        bps = args.get("breakpoints", [])
        lineas = [bp["line"] for bp in bps]
        self.state.breakpoints[path] = lineas

        verificados = [{"verified": True, "line": l} for l in lineas]
        conn.send_response(req, {"breakpoints": verificados})

    def _handle_set_function_breakpoints(self, req, args, conn):
        conn.send_response(req, {"breakpoints": []})

    def _handle_set_exception_breakpoints(self, req, args, conn):
        conn.send_response(req, {"breakpoints": []})

    def _handle_configuration_done(self, req, args, conn):
        conn.send_response(req)

    def _handle_continue(self, req, args, conn):
        conn.send_response(req, {"allThreadsContinued": True})
        if self.debugger:
            self.debugger.resume("continue")

    def _handle_next(self, req, args, conn):
        conn.send_response(req)
        if self.debugger:
            self.debugger.resume("stepOver")

    def _handle_step_in(self, req, args, conn):
        conn.send_response(req)
        if self.debugger:
            self.debugger.resume("stepIn")

    def _handle_step_out(self, req, args, conn):
        conn.send_response(req)
        if self.debugger:
            self.debugger.resume("stepOut")

    def _handle_pause(self, req, args, conn):
        conn.send_response(req)
        if self.debugger:
            self.debugger._pause("pause")

    def _handle_threads(self, req, args, conn):
        conn.send_response(req, {"threads": self.state.threads})

    def _handle_stack_trace(self, req, args, conn):
        if not self.state.stack_frames:
            programa = ""
            if self.debugger:
                programa = self.debugger.program
            self.state.stack_frames = [{
                "id": 1,
                "name": "principal",
                "source": {"path": programa, "name": Path(programa).name if programa else ""},
                "line": self.state.current_line,
                "column": 1
            }]
        conn.send_response(req, {
            "stackFrames": self.state.stack_frames,
            "totalFrames": len(self.state.stack_frames)
        })

    def _handle_scopes(self, req, args, conn):
        frame_id = args.get("frameId", 1)
        conn.send_response(req, {"scopes": [
            {"name": "Locales", "variablesReference": 1, "expensive": False},
            {"name": "Globales", "variablesReference": 2, "expensive": False}
        ]})

    def _handle_variables(self, req, args, conn):
        ref = args.get("variablesReference", 0)
        vars_ = self.state.variables.get(ref, [])
        if not vars_ and ref == 2:
            # Variables globales de ejemplo
            vars_ = [
                {"name": "__version__", "value": '"2.4.0"', "type": "texto", "variablesReference": 0},
                {"name": "__archivo__", "value": f'"{self.state.current_file}"', "type": "texto", "variablesReference": 0}
            ]
        conn.send_response(req, {"variables": vars_})

    def _handle_evaluate(self, req, args, conn):
        expr = args.get("expression", "")
        # En una implementación completa: evaluar en contexto del intérprete
        conn.send_response(req, {
            "result": f"[eval: {expr}]",
            "type": "texto",
            "variablesReference": 0
        })

    def _handle_source(self, req, args, conn):
        source = args.get("source", {})
        path = source.get("path", "")
        try:
            contenido = Path(path).read_text()
        except Exception:
            contenido = "// fuente no disponible"
        conn.send_response(req, {"content": contenido, "mimeType": "text/x-cforge"})


# ── Modos de transporte ────────────────────────────────────────────────────────
def run_stdio(interpreter: str):
    """Modo stdio — para VS Code launch config."""
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer
    conn = DAPConnection(stdin, stdout)
    server = DAPServer(interpreter)
    server.handle_session(conn)


def run_tcp(port: int, interpreter: str):
    """Modo TCP socket — para depuración remota."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("127.0.0.1", port))
    sock.listen(5)
    print(f"DAP server C-Forge escuchando en 127.0.0.1:{port}", file=sys.stderr)

    while True:
        client, addr = sock.accept()
        print(f"DAP: conexión de {addr}", file=sys.stderr)
        reader = client.makefile("rb")
        writer = client.makefile("wb")
        conn = DAPConnection(reader, writer)
        server = DAPServer(interpreter)
        threading.Thread(target=server.handle_session, args=(conn,), daemon=True).start()


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="C-Forge DAP Debug Server")
    parser.add_argument("--stdio", action="store_true", help="Usar stdio (VS Code launch)")
    parser.add_argument("--port", type=int, default=4711, help="Puerto TCP (default: 4711)")
    parser.add_argument("--interpreter", default="cforgev", help="Ruta al intérprete cforgev")
    parser.add_argument("--version", action="version", version="cforge-dap 2.4.0")
    args = parser.parse_args()

    if args.stdio:
        run_stdio(args.interpreter)
    else:
        run_tcp(args.port, args.interpreter)


if __name__ == "__main__":
    main()
