import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0_SOURCE = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
DIRECT_SOURCE = ROOT / "bootstrap/direct/cforge_pe_x64_core.cfv"
BACKEND_SOURCE = ROOT / "bootstrap/direct/cforge_pe_x64_core_backend.cfv"
FIXTURE = ROOT / "bootstrap/fixtures/machine_control_windows_b6.cfv"
GENERATOR = ROOT / "herramientas/generar_pe_core.py"
EXPECTED_LINES = (
    "PE-ARITMETICA-CONDICION-CICLO-OK",
    "PE-DIVISION-COMPARACION-OK",
    "PE-DISTINTO-OK",
    "PE-MENOR-IGUAL-OK",
    "PE-MAYOR-OK",
)


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


class BootstrapB6PECoreSourceTests(unittest.TestCase):
    def test_backend_is_cforge_without_foreign_bridges(self):
        source = DIRECT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("funcion compilar_core_pe", source)
        self.assertIn("funcion emitir_expresion_core_pe", source)
        self.assertIn("funcion resolver_operaciones_core_pe", source)
        self.assertNotIn('extern("', source)
        self.assertNotIn("compilar_cpp_nativo", source)
        self.assertNotIn("sys_run", source)

    def test_generated_backend_is_reproducible(self):
        before = DIRECT_SOURCE.read_bytes()
        require_success(run(["python3", str(GENERATOR)], cwd=ROOT))
        self.assertEqual(DIRECT_SOURCE.read_bytes(), before)


@unittest.skipUnless(
    shutil.which("clang++"),
    "clang++ solo es necesario para construir Stage 0 durante la prueba",
)
class BootstrapB6PECoreEmissionTests(unittest.TestCase):
    def test_variables_arithmetic_comparisons_if_else_and_while(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "stage0"
            backend = build / "cforge-pe-core"
            first = build / "control-primero.exe"
            second = build / "control-segundo.exe"
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
                "clang++", "clang", "g++", "gcc", "cc", "cl", "link",
                "lld", "ld", "as", "python", "python3",
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
            for output in (first, second):
                emission = run(
                    [str(backend), str(FIXTURE), "-o", str(output)],
                    env=environment,
                )
                require_success(emission)

            self.assertFalse(marker.exists())
            image = first.read_bytes()
            self.assertEqual(image, second.read_bytes())
            self.assertEqual(len(image), 5632)
            self.assertEqual(image[:2], b"MZ")
            self.assertEqual(image[0x80:0x84], b"PE\0\0")
            self.assertEqual(struct.unpack_from("<H", image, 0x84)[0], 0x8664)
            self.assertEqual(struct.unpack_from("<H", image, 0x98)[0], 0x20B)
            self.assertEqual(struct.unpack_from("<I", image, 0xA8)[0], 0x1000)
            self.assertEqual(struct.unpack_from("<I", image, 0x9C)[0], 4096)
            self.assertEqual(struct.unpack_from("<I", image, 0x198)[0], 4096)
            self.assertEqual(struct.unpack_from("<I", image, 0x1C4)[0], 4608)
            self.assertEqual(struct.unpack_from("<I", image, 0x1EC)[0], 5120)
            self.assertEqual(image[0x200:0x20B], bytes([
                85, 72, 137, 229, 72, 129, 236, 0, 16, 0, 0
            ]))
            self.assertIn(b"\x48\x0f\xaf\xc1", image[:0x600])
            self.assertIn(b"\x49\xf7\xfa", image[:0x600])
            self.assertIn(b"\x0f\x84", image[:0x600])
            self.assertIn(b"\xe9", image[:0x600])
            self.assertIn(b"KERNEL32.dll\0", image)
            self.assertIn(b"GetStdHandle\0", image)
            self.assertIn(b"WriteFile\0", image)
            self.assertIn(b"ExitProcess\0", image)
            for line in EXPECTED_LINES:
                self.assertIn((line + "\r\n").encode("utf-8"), image)


if __name__ == "__main__":
    unittest.main()
