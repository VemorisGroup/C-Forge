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
DIRECT_SOURCE = ROOT / "bootstrap/direct/cforge_pe_x64.cfv"
HELLO_SOURCE = ROOT / "bootstrap/fixtures/machine_hello_windows_b6.cfv"
EXPECTED = "Hola maquina Windows desde C-Forge\r\n"


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


class BootstrapB6PESourceTests(unittest.TestCase):
    def test_pe_backend_is_cforge_without_foreign_bridges(self):
        source = DIRECT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("funcion emitir_pe_x64", source)
        self.assertIn("funcion seccion_importaciones_pe", source)
        self.assertIn("funcion codigo_x64_pe", source)
        self.assertNotIn('extern("', source)
        self.assertNotIn("compilar_cpp_nativo", source)
        self.assertNotIn("sys_run", source)


@unittest.skipUnless(
    shutil.which("clang++"),
    "clang++ solo es necesario para construir Stage 0 durante la prueba",
)
class BootstrapB6PEDirectEmissionTests(unittest.TestCase):
    def test_cforge_emits_deterministic_pe_without_external_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "stage0"
            backend = build / "cforge-pe-x64"
            first = build / "hola-primero.exe"
            second = build / "hola-segundo.exe"
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
                    [str(backend), str(HELLO_SOURCE), "-o", str(output)],
                    env=environment,
                )
                require_success(emission)

            self.assertFalse(marker.exists())
            self.assertEqual(first.read_bytes(), second.read_bytes())
            image = first.read_bytes()
            self.assertEqual(len(image), 2048)
            self.assertEqual(image[:2], b"MZ")
            pe_offset = struct.unpack_from("<I", image, 0x3C)[0]
            self.assertEqual(pe_offset, 0x80)
            self.assertEqual(image[pe_offset:pe_offset + 4], b"PE\0\0")
            self.assertEqual(struct.unpack_from("<H", image, 0x84)[0], 0x8664)
            self.assertEqual(struct.unpack_from("<H", image, 0x86)[0], 3)
            self.assertEqual(struct.unpack_from("<H", image, 0x98)[0], 0x20B)
            self.assertEqual(struct.unpack_from("<I", image, 0xA8)[0], 0x1000)
            self.assertEqual(struct.unpack_from("<Q", image, 0xB0)[0], 0x140000000)
            self.assertEqual(struct.unpack_from("<I", image, 0xD0)[0], 0x4000)
            self.assertEqual(struct.unpack_from("<H", image, 0xDC)[0], 3)
            self.assertEqual(struct.unpack_from("<I", image, 0x110)[0], 0x2000)
            self.assertEqual(struct.unpack_from("<I", image, 0x118)[0], 0)
            self.assertEqual(struct.unpack_from("<I", image, 0x168)[0], 0x3000)

            self.assertEqual(image[0x188:0x190].rstrip(b"\0"), b".text")
            self.assertEqual(image[0x1B0:0x1B8].rstrip(b"\0"), b".rdata")
            self.assertEqual(image[0x1D8:0x1E0].rstrip(b"\0"), b".data")
            self.assertEqual(image[0x200:0x204], b"\x48\x83\xec\x38")
            self.assertEqual(image[0x400:0x404], b"\x40\x20\x00\x00")
            self.assertIn(b"KERNEL32.dll\0", image)
            self.assertIn(b"GetStdHandle\0", image)
            self.assertIn(b"WriteFile\0", image)
            self.assertIn(b"ExitProcess\0", image)
            self.assertIn(EXPECTED.encode("utf-8"), image)

            if platform.system() == "Windows":
                execution = run([str(first)])
                require_success(execution)
                self.assertEqual(execution.stdout, EXPECTED)


if __name__ == "__main__":
    unittest.main()
