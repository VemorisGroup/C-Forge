from __future__ import annotations

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from cforgev import connector_engine
from compilador_nativo import compile_native


ROOT = Path(__file__).resolve().parent.parent


class SharedArenaTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
