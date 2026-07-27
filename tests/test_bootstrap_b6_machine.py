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
DIRECT_SOURCE = ROOT / "bootstrap/direct/cforge_elf_x64.cfv"
HELLO_SOURCE = ROOT / "bootstrap/fixtures/machine_hello_b6.cfv"


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


@unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible para bootstrap")
class BootstrapB6DirectMachineCodeTests(unittest.TestCase):
    def test_cforge_backend_emits_elf_machine_code_without_external_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "stage0"
            direct = build / "cforge-elf-x64"
            output = build / "hola-elf"
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
                run([str(stage0), str(DIRECT_SOURCE), "-o", str(direct)])
            )

            poison.mkdir()
            for name in (
                "clang++", "clang", "g++", "gcc", "cc", "ld", "as",
                "python", "python3",
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
                [str(direct), str(HELLO_SOURCE), "-o", str(output)],
                env=environment,
            )
            require_success(emission)
            self.assertFalse(marker.exists())
            self.assertTrue(output.is_file())
            self.assertTrue(output.stat().st_mode & 0o111)

            image = output.read_bytes()
            self.assertEqual(image[:4], b"\x7fELF")
            self.assertEqual(image[4:7], b"\x02\x01\x01")
            self.assertEqual(struct.unpack_from("<H", image, 16)[0], 2)
            self.assertEqual(struct.unpack_from("<H", image, 18)[0], 62)
            self.assertEqual(struct.unpack_from("<Q", image, 24)[0], 0x400078)
            self.assertEqual(struct.unpack_from("<Q", image, 32)[0], 64)
            self.assertEqual(struct.unpack_from("<H", image, 54)[0], 56)
            self.assertEqual(struct.unpack_from("<H", image, 56)[0], 1)
            self.assertEqual(image[120:125], b"\xb8\x01\x00\x00\x00")
            self.assertTrue(image.endswith(b"Hola maquina C-Forge\n"))

            if platform.system() == "Linux" and platform.machine() in {
                "x86_64", "amd64"
            }:
                execution = run([str(output)])
                require_success(execution)
                self.assertEqual(execution.stdout, "Hola maquina C-Forge\n")

    def test_direct_backend_is_cforge_without_foreign_bridges(self):
        source = DIRECT_SOURCE.read_text(encoding="utf-8")
        self.assertNotIn('extern("', source)
        self.assertNotIn("compilar_cpp_nativo", source)
        self.assertNotIn("sys_run", source)
        self.assertIn("funcion emitir_elf_x64", source)
        self.assertIn("escribir_bytes", source)


if __name__ == "__main__":
    unittest.main()
