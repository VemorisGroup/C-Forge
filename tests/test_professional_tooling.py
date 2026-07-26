import io
import importlib.util
import hashlib
import json
import tempfile
import unittest
import shutil
import subprocess
import sys
from pathlib import Path

from cforge_diagnostics import analyze_file, analyze_source
from cforge_dap import DAPSession, run as run_dap
from cforge_lsp import _declarations, _definitions, _locations, _symbols, run
from cforge_packages import (add, build_package, generate_keypair, init,
                             list_packages, remove, sign_package,
                             verify_package_signature, verify_publisher)
from cforge_parity import compare_file, reports_json
from cforge_vm import (BYTECODE_HEADER, VirtualMachine, compile_file, compile_source,
                       deserialize, disassemble, serialize)
from cforgev import CForgevError
from compilador_llvm import compile_file as compile_llvm_file, compile_source as compile_llvm_source


class ProfessionalToolingTests(unittest.TestCase):
    def test_interpreter_vm_llvm_parity_gate(self):
        if not shutil.which("clang"):
            self.skipTest("clang no está disponible")
        report = compare_file(Path("ejemplos/paridad_16.cfv"))
        self.assertTrue(report.equal, reports_json([report]))
        self.assertEqual(
            {result.stdout for result in report.results},
            {"C-Forge parity\n6\nverdadero\n"},
        )

    def test_lsp_infers_option_and_ffi_declaration_details(self):
        source = '''
extern_c segura funcion native_divide(a: numero, b: numero): numero
sea resultado = algunos(42)
sea vacio: opcion<texto> = ninguno()
'''
        declarations = {item["name"]: item["detail"] for item in _declarations(source)}
        self.assertEqual(
            declarations["native_divide"],
            "extern C ABI segura native_divide(a: numero, b: numero): numero",
        )
        self.assertEqual(declarations["resultado"], "resultado: opcion<numero>")
        self.assertEqual(declarations["vacio"], "vacio: opcion<texto>")

    def test_lsp_resolves_hover_and_definition_across_open_files(self):
        ffi_uri = "file:///proyecto/ffi.cfv"; main_uri = "file:///proyecto/main.cfv"
        ffi = "extern_c segura funcion native_divide(a: numero, b: numero): numero\n"
        main = "mostrar(native_divide(84, 2))\n"
        incoming = b"".join([
            self._rpc({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
            self._rpc({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                "textDocument": {"uri": ffi_uri, "text": ffi}}}),
            self._rpc({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {
                "textDocument": {"uri": main_uri, "text": main}}}),
            self._rpc({"jsonrpc": "2.0", "id": 2, "method": "textDocument/definition", "params": {
                "textDocument": {"uri": main_uri}, "position": {"line": 0, "character": 12}}}),
            self._rpc({"jsonrpc": "2.0", "id": 3, "method": "textDocument/hover", "params": {
                "textDocument": {"uri": main_uri}, "position": {"line": 0, "character": 12}}}),
            self._rpc({"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": {}}),
        ])
        output = io.BytesIO()
        self.assertEqual(run(io.BytesIO(incoming), output), 0)
        text = output.getvalue().decode()
        self.assertIn(ffi_uri, text)
        self.assertIn("extern C ABI segura native_divide", text)

    def test_dap_accepts_source_line_breakpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "debug.cfv"
            source.write_text("sea valor = 21\nmostrar(valor * 2)\n", encoding="utf-8")
            incoming = b"".join([
                self._rpc({"seq": 1, "type": "request", "command": "initialize", "arguments": {}}),
                self._rpc({"seq": 2, "type": "request", "command": "launch",
                           "arguments": {"program": str(source)}}),
                self._rpc({"seq": 3, "type": "request", "command": "setBreakpoints",
                           "arguments": {"source": {"path": str(source)},
                                         "breakpoints": [{"line": 2}, {"line": 99}]}}),
                self._rpc({"seq": 4, "type": "request", "command": "disconnect", "arguments": {}}),
            ])
            output = io.BytesIO()
            self.assertEqual(run_dap(io.BytesIO(incoming), output), 0)
            text = output.getvalue().decode()
            self.assertIn('"event":"initialized"', text)
            self.assertIn('"verified":true,"line":2', text)
            self.assertIn('"verified":false,"line":99', text)

    def test_dap_safe_conditions_hit_counts_and_logpoints(self):
        scope = {"contador": 4, "activo": True, "cuenta": {"saldo": 42}, "datos": [3, 7]}
        self.assertTrue(DAPSession._evaluate_safe(
            "contador >= 4 y activo == verdadero", scope
        ))
        self.assertEqual(DAPSession._evaluate_safe("cuenta.saldo + datos[1]", scope), 49)
        with self.assertRaisesRegex(Exception, "no permitida"):
            DAPSession._evaluate_safe("open('secreto')", scope)
        self.assertTrue(DAPSession._hit_matches("3", 3))
        self.assertTrue(DAPSession._hit_matches(">=3", 4))
        self.assertTrue(DAPSession._hit_matches("%2", 4))
        self.assertEqual(
            DAPSession._format_logpoint("contador={contador}, saldo={cuenta.saldo}", scope),
            "contador=4, saldo=42",
        )

    def test_bytecode_preserves_source_lines(self):
        program = compile_source("sea valor = 21\nmostrar(valor * 2)\n")
        restored = deserialize(serialize(program))
        lines = {instruction.line for instruction in restored.main.code}
        self.assertTrue({1, 2}.issubset(lines))

    def test_tuple_and_set_survive_bytecode_roundtrip(self):
        source = '''
sea identidad: tupla = ("C-Forge", 2, verdadero)
sea motores: conjunto = conjunto("LLVM", "VM", "LLVM")
mostrar(identidad[0])
mostrar(longitud(identidad))
mostrar(motores)
mostrar(longitud(motores))
'''
        output = []
        program = deserialize(serialize(compile_source(source)))
        VirtualMachine(program, output.append).run()
        self.assertEqual(output, ["C-Forge", "3", "conjunto(LLVM, VM)", "2"])

    def test_vm_exposes_real_nested_debug_frames(self):
        source = '''
funcion interior(x: numero): numero { retornar x * 2 }
funcion exterior(x: numero): numero { retornar interior(x) }
mostrar(exterior(21))
'''
        depths = []
        vm = None
        def trace(*_args): depths.append(len(vm.debug_frames()))
        vm = VirtualMachine(compile_source(source), lambda _text: None, trace=trace)
        vm.run()
        self.assertGreaterEqual(max(depths), 3)

    def test_dap_expands_values_and_evaluates_without_execution(self):
        output = io.BytesIO(); session = DAPSession(io.BytesIO(), output)
        session.snapshot = {"cuenta": {"saldo": 42, "movimientos": [10, 32]}}
        session.frames = [{"name": "<main>", "line": 1, "offset": 0,
                           "scope": session.snapshot}]
        session._rebuild_references()
        self.assertEqual(session._inspect("cuenta.saldo"), 42)
        self.assertEqual(session._inspect("cuenta.movimientos[1]"), 32)
        reference = session._reference(session.snapshot["cuenta"])
        session.handle({"seq": 1, "type": "request", "command": "variables",
                        "arguments": {"variablesReference": reference}})
        text = output.getvalue().decode()
        self.assertIn('"name":"saldo"', text)
        self.assertIn('"name":"movimientos"', text)
        with self.assertRaisesRegex(Exception, "no permite llamadas"):
            session._inspect("cuenta.saldo + 1")

    def test_registry_publisher_identity_is_enforced(self):
        index = {"publishers": {"vemoris": {"status": "active", "keys": ["abc"]}}}
        self.assertEqual(verify_publisher(index, {"publisher": "vemoris"}, "abc"), "vemoris")
        with self.assertRaisesRegex(Exception, "no está autorizada"):
            verify_publisher(index, {"publisher": "vemoris"}, "otra")
        index["publishers"]["vemoris"]["status"] = "revoked"
        with self.assertRaisesRegex(Exception, "no está autorizada"):
            verify_publisher(index, {"publisher": "vemoris"}, "abc")

    @unittest.skipUnless(importlib.util.find_spec("cryptography"), "cryptography no está instalado")
    def test_ed25519_package_signature_and_revocation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); archive = root / "demo-1.0.0.tar.gz"
            archive.write_bytes(b"paquete C-Forge reproducible")
            private, public = root / "private.pem", root / "public.pem"
            key_id = generate_keypair(private, public)
            signature_path = sign_package(archive, private, "demo", "1.0.0")
            release = json.loads(signature_path.read_text(encoding="utf-8"))
            self.assertEqual(verify_package_signature(
                archive.read_bytes(), release, "demo", "1.0.0"
            ), key_id)
            with self.assertRaisesRegex(Exception, "Firma Ed25519 inválida|SHA-256"):
                verify_package_signature(archive.read_bytes() + b"alterado", release, "demo", "1.0.0")
            with self.assertRaisesRegex(Exception, "revocados"):
                verify_package_signature(archive.read_bytes(), release, "demo", "1.0.0", {key_id})

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
            first_bytes = archive.read_bytes()
            second, second_digest = build_package(root, root / "salida")
            self.assertTrue(archive.is_file())
            self.assertEqual(len(digest), 64)
            self.assertEqual(digest, second_digest)
            self.assertEqual(first_bytes, second.read_bytes())

    def test_portable_archives_are_reproducible(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            command = [
                sys.executable, "packaging/build_portable.py",
                "--version", "1.6.0-test", "--platform", "linux",
                "--output", str(output),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            first = output / "cforgev-1.6.0-test-linux-x64.tar.gz"
            first_digest = hashlib.sha256(first.read_bytes()).hexdigest()
            subprocess.run(command, check=True, capture_output=True, text=True)
            self.assertEqual(first_digest, hashlib.sha256(first.read_bytes()).hexdigest())

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

    def test_real_llvm_ir_is_accepted_by_clang(self):
        source = """
funcion doble(x) { retornar x * 2 }
sea valor: numero = 21
mostrar(doble(valor))
"""
        llvm = compile_llvm_source(source)
        self.assertIn("define double @doble", llvm)
        self.assertIn("define i32 @main", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "programa.ll"; binary = root / "programa"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout.strip(), "42")

    def test_llvm_text_functions_concat_and_comparison(self):
        source = '''
funcion saludar(nombre: texto): texto { retornar "Hola " + nombre }
sea mensaje: texto = saludar("Javier")
mostrar(mensaje)
mostrar(mensaje == "Hola Javier")
'''
        llvm = compile_llvm_source(source)
        self.assertIn("define ptr @saludar", llvm)
        self.assertIn("@cfv_concat", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "texto.ll"; binary = root / "texto"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "Hola Javier\nverdadero\n")

    def test_llvm_catches_checked_division_errors(self):
        source = '''
sea capturado = falso
intentar {
    mostrar(10 / 0)
} capturar (error) {
    mostrar(error)
    capturado = verdadero
}
mostrar(capturado)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("try.catch", llvm)
        self.assertIn("fcmp oeq double", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                module = Path(directory) / "catch.ll"; binary = Path(directory) / "catch"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "no se puede dividir por cero\nverdadero\n")

    def test_llvm_uncaught_division_error_aborts_cleanly(self):
        llvm = compile_llvm_source("mostrar(1 / 0)\n")
        self.assertIn("division.error", llvm)
        self.assertIn("[C-Forge Runtime Exception]", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                module = Path(directory) / "uncaught.ll"; binary = Path(directory) / "uncaught"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("[C-Forge Runtime Exception] no se puede dividir por cero", result.stdout)

    def test_llvm_typed_c_abi_ffi(self):
        source = '''
extern_c funcion native_add(a: numero, b: numero): numero
extern_c funcion native_echo(value: texto): texto
extern_c funcion native_not(value: booleano): booleano
mostrar(native_add(20, 22))
mostrar(native_echo("C-Forge FFI"))
mostrar(native_not(falso))
'''
        llvm = compile_llvm_source(source)
        self.assertIn("declare double @native_add(double, double)", llvm)
        self.assertIn("declare ptr @native_echo(ptr)", llvm)
        self.assertIn("declare i1 @native_not(i1)", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                module = root / "ffi.ll"; bridge = root / "bridge.c"; binary = root / "ffi"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "#include <stdbool.h>\n"
                    "double native_add(double a, double b) { return a + b; }\n"
                    "const char *native_echo(const char *value) { return value; }\n"
                    "bool native_not(bool value) { return !value; }\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "42\nC-Forge FFI\nverdadero\n")

    def test_llvm_ffi_rejects_non_abi_safe_values(self):
        source = "extern_c funcion unsafe_list(values: lista): lista\n"
        with self.assertRaisesRegex(CForgevError, "retornos propietarios"):
            compile_llvm_source(source)

    def test_llvm_ffi_borrows_numeric_list_as_zero_copy_slice(self):
        source = '''
extern_c funcion native_sum(values: lista<numero>): numero
mostrar(native_sum([1, 2, 3, 4, 5]))
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvNumberSlice = type { ptr, i64 }", llvm)
        self.assertIn("declare double @native_sum(ptr)", llvm)
        self.assertIn("getelementptr %CfvList", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "slice.ll"
                bridge = root / "slice.c"; binary = root / "slice"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "#include <stdint.h>\n"
                    "typedef struct { const double *data; uint64_t length; } CfvNumberSlice;\n"
                    "double native_sum(const CfvNumberSlice *values) {\n"
                    "  double sum = 0; for (uint64_t i = 0; i < values->length; ++i) sum += values->data[i];\n"
                    "  return sum;\n}\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "15\n")

    def test_llvm_checked_ffi_adopts_owned_numeric_list_and_releases_once(self):
        source = '''
extern_c segura funcion native_numbers(): lista<numero>
extern_c funcion native_release_count(): numero
sea values: lista<numero> = native_numbers()
mostrar(values)
destruir(values)
mostrar(native_release_count())
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvOwnedNumberList = type { ptr, i64, ptr, ptr }", llvm)
        self.assertIn("call ptr @cfv_list_from_ffi", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "owned.ll"
                bridge = root / "owned.c"; binary = root / "owned"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "#include <stdint.h>\n#include <stdlib.h>\n"
                    "typedef void (*Release)(void *);\n"
                    "typedef struct { const double *data; uint64_t length; void *owner; Release release; } Owned;\n"
                    "static double releases = 0;\n"
                    "static void release_values(void *p) { ++releases; free(p); }\n"
                    "int native_numbers(Owned *out, const char **error) {\n"
                    "  double *p = malloc(2 * sizeof(double)); p[0] = 3; p[1] = 4;\n"
                    "  out->data = p; out->length = 2; out->owner = p; out->release = release_values;\n"
                    "  *error = 0; return 0;\n}\n"
                    "double native_release_count(void) { return releases; }\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "[3, 4]\n1\n")

    def test_public_ffi_header_has_stable_64_bit_collection_layout(self):
        clang = shutil.which("clang")
        if not clang:
            self.skipTest("clang no disponible")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "layout.c"; object_file = root / "layout.o"
            source.write_text(
                '#include "cforgev_ffi.h"\n#include <stddef.h>\n'
                '_Static_assert(sizeof(void*) == 8, "esta puerta verifica ABI de 64 bits");\n'
                '_Static_assert(sizeof(CfvNumberSlice) == 16, "slice ABI inestable");\n'
                '_Static_assert(offsetof(CfvNumberSlice, length) == 8, "offset length inválido");\n'
                '_Static_assert(sizeof(CfvOwnedNumberList) == 32, "owned ABI inestable");\n'
                '_Static_assert(offsetof(CfvOwnedNumberList, release) == 24, "offset release inválido");\n'
                '_Static_assert(sizeof(CfvOwnedText) == 32, "texto ABI inestable");\n'
                '_Static_assert(offsetof(CfvOwnedText, release) == 24, "offset texto release inválido");\n'
                '_Static_assert(sizeof(CfvNumberMapView) == 24, "map view ABI inestable");\n'
                '_Static_assert(offsetof(CfvNumberMapView, length) == 16, "offset map length inválido");\n'
                '_Static_assert(sizeof(CfvValue) == 48, "ForgeValue ABI inestable");\n'
                '_Static_assert(sizeof(CfvRecordField) == 56, "record field ABI inestable");\n'
                '_Static_assert(sizeof(CfvRecordView) == 24, "record view ABI inestable");\n'
                '_Static_assert(offsetof(CfvRecordView, field_count) == 16, "record count inválido");\n'
                '_Static_assert(sizeof(CfvValueV2) == 64, "ForgeValue V2 ABI inestable");\n'
                '_Static_assert(offsetof(CfvValueV2, data) == 40, "offset V2 data inválido");\n'
                '_Static_assert(offsetof(CfvValueV2, release) == 56, "offset V2 release inválido");\n'
                '_Static_assert(sizeof(CfvMapEntryV2) == 128, "map entry V2 inestable");\n'
                '_Static_assert(sizeof(CfvRecordFieldV2) == 80, "record field V2 inestable");\n'
                '_Static_assert(sizeof(CfvRecordV2) == 32, "record V2 inestable");\n'
                '_Static_assert(offsetof(CfvRecordV2, fields) == 16, "record fields V2 inválido");\n',
                encoding="utf-8",
            )
            subprocess.run(
                [clang, "-std=c11", "-I", str(Path("include").resolve()), "-c", source, "-o", object_file],
                check=True, capture_output=True,
            )

    def test_llvm_checked_ffi_copies_owned_utf8_and_releases_invalid_results(self):
        source = '''
extern_c segura funcion native_label(mode: numero): texto
extern_c funcion native_text_release_count(): numero
mostrar(native_label(0))
mostrar(native_text_release_count())
intentar {
    mostrar(native_label(1))
} capturar (error) {
    mostrar(error)
}
mostrar(native_text_release_count())
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvOwnedText = type { ptr, i64, ptr, ptr }", llvm)
        self.assertIn("call i1 @cfv_ffi_text_invalid", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "text.ll"
                bridge = root / "text.c"; binary = root / "text"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "#include <stdint.h>\n#include <stdlib.h>\n#include <string.h>\n"
                    "typedef void (*Release)(void *);\n"
                    "typedef struct { const char *data; uint64_t length; void *owner; Release release; } OwnedText;\n"
                    "static double releases = 0;\n"
                    "static void release_text(void *p) { ++releases; free(p); }\n"
                    "int native_label(double mode, OwnedText *out, const char **error) {\n"
                    "  char *p = malloc(8);\n"
                    "  if (mode == 0) { memcpy(p, \"C-Forge\", 7); out->length = 7; }\n"
                    "  else { p[0] = 'a'; p[1] = 0; p[2] = 'b'; out->length = 3; }\n"
                    "  out->data = p; out->owner = p; out->release = release_text; *error = 0; return 0;\n}\n"
                    "double native_text_release_count(void) { return releases; }\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(
                    result.stdout,
                    "C-Forge\n1\nnative_label devolvió texto UTF-8 ABI inválido\n2\n",
                )

    def test_llvm_ffi_borrows_numeric_map_as_zero_copy_parallel_view(self):
        source = '''
extern_c funcion native_map_total(values: mapa<numero>): numero
mostrar(native_map_total({"uno": 1, "dos": 2, "tres": 3}))
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvNumberMapView = type { ptr, ptr, i64 }", llvm)
        self.assertIn("declare double @native_map_total(ptr)", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "map.ll"
                bridge = root / "map.c"; binary = root / "map"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "#include <stdint.h>\n"
                    "typedef struct { const char *const *keys; const double *values; uint64_t length; } View;\n"
                    "double native_map_total(const View *view) {\n"
                    "  double total = 0; for (uint64_t i = 0; i < view->length; ++i) {\n"
                    "    if (!view->keys[i]) return -1; total += view->values[i];\n"
                    "  } return total;\n}\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "6\n")

    def test_llvm_ffi_borrows_scalar_nominal_object_as_tagged_record(self):
        source = '''
estructura Persona {
    nombre: texto
    edad: numero
    activa: booleano
}
extern_c funcion native_person_score(persona: Persona): numero
sea persona = Persona("Javier", 41, verdadero)
mostrar(native_person_score(persona))
destruir(persona)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvRecordView = type { ptr, ptr, i64 }", llvm)
        self.assertIn("%CfvAbiValue = type { i32, i32, i64, double, ptr, ptr, ptr }", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "record.ll"
                bridge = root / "record.c"; binary = root / "record"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "#include <stdint.h>\n#include <string.h>\n"
                    "typedef void (*Release)(void *);\n"
                    "typedef struct { int32_t type; int64_t integer; double decimal; const char *text; void *owner; Release release; } Value;\n"
                    "typedef struct { const char *name; Value value; } Field;\n"
                    "typedef struct { const char *type_name; const Field *fields; uint64_t field_count; } Record;\n"
                    "double native_person_score(const Record *record) {\n"
                    "  if (!record || strcmp(record->type_name, \"Persona\") || record->field_count != 3) return -1;\n"
                    "  if (strcmp(record->fields[0].name, \"nombre\") || record->fields[0].value.type != 3) return -2;\n"
                    "  if (strcmp(record->fields[0].value.text, \"Javier\")) return -3;\n"
                    "  if (record->fields[1].value.type != 2 || record->fields[2].value.type != 4) return -4;\n"
                    "  return record->fields[1].value.decimal + record->fields[2].value.integer;\n}\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "42\n")

    def test_llvm_ffi_rejects_nominal_objects_with_nested_ownership(self):
        source = '''
estructura Caja {
    valores: lista<numero>
}
extern_c funcion native_box(caja: Caja): numero
'''
        with self.assertRaisesRegex(CForgevError, "contiene campos no escalares"):
            compile_llvm_source(source)

    def test_llvm_checked_ffi_propagates_native_errors(self):
        source = '''
extern_c segura funcion native_divide(a: numero, b: numero): numero
mostrar(native_divide(84, 2))
intentar {
    mostrar(native_divide(10, 0))
} capturar (error) {
    mostrar(error)
}
intentar {
    mostrar(native_divide(10, -1))
} capturar (error) {
    mostrar(error)
}
'''
        llvm = compile_llvm_source(source)
        self.assertIn("declare i32 @native_divide(double, double, ptr, ptr)", llvm)
        self.assertIn("ffi.valid", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "checked.ll"
                bridge = root / "checked.c"; binary = root / "checked"
                module.write_text(llvm, encoding="utf-8")
                bridge.write_text(
                    "int native_divide(double a, double b, double *out, const char **error) {\n"
                    "  if (b == 0.0) { *error = \"divisor nativo cero\"; return 1; }\n"
                    "  if (b < 0.0) { *error = 0; return 2; }\n"
                    "  *out = a / b; *error = 0; return 0;\n}\n",
                    encoding="utf-8",
                )
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(
                    result.stdout,
                    "42\ndivisor nativo cero\nnative_divide falló sin mensaje\n",
                )
                uncaught = compile_llvm_source('''
extern_c segura funcion native_divide(a: numero, b: numero): numero
mostrar(native_divide(10, 0))
''')
                module.write_text(uncaught, encoding="utf-8")
                subprocess.run([clang, module, bridge, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], capture_output=True, text=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(
                    "[C-Forge Runtime Exception] divisor nativo cero", result.stdout
                )

    def test_llvm_native_typed_options_and_checked_unwrap(self):
        source = '''
sea numero: opcion = algunos(42)
sea texto: opcion = algunos("C-Forge")
sea estado: opcion = algunos(verdadero)
sea vacio: opcion<numero> = ninguno()
mostrar(numero)
mostrar(texto)
mostrar(estado)
mostrar(vacio)
mostrar(desenvolver(numero))
mostrar(es_algunos(vacio))
intentar {
    mostrar(desenvolver(vacio))
} capturar (error) {
    mostrar(error)
}
destruir(numero)
destruir(texto)
destruir(estado)
destruir(vacio)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvOption = type", llvm)
        self.assertIn("@cfv_option_new", llvm)
        self.assertIn("@cfv_option_free", llvm)
        self.assertIn("option.valid", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                module = Path(directory) / "option.ll"; binary = Path(directory) / "option"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(
                    result.stdout,
                    "algunos(42)\nalgunos(C-Forge)\nalgunos(verdadero)\nninguno\n"
                    "42\nfalso\nno se puede desenvolver ninguno\n",
                )

    def test_llvm_native_numeric_lists(self):
        source = '''
funcion suma(datos: lista): numero { retornar datos[0] + datos[1] }
sea numeros: lista = [2, 3, 4]
numeros.append(5)
mostrar(numeros)
mostrar(numeros.length)
mostrar(suma(numeros))
destruir(numeros)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvList = type", llvm)
        self.assertIn("@cfv_list_append", llvm)
        self.assertIn("@cfv_list_free", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "lista.ll"; binary = root / "lista"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "[2, 3, 4, 5]\n4\n5\n")

    def test_llvm_native_typed_maps(self):
        source = '''
funcion buscar(datos: mapa<numero>): numero { retornar datos["dos"] }
sea numeros: mapa = {"uno": 1, "dos": 2}
sea textos: mapa = {"lenguaje": "C-Forge"}
sea estados: mapa = {"llvm": verdadero}
mostrar(numeros["dos"])
mostrar(buscar(numeros))
mostrar(textos["lenguaje"])
mostrar(estados["llvm"])
mostrar(numeros.length)
destruir(numeros)
destruir(textos)
destruir(estados)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvMap = type", llvm)
        self.assertIn("@cfv_map_get_number", llvm)
        self.assertIn("@cfv_map_get_text", llvm)
        self.assertIn("@cfv_map_get_bool", llvm)
        self.assertIn("@cfv_map_free", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "mapa.ll"; binary = root / "mapa"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "2\n2\nC-Forge\nverdadero\n2\n")

    def test_llvm_rejects_heterogeneous_maps_explicitly(self):
        with self.assertRaisesRegex(CForgevError, "valores homogéneos"):
            compile_llvm_source('sea datos: mapa = {"numero": 1, "texto": "uno"}')

    def test_llvm_native_tuples_and_sets(self):
        source = '''
funcion lenguaje(datos: tupla<texto,numero,booleano>): texto { retornar datos[0] }
sea version: tupla = ("C-Forge", 2, verdadero)
sea motores: conjunto = conjunto("LLVM", "VM", "LLVM")
mostrar(version)
mostrar(lenguaje(version))
mostrar(version[1])
mostrar(longitud(version))
mostrar(motores)
mostrar(longitud(motores))
destruir(version)
destruir(motores)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%CfvScalarCollection = type", llvm)
        self.assertIn("@cfv_collection_add_text", llvm)
        self.assertIn("@cfv_collection_get_number", llvm)
        self.assertIn("@cfv_collection_free", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "colecciones.ll"; binary = root / "colecciones"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(
                    result.stdout,
                    "(C-Forge, 2, verdadero)\nC-Forge\n2\n3\nconjunto(LLVM, VM)\n2\n",
                )

    def test_llvm_native_classes_fields_methods_and_free(self):
        source = '''
clase Contador {
 campo valor: numero
 metodo sumar(x: numero): numero {
   este.valor = este.valor + x
   retornar este.valor
 }
}
sea contador = Contador(10)
mostrar(contador.sumar(5))
mostrar(contador.valor)
destruir(contador)
'''
        llvm = compile_llvm_source(source)
        self.assertIn("%Cfv.Contador = type {double}", llvm)
        self.assertIn("define double @Contador_sumar", llvm)
        self.assertIn("call void @free", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                root = Path(directory); module = root / "objeto.ll"; binary = root / "objeto"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "15\n15\n")

    def test_llvm_transitive_destructors_release_owned_fields(self):
        source = '''
estructura Interior {
 datos: lista
}
estructura Exterior {
 interior: Interior
}
sea valores = [1, 2, 3]
sea interior = Interior(valores)
sea exterior = Exterior(interior)
destruir(exterior)
mostrar("liberado")
'''
        llvm = compile_llvm_source(source)
        self.assertIn("define void @cfv_drop_Interior", llvm)
        self.assertIn("define void @cfv_drop_Exterior", llvm)
        self.assertIn("call void @cfv_list_free", llvm)
        self.assertIn("call void @cfv_drop_Interior", llvm)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                module = Path(directory) / "drop.ll"; binary = Path(directory) / "drop"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "liberado\n")

    def test_llvm_monomorphizes_generic_functions(self):
        source = '''
funcion identidad<T>(valor: T): T { retornar valor }
mostrar(identidad(42))
mostrar(identidad("C-Forge"))
'''
        llvm = compile_llvm_source(source)
        self.assertEqual(llvm.count("define double @identidad__numero"), 1)
        self.assertEqual(llvm.count("define ptr @identidad__texto"), 1)
        clang = shutil.which("clang")
        if clang:
            with tempfile.TemporaryDirectory() as directory:
                module = Path(directory) / "generic.ll"; binary = Path(directory) / "generic"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "42\nC-Forge\n")

    def test_llvm_resolves_modules_before_codegen(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "matematica.cfv").write_text(
                "funcion triple(x: numero): numero { retornar x * 3 }\n", encoding="utf-8"
            )
            main = root / "main.cfv"
            main.write_text('usar "matematica.cfv"\nmostrar(triple(14))\n', encoding="utf-8")
            llvm = compile_llvm_file(main)
            self.assertIn("define double @triple", llvm)
            clang = shutil.which("clang")
            if clang:
                module = root / "main.ll"; binary = root / "main"
                module.write_text(llvm, encoding="utf-8")
                subprocess.run([clang, module, "-o", binary], check=True, capture_output=True)
                result = subprocess.run([binary], check=True, capture_output=True, text=True)
                self.assertEqual(result.stdout, "42\n")

    def test_memory_analyzer_rejects_use_after_move(self):
        from cforge_diagnostics import analyze_source
        diagnostics = analyze_source('sea texto = "C-Forge"\nsea destino = mover(texto)\nmostrar(texto)\n')
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("uso después de mover", diagnostics[0].message)

    def test_memory_analyzer_rejects_mutable_alias(self):
        from cforge_diagnostics import analyze_source
        diagnostics = analyze_source("sea datos = [1, 2]\nsea vista = prestar(datos)\nprestar_mut(datos)\n")
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("ya está prestada", diagnostics[0].message)

    def test_option_executes_in_bytecode_vm(self):
        output = []
        program = compile_source("sea valor: opcion = algunos(42)\nmostrar(desenvolver(valor))\n")
        VirtualMachine(program, output.append).run()
        self.assertEqual(output, ["42"])

    def test_typed_function_signatures_check_arguments_and_returns(self):
        valid = "funcion doble(x: numero): numero { retornar x * 2 }\nmostrar(doble(21))\n"
        self.assertEqual(analyze_source(valid), [])
        wrong_argument = valid.replace("doble(21)", 'doble("texto")')
        self.assertEqual(analyze_source(wrong_argument)[0].code, "CF2001")
        wrong_return = "funcion f(x: numero): texto { retornar x }\n"
        self.assertIn("retorno numero", analyze_source(wrong_return)[0].message)

    def test_generic_function_infers_and_preserves_type(self):
        valid = '''
funcion identidad<T>(valor: T): T { retornar valor }
sea numero: numero = identidad(42)
sea texto: texto = identidad("C-Forge")
mostrar(numero)
mostrar(texto)
'''
        self.assertEqual(analyze_source(valid), [])
        output = []
        VirtualMachine(compile_source(valid), output.append).run()
        self.assertEqual(output, ["42", "C-Forge"])
        conflicting = '''
funcion elegir<T>(a: T, b: T): T { retornar a }
mostrar(elegir(1, "dos"))
'''
        diagnostic = analyze_source(conflicting)[0]
        self.assertEqual(diagnostic.code, "CF2001")
        self.assertIn("genérico 'T'", diagnostic.message)

    def test_interface_contract_is_checked_statically(self):
        valid = '''
interfaz Sumable {
    metodo sumar(x: numero): numero
}
clase Contador implementa Sumable {
    campo valor: numero
    metodo sumar(x: numero): numero { retornar este.valor + x }
}
sea contador = Contador(10)
mostrar(contador.sumar(5))
'''
        self.assertEqual(analyze_source(valid), [])
        output = []
        VirtualMachine(compile_source(valid), output.append).run()
        self.assertEqual(output, ["15"])
        missing = '''
interfaz Sumable { metodo sumar(x: numero): numero }
clase Vacia implementa Sumable { campo valor: numero }
'''
        diagnostic = analyze_source(missing)[0]
        self.assertEqual(diagnostic.code, "CF2001")
        self.assertIn("no implementa", diagnostic.message)

    def test_bytecode_format_roundtrip_and_integrity(self):
        program = compile_source("mostrar(42)\n")
        encoded = serialize(program)
        output = []
        VirtualMachine(deserialize(encoded), output.append).run()
        self.assertEqual(output, ["42"])
        damaged = encoded[:-1] + bytes([encoded[-1] ^ 1])
        with self.assertRaisesRegex(Exception, "SHA-256"):
            deserialize(damaged)

    def test_bytecode_rejects_future_minor_version_and_invalid_jump(self):
        encoded = serialize(compile_source("mostrar(42)\n"))
        magic, major, minor, length, digest = BYTECODE_HEADER.unpack_from(encoded)
        future = BYTECODE_HEADER.pack(magic, major, minor + 1, length, digest) + encoded[BYTECODE_HEADER.size:]
        with self.assertRaisesRegex(Exception, "versión 1.2 incompatible"):
            deserialize(future)

        document = json.loads(encoded[BYTECODE_HEADER.size:])
        document["main"]["code"].insert(0, {"op": "JUMP", "arg": 999999, "line": 1})
        payload = json.dumps(document, ensure_ascii=False, sort_keys=True,
                             separators=(",", ":")).encode("utf-8")
        malformed = BYTECODE_HEADER.pack(
            magic, major, minor, len(payload), hashlib.sha256(payload).digest()
        ) + payload
        with self.assertRaisesRegex(Exception, "destino de salto inválido"):
            deserialize(malformed)

    def test_lexical_region_releases_borrows_and_unsafe_is_explicit(self):
        safe = "sea x = [1]\nregion { sea vista = prestar(x) }\nsea unica = prestar_mut(x)\n"
        self.assertEqual(analyze_source(safe), [])
        escaped = "sea x = 1\nregion { sea local = 2 }\nmostrar(local)\n"
        self.assertEqual(analyze_source(escaped)[0].code, "CF3001")
        explicit = 'sea x = "dato"\nsea y = mover(x)\nunsafe { mostrar(x) }\n'
        self.assertEqual(analyze_source(explicit), [])

    def test_borrow_alias_lifetime_and_cycle_detection(self):
        automatic = '''
sea datos = [1]
region {
    sea vista = prestar(datos)
    mostrar(vista)
}
sea unica = prestar_mut(datos)
destruir(unica)
mostrar(datos)
'''
        self.assertEqual(analyze_source(automatic), [])
        escaped = '''
sea datos = [1]
sea vista = prestar(datos)
destruir(vista)
mostrar(vista)
'''
        diagnostic = analyze_source(escaped)[0]
        self.assertEqual(diagnostic.code, "CF3001")
        self.assertIn("uso después de mover", diagnostic.message)
        cycle = "sea datos = [1]\ndatos.append(datos)\n"
        diagnostic = analyze_source(cycle)[0]
        self.assertEqual(diagnostic.code, "CF3001")
        self.assertIn("ciclo de ownership", diagnostic.message)

    def test_interprocedural_move_effect_reaches_caller(self):
        source = '''
funcion consumir(datos: lista) { destruir(datos) }
funcion reenviar(datos: lista) { consumir(datos) }
sea valores = [1, 2]
reenviar(valores)
mostrar(valores)
'''
        diagnostics = analyze_source(source)
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("uso después de mover", diagnostics[0].message)

    def test_constructor_transfers_non_copy_field_ownership(self):
        source = '''
estructura Caja { datos: lista }
sea valores = [1, 2]
sea caja = Caja(valores)
mostrar(valores)
'''
        diagnostics = analyze_source(source)
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("uso después de mover", diagnostics[0].message)

    def test_returned_borrow_lifetime_is_enforced_and_releasable(self):
        invalid = '''
funcion vista(datos: lista): lista { retornar prestar(datos) }
sea valores = [1]
sea referencia = vista(valores)
prestar_mut(valores)
'''
        diagnostics = analyze_source(invalid)
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("ya está prestada", diagnostics[0].message)
        valid = '''
funcion vista(datos: lista): lista { retornar prestar(datos) }
sea valores = [1]
sea referencia = vista(valores)
soltar_prestamo(referencia)
sea exclusiva = prestar_mut(valores)
soltar_prestamo(exclusiva)
'''
        self.assertEqual(analyze_source(valid), [])

    def test_borrow_of_local_cannot_escape_function(self):
        source = '''
funcion invalida(): lista {
    sea local = [1]
    retornar prestar(local)
}
'''
        diagnostics = analyze_source(source)
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("variable local", diagnostics[0].message)

    def test_indirect_ownership_cycle_is_rejected(self):
        source = '''
sea izquierda = []
sea derecha = []
izquierda.append(derecha)
derecha.append(izquierda)
'''
        diagnostics = analyze_source(source)
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("ciclo de ownership", diagnostics[0].message)
        self.assertEqual(analyze_source("sea datos = [1]\nunsafe { datos.append(datos) }\n"), [])

    def test_vm_classes_methods_survive_bytecode_roundtrip(self):
        source = '''
clase Cuenta {
 campo saldo: numero
 metodo depositar(x) { este.saldo = este.saldo + x retornar este.saldo }
}
sea cuenta = Cuenta(10)
mostrar(cuenta.depositar(5))
mostrar(cuenta.saldo)
'''
        output = []
        program = deserialize(serialize(compile_source(source)))
        VirtualMachine(program, output.append).run()
        self.assertEqual(output, ["15", "15"])

    def test_vm_resolves_modules_and_confines_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "modulo.cfv").write_text(
                'funcion doble(x: numero): numero { retornar x * 2 }\n', encoding="utf-8"
            )
            main = root / "main.cfv"
            main.write_text(
                'usar "modulo.cfv"\nfile_write("dato.txt", "seguro")\nmostrar(doble(21))\n',
                encoding="utf-8",
            )
            output = []
            VirtualMachine(compile_file(main), output.append, base_dir=root).run()
            self.assertEqual(output, ["42"])
            self.assertEqual((root / "dato.txt").read_text(), "seguro")
            escaped = compile_source('file_write("../escape.txt", "no")\n')
            with self.assertRaisesRegex(Exception, "fuera del proyecto"):
                VirtualMachine(escaped, base_dir=root).run()

    def test_static_analysis_resolves_types_across_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "matematica.cfv").write_text(
                "funcion doble(x: numero): numero { retornar x * 2 }\n", encoding="utf-8"
            )
            valid = root / "valid.cfv"
            valid.write_text('usar "matematica.cfv"\nmostrar(doble(21))\n', encoding="utf-8")
            self.assertEqual(analyze_file(valid), [])
            invalid = root / "invalid.cfv"
            invalid.write_text('usar "matematica.cfv"\nmostrar(doble("texto"))\n', encoding="utf-8")
            diagnostic = analyze_file(invalid)[0]
            self.assertEqual(diagnostic.code, "CF2001")
            self.assertIn("argumento 1", diagnostic.message)

    def test_vm_denies_processes_without_explicit_permission(self):
        program = compile_source('sys_run("printf hola")\n')
        with self.assertRaisesRegex(Exception, "falta permiso 'process'"):
            VirtualMachine(program, permissions=set()).run()

    def test_structured_tasks_and_channels_in_vm(self):
        source = '''
funcion cuadrado(x: numero): numero { retornar x * x }
sea trabajo = tarea("cuadrado", [9])
sea mensajes = canal(1)
enviar(mensajes, esperar(trabajo))
mostrar(recibir(mensajes))
cerrar_canal(mensajes)
'''
        output = []
        VirtualMachine(compile_source(source), output.append).run()
        self.assertEqual(output, ["81"])

    def test_async_await_survives_bytecode_roundtrip(self):
        source = '''
async funcion cuadrado(x: numero): numero { retornar x * x }
sea trabajo = cuadrado(12)
mostrar(await trabajo)
mostrar(await cuadrado(5))
'''
        output = []
        program = deserialize(serialize(compile_source(source)))
        VirtualMachine(program, output.append).run()
        self.assertEqual(output, ["144", "25"])
        invalid = "funcion normal(): numero { retornar 1 }\nmostrar(await normal())\n"
        self.assertIn("await requiere", analyze_source(invalid)[0].message)

    def test_normative_language_contracts_are_present_and_executable(self):
        grammar = Path("docs/GRAMMAR-1.6.ebnf").read_text(encoding="utf-8")
        types = Path("docs/TYPE-SYSTEM-1.6.md").read_text(encoding="utf-8")
        backends = Path("docs/BACKEND-SEMANTICS-1.6.md").read_text(encoding="utf-8")
        self.assertIn("programa          = { sentencia }, EOF", grammar)
        self.assertIn("Γ ⊢ e : T", types)
        self.assertIn("cforge parity archivo.cfv", backends)

        example = Path("ejemplos/especificacion_formal_16.cfv")
        self.assertEqual(analyze_file(example), [])
        output = []
        VirtualMachine(compile_file(example), output.append).run()
        self.assertEqual(output, ["C-Forge formal", "42"])

    def test_ownership_rejects_implicit_double_move_of_structure(self):
        source = """
estructura Recurso { id: numero }
sea a: Recurso = Recurso(1)
sea b: Recurso = a
sea c: Recurso = a
"""
        diagnostics = analyze_source(source)
        self.assertTrue(diagnostics)
        self.assertEqual(diagnostics[0].code, "CF3001")
        self.assertIn("uso después de mover", diagnostics[0].message)


if __name__ == "__main__":
    unittest.main()
