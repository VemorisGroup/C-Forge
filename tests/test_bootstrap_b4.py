import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0_SOURCE = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
STAGE1_SOURCE = ROOT / "bootstrap/stage1/cforge_stage1.cfv"
DRIVER_SOURCE = ROOT / "bootstrap/core_driver.cfv"


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        capture_output=True,
        text=True,
        **kwargs,
    )


def build_stage0(output: Path) -> None:
    result = run(
        ["clang++", "-std=c++17", "-O2", str(STAGE0_SOURCE), "-o", str(output)]
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr)


def build_stage1(stage0: Path, output: Path) -> None:
    result = run([str(stage0), str(STAGE1_SOURCE), "-o", str(output)])
    if result.returncode != 0:
        raise AssertionError(result.stderr)


def linked_libraries(executable: Path) -> str:
    if shutil.which("otool"):
        return run(["otool", "-L", str(executable)]).stdout
    if shutil.which("ldd"):
        return run(["ldd", str(executable)]).stdout
    return ""


@unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
class BootstrapB4Tests(unittest.TestCase):
    def test_stage1_controller_is_cforge_only(self):
        controller = DRIVER_SOURCE.read_text(encoding="utf-8")
        stage1 = STAGE1_SOURCE.read_text(encoding="utf-8")
        self.assertIn("funcion compilar_fuente_stage1", controller)
        self.assertIn("tokenizar_core(fuente)", controller)
        self.assertIn("parsear_tokens_core(tokens)", controller)
        self.assertIn("analizar_semantica_core(programa)", controller)
        self.assertIn("emitir_programa_core(programa)", controller)
        self.assertIn("compilar_cpp_nativo", controller)
        self.assertTrue(stage1.startswith("// C-Forge Stage 1 Bootstrap B4."))
        self.assertNotIn('extern("', stage1)
        self.assertNotIn("Python", stage1)

    def test_stage0_builds_stage1_and_stage1_builds_native_program(self):
        source = (
            'sea nombre: texto = "C-Forge"\n'
            "sea base: numero = 40\n"
            "sea resultado: numero = base + 2\n"
            'mostrar(nombre + " Stage 1")\n'
            "mostrar(resultado)\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "cforge-bootstrap"
            stage1 = build / "cforge-stage1"
            program_source = build / "programa.cfv"
            program = build / "programa"
            program_source.write_text(source, encoding="utf-8")

            build_stage0(stage0)
            build_stage1(stage0, stage1)
            compile_result = run(
                [str(stage1), str(program_source), "-o", str(program)],
                env={
                    **os.environ,
                    "PYTHONHOME": "/ruta-que-no-existe",
                    "PYTHONPATH": "/ruta-que-no-existe",
                },
            )
            self.assertEqual(
                compile_result.returncode, 0, compile_result.stderr
            )
            self.assertIn("C-Forge Stage 1 creó:", compile_result.stdout)
            self.assertTrue(program.is_file())
            self.assertFalse(Path(str(program) + ".stage1.cpp").exists())
            self.assertNotIn("python", linked_libraries(stage1).lower())

            execution = run([str(program)])
            self.assertEqual(execution.returncode, 0, execution.stderr)
            self.assertEqual(execution.stdout, "C-Forge Stage 1\n42\n")

    def test_stage1_rejects_semantic_error_without_native_output(self):
        source = (
            'sea nombre: texto = "Javier"\n'
            "sea destino: texto = mover(nombre)\n"
            "mostrar(nombre)\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "cforge-bootstrap"
            stage1 = build / "cforge-stage1"
            program_source = build / "movimiento_invalido.cfv"
            program = build / "no-debe-existir"
            program_source.write_text(source, encoding="utf-8")
            build_stage0(stage0)
            build_stage1(stage0, stage1)

            result = run(
                [str(stage1), str(program_source), "-o", str(program)]
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("CFB2003", result.stderr)
            self.assertIn("uso después de mover 'nombre'", result.stderr)
            self.assertFalse(program.exists())
            self.assertFalse(Path(str(program) + ".stage1.cpp").exists())

    def test_stage1_reports_parser_error_without_traceback(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "cforge-bootstrap"
            stage1 = build / "cforge-stage1"
            program_source = build / "sintaxis_invalida.cfv"
            program = build / "no-debe-existir"
            program_source.write_text(
                "sea resultado: numero =\n", encoding="utf-8"
            )
            build_stage0(stage0)
            build_stage1(stage0, stage1)

            result = run(
                [str(stage1), str(program_source), "-o", str(program)]
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("expresión inválida en línea", result.stderr)
            self.assertNotIn("Traceback", result.stderr)
            self.assertFalse(program.exists())


if __name__ == "__main__":
    unittest.main()
