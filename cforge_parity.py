"""Matriz de paridad verificable entre los motores de C-Forge.

Una característica solo puede declararse común a intérprete, VM y LLVM cuando
el mismo archivo produce exactamente la misma salida y estado de terminación.
"""

from __future__ import annotations

import contextlib
import io
import json
import shutil
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

from cforgev import CForgevError, execute
from cforge_vm import VirtualMachine, compile_file as compile_vm_file
from compilador_llvm import compile_native as compile_llvm_native


@dataclass(frozen=True)
class EngineResult:
    engine: str
    supported: bool
    returncode: int
    stdout: str
    stderr: str
    error: str | None = None


@dataclass(frozen=True)
class ParityReport:
    source: str
    equal: bool
    reference: str | None
    results: tuple[EngineResult, ...]

    def to_dict(self) -> dict[str, object]:
        value = asdict(self)
        value["results"] = [asdict(result) for result in self.results]
        return value


def _interpreter(path: Path) -> EngineResult:
    output = io.StringIO()
    errors = io.StringIO()
    try:
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            execute(path)
        return EngineResult("interpreter", True, 0, output.getvalue(), errors.getvalue())
    except Exception as error:
        return EngineResult(
            "interpreter", True, 1, output.getvalue(), errors.getvalue(), str(error)
        )


def _vm(path: Path) -> EngineResult:
    output: list[str] = []
    try:
        program = compile_vm_file(path)
        VirtualMachine(program, output.append, base_dir=path.resolve().parent).run()
        stdout = "".join(item + "\n" for item in output)
        return EngineResult("vm", True, 0, stdout, "")
    except Exception as error:
        return EngineResult("vm", True, 1, "".join(item + "\n" for item in output), "", str(error))


def _llvm(path: Path, clang: str, timeout: float) -> EngineResult:
    resolved = shutil.which(clang)
    if resolved is None:
        return EngineResult("llvm", False, 127, "", "", f"no se encontró {clang}")
    try:
        with tempfile.TemporaryDirectory(prefix="cforge-parity-") as directory:
            binary = Path(directory) / "programa"
            compile_llvm_native(path, binary, clang=resolved)
            completed = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                timeout=timeout,
                cwd=path.resolve().parent,
            )
            return EngineResult(
                "llvm", True, completed.returncode, completed.stdout, completed.stderr
            )
    except subprocess.TimeoutExpired:
        return EngineResult("llvm", True, 124, "", "", "tiempo de ejecución agotado")
    except Exception as error:
        return EngineResult("llvm", False, 1, "", "", str(error))


def compare_file(path: Path, clang: str = "clang", timeout: float = 30.0) -> ParityReport:
    path = path.resolve()
    if path.suffix != ".cfv":
        raise CForgevError("Parity requiere un archivo .cfv")
    if not path.is_file():
        raise CForgevError(f"No se pudo abrir {path}")
    results = (_interpreter(path), _vm(path), _llvm(path, clang, timeout))
    supported = [result for result in results if result.supported]
    reference = supported[0].engine if supported else None
    signatures = {(result.returncode, result.stdout, result.stderr) for result in supported}
    equal = (
        len(supported) == len(results)
        and all(result.error is None for result in supported)
        and len(signatures) == 1
    )
    return ParityReport(str(path), equal, reference, results)


def format_report(report: ParityReport) -> str:
    lines = [f"C-Forge parity: {'OK' if report.equal else 'FALLO'} — {report.source}"]
    for result in report.results:
        state = "OK" if result.supported and result.error is None else (
            "NO SOPORTADO" if not result.supported else "ERROR"
        )
        lines.append(
            f"[{state}] {result.engine}: código={result.returncode}, "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
        if result.error:
            lines.append(f"  {result.error}")
    return "\n".join(lines)


def reports_json(reports: list[ParityReport]) -> str:
    return json.dumps(
        {"schema": 1, "ok": all(report.equal for report in reports),
         "reports": [report.to_dict() for report in reports]},
        ensure_ascii=False,
        sort_keys=True,
    )
