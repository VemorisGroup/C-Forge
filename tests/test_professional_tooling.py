import io
import json
import tempfile
import unittest
from pathlib import Path

from cforge_diagnostics import analyze_source
from cforge_lsp import _definitions, _locations, _symbols, run
from cforge_packages import add, build_package, init, list_packages, remove
from cforge_vm import VirtualMachine, compile_source, disassemble


class ProfessionalToolingTests(unittest.TestCase):
    def test_structured_static_diagnostics(self):
        self.assertEqual(analyze_source('sea valor: numero = "texto"\n')[0].code, "CF2001")
        self.assertEqual(analyze_source('sea valor = "texto" - 2\n')[0].code, "CF2001")
        self.assertEqual(analyze_source("sea valor: numero = 42\nmostrar(valor)\n"), [])

    def test_reproducible_local_package_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dependency = root / "biblioteca"; dependency.mkdir()
            (dependency / "modulo.cfv").write_text("sea valor = 42\n", encoding="utf-8")
            project = root / "aplicacion"; project.mkdir()
            init(project, "aplicacion"); add(project, "biblioteca", str(dependency))
            self.assertEqual(list_packages(project), [("biblioteca", str(dependency))])
            lock = json.loads((project / "cforge.lock").read_text(encoding="utf-8"))
            self.assertEqual(len(lock["dependencies"]["biblioteca"]["sha256"]), 64)
            remove(project, "biblioteca")
            self.assertEqual(list_packages(project), [])

    def test_package_builder_produces_a_verifiable_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); init(root, "paquete-seguro")
            (root / "modulo.cfv").write_text("mostrar(42)\n", encoding="utf-8")
            archive, digest = build_package(root, root / "salida")
            self.assertTrue(archive.is_file())
            self.assertEqual(len(digest), 64)

    @staticmethod
    def _rpc(message: dict) -> bytes:
        body = json.dumps(message).encode()
        return f"Content-Length: {len(body)}\r\n\r\n".encode() + body

    def test_lsp_initialize_completion_and_shutdown(self):
        incoming = b"".join([
            self._rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
            self._rpc({"jsonrpc": "2.0", "id": 2, "method": "textDocument/completion", "params": {}}),
            self._rpc({"jsonrpc": "2.0", "id": 3, "method": "shutdown", "params": {}}),
            self._rpc({"jsonrpc": "2.0", "method": "exit", "params": {}}),
        ])
        output = io.BytesIO()
        self.assertEqual(run(io.BytesIO(incoming), output), 0)
        text = output.getvalue().decode()
        self.assertIn("C-Forge LSP", text); self.assertIn("console.log", text)
        self.assertIn("definitionProvider", text); self.assertIn("renameProvider", text)

    def test_lsp_symbols_definitions_and_references(self):
        source = "sea valor = 21\nfuncion doble(x) { retornar x * 2 }\nmostrar(valor)\n"
        self.assertEqual(_symbols(source)[0]["name"], "valor")
        self.assertEqual(_definitions("file:///main.cfv", source, "valor")[0]["range"]["start"]["line"], 0)
        self.assertEqual(len(_locations("file:///main.cfv", source, "valor")), 2)

    def test_bytecode_vm_functions_loops_and_compatibility_syntax(self):
        source = """
funcion doble(x) { retornar x * 2 }
datos = [1, 2]
datos.push(3)
sea i = 0
sea total = 0
mientras (i < datos.length) {
    total = total + datos[i]
    i = i + 1
}
console.log(doble(total))
"""
        output = []
        program = compile_source(source)
        VirtualMachine(program, output.append).run()
        self.assertEqual(output, ["12"])
        self.assertIn("CALL", disassemble(program))

    def test_vm_catches_runtime_errors(self):
        source = """
sea capturado = falso
intentar {
    10 / 0
} capturar (error) {
    capturado = verdadero
}
mostrar(capturado)
"""
        output = []
        VirtualMachine(compile_source(source), output.append).run()
        self.assertEqual(output, ["verdadero"])


if __name__ == "__main__":
    unittest.main()
