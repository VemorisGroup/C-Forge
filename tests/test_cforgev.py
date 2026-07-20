import contextlib
import io
import json
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from pathlib import Path

from cforgev import (
    CForgevError, Interpreter, execute, execute_watch, format_source, repair_source,
    run_repl, run_test_file, tokenize,
)
from compilador_nativo import Parser, compile_native
from compilador_wasm import compile_wasm


class InterpreterTests(unittest.TestCase):
    def output(self, source: str) -> str:
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            Interpreter(tokenize(source)).run()
        return buffer.getvalue()

    def test_variables_math_and_strings(self) -> None:
        source = 'sea nombre = "C-Forgev"; mostrar(nombre); mostrar(2 + 3 * 4);'
        self.assertEqual(self.output(source), "C-Forgev\n14\n")

    def test_division_by_zero_is_explained(self) -> None:
        with self.assertRaisesRegex(CForgevError, "dividir por cero"):
            self.output("mostrar(10 / 0);")

    def test_if_else_and_comparisons(self) -> None:
        source = """
        sea edad = 20;
        si (edad >= 18) { mostrar("adulto"); } sino { mostrar("menor"); }
        si (falso) { mostrar("mal"); } sino { mostrar("bien"); }
        mostrar(3 != 4);
        """
        self.assertEqual(self.output(source), "adulto\nbien\nverdadero\n")

    def test_nested_if(self) -> None:
        source = "si (verdadero) { si (2 < 3) { mostrar(\"anidado\"); } }"
        self.assertEqual(self.output(source), "anidado\n")

    def test_assignment_and_while(self) -> None:
        source = """
        sea numero = 1;
        mientras (numero <= 3) {
            mostrar(numero);
            numero = numero + 1;
        }
        mostrar(numero);
        """
        self.assertEqual(self.output(source), "1\n2\n3\n4\n")

    def test_assignment_without_sea_infers_a_stable_type(self) -> None:
        self.assertEqual(self.output("fantasma = 5; mostrar(fantasma);"), "5\n")
        with self.assertRaisesRegex(CForgevError, "no puede recibir texto"):
            self.output('fantasma = 5; fantasma = "texto";')

    def test_native_compiler_rejects_inferred_type_contradiction(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "tipos.cfv"
            source_path.write_text('dato = 10; dato = "texto";', encoding="utf-8")
            with self.assertRaisesRegex(CForgevError, "Inferencia estática"):
                compile_native(source_path, root / "tipos")

    def test_universal_pip_import(self) -> None:
        self.assertEqual(self.output("import pip:math; mostrar(math.sqrt(81));"), "9\n")

    def test_parallel_map_and_jit_profile(self) -> None:
        source = (
            "funcion cuadrado(n) { retornar n * n; }"
            'mostrar(paralelo("cuadrado", [2, 3, 4]));'
            'mostrar(jit_estado("cuadrado"));'
        )
        self.assertEqual(self.output(source), "[4, 9, 16]\n3\n")

    def test_universal_nuget_import_when_available(self) -> None:
        library = Path("build/csharp-native/CSharpNative.dylib")
        if not library.exists():
            self.skipTest("La biblioteca C# Native AOT no fue publicada")
        source = "import nuget:CSharpNative; mostrar(CSharpNative.csharp_add(20, 22));"
        with tempfile.TemporaryDirectory(dir=Path.cwd()) as directory:
            source_path = Path(directory) / "nuget.cfv"
            output_path = Path(directory) / "nuget"
            source_path.write_text(source, encoding="utf-8")
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                execute(source_path)
            self.assertEqual(buffer.getvalue(), "42\n")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "42\n")

    def test_gpu_block_interpreter_and_native(self) -> None:
        source = (
            "funcion doble(n) { retornar n * 2; }"
            'gpu { mostrar(paralelo("doble", [1, 2, 3, 4])); }'
        )
        self.assertEqual(self.output(source), "[2, 4, 6, 8]\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "gpu.cfv", root / "gpu"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "[2, 4, 6, 8]\n")

    def test_self_healing_repairs_high_confidence_tokens(self) -> None:
        repaired, changes = repair_source('mostar(“hola”);\nsi (verdadero) { mostrar("ok");')
        self.assertIn('mostrar("hola")', repaired)
        self.assertTrue(repaired.rstrip().endswith("}"))
        self.assertGreaterEqual(len(changes), 3)

    def test_repair_missing_file_reports_cforgev_error_without_traceback(self) -> None:
        from cforgev import main
        stderr = io.StringIO()
        with patch("sys.argv", ["cforgev", "--reparar", "archivo_que_no_existe.cfv"]):
            with contextlib.redirect_stderr(stderr):
                status = main()
        self.assertEqual(status, 1)
        self.assertIn("[C-Forgev Runtime Exception]", stderr.getvalue())
        self.assertNotIn("Traceback", stderr.getvalue())

    def test_wasm_backend_generates_valid_module_structure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "web.cfv", root / "web.wat"
            source_path.write_text("x = 10; mostrar(x * 2);", encoding="utf-8")
            compile_wasm(source_path, output_path)
            wat = output_path.read_text(encoding="utf-8")
            self.assertIn('(module', wat)
            self.assertIn('(export "_start")', wat)
            self.assertIn("f64.mul", wat)

    def test_javascript_npm_extern_and_universal_data(self) -> None:
        if not subprocess.run(["node", "--version"], capture_output=True).returncode == 0:
            self.skipTest("Node.js no está instalado")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            module = root / "bridge.js"
            module.write_text(
                "exports.build = (name) => ({name, values: [1, 2, 3], active: true});\n",
                encoding="utf-8",
            )
            source = (
                f'import npm:path; dato = use_javascript({json.dumps(str(module))}, "build", ["CFV"]);'
                'mostrar(path.basename("/tmp/demo.js")); mostrar(dato["name"]);'
                'mostrar(dato["values"][2]);'
                'extern("javascript") { console.log("JS externo"); }'
                'extern("typescript") { const n: number = 21; console.log(n * 2); }'
            )
            interpreted = self.output(source)
            self.assertEqual(interpreted, "demo.js\nCFV\n3\nJS externo\n42\n")
            path, output = root / "polyglot.cfv", root / "polyglot"
            path.write_text(source, encoding="utf-8")
            compile_native(path, output)
            result = subprocess.run([str(output)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, interpreted)

    def test_python_bridge_roundtrips_nested_universal_data(self) -> None:
        source = 'dato = use_python("json", "loads", ["{\\"items\\":[1,true,null]}"]); mostrar(dato["items"]);'
        self.assertEqual(self.output(source), "[1, verdadero, nulo]\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); path, output = root / "data.cfv", root / "data"
            path.write_text(source, encoding="utf-8")
            compile_native(path, output)
            result = subprocess.run([str(output)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "[1, verdadero, nulo]\n")

    def test_maven_syntax_is_registered_even_without_local_jdk(self) -> None:
        program = Parser(tokenize("import maven:demo; importar = 1;")).program()
        self.assertIn("maven", repr(program))
        with self.assertRaisesRegex(CForgevError, "JNI/JDK|Java falló"):
            self.output('mostrar(use_java("demo.jar", "Demo", "sum", [1, 2]));')

    def test_shared_forge_symbols_and_cross_language_members(self) -> None:
        if subprocess.run(["node", "--version"], capture_output=True).returncode:
            self.skipTest("Node.js no está instalado")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            javascript_module = root / "forge_js.js"
            javascript_module.write_text(
                "exports.read = extra => ({answer: ForgeSymbols.base + extra, origin: 'javascript'});\n",
                encoding="utf-8",
            )
            source = (
                'base = 40; py = use_python("cforgev_runtime", "get", ["base"]) + 2;'
                f'js = use_javascript({json.dumps(str(javascript_module))}, "read", [2]);'
                'mostrar(py); mostrar(js.answer); mostrar(js.origin);'
            )
            path, output = root / "shared.cfv", root / "shared"
            path.write_text(source, encoding="utf-8")
            interpreted_buffer = io.StringIO()
            with contextlib.redirect_stdout(interpreted_buffer):
                execute(path)
            expected = "42\n42\njavascript\n"
            self.assertEqual(interpreted_buffer.getvalue(), expected)
            compile_native(path, output)
            result = subprocess.run([str(output)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, expected)

    def test_extern_python_and_cpp_are_literal_and_native(self) -> None:
        source = '''
        extern("python") {
print("Python externo")
        }
        extern("cpp") {
std::string value = "{C++ literal}";
std::cout << value << std::endl;
        }
        '''
        self.assertEqual(self.output(source), "Python externo\n{C++ literal}\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "extern.cfv", root / "extern"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "Python externo\n{C++ literal}\n")

    def test_memory_safety_rejects_manual_native_memory(self) -> None:
        source = 'extern("cpp") { int* data = new int(10); delete data; }'
        with self.assertRaisesRegex(CForgevError, "Memory Safety"):
            self.output(source)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "unsafe.cfv"
            path.write_text(source, encoding="utf-8")
            with self.assertRaisesRegex(CForgevError, "Memory Safety"):
                compile_native(path, Path(directory) / "unsafe")

    def test_cluster_symbol_table_interpreter_and_native(self) -> None:
        source = (
            'cluster version = "1.0";'
            'cluster funcion doble(n) { retornar n * 2; }'
            'mostrar(cluster_estado());'
        )
        expected = "[funcion:doble, variable:version]\n"
        self.assertEqual(self.output(source), expected)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path, output = root / "cluster.cfv", root / "cluster"
            path.write_text(source, encoding="utf-8")
            compile_native(path, output)
            result = subprocess.run([str(output)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, expected)

    def test_formatter_is_clean_and_idempotent(self) -> None:
        damaged = 'si (verdadero) {\nmostrar("ok");   \n}\n\n\n'
        formatted = format_source(damaged)
        self.assertEqual(formatted, 'si (verdadero) {\n    mostrar("ok");\n}\n')
        self.assertEqual(format_source(formatted), formatted)

    def test_test_runner_reports_success_and_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "suite.cfv"
            path.write_text('test "suma" { afirmar(2 + 2 == 4, "suma"); }', encoding="utf-8")
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(run_test_file(path), 1)
            self.assertIn("1 aprobados, 0 fallidos", output.getvalue())
            path.write_text('test "fallo" { afirmar(falso, "controlado"); }', encoding="utf-8")
            with self.assertRaisesRegex(CForgevError, "Test 'fallo' falló"):
                run_test_file(path)

    def test_print_and_use_csharp_aliases_parse(self) -> None:
        self.assertEqual(self.output('print("alias");'), "alias\n")
        program = Parser(tokenize('mostrar(use_csharp("lib.dylib", "sum", [1, 2]));')).program()
        self.assertIn("use_csharp", repr(program))

    def test_hot_reload_preserves_global_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "live.cfv"
            path.write_text("contador = 1; mostrar(contador);", encoding="utf-8")
            def modify() -> None:
                time.sleep(0.05)
                path.write_text("contador = contador + 1; mostrar(contador);", encoding="utf-8")
            worker = threading.Thread(target=modify)
            worker.start()
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                execute_watch(path, interval=0.01, max_reloads=2)
            worker.join()
            self.assertIn("1\n", buffer.getvalue())
            self.assertIn("2\n", buffer.getvalue())

    def test_functions_parameters_and_return(self) -> None:
        source = """
        funcion multiplicar(a, b) { retornar a * b; }
        funcion anunciar(valor) { mostrar(valor); }
        sea resultado = multiplicar(6, 7);
        anunciar(resultado);
        """
        self.assertEqual(self.output(source), "42\n")

    def test_function_checks_argument_count(self) -> None:
        with self.assertRaisesRegex(CForgevError, "requiere 2 argumentos"):
            self.output("funcion sumar(a, b) { retornar a + b; } mostrar(sumar(1));")

    def test_native_compiler_builds_executable(self) -> None:
        source = 'funcion doble(n) { retornar n * 2; } mostrar(doble(21));'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "prueba.cfv"
            output_path = root / "prueba"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run(
                [str(output_path)], capture_output=True, text=True, check=True
            )
            self.assertEqual(result.stdout, "42\n")

    def test_inferred_and_explicit_types_are_safe(self) -> None:
        self.assertEqual(self.output("sea edad: numero = 20; mostrar(edad);"), "20\n")
        with self.assertRaisesRegex(CForgevError, "no puede recibir texto"):
            self.output('sea edad = 20; edad = "veinte";')

    def test_logical_operators(self) -> None:
        source = "mostrar(verdadero y no falso); mostrar(falso o verdadero);"
        self.assertEqual(self.output(source), "verdadero\nverdadero\n")

    def test_conversions(self) -> None:
        self.assertEqual(self.output('mostrar(a_numero("42") + 8);'), "50\n")

    def test_lists_maps_indexing_and_builtins(self) -> None:
        source = """
        sea numeros: lista = [10, 20];
        agregar(numeros, 30);
        sea persona: mapa = {"nombre": "Javier", "edad": 20};
        mostrar(numeros[2]);
        mostrar(persona["nombre"]);
        mostrar(longitud(numeros));
        """
        self.assertEqual(self.output(source), "30\nJavier\n3\n")

    def test_user_input(self) -> None:
        with patch("builtins.input", return_value="Javier"):
            self.assertEqual(self.output('sea nombre = leer("Nombre: "); mostrar(nombre);'), "Javier\n")

    def test_native_collections_logic_and_types(self) -> None:
        source = """
        sea datos: lista = [10, 20];
        agregar(datos, 30);
        sea persona: mapa = {"nombre": "Javier"};
        si (longitud(datos) == 3 y no falso) {
            mostrar(datos[2]);
            mostrar(persona["nombre"]);
        }
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "colecciones.cfv", root / "colecciones"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "30\nJavier\n")

    def test_native_user_input(self) -> None:
        source = 'sea nombre: texto = leer(""); mostrar("Hola " + nombre);'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "entrada.cfv", root / "entrada"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run(
                [str(output_path)], input="Javier\n", capture_output=True, text=True, check=True
            )
            self.assertEqual(result.stdout, "Hola Javier\n")

    def test_structured_error_handling(self) -> None:
        source = 'intentar { mostrar(10 / 0); } capturar(error) { mostrar("capturado"); }'
        self.assertEqual(self.output(source), "capturado\n")

    def test_files_and_local_modules(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "utilidades.cfv").write_text(
                'funcion saludo(nombre) { retornar "Hola " + nombre; }', encoding="utf-8"
            )
            main = root / "principal.cfv"
            main.write_text(
                'usar "utilidades.cfv"; escribir_archivo("dato.txt", saludo("Javier")); '
                'mostrar(leer_archivo("dato.txt")); mostrar(existe_archivo("dato.txt"));',
                encoding="utf-8",
            )
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                execute(main)
            self.assertEqual(buffer.getvalue(), "Hola Javier\nverdadero\n")

    def test_native_system_file_and_fast_math_core(self) -> None:
        source = (
            'info = sys_info(); mostrar(info.nucleos > 0); '
            'proceso = sys_run("printf ForgeCore"); mostrar(proceso.estado); mostrar(proceso.salida); '
            'file_write("dato.txt", "Forge"); file_append("dato.txt", "v"); '
            'mostrar(file_read("dato.txt")); '
            'mostrar(array_fast([1, 2, 3])); mostrar(matrix(2, 2, 4));'
        )
        expected = "verdadero\n0\nForgeCore\nForgev\n[1, 2, 3]\n[[4, 4], [4, 4]]\n"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path = root / "nucleo.cfv"
            output_path = root / "nucleo"
            source_path.write_text(source, encoding="utf-8")
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                execute(source_path)
            self.assertEqual(buffer.getvalue(), expected)
            compile_native(source_path, output_path)
            result = subprocess.run(
                [str(output_path)], cwd=root, capture_output=True, text=True, check=True
            )
            self.assertEqual(result.stdout, expected)

    def test_native_modules_files_and_errors(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "utilidades.cfv").write_text(
                'funcion saludo(nombre) { retornar "Hola " + nombre; }', encoding="utf-8"
            )
            main = root / "principal.cfv"
            main.write_text(
                'usar "utilidades.cfv"; escribir_archivo("dato.txt", saludo("Javier")); '
                'mostrar(leer_archivo("dato.txt")); '
                'intentar { mostrar(leer_archivo("ausente.txt")); } '
                'capturar(error) { mostrar("error capturado"); }',
                encoding="utf-8",
            )
            output = root / "principal"
            compile_native(main, output)
            result = subprocess.run(
                [str(output)], cwd=root, capture_output=True, text=True, check=True
            )
            self.assertEqual(result.stdout, "Hola Javier\nerror capturado\n")

    def test_typed_structures_and_field_access(self) -> None:
        source = """
        estructura Persona { nombre: texto; edad: numero; }
        sea persona: Persona = Persona("Javier", 20);
        mostrar(persona.nombre);
        mostrar(persona.edad);
        """
        self.assertEqual(self.output(source), "Javier\n20\n")
        with self.assertRaisesRegex(CForgevError, "no puede recibir texto"):
            self.output('estructura Persona { edad: numero; } sea p = Persona("veinte");')

    def test_native_typed_structures(self) -> None:
        source = """
        estructura Producto { nombre: texto; precio: numero; }
        sea producto: Producto = Producto("Teclado", 99);
        mostrar(producto.nombre);
        mostrar(producto.precio);
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "estructura.cfv", root / "estructura"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "Teclado\n99\n")

    def test_math_library(self) -> None:
        self.assertEqual(
            self.output("mostrar(raiz(81)); mostrar(potencia(2, 5)); mostrar(absoluto(-7)); mostrar(tiempo_actual() > 0);"),
            "9\n32\n7\nverdadero\n",
        )

    def test_classes_methods_this_and_safe_mutation(self) -> None:
        source = """
        clase Cuenta {
            campo titular: texto;
            campo saldo: numero;
            metodo depositar(cantidad) {
                este.saldo = este.saldo + cantidad;
                retornar este.saldo;
            }
            metodo describir() { retornar este.titular + ": " + a_texto(este.saldo); }
        }
        sea cuenta: Cuenta = Cuenta("Javier", 100);
        cuenta.depositar(50);
        mostrar(cuenta.saldo);
        mostrar(cuenta.describir());
        """
        self.assertEqual(self.output(source), "150\nJavier: 150\n")

    def test_native_classes_methods_and_mutation(self) -> None:
        source = """
        clase Cuenta {
            campo titular: texto;
            campo saldo: numero;
            metodo depositar(cantidad) {
                este.saldo = este.saldo + cantidad;
                retornar este.saldo;
            }
            metodo describir() { retornar este.titular + ": " + a_texto(este.saldo); }
        }
        sea cuenta: Cuenta = Cuenta("Javier", 100);
        mostrar(cuenta.depositar(50));
        mostrar(cuenta.describir());
        """
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "clase.cfv", root / "clase"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "150\nJavier: 150\n")

    def test_program_arguments_in_interpreter_and_native(self) -> None:
        source = "mostrar(argumentos());"
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            Interpreter(tokenize(source), program_arguments=["uno", "dos"]).run()
        self.assertEqual(buffer.getvalue(), "[uno, dos]\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "args.cfv", root / "args"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path), "uno", "dos"], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "[uno, dos]\n")

    def test_native_math_and_time_library(self) -> None:
        source = "mostrar(raiz(81)); mostrar(potencia(2, 5)); mostrar(absoluto(-7)); mostrar(redondear(3.6)); mostrar(tiempo_actual() > 0);"
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "math.cfv", root / "math"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "9\n32\n7\n4\nverdadero\n")

    def test_class_field_mutation_rejects_wrong_type(self) -> None:
        source = 'clase Cuenta { campo saldo: numero; metodo romper() { este.saldo = "mal"; } } sea c = Cuenta(10); c.romper();'
        with self.assertRaisesRegex(CForgevError, "no puede recibir texto"):
            self.output(source)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "tipo.cfv", root / "tipo"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("tipo incompatible", result.stderr)

    def test_real_embedded_python_interop(self) -> None:
        source = (
            'mostrar(use_python("math", "sqrt", [81]));'
            'mostrar(use_python("builtins", "len", ["abc"]));'
            'mostrar(use_python("os.path", "basename", ["/tmp/demo.txt"]));'
        )
        self.assertEqual(self.output(source), "9\n3\ndemo.txt\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "python.cfv", root / "python"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "9\n3\ndemo.txt\n")

    def test_dynamic_library_common_abi(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            suffix = ".dylib" if sys.platform == "darwin" else ".so"
            library_name = "libnative_math" + suffix
            library = root / library_name
            library_flags = ["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"]
            subprocess.run(
                ["clang++", "-std=c++17", *library_flags, "-DCFV_NO_AUTO_REGISTER",
                 "ejemplos/interop/native_math.cpp",
                 "-I", "include", "-o", str(library)], check=True
            )
            source = (
                f'mostrar(use_native("{library_name}", "native_multiply", [6, 7]));'
                f'mostrar(use_native("{library_name}", "native_half", [5.0]));'
                f'mostrar(use_native("{library_name}", "native_greet", ["Javier"]));'
            )
            source_path, output_path = root / "native.cfv", root / "native"
            source_path.write_text(source, encoding="utf-8")
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                execute(source_path)
            self.assertEqual(buffer.getvalue(), "42\n2.5\nHola Javier desde C++\n")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "42\n2.5\nHola Javier desde C++\n")

    def test_linked_cpp_function_registry(self) -> None:
        source = 'mostrar(use_cpp("multiply", [8, 9]));'
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "cpp.cfv", root / "cpp"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path, [Path("ejemplos/interop/native_math.cpp")])
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "72\n")

    def test_csharp_native_aot_common_abi_when_available(self) -> None:
        library = Path("build/csharp-native/CSharpNative.dylib").resolve()
        if not library.exists():
            self.skipTest("La biblioteca C# Native AOT no fue publicada")
        source = (
            f'mostrar(use_native({json.dumps(str(library))}, "csharp_add", [20, 22]));'
            f'mostrar(use_native({json.dumps(str(library))}, "csharp_half", [5.0]));'
            f'mostrar(use_native({json.dumps(str(library))}, "csharp_greet", ["Javier"]));'
            f'intentar {{ mostrar(use_native({json.dumps(str(library))}, "csharp_fail", [])); }}'
            'capturar(error) { mostrar("C# capturado"); }'
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "csharp.cfv", root / "csharp"
            source_path.write_text(source, encoding="utf-8")
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer):
                execute(source_path)
            self.assertEqual(buffer.getvalue(), "42\n2.5\nHola Javier desde C#\nC# capturado\n")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, buffer.getvalue())

    def test_repl_persists_state_handles_multiline_and_recovers(self) -> None:
        lines = iter([
            "sea valor = 10;", "valor + 5", "funcion doble(n) {",
            "retornar n * 2;", "}", "doble(valor)", "10 / 0",
            'mostrar("continua");', "salir",
        ])
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            run_repl(lambda _prompt: next(lines))
        output = buffer.getvalue()
        self.assertIn("15\n", output)
        self.assertIn("20\n", output)
        self.assertIn("[C-Forgev Runtime Exception]", output)
        self.assertTrue(output.endswith("continua\n"))

    def test_repl_universal_import_prints_method_result(self) -> None:
        lines = iter(["import pip:math;", "math.sqrt(225)", "salir"])
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer):
            run_repl(lambda _prompt: next(lines))
        self.assertTrue(buffer.getvalue().endswith("15\n"))

    def test_python_exception_is_captured_in_interpreter_and_native(self) -> None:
        source = (
            'intentar { mostrar(use_python("math", "sqrt", [-1])); } '
            'capturar(error) { mostrar("python capturado: " + error); }'
        )
        interpreted = self.output(source)
        self.assertIn("math domain error", interpreted)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "python_error.cfv", root / "python_error"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path)
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertIn("math domain error", result.stdout)

    def test_native_owned_result_is_released_exactly_once(self) -> None:
        source = (
            'mostrar(use_cpp("owned_greet", ["Javier"]));'
            'mostrar(use_cpp("release_count", []));'
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, output_path = root / "raii.cfv", root / "raii"
            source_path.write_text(source, encoding="utf-8")
            compile_native(source_path, output_path, [Path("ejemplos/interop/native_math.cpp")])
            result = subprocess.run([str(output_path)], capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, "Texto RAII para Javier\n1\n")


if __name__ == "__main__":
    unittest.main()
