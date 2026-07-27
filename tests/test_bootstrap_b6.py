import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0_SOURCE = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
STAGE1_SOURCE = ROOT / "bootstrap/stage1/cforge_stage1.cfv"
RUNTIME_FIXTURE = ROOT / "bootstrap/fixtures/runtime_b6.cfv"


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def linked_libraries(executable: Path) -> str:
    if shutil.which("otool"):
        return run(["otool", "-L", str(executable)]).stdout
    if shutil.which("ldd"):
        return run(["ldd", str(executable)]).stdout
    return ""


@unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
class BootstrapB6Tests(unittest.TestCase):
    def test_generated_runtime_executes_with_python_commands_blocked(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "stage0"
            stage1 = build / "stage1"
            source = build / "cforge_stage1.cfv"
            program = build / "runtime-b6"
            data_file = build / "runtime-data.txt"
            poison_bin = build / "sin-python"
            marker = build / "python-invocado"

            source.write_bytes(STAGE1_SOURCE.read_bytes())
            poison_bin.mkdir()
            for name in ("python", "python3"):
                blocker = poison_bin / name
                blocker.write_text(
                    "#!/bin/sh\n"
                    f"touch '{marker}'\n"
                    "exit 97\n",
                    encoding="utf-8",
                )
                blocker.chmod(0o755)

            require_success(
                run(
                    [
                        "clang++", "-std=c++17", "-O2",
                        str(STAGE0_SOURCE), "-o", str(stage0),
                    ]
                )
            )
            require_success(run([str(stage0), str(source), "-o", str(stage1)]))
            require_success(
                run(
                    [
                        str(stage1), str(RUNTIME_FIXTURE),
                        "-o", str(program),
                    ]
                )
            )

            environment = os.environ.copy()
            environment["PATH"] = str(poison_bin)
            execution = run(
                [str(program), str(data_file)],
                env=environment,
            )
            require_success(execution)

            self.assertEqual(
                execution.stdout,
                "C-Forge Runtime B6\n3\n12\n",
            )
            self.assertFalse(marker.exists(), "el runtime intentó iniciar Python")
            self.assertFalse(data_file.exists())
            self.assertNotIn("python", linked_libraries(program).lower())

    def test_runtime_source_has_no_python_abi(self):
        runtime = (ROOT / "bootstrap/core_runtime.cfv").read_text(
            encoding="utf-8"
        )
        lowered = runtime.lower()
        self.assertNotIn("python.h", lowered)
        self.assertNotIn("py_initialize", lowered)
        self.assertNotIn("pyrun_", lowered)
        self.assertNotIn("libpython", lowered)


if __name__ == "__main__":
    unittest.main()
