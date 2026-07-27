import io
import shutil
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from cforge_vm import VirtualMachine, compile_file
from cforgev import CForgevError, Interpreter, tokenize


ROOT = Path(__file__).resolve().parents[1]
B1_PARTS = (
    ROOT / "bootstrap/core_lexer.cfv",
    ROOT / "bootstrap/core_ast.cfv",
    ROOT / "bootstrap/core_parser.cfv",
)


def without_embedded_lexer_test(source: str) -> str:
    marker = 'test "lexer core reconoce el programa mínimo"'
    return source.split(marker, 1)[0] if marker in source else source


def build_unit(driver: Path) -> str:
    parts = [
        without_embedded_lexer_test(path.read_text(encoding="utf-8"))
        for path in B1_PARTS
    ]
    parts.append(driver.read_text(encoding="utf-8"))
    return "\n".join(parts)


def interpreter_output(path: Path) -> str:
    output = io.StringIO()
    with redirect_stdout(output):
        Interpreter(tokenize(path.read_text(encoding="utf-8"))).run()
    return output.getvalue()


def vm_output(path: Path) -> str:
    values: list[str] = []
    VirtualMachine(compile_file(path), output=values.append).run()
    return "".join(f"{value}\n" for value in values)


class BootstrapB1Tests(unittest.TestCase):
    def test_core_grammar_ast_and_parser_are_cforge_sources(self):
        grammar = (ROOT / "docs/CORE-GRAMMAR-0.4.ebnf").read_text(
            encoding="utf-8"
        )
        ast = (ROOT / "bootstrap/core_ast.cfv").read_text(encoding="utf-8")
        parser = (ROOT / "bootstrap/core_parser.cfv").read_text(encoding="utf-8")
        self.assertIn("programa          = { sentencia }, EOF", grammar)
        self.assertIn("estructura NodoASTCore", ast)
        self.assertIn("funcion ast_core_canonico", ast)
        self.assertIn("funcion parsear_tokens_core", parser)
        self.assertIn("funcion expresion_parser_core", parser)
        self.assertNotIn('extern("', ast + parser)

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
    def test_stage0_compiles_b1_parser_and_all_engines_emit_identical_ast(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            unit = build / "frontend_b1.cfv"
            compiler = build / "cforge-bootstrap"
            executable = build / "frontend_b1"
            unit.write_text(
                build_unit(ROOT / "bootstrap/fixtures/parser_b1_driver.cfv"),
                encoding="utf-8",
            )

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
            self.assertTrue(executable.is_file())

            native = subprocess.run(
                [str(executable)], capture_output=True, text=True,
            )
            self.assertEqual(native.returncode, 0, native.stderr)
            interpreted = interpreter_output(unit)
            virtual = vm_output(unit)
            self.assertEqual(native.stdout, interpreted)
            self.assertEqual(virtual, interpreted)

            lines = interpreted.splitlines()
            self.assertEqual(len(lines), 3)
            self.assertTrue(lines[0].startswith("Programa:8:Core-0.4@3["))
            self.assertIn("Binario:1:+", lines[0])
            self.assertIn("Binario:1:*", lines[0])
            self.assertIn('Texto:4:"C-"', lines[1])
            self.assertIn('Texto:7:"Forge"', lines[1])
            self.assertIn("Binario:1:-", lines[2])

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
    def test_b1_invalid_source_fails_cleanly_in_all_engines(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            unit = build / "frontend_b1_invalid.cfv"
            compiler = build / "cforge-bootstrap"
            executable = build / "frontend_b1_invalid"
            unit.write_text(
                build_unit(
                    ROOT / "bootstrap/fixtures/parser_b1_invalid_driver.cfv"
                ),
                encoding="utf-8",
            )
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
            native = subprocess.run(
                [str(executable)], capture_output=True, text=True,
            )
            self.assertNotEqual(native.returncode, 0)
            self.assertIn("se esperaba el nombre de la variable", native.stderr)
            self.assertNotIn("Traceback", native.stderr)

            with self.assertRaisesRegex(
                CForgevError, "se esperaba el nombre de la variable"
            ):
                Interpreter(tokenize(unit.read_text(encoding="utf-8"))).run()
            with self.assertRaisesRegex(
                Exception, "se esperaba el nombre de la variable"
            ):
                VirtualMachine(compile_file(unit)).run()


if __name__ == "__main__":
    unittest.main()
