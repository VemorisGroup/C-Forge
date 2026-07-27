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
DIRECT_SOURCE = ROOT / "bootstrap/direct/cforge_macho_arm64.cfv"
HELLO_SOURCE = ROOT / "bootstrap/fixtures/machine_hello_b6.cfv"


def run(command: list[str], **kwargs) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True, **kwargs)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


class BootstrapB6MachOSourceTests(unittest.TestCase):
    def test_macho_backend_is_cforge_without_foreign_bridges(self):
        source = DIRECT_SOURCE.read_text(encoding="utf-8")
        self.assertIn("funcion emitir_macho_arm64", source)
        self.assertIn("funcion firma_macho", source)
        self.assertIn("sha256_rango", source)
        self.assertNotIn('extern("', source)
        self.assertNotIn("compilar_cpp_nativo", source)
        self.assertNotIn("sys_run", source)


@unittest.skipUnless(
    platform.system() == "Darwin"
    and platform.machine() in {"arm64", "aarch64"}
    and shutil.which("clang++"),
    "requiere macOS ARM64 y clang++ únicamente para construir el bootstrap",
)
class BootstrapB6MachOExecutionTests(unittest.TestCase):
    def test_cforge_emits_signed_native_macho_without_external_toolchain(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            stage0 = build / "stage0"
            direct = build / "cforge-macho-arm64"
            output = build / "hola-macho"
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
                [str(direct), str(HELLO_SOURCE), "-o", str(output)],
                env=environment,
            )
            require_success(emission)
            self.assertFalse(marker.exists())
            self.assertTrue(output.stat().st_mode & 0o111)

            image = output.read_bytes()
            self.assertEqual(struct.unpack_from("<I", image, 0)[0], 0xFEEDFACF)
            self.assertEqual(struct.unpack_from("<I", image, 4)[0], 0x0100000C)
            self.assertEqual(struct.unpack_from("<I", image, 16)[0], 10)
            self.assertEqual(struct.unpack_from("<I", image, 20)[0], 496)
            self.assertEqual(image[16384:16388], b"\xfa\xde\x0c\xc0")

            verification = run(["codesign", "--verify", "--verbose=4", str(output)])
            require_success(verification)
            execution = run([str(output)])
            require_success(execution)
            self.assertEqual(execution.stdout, "Hola maquina C-Forge\n")


if __name__ == "__main__":
    unittest.main()
