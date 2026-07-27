import io
import shutil
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from cforge_vm import VirtualMachine, compile_file
from cforgev import Interpreter, tokenize


ROOT = Path(__file__).resolve().parents[1]
B3_PARTS = (
    ROOT / "bootstrap/core_lexer.cfv",
    ROOT / "bootstrap/core_ast.cfv",
    ROOT / "bootstrap/core_parser.cfv",
    ROOT / "bootstrap/core_semantics.cfv",
    ROOT / "bootstrap/core_emitter.cfv",
)


def without_lexer_test(source: str) -> str:
    marker = 'test "lexer core reconoce el programa mínimo"'
    return source.split(marker, 1)[0] if marker in source else source


def build_unit(driver: Path) -> str:
    parts = [
        without_lexer_test(path.read_text(encoding="utf-8"))
        for path in B3_PARTS
    ]
    parts.append(driver.read_text(encoding="utf-8"))
    return "\n".join(parts)


def interpreter_output(path: Path) -> str:
    output = io.StringIO()
    with redirect_stdout(output):
        Interpreter(tokenize(path.read_text(encoding="utf-8"))).run()
    return output.getvalue()


def vm_output(path: Path) -> str:
    output: list[str] = []
    VirtualMachine(compile_file(path), output=output.append).run()
    return "".join(f"{line}\n" for line in output)


def build_stage0(output: Path) -> None:
    subprocess.run(
        [
            "clang++", "-std=c++17", "-O2",
            str(ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"),
            "-o", str(output),
        ],
        check=True, capture_output=True, text=True,
    )


class BootstrapB3Tests(unittest.TestCase):
    def test_native_emitter_is_written_in_cforge(self):
        source = (ROOT / "bootstrap/core_emitter.cfv").read_text(
            encoding="utf-8"
        )
        self.assertIn("estructura ResultadoEmisionCore", source)
        self.assertIn("funcion emitir_expresion_core", source)
        self.assertIn("funcion emitir_sentencia_core", source)
        self.assertIn("funcion emitir_programa_core", source)
        self.assertIn("analizar_semantica_core(programa)", source)
        self.assertNotIn('extern("', source)

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
    def test_stage0_compiles_emitter_and_emitted_cpp_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            unit = build / "emitter_b3.cfv"
            stage0 = build / "cforge-bootstrap"
            emitter = build / "emitter_b3"
            generated = build / "programa_b3.cpp"
            program = build / "programa_b3"
            unit.write_text(
                build_unit(ROOT / "bootstrap/fixtures/emitter_b3_driver.cfv"),
                encoding="utf-8",
            )

            build_stage0(stage0)
            compiled_emitter = subprocess.run(
                [str(stage0), str(unit), "-o", str(emitter)],
                capture_output=True, text=True,
            )
            self.assertEqual(compiled_emitter.returncode, 0, compiled_emitter.stderr)

            native_emission = subprocess.run(
                [str(emitter)], capture_output=True, text=True,
            )
            self.assertEqual(native_emission.returncode, 0, native_emission.stderr)
            interpreted = interpreter_output(unit)
            virtual = vm_output(unit)
            self.assertEqual(native_emission.stdout, interpreted)
            self.assertEqual(virtual, interpreted)
            self.assertIn("#include <variant>", native_emission.stdout)
            self.assertIn("int main()", native_emission.stdout)
            self.assertIn("Valor cfv_resultado", native_emission.stdout)

            generated.write_text(native_emission.stdout, encoding="utf-8")
            native_program = subprocess.run(
                ["clang++", "-std=c++17", "-O2", str(generated), "-o", str(program)],
                capture_output=True, text=True,
            )
            self.assertEqual(native_program.returncode, 0, native_program.stderr)
            result = subprocess.run(
                [str(program)], capture_output=True, text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, "C-Forge B3\n42\n")

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
    def test_b3_refuses_semantically_invalid_ast_before_emission(self):
        expected = (
            "falso\n"
            "CFB2003 línea 3: uso después de mover 'nombre'\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            unit = build / "emitter_b3_invalid.cfv"
            stage0 = build / "cforge-bootstrap"
            emitter = build / "emitter_b3_invalid"
            unit.write_text(
                build_unit(
                    ROOT / "bootstrap/fixtures/emitter_b3_invalid_driver.cfv"
                ),
                encoding="utf-8",
            )
            build_stage0(stage0)
            subprocess.run(
                [str(stage0), str(unit), "-o", str(emitter)],
                check=True, capture_output=True, text=True,
            )
            native = subprocess.run(
                [str(emitter)], check=True, capture_output=True, text=True
            ).stdout
            self.assertEqual(native, expected)
            self.assertEqual(interpreter_output(unit), expected)
            self.assertEqual(vm_output(unit), expected)
            self.assertNotIn("#include", native)


if __name__ == "__main__":
    unittest.main()
