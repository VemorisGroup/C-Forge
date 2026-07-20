from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

from cforgev import Interpreter, connector_engine, tokenize
from compilador_nativo import compile_native


ROOT = Path(__file__).resolve().parent.parent


class SharedArenaTests(unittest.TestCase):
    COMPAT_SOURCE = r'''
print("python")
console.log("javascript")
System.out.println("java")
std::cout << "cpp" << std::endl
cout << "cpp-simple"
datos = [1, 2]
datos.append(3)
datos.push(4)
print(datos.length)
print(datos.length())
print(datos.len())
print("Forge".length)
'''

    def test_multilanguage_syntax_in_interpreter(self) -> None:
        output = StringIO()
        with redirect_stdout(output):
            Interpreter(tokenize(self.COMPAT_SOURCE)).run()
        self.assertEqual(
            output.getvalue().splitlines(),
            ["python", "javascript", "java", "cpp", "cpp-simple", "4", "4", "4", "5"],
        )

    def test_declarative_catalog_is_deterministic(self) -> None:
        self.assertEqual(connector_engine("ia_procesar"), "python")
        self.assertEqual(connector_engine("ui_crear"), "java")
        self.assertEqual(connector_engine("web_enviar"), "javascript")
        self.assertIsNone(connector_engine("procesar_ia"))

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no está disponible")
    def test_two_mappings_share_the_same_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "arena-test"
            arena = Path(directory) / "arena.bin"
            build = subprocess.run(
                [
                    "clang++", "-std=c++17", "-O2", "-pthread",
                    "-I", str(ROOT / "include"),
                    str(ROOT / "tests" / "arena_roundtrip.cpp"),
                    "-o", str(executable),
                ],
                capture_output=True,
                text=True,
            )
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable), str(arena)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("ForgeSharedArena OK", run.stdout)

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no está disponible")
    def test_native_connectors_stage_forge_values(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "arena.cfv"
            executable = Path(directory) / "arena"
            source.write_text(
                'datos = json_parse("{\\"valor\\":42}")\n'
                'estado = forge_arena_estado()\n'
                'mostrar(datos.valor)\n'
                'mostrar(estado.registros_vivos > 0)\n'
                'mostrar(forge_catalogo().ia_)\n',
                encoding="utf-8",
            )
            compile_native(source, executable)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(run.stdout.splitlines(), ["42", "verdadero", "python"])

    @unittest.skipUnless(shutil.which("clang++"), "clang++ no está disponible")
    def test_multilanguage_syntax_in_native_compiler(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "compat.cfv"
            executable = Path(directory) / "compat"
            source.write_text(self.COMPAT_SOURCE, encoding="utf-8")
            compile_native(source, executable)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual(
                run.stdout.splitlines(),
                ["python", "javascript", "java", "cpp", "cpp-simple", "4", "4", "4", "5"],
            )


if __name__ == "__main__":
    unittest.main()
