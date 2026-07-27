from __future__ import annotations

import os
import platform
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0 = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
MACHO = ROOT / "bootstrap/direct/cforge_macho_arm64_core.cfv"
PE = ROOT / "bootstrap/direct/cforge_pe_x64_core.cfv"
FIXTURE = ROOT / "bootstrap/fixtures/machine_runtime_b6.cfv"


def run(command: list[str], *, env: dict[str, str] | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        command,
        cwd=ROOT,
        env=env,
        check=True,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


@unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible para construir Stage 0")
class BootstrapB67RuntimeValueTests(unittest.TestCase):
    def build_backend(self, source: Path, directory: Path) -> Path:
        stage0 = directory / "stage0"
        backend = directory / source.stem
        run(["clang++", "-std=c++17", "-O2", str(STAGE0), "-o", str(stage0)])
        run([str(stage0), str(source), "-o", str(backend)])
        return backend

    def test_sources_are_cforge_and_cover_runtime_values(self):
        for source in (MACHO, PE):
            text = source.read_text(encoding="utf-8")
            self.assertIn("Funcion", text)
            self.assertIn("Coleccion", text)
            self.assertIn("emitir_texto", text)
            self.assertNotIn("extern(", text)
            self.assertNotIn("use_python", text)

    def test_pe_emits_functions_lists_indexing_and_dynamic_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            backend = self.build_backend(PE, directory)
            executable = directory / "runtime.exe"
            blocked = directory / "blocked"
            blocked.mkdir()
            for name in ("clang", "clang++", "cl", "link", "lld", "ld", "as", "python", "python3"):
                path = blocked / name
                path.write_text("#!/bin/sh\nexit 97\n", encoding="utf-8")
                path.chmod(0o755)
            env = os.environ.copy()
            env["PATH"] = str(blocked)
            run([str(backend), str(FIXTURE), "-o", str(executable)], env=env)
            image = executable.read_bytes()
            self.assertEqual(image[:2], b"MZ")
            self.assertEqual(image[0x80:0x84], b"PE\0\0")
            self.assertEqual(struct.unpack_from("<H", image, 0x84)[0], 0x8664)
            self.assertEqual(len(image), 5632)
            text = image[512:4608]
            self.assertIn(b"\xe8", text)  # CALL rel32 de una función C-Forge.
            self.assertIn(b"\x48\x8b\x04\xc1", text)  # indexación de lista.
            self.assertIn(b"\x41\x88\x01", text)  # copia dinámica de texto.
            self.assertIn(b"C-FORGE-", image)
            self.assertIn(b"B6.7-OK", image)

    @unittest.skipUnless(
        platform.system() == "Darwin" and platform.machine() == "arm64",
        "ejecución Mach-O ARM64 disponible solo en macOS ARM64",
    )
    def test_macho_executes_functions_lists_indexing_and_dynamic_text(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            backend = self.build_backend(MACHO, directory)
            executable = directory / "runtime"
            run([str(backend), str(FIXTURE), "-o", str(executable)])
            result = run([str(executable)])
            self.assertEqual(result.stdout, "C-FORGE-B6.7-OK\n")


if __name__ == "__main__":
    unittest.main()
