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
B2_PARTS = (
    ROOT / "bootstrap/core_lexer.cfv",
    ROOT / "bootstrap/core_ast.cfv",
    ROOT / "bootstrap/core_parser.cfv",
    ROOT / "bootstrap/core_semantics.cfv",
)


def without_lexer_test(source: str) -> str:
    marker = 'test "lexer core reconoce el programa mínimo"'
    return source.split(marker, 1)[0] if marker in source else source


def build_b2_unit() -> str:
    sources = [
        without_lexer_test(path.read_text(encoding="utf-8"))
        for path in B2_PARTS
    ]
    sources.append(
        (ROOT / "bootstrap/fixtures/semantics_b2_driver.cfv").read_text(
            encoding="utf-8"
        )
    )
    return "\n".join(sources)


def run_interpreter(path: Path) -> str:
    output = io.StringIO()
    with redirect_stdout(output):
        Interpreter(tokenize(path.read_text(encoding="utf-8"))).run()
    return output.getvalue()


def run_vm(path: Path) -> str:
    output: list[str] = []
    VirtualMachine(compile_file(path), output=output.append).run()
    return "".join(f"{line}\n" for line in output)


class BootstrapB2Tests(unittest.TestCase):
    def test_type_and_ownership_analyzer_is_written_in_cforge(self):
        source = (ROOT / "bootstrap/core_semantics.cfv").read_text(
            encoding="utf-8"
        )
        grammar = (ROOT / "docs/CORE-GRAMMAR-0.4.ebnf").read_text(
            encoding="utf-8"
        )
        self.assertIn("clase SimboloCore", source)
        self.assertIn("funcion tipo_expresion_core", source)
        self.assertIn("funcion analizar_semantica_core", source)
        self.assertIn("CFB2001", source)
        self.assertIn("CFB2002", source)
        self.assertIn("CFB2003", source)
        self.assertIn('"mover", "(", expresion, ")"', grammar)
        self.assertNotIn('extern("', source)

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
    def test_stage0_compiles_b2_and_all_engines_agree(self):
        expected = (
            "verdadero\n"
            "CFB2001 línea 1: la variable 'edad' requiere numero pero recibió texto\n"
            "CFB2002 línea 1: variable no declarada 'fantasma'\n"
            "CFB2003 línea 3: uso después de mover 'nombre'\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            unit = build / "frontend_b2.cfv"
            compiler = build / "cforge-bootstrap"
            executable = build / "frontend_b2"
            unit.write_text(build_b2_unit(), encoding="utf-8")

            subprocess.run(
                [
                    "clang++", "-std=c++17", "-O2",
                    str(ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"),
                    "-o", str(compiler),
                ],
                check=True, capture_output=True, text=True,
            )
            compiled = subprocess.run(
                [str(compiler), str(unit), "-o", str(executable)],
                capture_output=True, text=True,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)

            native = subprocess.run(
                [str(executable)], capture_output=True, text=True,
            )
            self.assertEqual(native.returncode, 0, native.stderr)
            interpreted = run_interpreter(unit)
            virtual = run_vm(unit)
            self.assertEqual(native.stdout, expected)
            self.assertEqual(interpreted, expected)
            self.assertEqual(virtual, expected)

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
    def test_b2_output_is_deterministic_across_native_runs(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            unit = build / "frontend_b2.cfv"
            compiler = build / "cforge-bootstrap"
            executable = build / "frontend_b2"
            unit.write_text(build_b2_unit(), encoding="utf-8")
            subprocess.run(
                [
                    "clang++", "-std=c++17", "-O2",
                    str(ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"),
                    "-o", str(compiler),
                ],
                check=True, capture_output=True, text=True,
            )
            subprocess.run(
                [str(compiler), str(unit), "-o", str(executable)],
                check=True, capture_output=True, text=True,
            )
            first = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True
            ).stdout
            second = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True
            ).stdout
            self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
