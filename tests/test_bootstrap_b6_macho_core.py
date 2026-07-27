import os
import platform
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0_SOURCE = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
DIRECT_SOURCE = ROOT / "bootstrap/direct/cforge_macho_arm64_core.cfv"
BACKEND_SOURCE = ROOT / "bootstrap/direct/cforge_macho_arm64_core_backend.cfv"
FIXTURE = ROOT / "bootstrap/fixtures/machine_control_b6.cfv"
GENERATOR = ROOT / "herramientas/generar_macho_core.py"
EXPECTED = (
    "ARITMETICA-CONDICION-CICLO-OK\n"
    "DIVISION-COMPARACION-OK\n"
    "DISTINTO-OK\n"
    "MENOR-IGUAL-OK\n"
    "MAYOR-OK\n"
)


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


class BootstrapB6MachOCoreSourceTests(unittest.TestCase):
    def test_backend_is_native_cforge_without_foreign_bridges(self):
        source = DIRECT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("funcion compilar_core_macho", source)
        self.assertIn("funcion emitir_expresion_arm", source)
        self.assertIn("funcion emitir_sentencia_arm", source)
        self.assertNotIn('extern("', source)
        self.assertNotIn("compilar_cpp_nativo", source)
        self.assertNotIn("sys_run", source)

    def test_generated_backend_is_reproducible(self):
        before = DIRECT_SOURCE.read_bytes()
        require_success(run(["python3", str(GENERATOR)], cwd=ROOT))
        self.assertEqual(DIRECT_SOURCE.read_bytes(), before)


@unittest.skipUnless(
    platform.system() == "Darwin"
    and platform.machine() in {"arm64", "aarch64"}
    and shutil.which("clang++"),
    "requiere macOS ARM64 y clang++ únicamente para construir Stage 0",
)
class BootstrapB6MachOCoreExecutionTests(unittest.TestCase):
    def test_variables_arithmetic_comparisons_if_else_and_while(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "stage0"
            backend = build / "cforge-macho-core"
            output = build / "control-macho"
            poison = build / "sin-toolchain"
            marker = build / "herramienta-invocada"

            require_success(
                run(
                    [
                        "clang++", "-std=c++17", "-O2",
                        str(STAGE0_SOURCE), "-o", str(stage0),
                    ]
                )
            )
            require_success(
                run([str(stage0), str(DIRECT_SOURCE), "-o", str(backend)])
            )

            poison.mkdir()
            for name in (
                "clang++", "clang", "g++", "gcc", "cc", "ld", "as",
                "codesign", "python", "python3",
            ):
                blocker = poison / name
                blocker.write_text(
                    "#!/bin/sh\n"
                    f"touch '{marker}'\n"
                    "exit 97\n",
                    encoding="utf-8",
                )
                blocker.chmod(0o755)

            environment = os.environ.copy()
            environment["PATH"] = str(poison)
            emission = run(
                [str(backend), str(FIXTURE), "-o", str(output)],
                env=environment,
            )
            require_success(emission)
            self.assertFalse(marker.exists())
            self.assertTrue(output.stat().st_mode & 0o111)

            image = output.read_bytes()
            self.assertEqual(struct.unpack_from("<I", image, 0)[0], 0xFEEDFACF)
            self.assertEqual(struct.unpack_from("<I", image, 4)[0], 0x0100000C)
            self.assertEqual(image[16384:16388], b"\xfa\xde\x0c\xc0")
            self.assertIn(b"ARITMETICA-CONDICION-CICLO-OK\n", image)
            self.assertIn(b"DIVISION-COMPARACION-OK\n", image)
            self.assertIn(b"DISTINTO-OK\n", image)
            self.assertIn(b"MENOR-IGUAL-OK\n", image)
            self.assertIn(b"MAYOR-OK\n", image)

            verification = run(
                ["codesign", "--verify", "--verbose=4", str(output)]
            )
            require_success(verification)
            execution = run([str(output)])
            require_success(execution)
            self.assertEqual(execution.stdout, EXPECTED)


if __name__ == "__main__":
    unittest.main()
