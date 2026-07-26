import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class BootstrapStage0Tests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("clang++"), "clang++ no está disponible")
    def test_cpp_stage0_builds_and_runs_a_native_cforge_program(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            compiler = build / "cforge-bootstrap"
            executable = build / "minimal"
            built = subprocess.run(
                [
                    "clang++", "-std=c++17", "-O2",
                    str(root / "bootstrap/stage0/cforge_bootstrap.cpp"),
                    "-o", str(compiler),
                ],
                capture_output=True, text=True,
            )
            self.assertEqual(built.returncode, 0, built.stderr)
            compiled = subprocess.run(
                [
                    str(compiler), str(root / "bootstrap/fixtures/minimal.cfv"),
                    "-o", str(executable),
                ],
                capture_output=True, text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            self.assertTrue(executable.is_file())
            result = subprocess.run(
                [str(executable)], capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "C-Forge Core Bootstrap\n42\n")

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no está disponible")
    def test_cpp_stage0_reports_cforge_diagnostics_without_python(self):
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            compiler = build / "cforge-bootstrap"
            invalid = build / "invalid.cfv"
            invalid.write_text("sea valor = desconocida + 1\n", encoding="utf-8")
            subprocess.run(
                [
                    "clang++", "-std=c++17", "-O2",
                    str(root / "bootstrap/stage0/cforge_bootstrap.cpp"),
                    "-o", str(compiler),
                ],
                check=True, capture_output=True, text=True,
            )
            result = subprocess.run(
                [str(compiler), str(invalid), "-o", str(build / "invalid")],
                capture_output=True, text=True,
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("[C-Forge Bootstrap Error]", result.stderr)
            self.assertIn("variable desconocida", result.stderr)
            self.assertNotIn("Traceback", result.stderr)


if __name__ == "__main__":
    unittest.main()
