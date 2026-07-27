import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STAGE0_SOURCE = ROOT / "bootstrap/stage0/cforge_bootstrap.cpp"
STAGE1_SOURCE = ROOT / "bootstrap/stage1/cforge_stage1.cfv"


def run(command: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(command, capture_output=True, text=True)


def require_success(result: subprocess.CompletedProcess[str]) -> None:
    if result.returncode != 0:
        raise AssertionError(
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def linked_libraries(executable: Path) -> str:
    if shutil.which("otool"):
        return run(["otool", "-L", str(executable)]).stdout
    if shutil.which("ldd"):
        return run(["ldd", str(executable)]).stdout
    return ""


@unittest.skipUnless(shutil.which("clang++"), "clang++ no disponible")
class BootstrapB5Tests(unittest.TestCase):
    def test_stage2_and_stage3_are_byte_identical(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            source = build / "cforge_stage1.cfv"
            stage0 = build / "stage0"
            stage1 = build / "stage1"
            stage2 = build / "stage2"
            stage3 = build / "stage3"
            stage2_cpp = build / "stage2.cpp"
            stage3_cpp = build / "stage3.cpp"
            source.write_bytes(STAGE1_SOURCE.read_bytes())

            require_success(
                run(
                    [
                        "clang++", "-std=c++17", "-O2",
                        str(STAGE0_SOURCE), "-o", str(stage0),
                    ]
                )
            )
            require_success(
                run([str(stage0), str(source), "-o", str(stage1)])
            )

            # Stage 1 compila sus propias fuentes y crea Stage 2.
            require_success(
                run(
                    [
                        str(stage1), str(source),
                        "--emitir-cpp", str(stage2_cpp),
                    ]
                )
            )
            require_success(
                run([str(stage1), str(source), "-o", str(stage2)])
            )

            # Stage 2 repite exactamente la compilación y crea Stage 3.
            require_success(
                run(
                    [
                        str(stage2), str(source),
                        "--emitir-cpp", str(stage3_cpp),
                    ]
                )
            )
            require_success(
                run([str(stage2), str(source), "-o", str(stage3)])
            )

            self.assertEqual(stage2_cpp.read_bytes(), stage3_cpp.read_bytes())
            self.assertEqual(stage2.read_bytes(), stage3.read_bytes())
            self.assertEqual(digest(stage2_cpp), digest(stage3_cpp))
            self.assertEqual(digest(stage2), digest(stage3))
            self.assertNotIn("python", linked_libraries(stage2).lower())
            self.assertFalse(Path(str(source) + ".stage1.cpp").exists())

            # El compilador reproducido debe seguir compilando programas Core.
            sample = build / "muestra.cfv"
            native = build / "muestra"
            sample.write_text(
                'sea nombre: texto = "C-Forge autoalojado"\n'
                "sea resultado: numero = 40 + 2\n"
                "mostrar(nombre)\n"
                "mostrar(resultado)\n",
                encoding="utf-8",
            )
            require_success(
                run([str(stage3), str(sample), "-o", str(native)])
            )
            execution = run([str(native)])
            require_success(execution)
            self.assertEqual(
                execution.stdout,
                "C-Forge autoalojado\n42\n",
            )

    def test_stage1_source_contains_no_foreign_runtime_bridge(self):
        source = STAGE1_SOURCE.read_text(encoding="utf-8")
        self.assertIn("funcion compilar_fuente_stage1", source)
        self.assertIn("funcion emitir_programa_core", source)
        self.assertNotIn('extern("', source)
        self.assertNotIn("use_python", source)
        self.assertNotIn("use_javascript", source)
        self.assertNotIn("use_java", source)


if __name__ == "__main__":
    unittest.main()
