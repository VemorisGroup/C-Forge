// C-Forge 1.4.0 Definitive — distribución monolítica generada.
// Fuente reproducible: herramientas/generar_amalgama.py

#include <Python.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cforgev {

class PyOwned final {
public:
    explicit PyOwned(PyObject* value = nullptr) noexcept : value_(value) {}
    ~PyOwned() { Py_XDECREF(value_); }
    PyOwned(const PyOwned&) = delete;
    PyOwned& operator=(const PyOwned&) = delete;
    PyOwned(PyOwned&& other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    PyObject* get() const noexcept { return value_; }
    explicit operator bool() const noexcept { return value_ != nullptr; }
private:
    PyObject* value_;
};

class PythonRuntime final {
public:
    PythonRuntime() {
        Py_DontWriteBytecodeFlag = 1;
        Py_Initialize();
        if (!Py_IsInitialized()) throw std::runtime_error("no se pudo inicializar CPython");
    }
    ~PythonRuntime() { if (Py_IsInitialized()) Py_Finalize(); }
    PythonRuntime(const PythonRuntime&) = delete;
    PythonRuntime& operator=(const PythonRuntime&) = delete;
};

class TemporaryWorkspace final {
public:
    TemporaryWorkspace() {
        auto base = std::filesystem::temp_directory_path();
        auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        for (int attempt = 0; attempt < 100; ++attempt) {
            path_ = base / ("cforgev-master-" + std::to_string(stamp) + "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(path_, error)) return;
        }
        throw std::runtime_error("no se pudo crear el espacio temporal RAII");
    }
    ~TemporaryWorkspace() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
};

const std::map<std::string, std::string>& embedded_resources() {
    static const std::map<std::string, std::string> resources = {
        {R"CFV0DATA(cforgev.py)CFV0DATA", R"CFV1DATA(#!/usr/bin/env python3
"""Primer intérprete del lenguaje C-Forge."""

from __future__ import annotations

import argparse
import builtins
import concurrent.futures
import difflib
import hashlib
import importlib
import json
import math
import os
import platform
import re
import shutil
import socket
import subprocess
import sys
import textwrap
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field as dc_field
from pathlib import Path

VERSION = "1.4.0-definitive"


class CForgevError(Exception):
    pass


class ReturnSignal(Exception):
    def __init__(self, value: object) -> None:
        self.value = value


@dataclass(frozen=True)
class Function:
    name: str
    parameters: list[str]
    body: list[Token]


@dataclass(frozen=True)
class Structure:
    name: str
    fields: list[tuple[str, str]]
    methods: dict[str, Function] = dc_field(default_factory=dict)


class StructureValue(dict[str, object]):
    def __init__(self, structure_name: str, values: dict[str, object]) -> None:
        super().__init__(values)
        self.structure_name = structure_name


@dataclass(frozen=True)
class UniversalModule:
    ecosystem: str
    package: str


@dataclass(frozen=True)
class Token:
    kind: str
    value: str
    line: int


@dataclass(frozen=True)
class SystemDependency:
    public_name: str
    detected_modules: tuple[str, ...]
    command: tuple[str, ...]


def _gui_dependency() -> SystemDependency | None:
    """Devuelve una receta fija y auditable para la interfaz gráfica."""
    if sys.platform == "darwin":
        version = f"{sys.version_info.major}.{sys.version_info.minor}"
        return SystemDependency(
            ".cfv-gui", ("tkinter", "_tkinter"),
            ("brew", "install", f"python-tk@{version}"),
        )
    if sys.platform.startswith("linux"):
        prefix = () if hasattr(os, "geteuid") and os.geteuid() == 0 else ("sudo",)
        return SystemDependency(
            ".cfv-gui", ("tkinter", "_tkinter"),
            prefix + ("apt-get", "install", "-y", "python3-tk"),
        )
    return None


def dependency_for_error(error: BaseException) -> SystemDependency | None:
    """Mapea errores conocidos sin interpretar comandos provenientes del script."""
    missing = getattr(error, "name", None)
    message = str(error).lower()
    dependency = _gui_dependency()
    if dependency and (
        missing in dependency.detected_modules
        or any(module.lower() in message for module in dependency.detected_modules)
    ):
        return dependency
    return None


_INTERNAL_INSTALL_RE = re.compile(
    r"(?im)^.*(?:brew\s+install|pip\s+install|npm\s+install|apt(?:-get)?\s+install).*$"
)


def branded_process_output(output: str) -> str:
    """Elimina ruido de gestores externos sin ocultar errores no relacionados."""
    cleaned = _INTERNAL_INSTALL_RE.sub(
        "[C-Forge Package Manager] Configurando dependencias del núcleo para entorno .cfv...",
        output,
    )
    lines = [line.strip() for line in cleaned.splitlines() if line.strip()]
    return "\n".join(lines[-8:])


def ensure_system_dependency(
    dependency: SystemDependency,
    input_fn=input,
    runner=subprocess.run,
) -> bool:
    """Solicita consentimiento y ejecuta solo recetas internas predefinidas."""
    print(f"[C-Forge] Para usar esta función, se requiere el módulo del sistema {dependency.public_name}.")
    print("Componente del sistema que se instalará:")
    print("  " + " ".join(dependency.command))
    if not sys.stdin.isatty() and os.environ.get("CFORGE_ASSUME_YES") != "1":
        print("[C-Forge] Instalación cancelada: se requiere una terminal interactiva.")
        return False
    answer = "S" if os.environ.get("CFORGE_ASSUME_YES") == "1" else input_fn(
        "¿Deseas instalarlo automáticamente ahora? (S/N): "
    )
    if answer.strip().lower() not in {"s", "si", "sí", "y", "yes"}:
        print("[C-Forge] Instalación cancelada por el usuario.")
        return False
    if not shutil.which(dependency.command[0]):
        print(
            f"[C-Forge Package Manager] No está disponible "
            f"'{dependency.command[0]}' en este sistema."
        )
        return False
    print("[C-Forge Package Manager] Configurando dependencias del núcleo para entorno .cfv...")
    try:
        completed = runner(
            list(dependency.command),
            capture_output=True,
            text=True,
            check=False,
        )
    except OSError as install_error:
        print(f"[C-Forge Package Manager] No se pudo iniciar la instalación: {install_error}")
        return False
    if completed.returncode == 0:
        importlib.invalidate_caches()
        print("[C-Forge Package Manager] Progreso: [████████████████████] 100%")
        print(f"[C-Forge Package Manager] {dependency.public_name} quedó disponible.")
        return True
    details = branded_process_output((completed.stdout or "") + "\n" + (completed.stderr or ""))
    print(f"[C-Forge Package Manager] La instalación terminó con código {completed.returncode}.")
    if details:
        print(details)
    return False


TOKEN_RE = re.compile(
    r"(?P<SPACE>[ \t\r]+)|(?P<COMMENT>//[^\n]*)|(?P<NEWLINE>\n)|"
    r'(?P<STRING>"(?:\\.|[^"\\])*")|(?P<NUMBER>\d+(?:\.\d+)?)|'
    r"(?P<IDENT>[A-Za-z_][A-Za-z0-9_]*)|(?P<OP>==|!=|>=|<=|[+\-*/=(),;.:{}<>\[\]])|(?P<BAD>.)"
)


def tokenize(source: str) -> list[Token]:
    tokens: list[Token] = []
    line = 1
    position = 0
    extern_header = re.compile(
        r'extern\s*\(\s*"(python|cpp|javascript|typescript|java)"\s*\)\s*\{'
    )
    while position < len(source):
        header = extern_header.match(source, position)
        if header:
            start_line = line
            body_start = header.end()
            cursor, depth = body_start, 1
            quote: str | None = None
            escaped = False
            while cursor < len(source) and depth:
                character = source[cursor]
                if quote:
                    if escaped:
                        escaped = False
                    elif character == "\\":
                        escaped = True
                    elif character == quote:
                        quote = None
                elif character in {'"', "'"}:
                    quote = character
                elif character == "{":
                    depth += 1
                elif character == "}":
                    depth -= 1
                cursor += 1
            if depth:
                raise CForgevError(f"Línea {start_line}: bloque extern sin '}}'")
            body = source[body_start:cursor - 1]
            language = header.group(1)
            tokens.extend([
                Token("IDENT", "extern", start_line), Token("OP", "(", start_line),
                Token("STRING", json.dumps(language), start_line), Token("OP", ")", start_line),
                Token("OP", "{", start_line), Token("FOREIGN", body, start_line),
                Token("OP", "}", start_line),
            ])
            consumed = source[position:cursor]
            line += consumed.count("\n")
            position = cursor
            continue
        match = TOKEN_RE.match(source, position)
        if match is None:
            raise CForgevError(f"Línea {line}: no se pudo tokenizar el código")
        kind, value = match.lastgroup, match.group()
        position = match.end()
        if kind in {"SPACE", "COMMENT"}:
            continue
        if kind == "NEWLINE":
            line += 1
            continue
        if kind == "BAD":
            raise CForgevError(f"Línea {line}: símbolo desconocido {value!r}")
        tokens.append(Token(kind or "BAD", value, line))
    tokens.append(Token("EOF", "", line))
    return tokens


class Interpreter:
    MAX_LOOP_ITERATIONS = 1_000_000

    def __init__(
        self,
        tokens: list[Token],
        variables: dict[str, object] | None = None,
        functions: dict[str, Function] | None = None,
        variable_types: dict[str, str] | None = None,
        base_dir: Path | None = None,
        imported_modules: set[Path] | None = None,
        structures: dict[str, Structure] | None = None,
        program_arguments: list[str] | None = None,
        jit_counts: dict[str, int] | None = None,
        cluster_symbols: dict[str, str] | None = None,
        test_results: list[str] | None = None,
    ) -> None:
        self.tokens = tokens
        self.current = 0
        self.variables = variables if variables is not None else {}
        self.functions = functions if functions is not None else {}
        self.variable_types = variable_types if variable_types is not None else {}
        self.base_dir = (base_dir or Path.cwd()).resolve()
        self.imported_modules = imported_modules if imported_modules is not None else set()
        self.structures = structures if structures is not None else {}
        self.program_arguments = program_arguments if program_arguments is not None else []
        self.jit_counts = jit_counts if jit_counts is not None else {}
        self.cluster_symbols = cluster_symbols if cluster_symbols is not None else {}
        self.cluster_mode = False
        self.test_results = test_results

    def run(self) -> None:
        while not self.check("EOF"):
            self.statement()

    def statement(self) -> None:
        if self.match_ident("cluster"):
            previous = self.cluster_mode
            self.cluster_mode = True
            before = len(self.cluster_symbols)
            inferred_name = (
                self.peek().value if self.check("IDENT") and self.peek_next().value == "=" else None
            )
            try:
                self.statement()
            finally:
                self.cluster_mode = previous
            if inferred_name is not None:
                self.cluster_symbols[inferred_name] = "variable"
            if len(self.cluster_symbols) == before:
                raise CForgevError("'cluster' solo puede modificar variables o funciones")
            return
        if self.match_ident("extern"):
            self.extern_statement()
            return
        if self.match_ident("test"):
            self.test_statement()
            return
        if self.match_ident("clase"):
            self.class_declaration()
            return
        if self.match_ident("estructura"):
            self.structure_declaration()
            return
        if self.match_ident("usar"):
            self.import_statement()
            return
        if self.match_ident("import"):
            self.universal_import_statement()
            return
        if self.match_ident("gpu"):
            self.gpu_statement()
            return
        if self.match_ident("intentar"):
            self.try_statement()
            return
        if self.match_ident("funcion"):
            self.function_declaration()
            return
        if self.match_ident("retornar"):
            value = self.expression()
            self.optional_semicolon()
            raise ReturnSignal(value)
        if self.match_ident("si"):
            self.if_statement()
            return
        if self.match_ident("mientras"):
            self.while_statement()
            return
        if self.match_ident("sea"):
            name = self.consume("IDENT", "Se esperaba el nombre de la variable")
            declared_type: str | None = None
            if self.match_value(":"):
                declared_type = self.consume("IDENT", "Se esperaba un tipo").value
                if declared_type not in {"numero", "texto", "booleano", "lista", "mapa", "nulo", "cualquiera"} and declared_type not in self.structures:
                    raise CForgevError(f"Línea {name.line}: tipo desconocido '{declared_type}'")
            self.consume_value("=", "Se esperaba '='")
            value = self.expression()
            actual_type = value_type(value)
            expected_type = declared_type or actual_type
            ensure_type(name.value, expected_type, value, name.line)
            self.variables[name.value] = value
            self.variable_types[name.value] = expected_type
            if self.cluster_mode:
                self.cluster_symbols[name.value] = "variable"
            self.optional_semicolon()
            return
        if self.match_ident("mostrar") or self.match_ident("print"):
            self.consume_value("(", "Se esperaba '('")
            value = self.expression()
            self.consume_value(")", "Se esperaba ')'")
            self.optional_semicolon()
            print(format_value(value))
            return
        if (
            self.check("IDENT") and self.peek_next().value == "."
            and self.current + 3 < len(self.tokens) and self.tokens[self.current + 3].value == "="
        ):
            owner = self.advance()
            self.advance()
            if owner.value != "este":
                raise CForgevError(
                    f"Línea {owner.line}: los campos solo pueden modificarse desde métodos mediante 'este'"
                )
            field_token = self.consume("IDENT", "Se esperaba el campo")
            self.advance()
            instance = self.variables.get(owner.value)
            if not isinstance(instance, StructureValue) or field_token.value not in instance:
                raise CForgevError(f"Línea {field_token.line}: campo desconocido '{field_token.value}'")
            structure = self.structures[instance.structure_name]
            expected = dict(structure.fields)[field_token.value]
            value = self.expression()
            ensure_type(field_token.value, expected, value, field_token.line)
            instance[field_token.value] = value
            self.optional_semicolon()
            return
        if self.check("IDENT") and self.peek_next().value == "=":
            name = self.advance()
            self.advance()
            value = self.expression()
            if name.value not in self.variables:
                self.variables[name.value] = value
                self.variable_types[name.value] = value_type(value)
                self.optional_semicolon()
                return
            ensure_type(
                name.value,
                self.variable_types.get(name.value, value_type(self.variables[name.value])),
                value,
                name.line,
            )
            self.variables[name.value] = value
            self.optional_semicolon()
            return
        if self.check("IDENT") and self.peek_next().value in {"(", "."}:
            self.expression()
            self.optional_semicolon()
            return
        token = self.peek()
        raise CForgevError(
            f"Línea {token.line}: instrucción desconocida {token.value!r}"
        )

    def structure_declaration(self) -> None:
        name = self.consume("IDENT", "Se esperaba el nombre de la estructura")
        self.consume_value("{", "Se esperaba '{'")
        fields: list[tuple[str, str]] = []
        while self.peek().value != "}" and not self.check("EOF"):
            field = self.consume("IDENT", "Se esperaba el nombre del campo")
            self.consume_value(":", "Se esperaba ':'")
            field_type = self.consume("IDENT", "Se esperaba el tipo del campo")
            if field_type.value not in {
                "numero", "texto", "booleano", "lista", "mapa", "nulo", "cualquiera"
            } and field_type.value not in self.structures:
                raise CForgevError(f"Línea {field_type.line}: tipo desconocido '{field_type.value}'")
            if any(existing == field.value for existing, _ in fields):
                raise CForgevError(f"Línea {field.line}: campo repetido '{field.value}'")
            fields.append((field.value, field_type.value))
            self.optional_semicolon()
        self.consume_value("}", "Falta '}' para cerrar la estructura")
        self.structures[name.value] = Structure(name.value, fields)

    def class_declaration(self) -> None:
        name = self.consume("IDENT", "Se esperaba el nombre de la clase")
        self.consume_value("{", "Se esperaba '{'")
        fields: list[tuple[str, str]] = []
        methods: dict[str, Function] = {}
        while self.peek().value != "}" and not self.check("EOF"):
            if self.match_ident("campo"):
                field_token = self.consume("IDENT", "Se esperaba el nombre del campo")
                self.consume_value(":", "Se esperaba ':'")
                field_type = self.consume("IDENT", "Se esperaba el tipo del campo")
                fields.append((field_token.value, field_type.value))
                self.optional_semicolon()
                continue
            if self.match_ident("metodo"):
                method_name = self.consume("IDENT", "Se esperaba el nombre del método")
                self.consume_value("(", "Se esperaba '('")
                parameters: list[str] = []
                if self.peek().value != ")":
                    while True:
                        parameters.append(self.consume("IDENT", "Se esperaba un parámetro").value)
                        if not self.match_value(","):
                            break
                self.consume_value(")", "Se esperaba ')'")
                methods[method_name.value] = Function(method_name.value, parameters, self.block())
                continue
            raise CForgevError(f"Línea {self.peek().line}: se esperaba 'campo' o 'metodo'")
        self.consume_value("}", "Falta '}' para cerrar la clase")
        self.structures[name.value] = Structure(name.value, fields, methods)

    def import_statement(self) -> None:
        path_token = self.consume("STRING", "Se esperaba la ruta del módulo")
        self.optional_semicolon()
        relative = Path(json.loads(path_token.value))
        if relative.suffix != ".cfv":
            raise CForgevError(f"Línea {path_token.line}: el módulo debe terminar en .cfv")
        module_path = (self.base_dir / relative).resolve()
        if module_path in self.imported_modules:
            return
        try:
            source = module_path.read_text(encoding="utf-8")
        except OSError as error:
            raise CForgevError(
                f"Línea {path_token.line}: no se pudo importar '{relative}': {error}"
            ) from error
        self.imported_modules.add(module_path)
        Interpreter(
            tokenize(source), self.variables, self.functions, self.variable_types,
            module_path.parent, self.imported_modules, self.structures, self.program_arguments
        ).run()

    def universal_import_statement(self) -> None:
        ecosystem = self.consume("IDENT", "Se esperaba 'pip' o 'nuget'")
        if ecosystem.value not in {"pip", "nuget", "npm", "maven"}:
            raise CForgevError(f"Línea {ecosystem.line}: ecosistema desconocido '{ecosystem.value}'")
        self.consume_value(":", "Se esperaba ':' después del ecosistema")
        package = self.consume("IDENT", "Se esperaba el nombre del paquete")
        self.optional_semicolon()
        if ecosystem.value == "pip":
            try:
                importlib.import_module(package.value)
            except Exception as error:
                raise CForgevError(
                    f"Línea {package.line}: no se pudo importar pip:{package.value}: {error}"
                ) from error
        self.variables[package.value] = UniversalModule(ecosystem.value, package.value)
        self.variable_types[package.value] = "modulo"

    def extern_statement(self) -> None:
        self.consume_value("(", "Se esperaba '(' después de extern")
        language_token = self.consume("STRING", "Se esperaba 'python' o 'cpp'")
        language = json.loads(language_token.value)
        self.consume_value(")", "Se esperaba ')' después del lenguaje")
        self.consume_value("{", "Se esperaba '{' para abrir extern")
        body = self.consume("FOREIGN", "Se esperaba código extranjero literal")
        self.consume_value("}", "Se esperaba '}' para cerrar extern")
        validate_foreign_memory(language, body.value, body.line)
        if language == "python":
            namespace = {"__name__": "__cforgev_extern__", **self.variables}
            code = textwrap.dedent(body.value).strip("\n") + "\n"
            try:
                exec(compile(code, "<extern python>", "exec"), namespace, namespace)
            except Exception as error:
                dependency = dependency_for_error(error)
                if not dependency or not ensure_system_dependency(dependency):
                    if dependency:
                        raise CForgevError(
                            f"Línea {body.line}: falta el módulo C-Forge {dependency.public_name}"
                        ) from error
                    raise CForgevError(f"Línea {body.line}: extern Python falló: {error}") from error
                try:
                    exec(compile(code, "<extern python>", "exec"), namespace, namespace)
                except Exception as retry_error:
                    raise CForgevError(
                        f"Línea {body.line}: {dependency.public_name} fue instalado, "
                        "pero el proceso actual debe reiniciarse"
                    ) from retry_error
            for key, value in namespace.items():
                if not key.startswith("__") and is_universal_data(value):
                    self.variables[key] = value
                    self.variable_types[key] = value_type(value)
            return
        if language in {"javascript", "typescript"}:
            suffix = ".ts" if language == "typescript" else ".js"
            command = ["node"]
            import tempfile
            with tempfile.TemporaryDirectory() as directory:
                script = Path(directory) / ("extern" + suffix)
                script.write_text(textwrap.dedent(body.value), encoding="utf-8")
                command.append(str(script))
                invoked = subprocess.run(command, capture_output=True, text=True)
            if invoked.returncode:
                raise CForgevError(
                    f"Línea {body.line}: extern {language} falló: {invoked.stderr.strip()}"
                )
            print(invoked.stdout, end="")
            return
        if language == "java":
            self.execute_external_java(body.value, body.line)
            return
        if language != "cpp":
            raise CForgevError(f"Línea {language_token.line}: lenguaje extern desconocido")
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, executable = root / "extern.cpp", root / "extern"
            source_path.write_text(
                "#include <iostream>\n#include <string>\nint main(){\n" + body.value + "\n}\n",
                encoding="utf-8",
            )
            built = subprocess.run(
                ["clang++", "-std=c++17", str(source_path), "-o", str(executable)],
                capture_output=True, text=True,
            )
            if built.returncode:
                raise CForgevError(f"Línea {body.line}: extern C++ no compiló: {built.stderr}")
            invoked = subprocess.run([str(executable)], capture_output=True, text=True)
            if invoked.returncode:
                raise CForgevError(f"Línea {body.line}: extern C++ falló: {invoked.stderr}")
            print(invoked.stdout, end="")

    def execute_external_java(self, body: str, line: int) -> None:
        import tempfile
        source = (
            "public final class CForgevExtern { public static void main(String[] args) throws Exception {\n"
            + textwrap.dedent(body) + "\n} }\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            java_file = root / "CForgevExtern.java"
            java_file.write_text(source, encoding="utf-8")
            compiled = subprocess.run(["javac", str(java_file)], capture_output=True, text=True)
            if compiled.returncode:
                raise CForgevError(f"Línea {line}: extern Java no compiló: {compiled.stderr.strip()}")
            invoked = subprocess.run(
                ["java", "-cp", str(root), "CForgevExtern"], capture_output=True, text=True
            )
            if invoked.returncode:
                raise CForgevError(f"Línea {line}: extern Java falló: {invoked.stderr.strip()}")
            print(invoked.stdout, end="")

    def test_statement(self) -> None:
        name_token = self.advance()
        if name_token.kind == "STRING":
            name = json.loads(name_token.value)
        elif name_token.kind == "IDENT":
            name = name_token.value
        else:
            raise CForgevError(f"Línea {name_token.line}: test requiere un nombre")
        body = self.block()
        try:
            Interpreter(
                body, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures,
                self.program_arguments, self.jit_counts, self.cluster_symbols,
                self.test_results,
            ).run()
        except CForgevError as error:
            raise CForgevError(f"Test '{name}' falló: {error}") from error
        if self.test_results is not None:
            self.test_results.append(name)
            print(f"[OK] {name}")

    def gpu_statement(self) -> None:
        body = self.block()
        def execute_isolated() -> None:
            Interpreter(
                body, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures,
                self.program_arguments, self.jit_counts, self.cluster_symbols,
            ).run()
        # Backend CPU funcional. La misma frontera permite sustituirlo por Metal/CUDA.
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            executor.submit(execute_isolated).result()

    def try_statement(self) -> None:
        protected = self.block()
        if not self.match_ident("capturar"):
            raise CForgevError(f"Línea {self.peek().line}: se esperaba 'capturar'")
        self.consume_value("(", "Se esperaba '('")
        error_name = self.consume("IDENT", "Se esperaba el nombre para el error")
        self.consume_value(")", "Se esperaba ')'")
        handler = self.block()
        try:
            Interpreter(
                protected, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments
            ).run()
        except CForgevError as error:
            self.variables[error_name.value] = str(error)
            self.variable_types[error_name.value] = "texto"
            Interpreter(
                handler, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments
            ).run()

    def function_declaration(self) -> None:
        name = self.consume("IDENT", "Se esperaba el nombre de la función")
        self.consume_value("(", "Se esperaba '(' después del nombre de la función")
        parameters: list[str] = []
        if self.peek().value != ")":
            while True:
                parameter = self.consume("IDENT", "Se esperaba el nombre de un parámetro")
                if parameter.value in parameters:
                    raise CForgevError(
                        f"Línea {parameter.line}: parámetro repetido '{parameter.value}'"
                    )
                parameters.append(parameter.value)
                if not self.match_value(","):
                    break
        self.consume_value(")", "Se esperaba ')' después de los parámetros")
        body = self.block()
        self.functions[name.value] = Function(name.value, parameters, body)
        if self.cluster_mode:
            self.cluster_symbols[name.value] = "funcion"

    def while_statement(self) -> None:
        condition_tokens = self.parenthesized("después de 'mientras'")
        body = self.block()
        iterations = 0
        while True:
            condition = Interpreter(
                condition_tokens, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments
            ).evaluate_only()
            if not isinstance(condition, bool):
                line = condition_tokens[0].line if condition_tokens else self.peek().line
                raise CForgevError(
                    f"Línea {line}: la condición de 'mientras' debe ser verdadera o falsa"
                )
            if not condition:
                break
            Interpreter(
                body, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments
            ).run()
            iterations += 1
            if iterations >= self.MAX_LOOP_ITERATIONS:
                raise CForgevError(
                    "El ciclo superó 1.000.000 de repeticiones; posiblemente es infinito"
                )

    def evaluate_only(self) -> object:
        value = self.expression()
        if not self.check("EOF"):
            raise CForgevError(f"Línea {self.peek().line}: expresión inválida")
        return value

    def parenthesized(self, context: str) -> list[Token]:
        opening = self.consume_value("(", f"Se esperaba '(' {context}")
        start = self.current
        depth = 1
        while depth > 0 and not self.check("EOF"):
            token = self.advance()
            if token.value == "(":
                depth += 1
            elif token.value == ")":
                depth -= 1
        if depth != 0:
            raise CForgevError(f"Línea {opening.line}: falta ')' para cerrar la expresión")
        expression = self.tokens[start : self.current - 1]
        return [*expression, Token("EOF", "", opening.line)]

    def if_statement(self) -> None:
        self.consume_value("(", "Se esperaba '(' después de 'si'")
        condition = self.expression()
        self.consume_value(")", "Se esperaba ')' después de la condición")
        if not isinstance(condition, bool):
            raise CForgevError(
                f"Línea {self.previous().line}: la condición de 'si' debe ser verdadera o falsa"
            )
        true_branch = self.block()
        false_branch: list[Token] | None = None
        if self.match_ident("sino"):
            false_branch = self.block()
        selected = true_branch if condition else false_branch
        if selected is not None:
            Interpreter(
                selected, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments
            ).run()

    def block(self) -> list[Token]:
        opening = self.consume_value("{", "Se esperaba '{' para abrir el bloque")
        start = self.current
        depth = 1
        while depth > 0 and not self.check("EOF"):
            token = self.advance()
            if token.value == "{":
                depth += 1
            elif token.value == "}":
                depth -= 1
        if depth != 0:
            raise CForgevError(f"Línea {opening.line}: falta '}}' para cerrar el bloque")
        body = self.tokens[start : self.current - 1]
        end_line = body[-1].line if body else opening.line
        return [*body, Token("EOF", "", end_line)]

    def expression(self) -> object:
        return self.logical_or()

    def logical_or(self) -> object:
        value = self.logical_and()
        while self.match_ident("o"):
            right = self.logical_and()
            value = require_bool(value, self.previous().line) or require_bool(
                right, self.previous().line
            )
        return value

    def logical_and(self) -> object:
        value = self.equality()
        while self.match_ident("y"):
            right = self.equality()
            value = require_bool(value, self.previous().line) and require_bool(
                right, self.previous().line
            )
        return value

    def equality(self) -> object:
        value = self.comparison()
        while self.match_value("==", "!="):
            op = self.previous().value
            right = self.comparison()
            value = value == right if op == "==" else value != right
        return value

    def comparison(self) -> object:
        value = self.addition()
        while self.match_value(">", ">=", "<", "<="):
            op = self.previous()
            right = self.addition()
            numeric_pair = (
                isinstance(value, (int, float)) and not isinstance(value, bool)
                and isinstance(right, (int, float)) and not isinstance(right, bool)
            )
            text_pair = isinstance(value, str) and isinstance(right, str)
            if not (numeric_pair or text_pair):
                raise CForgevError(
                    f"Línea {op.line}: '{op.value}' requiere valores comparables del mismo tipo"
                )
            if op.value == ">":
                value = value > right
            elif op.value == ">=":
                value = value >= right
            elif op.value == "<":
                value = value < right
            else:
                value = value <= right
        return value

    def addition(self) -> object:
        value = self.term()
        while self.match_value("+", "-"):
            op = self.previous()
            right = self.term()
            value = calculate(value, op, right)
        return value

    def term(self) -> object:
        value = self.unary()
        while self.match_value("*", "/"):
            op = self.previous()
            right = self.unary()
            value = calculate(value, op, right)
        return value

    def unary(self) -> object:
        if self.match_ident("no"):
            token = self.previous()
            return not require_bool(self.unary(), token.line)
        if self.match_value("-"):
            value = self.unary()
            if not isinstance(value, (int, float)):
                raise CForgevError(f"Línea {self.previous().line}: '-' requiere un número")
            return -value
        return self.primary()

    def primary(self) -> object:
        value = self.atom()
        while self.match_value("["):
            key = self.expression()
            bracket = self.consume_value("]", "Se esperaba ']' después del índice")
            try:
                if isinstance(value, list):
                    if not isinstance(key, int) or isinstance(key, bool):
                        raise CForgevError(f"Línea {bracket.line}: el índice de lista debe ser entero")
                    value = value[key]
                elif isinstance(value, dict):
                    if not isinstance(key, str):
                        raise CForgevError(f"Línea {bracket.line}: la clave del mapa debe ser texto")
                    value = value[key]
                else:
                    raise CForgevError(f"Línea {bracket.line}: este valor no admite índices")
            except (IndexError, KeyError) as error:
                raise CForgevError(f"Línea {bracket.line}: índice o clave inexistente") from error
        while self.match_value("."):
            field = self.consume("IDENT", "Se esperaba el nombre del campo")
            if self.match_value("("):
                value = self.call_method(value, field)
                continue
            if not isinstance(value, dict) or field.value not in value:
                raise CForgevError(f"Línea {field.line}: campo desconocido '{field.value}'")
            value = value[field.value]
        return value

    def call_method(self, instance: object, name: Token) -> object:
        arguments: list[object] = []
        if self.peek().value != ")":
            while True:
                arguments.append(self.expression())
                if not self.match_value(","):
                    break
        self.consume_value(")", "Se esperaba ')' después de los argumentos")
        if isinstance(instance, UniversalModule):
            if instance.ecosystem == "pip":
                try:
                    function = getattr(importlib.import_module(instance.package), name.value)
                    result = function(*arguments)
                except Exception as error:
                    raise CForgevError(
                        f"Línea {name.line}: pip:{instance.package}.{name.value} falló: {error}"
                    ) from error
                if result is not None and not isinstance(result, (int, float, str, bool)):
                    raise CForgevError(f"Línea {name.line}: pip devolvió un tipo no compatible")
                return result
            if instance.ecosystem == "npm":
                return self.invoke_javascript(instance.package, name.value, arguments, name.line)
            if instance.ecosystem == "maven":
                if name.value != "call" or len(arguments) != 3:
                    raise CForgevError(
                        f"Línea {name.line}: maven usa paquete.call(clase, método, argumentos)"
                    )
                jar = self.base_dir / "build" / "maven" / f"{instance.package}.jar"
                return self.invoke_java(str(jar), arguments[0], arguments[1], arguments[2], name.line)
            candidates = [
                self.base_dir / f"{instance.package}.dylib",
                self.base_dir / "build" / f"{instance.package}.dylib",
                self.base_dir.parent / "build" / instance.package / f"{instance.package}.dylib",
                self.base_dir.parent / "build" / "csharp-native" / f"{instance.package}.dylib",
            ]
            library = next((candidate for candidate in candidates if candidate.exists()), candidates[0])
            return self.invoke_dynamic_library(str(library), name.value, arguments, name.line)
        if not isinstance(instance, StructureValue):
            raise CForgevError(f"Línea {name.line}: el valor no posee métodos")
        structure = self.structures[instance.structure_name]
        method = structure.methods.get(name.value)
        if method is None:
            raise CForgevError(f"Línea {name.line}: método desconocido '{name.value}'")
        if len(arguments) != len(method.parameters):
            raise CForgevError(f"Línea {name.line}: cantidad incorrecta de argumentos")
        variables = {"este": instance, **dict(zip(method.parameters, arguments))}
        types = {key: value_type(value) for key, value in variables.items()}
        interpreter = Interpreter(
            method.body, variables, self.functions, types, self.base_dir,
            self.imported_modules, self.structures, self.program_arguments
        )
        try:
            interpreter.run()
        except ReturnSignal as signal:
            return signal.value
        return None

    def atom(self) -> object:
        if self.match("NUMBER"):
            text = self.previous().value
            return float(text) if "." in text else int(text)
        if self.match("STRING"):
            return json.loads(self.previous().value)
        if self.match_ident("verdadero"):
            return True
        if self.match_ident("falso"):
            return False
        if self.match_ident("nulo"):
            return None
        if self.match_value("["):
            values: list[object] = []
            if self.peek().value != "]":
                while True:
                    values.append(self.expression())
                    if not self.match_value(","):
                        break
            self.consume_value("]", "Se esperaba ']' para cerrar la lista")
            return values
        if self.match_value("{"):
            values: dict[str, object] = {}
            if self.peek().value != "}":
                while True:
                    key = self.expression()
                    if not isinstance(key, str):
                        raise CForgevError(f"Línea {self.previous().line}: la clave debe ser texto")
                    self.consume_value(":", "Se esperaba ':' después de la clave")
                    values[key] = self.expression()
                    if not self.match_value(","):
                        break
            self.consume_value("}", "Se esperaba '}' para cerrar el mapa")
            return values
        if self.match("IDENT"):
            token = self.previous()
            if self.match_value("("):
                return self.call_function(token)
            if token.value not in self.variables:
                raise CForgevError(f"Línea {token.line}: variable desconocida '{token.value}'")
            return self.variables[token.value]
        if self.match_value("("):
            value = self.expression()
            self.consume_value(")", "Se esperaba ')'")
            return value
        token = self.peek()
        raise CForgevError(f"Línea {token.line}: expresión inválida cerca de {token.value!r}")

    def call_function(self, name: Token) -> object:
        arguments: list[object] = []
        if self.peek().value != ")":
            while True:
                arguments.append(self.expression())
                if not self.match_value(","):
                    break
        self.consume_value(")", "Se esperaba ')' después de los argumentos")
        if name.value == "forge_hash":
            if len(arguments) != 1 or not is_universal_data(arguments[0]):
                raise CForgevError(
                    f"Línea {name.line}: forge_hash requiere un ForgeValue serializable"
                )
            canonical = json.dumps(
                arguments[0], ensure_ascii=False, sort_keys=True,
                separators=(",", ":"),
            ).encode("utf-8")
            return hashlib.sha256(canonical).hexdigest()
        if name.value == "json_parse":
            if len(arguments) != 1 or not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: json_parse requiere un texto")
            try:
                result = json.loads(arguments[0])
            except json.JSONDecodeError as error:
                raise CForgevError(
                    f"Línea {name.line}: JSON inválido en columna {error.colno}: {error.msg}"
                ) from error
            if not is_universal_data(result):
                raise CForgevError(f"Línea {name.line}: JSON produjo un tipo no compatible")
            return result
        if name.value == "sys_fetch":
            if len(arguments) != 1 or not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: sys_fetch requiere una URL")
            url = arguments[0]
            if not url.startswith(("https://", "http://")):
                raise CForgevError(f"Línea {name.line}: sys_fetch solo acepta HTTP o HTTPS")
            request = urllib.request.Request(
                url, headers={"User-Agent": f"C-Forge/{VERSION}"}
            )
            try:
                with urllib.request.urlopen(request, timeout=15) as response:
                    payload = response.read(16 * 1024 * 1024 + 1)
                    if len(payload) > 16 * 1024 * 1024:
                        raise CForgevError(
                            f"Línea {name.line}: sys_fetch superó el límite de 16 MiB"
                        )
                    charset = response.headers.get_content_charset() or "utf-8"
                    return payload.decode(charset)
            except CForgevError:
                raise
            except (OSError, UnicodeError, urllib.error.URLError) as error:
                raise CForgevError(f"Línea {name.line}: sys_fetch falló: {error}") from error
        if name.value == "forge_bench":
            if len(arguments) not in {2, 3} or not isinstance(arguments[0], str):
                raise CForgevError(
                    f"Línea {name.line}: forge_bench requiere nombre, iteraciones y argumentos opcionales"
                )
            if not isinstance(arguments[1], (int, float)):
                raise CForgevError(f"Línea {name.line}: las iteraciones deben ser numéricas")
            iterations = int(arguments[1])
            if iterations < 1 or iterations > 10_000_000:
                raise CForgevError(f"Línea {name.line}: iteraciones fuera del rango 1..10.000.000")
            function = self.functions.get(arguments[0])
            if function is None:
                raise CForgevError(
                    f"Línea {name.line}: función de benchmark desconocida '{arguments[0]}'"
                )
            call_arguments = arguments[2] if len(arguments) == 3 else []
            if not isinstance(call_arguments, list):
                raise CForgevError(f"Línea {name.line}: los argumentos deben ser una lista")
            started = time.perf_counter()
            result = None
            for _ in range(iterations):
                result = self.invoke_user_function(function, call_arguments, name.line)
            seconds = time.perf_counter() - started
            return {
                "resultado": result,
                "iteraciones": iterations,
                "segundos": seconds,
                "por_segundo": iterations / seconds if seconds else 0,
            }
        if name.value == "leer":
            if len(arguments) > 1:
                raise CForgevError(f"Línea {name.line}: 'leer' acepta cero o un argumento")
            if arguments and not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: el mensaje de 'leer' debe ser texto")
            return input(arguments[0] if arguments else "")
        if name.value == "a_numero":
            if len(arguments) != 1:
                raise CForgevError(f"Línea {name.line}: 'a_numero' requiere 1 argumento")
            try:
                return float(arguments[0])
            except (TypeError, ValueError) as error:
                raise CForgevError(f"Línea {name.line}: no se puede convertir a número") from error
        if name.value == "a_texto":
            if len(arguments) != 1:
                raise CForgevError(f"Línea {name.line}: 'a_texto' requiere 1 argumento")
            return format_value(arguments[0])
        if name.value == "longitud":
            if len(arguments) != 1 or not isinstance(arguments[0], (str, list, dict)):
                raise CForgevError(f"Línea {name.line}: 'longitud' requiere texto, lista o mapa")
            return len(arguments[0])
        if name.value == "agregar":
            if len(arguments) != 2 or not isinstance(arguments[0], list):
                raise CForgevError(f"Línea {name.line}: 'agregar' requiere una lista y un valor")
            arguments[0].append(arguments[1])
            return None
        if name.value == "sys_run":
            if len(arguments) != 1 or not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: sys_run requiere un comando de texto")
            try:
                completed = subprocess.run(
                    arguments[0], shell=True, capture_output=True, text=True,
                    cwd=self.base_dir,
                )
            except OSError as error:
                raise CForgevError(f"Línea {name.line}: sys_run falló: {error}") from error
            return {"estado": completed.returncode, "salida": completed.stdout, "error": completed.stderr}
        if name.value == "sys_info":
            if arguments:
                raise CForgevError(f"Línea {name.line}: sys_info no recibe argumentos")
            ram = 0
            try:
                if sys.platform == "darwin":
                    probe = subprocess.run(
                        ["sysctl", "-n", "hw.memsize"], capture_output=True, text=True
                    )
                    if probe.returncode == 0:
                        ram = int(probe.stdout.strip())
                elif hasattr(os, "sysconf"):
                    ram = int(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
            except (OSError, ValueError, subprocess.SubprocessError):
                pass
            if ram == 0 and hasattr(os, "sysconf"):
                try:
                    ram = int(os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES"))
                except (OSError, ValueError):
                    pass
            return {"cpu": platform.machine(), "nucleos": os.cpu_count() or 1, "ram_bytes": ram, "sistema": platform.system()}
        if name.value in {"file_read", "file_write", "file_append"}:
            expected = 1 if name.value == "file_read" else 2
            if len(arguments) != expected or not all(isinstance(value, str) for value in arguments):
                raise CForgevError(f"Línea {name.line}: {name.value} requiere {expected} texto(s)")
            path = (self.base_dir / arguments[0]).resolve()
            try:
                if name.value == "file_read":
                    return path.read_text(encoding="utf-8")
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("w" if name.value == "file_write" else "a", encoding="utf-8") as stream:
                    stream.write(arguments[1])
                return None
            except OSError as error:
                raise CForgevError(f"Línea {name.line}: error de archivo: {error}") from error
        if name.value == "net_send":
            if len(arguments) != 3 or not isinstance(arguments[0], str) or not isinstance(arguments[1], (int, float)) or not isinstance(arguments[2], str):
                raise CForgevError(f"Línea {name.line}: net_send requiere host, puerto y texto")
            try:
                data = arguments[2].encode("utf-8")
                with socket.create_connection((arguments[0], int(arguments[1])), timeout=5.0) as connection:
                    connection.sendall(data)
                return len(data)
            except OSError as error:
                raise CForgevError(f"Línea {name.line}: net_send falló: {error}") from error
        if name.value == "net_listen":
            if len(arguments) not in {1, 2} or not isinstance(arguments[0], (int, float)) or (len(arguments) == 2 and not isinstance(arguments[1], (int, float))):
                raise CForgevError(f"Línea {name.line}: net_listen requiere puerto y timeout opcional")
            timeout = float(arguments[1]) / 1000.0 if len(arguments) == 2 else 5.0
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
                    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    server.bind(("127.0.0.1", int(arguments[0])))
                    server.listen(1)
                    server.settimeout(timeout)
                    connection, address = server.accept()
                    with connection:
                        chunks: list[bytes] = []
                        while True:
                            chunk = connection.recv(65536)
                            if not chunk:
                                break
                            chunks.append(chunk)
                    return {"datos": b"".join(chunks).decode("utf-8"), "host": address[0], "puerto": address[1]}
            except OSError as error:
                raise CForgevError(f"Línea {name.line}: net_listen falló: {error}") from error
        if name.value == "array_fast":
            if len(arguments) != 1 or not isinstance(arguments[0], list) or not all(isinstance(value, (int, float)) for value in arguments[0]):
                raise CForgevError(f"Línea {name.line}: array_fast requiere una lista numérica")
            return [float(value) for value in arguments[0]]
        if name.value == "matrix":
            if len(arguments) not in {2, 3} or not all(isinstance(value, (int, float)) for value in arguments[:2]) or (len(arguments) == 3 and not isinstance(arguments[2], (int, float))):
                raise CForgevError(f"Línea {name.line}: matrix requiere filas, columnas y valor numérico opcional")
            rows, columns = int(arguments[0]), int(arguments[1])
            if rows < 0 or columns < 0 or rows * columns > 10_000_000:
                raise CForgevError(f"Línea {name.line}: dimensiones de matrix inválidas")
            fill = float(arguments[2]) if len(arguments) == 3 else 0.0
            return [[fill for _ in range(columns)] for _ in range(rows)]
        if name.value in {"leer_archivo", "escribir_archivo", "existe_archivo"}:
            expected = 2 if name.value == "escribir_archivo" else 1
            if len(arguments) != expected or not all(isinstance(value, str) for value in arguments):
                raise CForgevError(
                    f"Línea {name.line}: '{name.value}' requiere {expected} argumento(s) de texto"
                )
            path = (self.base_dir / arguments[0]).resolve()
            try:
                if name.value == "leer_archivo":
                    return path.read_text(encoding="utf-8")
                if name.value == "escribir_archivo":
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.write_text(arguments[1], encoding="utf-8")
                    return None
                return path.exists()
            except OSError as error:
                raise CForgevError(f"Línea {name.line}: error de archivo: {error}") from error
        if name.value in {"raiz", "absoluto", "redondear"}:
            if len(arguments) != 1 or not isinstance(arguments[0], (int, float)):
                raise CForgevError(f"Línea {name.line}: '{name.value}' requiere un número")
            if name.value == "raiz":
                if arguments[0] < 0:
                    raise CForgevError(f"Línea {name.line}: no existe raíz real negativa")
                return math.sqrt(arguments[0])
            if name.value == "absoluto":
                return abs(arguments[0])
            return round(arguments[0])
        if name.value == "potencia":
            if len(arguments) != 2 or not all(isinstance(value, (int, float)) for value in arguments):
                raise CForgevError(f"Línea {name.line}: 'potencia' requiere dos números")
            return math.pow(arguments[0], arguments[1])
        if name.value == "tiempo_actual":
            if arguments:
                raise CForgevError(f"Línea {name.line}: 'tiempo_actual' no recibe argumentos")
            return time.time()
        if name.value == "argumentos":
            if arguments:
                raise CForgevError(f"Línea {name.line}: 'argumentos' no recibe argumentos")
            return list(self.program_arguments)
        if name.value == "jit_estado":
            if len(arguments) != 1 or not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: jit_estado requiere un nombre de función")
            return self.jit_counts.get(arguments[0], 0)
        if name.value == "cluster_estado":
            if arguments:
                raise CForgevError(f"Línea {name.line}: cluster_estado no recibe argumentos")
            return sorted(f"{kind}:{name}" for name, kind in self.cluster_symbols.items())
        if name.value == "afirmar":
            if len(arguments) not in {1, 2} or not isinstance(arguments[0], bool):
                raise CForgevError(f"Línea {name.line}: afirmar requiere booleano y mensaje opcional")
            if not arguments[0]:
                message = arguments[1] if len(arguments) == 2 else "la condición es falsa"
                raise CForgevError(f"Línea {name.line}: afirmación fallida: {message}")
            return None
        if name.value == "jit_caliente":
            if len(arguments) != 1 or not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: jit_caliente requiere un nombre")
            return self.jit_counts.get(arguments[0], 0) >= 1000
        if name.value == "paralelo":
            if len(arguments) != 2 or not isinstance(arguments[0], str) or not isinstance(arguments[1], list):
                raise CForgevError(f"Línea {name.line}: paralelo requiere nombre y lista de trabajos")
            function = self.functions.get(arguments[0])
            if function is None:
                raise CForgevError(f"Línea {name.line}: función paralela desconocida '{arguments[0]}'")
            jobs = [job if isinstance(job, list) else [job] for job in arguments[1]]
            def run_job(job: list[object]) -> object:
                return self.invoke_user_function(function, job, name.line)
            with concurrent.futures.ThreadPoolExecutor() as executor:
                return list(executor.map(run_job, jobs))
        if name.value == "use_python":
            if len(arguments) != 3 or not all(isinstance(value, str) for value in arguments[:2]) or not isinstance(arguments[2], list):
                raise CForgevError(
                    f"Línea {name.line}: use_python requiere módulo, función y lista"
                )
            try:
                builtins.ForgeSymbols = self.variables
                if "cforgev_runtime" not in sys.modules:
                    import types
                    runtime_module = types.ModuleType("cforgev_runtime")
                    runtime_module.get = lambda symbol: builtins.ForgeSymbols[symbol]
                    runtime_module.snapshot = lambda: dict(builtins.ForgeSymbols)
                    sys.modules["cforgev_runtime"] = runtime_module
                module = importlib.import_module(arguments[0])
                function = getattr(module, arguments[1])
                result = function(*arguments[2])
            except Exception as error:
                raise CForgevError(f"Línea {name.line}: llamada Python falló: {error}") from error
            if not is_universal_data(result):
                raise CForgevError(f"Línea {name.line}: Python devolvió un tipo no compatible")
            return result
        if name.value in {"use_native", "use_csharp"}:
            if len(arguments) != 3 or not all(isinstance(value, str) for value in arguments[:2]) or not isinstance(arguments[2], list):
                raise CForgevError(
                    f"Línea {name.line}: {name.value} requiere ruta, símbolo y lista"
                )
            return self.invoke_dynamic_library(arguments[0], arguments[1], arguments[2], name.line)
        if name.value in {"use_javascript", "use_typescript"}:
            if len(arguments) != 3 or not all(isinstance(value, str) for value in arguments[:2]) or not isinstance(arguments[2], list):
                raise CForgevError(
                    f"Línea {name.line}: {name.value} requiere módulo, función y lista"
                )
            return self.invoke_javascript(arguments[0], arguments[1], arguments[2], name.line)
        if name.value == "use_java":
            if len(arguments) != 4 or not all(isinstance(value, str) for value in arguments[:3]) or not isinstance(arguments[3], list):
                raise CForgevError(
                    f"Línea {name.line}: use_java requiere jar, clase, método y lista"
                )
            return self.invoke_java(arguments[0], arguments[1], arguments[2], arguments[3], name.line)
        if name.value == "use_cpp":
            raise CForgevError(
                f"Línea {name.line}: use_cpp requiere compilar con --vincular"
            )
        structure = self.structures.get(name.value)
        if structure is not None:
            if len(arguments) != len(structure.fields):
                raise CForgevError(
                    f"Línea {name.line}: '{name.value}' requiere {len(structure.fields)} argumentos"
                )
            values: dict[str, object] = {}
            for (field, expected), value in zip(structure.fields, arguments):
                ensure_type(field, expected, value, name.line)
                values[field] = value
            return StructureValue(name.value, values)
        function = self.functions.get(name.value)
        if function is None:
            raise CForgevError(f"Línea {name.line}: función desconocida '{name.value}'")
        return self.invoke_user_function(function, arguments, name.line)

    def invoke_user_function(self, function: Function, arguments: list[object], line: int) -> object:
        if len(arguments) != len(function.parameters):
            raise CForgevError(
                f"Línea {line}: '{function.name}' requiere {len(function.parameters)} "
                f"argumentos, pero recibió {len(arguments)}"
            )
        self.jit_counts[function.name] = self.jit_counts.get(function.name, 0) + 1
        local_variables = dict(self.variables)
        local_variables.update(zip(function.parameters, arguments))
        local_types = dict(self.variable_types)
        local_types.update(
            (parameter, value_type(argument))
            for parameter, argument in zip(function.parameters, arguments)
        )
        interpreter = Interpreter(
            function.body, local_variables, self.functions, local_types,
            self.base_dir, self.imported_modules, self.structures, self.program_arguments,
            self.jit_counts, self.cluster_symbols,
        )
        try:
            interpreter.run()
        except ReturnSignal as signal:
            return signal.value
        return None

    def invoke_dynamic_library(
        self, library: str, symbol: str, arguments: list[object], line: int
    ) -> object:
        path = (self.base_dir / library).resolve()
        root = Path(__file__).resolve().parent
        runner = root / "build" / ".cforgev_ffi_runner"
        source = root / "herramientas" / "cforgev_ffi_runner.cpp"
        header = root / "include" / "cforgev_ffi.h"
        if not runner.exists() or runner.stat().st_mtime < max(source.stat().st_mtime, header.stat().st_mtime):
            runner.parent.mkdir(parents=True, exist_ok=True)
            build = subprocess.run(
                ["clang++", "-std=c++17", str(source), "-I", str(root / "include"), "-o", str(runner)],
                capture_output=True, text=True,
            )
            if build.returncode:
                raise CForgevError(f"Línea {line}: no se pudo construir el puente nativo: {build.stderr}")
        command = [str(runner), str(path), symbol]
        for value in arguments:
            if value is None:
                command.append("n:")
            elif isinstance(value, bool):
                command.append(f"i:{int(value)}")
            elif isinstance(value, int) or (isinstance(value, float) and value.is_integer()):
                command.append(f"i:{int(value)}")
            elif isinstance(value, float):
                command.append(f"d:{value}")
            elif isinstance(value, str):
                command.append("s:" + value)
            else:
                raise CForgevError(f"Línea {line}: tipo no compatible con ABI nativo")
        invoked = subprocess.run(command, capture_output=True, text=True)
        if invoked.returncode:
            raise CForgevError(f"Línea {line}: {invoked.stderr or 'función extranjera falló'}")
        type_line, _, payload = invoked.stdout.partition("\n")
        result_type = int(type_line)
        if result_type == 0:
            return None
        if result_type == 1:
            return int(payload)
        if result_type == 2:
            return float(payload)
        if result_type == 3:
            return payload
        raise CForgevError(f"Línea {line}: tipo ABI de retorno desconocido")

    def invoke_javascript(
        self, module: str, function: str, arguments: list[object], line: int
    ) -> object:
        if "/" in module and not Path(module).is_absolute():
            module = str((self.base_dir / module).resolve())
        marker = "__CFORGEV_JS_RESULT__"
        script = f'''(async () => {{
globalThis.ForgeSymbols = {json.dumps({key: value for key, value in self.variables.items() if is_universal_data(value)}, ensure_ascii=False)};
const target = require({json.dumps(module)});
const callable = target[{json.dumps(function)}] ?? target.default?.[{json.dumps(function)}];
if (typeof callable !== "function") throw new Error("función JavaScript inexistente");
const result = await callable(...{json.dumps(arguments, ensure_ascii=False)});
process.stdout.write("\\n{marker}" + JSON.stringify(result === undefined ? null : result));
}})().catch(error => {{ console.error(error?.stack ?? String(error)); process.exit(1); }});
'''
        try:
            invoked = subprocess.run(["node", "-e", script], capture_output=True, text=True)
        except FileNotFoundError as error:
            raise CForgevError(f"Línea {line}: Node.js no está instalado") from error
        if invoked.returncode:
            raise CForgevError(f"Línea {line}: JavaScript falló: {invoked.stderr.strip()}")
        visible, separator, payload = invoked.stdout.rpartition(marker)
        if not separator:
            raise CForgevError(f"Línea {line}: JavaScript no devolvió protocolo C-Forge")
        if visible.strip():
            print(visible.strip())
        try:
            return json.loads(payload)
        except json.JSONDecodeError as error:
            raise CForgevError(f"Línea {line}: resultado JavaScript inválido") from error

    def invoke_java(
        self, jar: str, class_name: str, method: str, arguments: list[object], line: int
    ) -> object:
        import tempfile
        tagged: list[str] = []
        for value in arguments:
            if value is None: tagged.append("n:")
            elif isinstance(value, bool): tagged.append("b:" + str(value).lower())
            elif isinstance(value, int): tagged.append("i:" + str(value))
            elif isinstance(value, float): tagged.append("d:" + str(value))
            elif isinstance(value, str): tagged.append("s:" + value)
            else: raise CForgevError(f"Línea {line}: Java 1.1 admite argumentos escalares")
        bridge = r'''
import java.lang.reflect.*;
import java.net.*;
import java.io.*;
public final class CForgevJavaBridge {
  static Object convert(String raw, Class<?> type) {
    String value=raw.substring(2); char tag=raw.charAt(0);
    if(tag=='n') return null;
    if(type==String.class) return value;
    if(type==int.class||type==Integer.class) return Integer.valueOf(value);
    if(type==long.class||type==Long.class) return Long.valueOf(value);
    if(type==double.class||type==Double.class) return Double.valueOf(value);
    if(type==float.class||type==Float.class) return Float.valueOf(value);
    if(type==boolean.class||type==Boolean.class) return Boolean.valueOf(value);
    throw new IllegalArgumentException("tipo Java no compatible: "+type);
  }
  public static void main(String[] args) throws Exception {
    URLClassLoader loader=new URLClassLoader(new URL[]{new File(args[0]).toURI().toURL()});
    Class<?> klass=Class.forName(args[1],true,loader); Method selected=null;
    for(Method candidate:klass.getMethods()) if(candidate.getName().equals(args[2])&&candidate.getParameterCount()==args.length-3){selected=candidate;break;}
    if(selected==null) throw new NoSuchMethodException(args[1]+"."+args[2]);
    Class<?>[] types=selected.getParameterTypes(); Object[] values=new Object[types.length];
    for(int i=0;i<types.length;i++) values[i]=convert(args[i+3],types[i]);
    Object result=selected.invoke(null,values);
    if(result==null) System.out.print("n:"); else if(result instanceof Boolean) System.out.print("b:"+result); else if(result instanceof Float||result instanceof Double) System.out.print("d:"+result); else if(result instanceof Number) System.out.print("i:"+result); else System.out.print("s:"+result);
  }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); source = root / "CForgevJavaBridge.java"
            source.write_text(bridge, encoding="utf-8")
            compiled = subprocess.run(["javac", str(source)], capture_output=True, text=True)
            if compiled.returncode:
                raise CForgevError(f"Línea {line}: JNI/JDK no disponible: {compiled.stderr.strip()}")
            invoked = subprocess.run(
                ["java", "-cp", str(root), "CForgevJavaBridge", jar, class_name, method, *tagged],
                capture_output=True, text=True,
            )
        if invoked.returncode:
            raise CForgevError(f"Línea {line}: Java falló: {invoked.stderr.strip()}")
        tag, payload = invoked.stdout[:2], invoked.stdout[2:]
        if tag == "n:": return None
        if tag == "b:": return payload == "true"
        if tag == "i:": return int(payload)
        if tag == "d:": return float(payload)
        if tag == "s:": return payload
        raise CForgevError(f"Línea {line}: protocolo Java inválido")

    def optional_semicolon(self) -> None:
        self.match_value(";")

    def match_ident(self, value: str) -> bool:
        if self.check("IDENT") and self.peek().value == value:
            self.advance()
            return True
        return False

    def match_value(self, *values: str) -> bool:
        if self.peek().value in values:
            self.advance()
            return True
        return False

    def match(self, kind: str) -> bool:
        if self.check(kind):
            self.advance()
            return True
        return False

    def consume(self, kind: str, message: str) -> Token:
        if self.check(kind):
            return self.advance()
        raise CForgevError(f"Línea {self.peek().line}: {message}")

    def consume_value(self, value: str, message: str) -> Token:
        if self.peek().value == value:
            return self.advance()
        raise CForgevError(f"Línea {self.peek().line}: {message}")

    def check(self, kind: str) -> bool:
        return self.peek().kind == kind

    def advance(self) -> Token:
        token = self.peek()
        if token.kind != "EOF":
            self.current += 1
        return token

    def peek(self) -> Token:
        return self.tokens[self.current]

    def previous(self) -> Token:
        return self.tokens[self.current - 1]

    def peek_next(self) -> Token:
        if self.current + 1 >= len(self.tokens):
            return self.tokens[-1]
        return self.tokens[self.current + 1]


def calculate(left: object, op: Token, right: object) -> object:
    if op.value == "+" and isinstance(left, str) and isinstance(right, str):
        return left + right
    if not isinstance(left, (int, float)) or not isinstance(right, (int, float)):
        raise CForgevError(f"Línea {op.line}: '{op.value}' requiere números")
    if op.value == "+":
        return left + right
    if op.value == "-":
        return left - right
    if op.value == "*":
        return left * right
    if right == 0:
        raise CForgevError(f"Línea {op.line}: no se puede dividir por cero")
    return left / right


def format_value(value: object) -> str:
    if isinstance(value, bool):
        return "verdadero" if value else "falso"
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    if value is None:
        return "nulo"
    if isinstance(value, list):
        return "[" + ", ".join(format_value(item) for item in value) + "]"
    if isinstance(value, dict):
        return "{" + ", ".join(f'"{key}": {format_value(item)}' for key, item in value.items()) + "}"
    return str(value)


def value_type(value: object) -> str:
    if isinstance(value, bool):
        return "booleano"
    if isinstance(value, (int, float)):
        return "numero"
    if isinstance(value, str):
        return "texto"
    if isinstance(value, list):
        return "lista"
    if isinstance(value, dict):
        return value.structure_name if isinstance(value, StructureValue) else "mapa"
    if value is None:
        return "nulo"
    return "cualquiera"


def is_universal_data(value: object) -> bool:
    if value is None or isinstance(value, (bool, int, float, str)):
        return True
    if isinstance(value, (list, tuple)):
        return all(is_universal_data(item) for item in value)
    if isinstance(value, dict):
        return all(isinstance(key, str) and is_universal_data(item) for key, item in value.items())
    return False


def ensure_type(name: str, expected: str, value: object, line: int) -> None:
    actual = value_type(value)
    if expected != "cualquiera" and expected != actual:
        raise CForgevError(
            f"Línea {line}: '{name}' es {expected} y no puede recibir {actual}"
        )


def require_bool(value: object, line: int) -> bool:
    if not isinstance(value, bool):
        raise CForgevError(f"Línea {line}: la operación lógica requiere booleanos")
    return value


def execute(path: Path, program_arguments: list[str] | None = None) -> None:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {path}: {error}") from error
    try:
        Interpreter(
            tokenize(source), base_dir=path.resolve().parent,
            program_arguments=program_arguments
        ).run()
    except ReturnSignal as signal:
        raise CForgevError("'retornar' solo se puede usar dentro de una función") from signal


def repair_source(source: str) -> tuple[str, list[str]]:
    """Aplica únicamente reparaciones sintácticas locales y de alta confianza."""
    repaired = source
    changes: list[str] = []
    smart = {"“": '"', "”": '"', "‘": "'", "’": "'"}
    for damaged, replacement in smart.items():
        if damaged in repaired:
            repaired = repaired.replace(damaged, replacement)
            changes.append(f"comilla tipográfica {damaged!r} → {replacement!r}")
    keywords = [
        "mostrar", "funcion", "retornar", "mientras", "estructura", "clase",
        "intentar", "capturar", "import", "usar", "gpu", "sea", "si", "sino",
    ]
    lines = repaired.splitlines(keepends=True)
    for index, line in enumerate(lines):
        match = re.match(r"(\s*)([A-Za-z_][A-Za-z0-9_]*)(?=\s|\()", line)
        if not match or match.group(2) in keywords:
            continue
        nearest = difflib.get_close_matches(match.group(2), keywords, n=1, cutoff=0.84)
        if nearest:
            old, new = match.group(2), nearest[0]
            lines[index] = line[:match.start(2)] + new + line[match.end(2):]
            changes.append(f"línea {index + 1}: {old!r} → {new!r}")
    repaired = "".join(lines)
    pairs = [("{", "}"), ("(", ")"), ("[", "]")]
    suffix = ""
    for opening, closing in pairs:
        missing = repaired.count(opening) - repaired.count(closing)
        if missing > 0:
            suffix += closing * missing
            changes.append(f"se agregó {closing!r} {missing} vez/veces")
    if suffix:
        repaired = repaired.rstrip() + "\n" + suffix + "\n"
    return repaired, changes


def validate_foreign_memory(language: str, body: str, line: int = 1) -> None:
    """Barrera conservadora: extern C++ solo admite código sin memoria manual."""
    if language != "cpp":
        return
    dangerous = re.compile(
        r"\b(new|delete|malloc|calloc|realloc|free|reinterpret_cast|const_cast)\b|->"
    )
    match = dangerous.search(body)
    if match:
        raise CForgevError(
            f"Línea {line}: Memory Safety rechazó operación C++ peligrosa {match.group()!r}; "
            "usa valores RAII y contenedores estándar"
        )


def format_source(source: str) -> str:
    """Formateador conservador: sangría por bloques y espacios finales limpios."""
    output: list[str] = []
    depth = 0
    previous_blank = False
    for raw_line in source.splitlines():
        stripped = raw_line.strip()
        if not stripped:
            if output and not previous_blank:
                output.append("")
            previous_blank = True
            continue
        previous_blank = False
        leading_closers = len(stripped) - len(stripped.lstrip("}"))
        line_depth = max(0, depth - leading_closers)
        output.append("    " * line_depth + stripped)
        quoted = False
        escaped = False
        opens = closes = 0
        for character in stripped:
            if quoted:
                if escaped:
                    escaped = False
                elif character == "\\":
                    escaped = True
                elif character == '"':
                    quoted = False
            elif character == '"':
                quoted = True
            elif character == "{":
                opens += 1
            elif character == "}":
                closes += 1
        depth = max(0, depth + opens - closes)
    while output and not output[-1]:
        output.pop()
    return "\n".join(output) + "\n"


def format_file(path: Path) -> bool:
    try:
        original = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {path}: {error.strerror or error}") from error
    formatted = format_source(original)
    if formatted == original:
        return False
    path.write_text(formatted, encoding="utf-8")
    return True


def run_test_file(path: Path) -> int:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {path}: {error.strerror or error}") from error
    results: list[str] = []
    Interpreter(tokenize(source), base_dir=path.resolve().parent, test_results=results).run()
    if not results:
        raise CForgevError(f"{path} no contiene bloques test")
    print(f"C-Forge Test: {len(results)} aprobados, 0 fallidos")
    return len(results)


def execute_watch(
    path: Path, program_arguments: list[str] | None = None,
    interval: float = 0.5, max_reloads: int | None = None,
) -> None:
    variables: dict[str, object] = {}
    functions: dict[str, Function] = {}
    variable_types: dict[str, str] = {}
    structures: dict[str, Structure] = {}
    imported: set[Path] = set()
    jit_counts: dict[str, int] = {}
    cluster_symbols: dict[str, str] = {}
    last_stamp: int | None = None
    reloads = 0
    print(f"C-Forge hot reload: observando {path} (Ctrl+C para terminar)")
    while max_reloads is None or reloads < max_reloads:
        try:
            stamp = path.stat().st_mtime_ns
            if stamp != last_stamp:
                source = path.read_text(encoding="utf-8")
                Interpreter(
                    tokenize(source), variables, functions, variable_types,
                    path.resolve().parent, imported, structures,
                    program_arguments or [], jit_counts, cluster_symbols,
                ).run()
                last_stamp = stamp
                reloads += 1
                print(f"[C-Forge Hot Reload] versión {reloads} cargada; estado conservado")
            time.sleep(interval)
        except KeyboardInterrupt:
            print("\nC-Forge hot reload finalizado")
            return
        except (OSError, CForgevError) as error:
            print(f"[C-Forge Runtime Exception] {error}")
            time.sleep(interval)


def source_is_complete(source: str) -> bool:
    """Indica si una entrada REPL tiene delimitadores y textos cerrados."""
    pairs = {"(": ")", "[": "]", "{": "}"}
    closing = set(pairs.values())
    stack: list[str] = []
    quoted = False
    escaped = False
    index = 0
    while index < len(source):
        character = source[index]
        if quoted:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
        elif character == "/" and index + 1 < len(source) and source[index + 1] == "/":
            newline = source.find("\n", index)
            if newline < 0:
                break
            index = newline
        elif character in pairs:
            stack.append(pairs[character])
        elif character in closing:
            if not stack or stack.pop() != character:
                return True
        index += 1
    return not quoted and not stack


def run_repl(input_fn=input) -> None:
    """Consola persistente que interpreta cada bloque directamente en memoria."""
    variables: dict[str, object] = {}
    functions: dict[str, Function] = {}
    variable_types: dict[str, str] = {}
    structures: dict[str, Structure] = {}
    imported_modules: set[Path] = set()
    jit_counts: dict[str, int] = {}
    cluster_symbols: dict[str, str] = {}
    buffer = ""
    print(f"C-Forge {VERSION} — REPL (escribe 'salir' para terminar)")
    while True:
        try:
            line = input_fn("cfv> " if not buffer else "...  ")
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if not buffer and line.strip() in {"salir", ":salir"}:
            return
        buffer += line + "\n"
        if not source_is_complete(buffer):
            continue
        source = buffer.strip()
        buffer = ""
        if not source:
            continue
        try:
            source_tokens = tokenize(source)
            first = source_tokens[0].value
            statement_words = {
                "sea", "mostrar", "si", "mientras", "funcion", "retornar",
                "intentar", "usar", "import", "gpu", "extern", "cluster", "estructura", "clase",
            }
            candidate = source.rstrip().rstrip(";")
            is_assignment = (
                len(source_tokens) > 1 and source_tokens[0].kind == "IDENT"
                and (
                    source_tokens[1].value == "="
                    or (
                        source_tokens[1].value == "." and len(source_tokens) > 3
                        and source_tokens[3].value == "="
                    )
                )
            )
            repl_source = source if first in statement_words or is_assignment else f"mostrar({candidate});"
            Interpreter(
                tokenize(repl_source), variables, functions, variable_types,
                Path.cwd(), imported_modules, structures, [], jit_counts, cluster_symbols,
            ).run()
        except ReturnSignal:
            print("[C-Forge Runtime Exception] 'retornar' solo se puede usar dentro de una función")
        except CForgevError as error:
            print(f"[C-Forge Runtime Exception] {error}")


def main() -> int:
    if len(sys.argv) == 2 and sys.argv[1] == "--setup":
        return setup_environment()
    if len(sys.argv) == 2 and sys.argv[1] == "--install":
        return install_global()
    if len(sys.argv) >= 2 and sys.argv[1] in {"fmt", "test"}:
        command = sys.argv[1]
        if len(sys.argv) != 3:
            print(f"Uso: cforge {command} archivo.cfv", file=sys.stderr)
            return 2
        path = Path(sys.argv[2])
        try:
            if command == "fmt":
                changed = format_file(path)
                print(f"C-Forge fmt: {'formateado' if changed else 'sin cambios'} — {path}")
            else:
                run_test_file(path)
        except CForgevError as error:
            print(f"[C-Forge Runtime Exception] {error}", file=sys.stderr)
            return 1
        return 0
    parser = argparse.ArgumentParser(prog="cforgev", description="Intérprete de C-Forge")
    parser.add_argument("archivo", nargs="?", type=Path, help="archivo .cfv que se ejecutará")
    parser.add_argument("--compilar", action="store_true", help="crear un ejecutable nativo")
    parser.add_argument("-o", "--salida", type=Path, help="ruta del ejecutable generado")
    parser.add_argument(
        "--vincular", action="append", type=Path, default=[],
        help="archivo C/C++ adicional que se compilará y vinculará"
    )
    parser.add_argument("--version", action="version", version=f"C-Forge {VERSION}")
    parser.add_argument("--reparar", action="store_true", help="reparar errores sintácticos seguros")
    parser.add_argument("--vigilar", action="store_true", help="recargar el archivo conservando estado")
    parser.add_argument("--intervalo", type=float, default=0.5, help="segundos entre revisiones")
    parser.add_argument("--wasm", action="store_true", help="exportar un módulo WebAssembly .wat")
    args, program_arguments = parser.parse_known_args()
    if args.archivo is None:
        if args.compilar or args.salida or args.vincular or args.reparar or args.vigilar or args.wasm:
            parser.error("esta operación requiere un archivo .cfv")
        run_repl()
        return 0
    if args.archivo.suffix != ".cfv":
        print("Aviso: los programas C-Forge normalmente usan la extensión .cfv", file=sys.stderr)
    try:
        if args.reparar:
            try:
                original = args.archivo.read_text(encoding="utf-8")
            except OSError as error:
                raise CForgevError(
                    f"No se pudo abrir {args.archivo}: {error.strerror or error}"
                ) from error
            repaired, changes = repair_source(original)
            if changes:
                backup = args.archivo.with_suffix(args.archivo.suffix + ".bak")
                backup.write_text(original, encoding="utf-8")
                args.archivo.write_text(repaired, encoding="utf-8")
                print("[C-Forge Self-Healing] " + "; ".join(changes))
                print(f"Respaldo creado: {backup}")
        if args.wasm:
            from compilador_wasm import compile_wasm
            output = args.salida or args.archivo.with_suffix(".wat")
            compile_wasm(args.archivo, output)
            print(f"Módulo WebAssembly C-Forge creado: {output}")
        elif args.vigilar:
            execute_watch(args.archivo, program_arguments, args.intervalo)
        elif args.compilar:
            from compilador_nativo import compile_native

            output = args.salida or args.archivo.with_suffix("")
            compile_native(args.archivo, output, args.vincular)
            print(f"Ejecutable C-Forge creado: {output}")
        else:
            execute(args.archivo, program_arguments)
    except CForgevError as error:
        print(f"[C-Forge Runtime Exception] {error}", file=sys.stderr)
        try:
            _, suggestions = repair_source(args.archivo.read_text(encoding="utf-8"))
            if suggestions:
                print("[C-Forge Self-Healing] Sugerencia: " + "; ".join(suggestions), file=sys.stderr)
                print("Ejecuta otra vez con --reparar para aplicarla.", file=sys.stderr)
        except OSError:
            pass
        return 1
    return 0


def setup_environment() -> int:
    """Diagnostica dependencias sin modificar el equipo silenciosamente."""
    print("C-Forge Setup 1.4.0")
    clang = shutil.which("clang++") is not None
    python = bool(getattr(sys, "frozen", False)) or shutil.which("python3") is not None
    node = shutil.which("node") is not None
    if sys.platform == "darwin":
        java = subprocess.run(
            ["/usr/libexec/java_home"], capture_output=True, text=True
        ).returncode == 0
    else:
        java = shutil.which("java") is not None and shutil.which("javac") is not None
    if clang:
        print("[OK] C++: clang++ disponible")
    elif sys.platform == "darwin":
        print("[FALTA] C++: ejecuta xcode-select --install")
    elif os.name == "nt":
        print("[OPCIONAL] C++: instala Visual Studio Build Tools o LLVM")
    else:
        print("[FALTA] C++: instala clang++ o g++")
    print("[OK] Python 3 disponible" if python else "[FALTA] Python 3")
    print("[OK] JavaScript/TypeScript: Node.js disponible" if node else "[OPCIONAL] Node.js no instalado")
    if java:
        print("[OK] Java: JDK y JVM disponibles")
    else:
        if sys.platform == "darwin":
            print("[FALTA] Java: brew install --cask temurin")
        elif os.name == "nt":
            print("[FALTA] Java: winget install EclipseAdoptium.Temurin.21.JDK")
        else:
            print("[FALTA] Java: sudo apt install default-jdk")
        print("Alternativa oficial: https://adoptium.net/temurin/releases/")
    print("Setup finalizado; no se realizaron instalaciones sin autorización.")
    return 0 if python else 1


def install_global() -> int:
    """Instala la distribución monolítica, nunca un lanzador incompleto."""
    master = Path(__file__).resolve().parent / "outputs" / "cforge-master"
    if not master.is_file():
        print(
            "No se encontró outputs/cforge-master. Genera primero la distribución maestra.",
            file=sys.stderr,
        )
        return 1
    destination = Path("/usr/local/bin/cforge")
    try:
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(master, destination)
        destination.chmod(0o755)
    except PermissionError:
        print(f'Permiso requerido. Ejecuta: sudo "{master}" --install', file=sys.stderr)
        return 1
    except OSError as error:
        print(f"No se pudo instalar {destination}: {error}", file=sys.stderr)
        return 1
    print(f"C-Forge instalado globalmente en {destination}")
    print("Ya puedes ejecutar: cforge --version")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
)CFV1DATA"},
        {R"CFV2DATA(compilador_nativo.py)CFV2DATA", R"CFV3DATA("""Backend nativo experimental de C-Forge: .cfv -> C++ -> ejecutable."""

from __future__ import annotations

import json
import re
import subprocess
import sys
import sysconfig
import textwrap
from dataclasses import dataclass
from pathlib import Path

from cforgev import CForgevError, Token, tokenize, validate_foreign_memory


Expr = tuple
Stmt = tuple


@dataclass
class Program:
    functions: list[Stmt]
    statements: list[Stmt]


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.current = 0
        self.structures: set[str] = set()
        self.declared: set[str] = set()
        self.universal_imports: dict[str, tuple[str, str]] = {}

    def program(self) -> Program:
        functions: list[Stmt] = []
        statements: list[Stmt] = []
        while self.peek().kind != "EOF":
            statement = self.statement()
            (functions if statement[0] == "function" else statements).append(statement)
        return Program(functions, statements)

    def statement(self) -> Stmt:
        if self.word("cluster"):
            declaration = self.statement()
            if declaration[0] == "let":
                return declaration + (True,)
            if declaration[0] == "function":
                return declaration + (True,)
            raise CForgevError("'cluster' solo puede modificar variables o funciones")
        if self.word("extern"):
            self.value("(", "Se esperaba '(' después de extern")
            language_token = self.advance()
            if language_token.kind != "STRING":
                raise CForgevError("Se esperaba 'python' o 'cpp'")
            language = json.loads(language_token.value)
            self.value(")", "Se esperaba ')' después del lenguaje")
            self.value("{", "Se esperaba '{' para abrir extern")
            body_token = self.advance()
            if body_token.kind != "FOREIGN":
                raise CForgevError("Se esperaba código extranjero literal")
            self.value("}", "Se esperaba '}' para cerrar extern")
            validate_foreign_memory(language, body_token.value, body_token.line)
            return ("extern", language, body_token.value)
        if self.word("test"):
            name_token = self.advance()
            if name_token.kind == "STRING":
                name = json.loads(name_token.value)
            elif name_token.kind == "IDENT":
                name = name_token.value
            else:
                raise CForgevError("test requiere un nombre")
            return ("test", name, self.block())
        if self.word("clase"):
            name = self.ident("Se esperaba el nombre de la clase")
            self.value("{", "Se esperaba '{'")
            fields: list[tuple[str, str]] = []
            methods: list[Stmt] = []
            while self.peek().value != "}" and self.peek().kind != "EOF":
                if self.word("campo"):
                    field = self.ident("Se esperaba el nombre del campo")
                    self.value(":", "Se esperaba ':'")
                    fields.append((field, self.ident("Se esperaba el tipo del campo")))
                    self.take(";")
                    continue
                if self.word("metodo"):
                    method = self.ident("Se esperaba el nombre del método")
                    self.value("(", "Se esperaba '('")
                    parameters: list[str] = []
                    if self.peek().value != ")":
                        while True:
                            parameters.append(self.ident("Se esperaba un parámetro"))
                            if not self.take(","):
                                break
                    self.value(")", "Se esperaba ')'")
                    methods.append(("method", name, method, parameters, self.block()))
                    continue
                raise CForgevError(f"Línea {self.peek().line}: se esperaba 'campo' o 'metodo'")
            self.value("}", "Falta '}' para cerrar la clase")
            self.structures.add(name)
            return ("class", name, fields, methods)
        if self.word("estructura"):
            name = self.ident("Se esperaba el nombre de la estructura")
            self.value("{", "Se esperaba '{'")
            fields: list[tuple[str, str]] = []
            while self.peek().value != "}" and self.peek().kind != "EOF":
                field = self.ident("Se esperaba el nombre del campo")
                self.value(":", "Se esperaba ':'")
                field_type = self.ident("Se esperaba el tipo del campo")
                fields.append((field, field_type))
                self.take(";")
            self.value("}", "Falta '}' para cerrar la estructura")
            self.structures.add(name)
            return ("structure", name, fields)
        if self.word("usar"):
            token = self.advance()
            if token.kind != "STRING":
                raise CForgevError(f"Línea {token.line}: se esperaba la ruta del módulo")
            self.take(";")
            return ("import", json.loads(token.value))
        if self.word("import"):
            ecosystem = self.ident("Se esperaba 'pip' o 'nuget'")
            if ecosystem not in {"pip", "nuget", "npm", "maven"}:
                raise CForgevError(f"Ecosistema desconocido '{ecosystem}'")
            self.value(":", "Se esperaba ':' después del ecosistema")
            package = self.ident("Se esperaba el nombre del paquete")
            self.take(";")
            self.universal_imports[package] = (ecosystem, package)
            self.declared.add(package)
            return ("universal_import", ecosystem, package)
        if self.word("gpu"):
            return ("gpu", self.block())
        if self.word("intentar"):
            protected = self.block()
            if not self.word("capturar"):
                raise CForgevError(f"Línea {self.peek().line}: se esperaba 'capturar'")
            self.value("(", "Se esperaba '('")
            error_name = self.ident("Se esperaba el nombre para el error")
            self.value(")", "Se esperaba ')'")
            return ("try", protected, error_name, self.block())
        if self.word("funcion"):
            name = self.ident("Se esperaba el nombre de la función")
            self.value("(", "Se esperaba '('")
            parameters: list[str] = []
            if self.peek().value != ")":
                while True:
                    parameters.append(self.ident("Se esperaba un parámetro"))
                    if not self.take(","):
                        break
            self.value(")", "Se esperaba ')'")
            return ("function", name, parameters, self.block())
        if self.word("sea"):
            name = self.ident("Se esperaba el nombre de la variable")
            self.declared.add(name)
            declared_type = None
            if self.take(":"):
                declared_type = self.ident("Se esperaba un tipo")
                if declared_type not in {
                    "numero", "texto", "booleano", "lista", "mapa", "nulo", "cualquiera"
                } and declared_type not in self.structures:
                    raise CForgevError(f"Tipo desconocido '{declared_type}'")
            self.value("=", "Se esperaba '='")
            expression = self.expression()
            self.take(";")
            return ("let", name, declared_type, expression)
        if self.word("mostrar") or self.word("print"):
            self.value("(", "Se esperaba '('")
            expression = self.expression()
            self.value(")", "Se esperaba ')'")
            self.take(";")
            return ("print", expression)
        if self.word("si"):
            condition = self.parenthesized()
            yes = self.block()
            no: list[Stmt] = []
            if self.word("sino"):
                no = self.block()
            return ("if", condition, yes, no)
        if self.word("mientras"):
            return ("while", self.parenthesized(), self.block())
        if self.word("retornar"):
            expression = self.expression()
            self.take(";")
            return ("return", expression)
        if (
            self.peek().kind == "IDENT" and self.next().value == "."
            and self.current + 3 < len(self.tokens) and self.tokens[self.current + 3].value == "="
        ):
            owner = self.advance().value
            self.advance()
            if owner != "este":
                raise CForgevError("Los campos solo pueden modificarse desde métodos mediante 'este'")
            field = self.ident("Se esperaba el campo")
            self.advance()
            expression = self.expression()
            self.take(";")
            return ("field_assign", owner, field, expression)
        if self.peek().kind == "IDENT" and self.next().value == "=":
            name = self.advance().value
            self.advance()
            expression = self.expression()
            self.take(";")
            if name not in self.declared:
                self.declared.add(name)
                return ("let", name, None, expression)
            return ("assign", name, expression)
        expression = self.expression()
        self.take(";")
        return ("expression", expression)

    def block(self) -> list[Stmt]:
        self.value("{", "Se esperaba '{'")
        result: list[Stmt] = []
        while self.peek().value != "}" and self.peek().kind != "EOF":
            result.append(self.statement())
        self.value("}", "Falta '}' para cerrar el bloque")
        return result

    def parenthesized(self) -> Expr:
        self.value("(", "Se esperaba '('")
        result = self.expression()
        self.value(")", "Se esperaba ')'")
        return result

    def expression(self) -> Expr:
        return self.logical_or()

    def logical_or(self) -> Expr:
        expression = self.logical_and()
        while self.word("o"):
            expression = ("binary", "o", expression, self.logical_and())
        return expression

    def logical_and(self) -> Expr:
        expression = self.equality()
        while self.word("y"):
            expression = ("binary", "y", expression, self.equality())
        return expression

    def equality(self) -> Expr:
        expression = self.comparison()
        while self.peek().value in {"==", "!="}:
            op = self.advance().value
            expression = ("binary", op, expression, self.comparison())
        return expression

    def comparison(self) -> Expr:
        expression = self.addition()
        while self.peek().value in {">", ">=", "<", "<="}:
            op = self.advance().value
            expression = ("binary", op, expression, self.addition())
        return expression

    def addition(self) -> Expr:
        expression = self.term()
        while self.peek().value in {"+", "-"}:
            op = self.advance().value
            expression = ("binary", op, expression, self.term())
        return expression

    def term(self) -> Expr:
        expression = self.unary()
        while self.peek().value in {"*", "/"}:
            op = self.advance().value
            expression = ("binary", op, expression, self.unary())
        return expression

    def unary(self) -> Expr:
        if self.word("no"):
            return ("unary", "no", self.unary())
        if self.take("-"):
            return ("unary", "-", self.unary())
        return self.primary()

    def primary(self) -> Expr:
        expression = self.atom()
        while self.take("["):
            key = self.expression()
            self.value("]", "Se esperaba ']'")
            expression = ("index", expression, key)
        while self.take("."):
            name = self.ident("Se esperaba el nombre del campo")
            if self.take("("):
                arguments: list[Expr] = []
                if self.peek().value != ")":
                    while True:
                        arguments.append(self.expression())
                        if not self.take(","):
                            break
                self.value(")", "Se esperaba ')' después del método")
                expression = ("method_call", expression, name, arguments)
            else:
                expression = ("field", expression, name)
        return expression

    def atom(self) -> Expr:
        token = self.advance()
        if token.kind == "NUMBER":
            return ("number", token.value)
        if token.kind == "STRING":
            return ("string", token.value)
        if token.kind == "IDENT":
            if token.value in {"verdadero", "falso"}:
                return ("bool", token.value == "verdadero")
            if token.value == "nulo":
                return ("null",)
            if self.take("("):
                arguments: list[Expr] = []
                if self.peek().value != ")":
                    while True:
                        arguments.append(self.expression())
                        if not self.take(","):
                            break
                self.value(")", "Se esperaba ')' después de los argumentos")
                return ("call", token.value, arguments)
            return ("variable", token.value)
        if token.value == "(":
            expression = self.expression()
            self.value(")", "Se esperaba ')'")
            return expression
        if token.value == "[":
            values: list[Expr] = []
            if self.peek().value != "]":
                while True:
                    values.append(self.expression())
                    if not self.take(","):
                        break
            self.value("]", "Se esperaba ']' para cerrar la lista")
            return ("list", values)
        if token.value == "{":
            values: list[tuple[Expr, Expr]] = []
            if self.peek().value != "}":
                while True:
                    key = self.expression()
                    self.value(":", "Se esperaba ':' después de la clave")
                    values.append((key, self.expression()))
                    if not self.take(","):
                        break
            self.value("}", "Se esperaba '}' para cerrar el mapa")
            return ("map", values)
        raise CForgevError(f"Línea {token.line}: expresión inválida cerca de {token.value!r}")

    def word(self, word: str) -> bool:
        if self.peek().kind == "IDENT" and self.peek().value == word:
            self.advance()
            return True
        return False

    def ident(self, message: str) -> str:
        if self.peek().kind != "IDENT":
            raise CForgevError(f"Línea {self.peek().line}: {message}")
        return self.advance().value

    def value(self, value: str, message: str) -> None:
        if not self.take(value):
            raise CForgevError(f"Línea {self.peek().line}: {message}")

    def take(self, value: str) -> bool:
        if self.peek().value == value:
            self.advance()
            return True
        return False

    def advance(self) -> Token:
        token = self.peek()
        if token.kind != "EOF":
            self.current += 1
        return token

    def peek(self) -> Token:
        return self.tokens[self.current]

    def next(self) -> Token:
        return self.tokens[min(self.current + 1, len(self.tokens) - 1)]


class StaticTypeAnalyzer:
    """Infiere tipos evidentes y rechaza contradicciones antes de invocar Clang."""

    def analyze(self, program: Program) -> None:
        self.statements(program.statements, {})
        for function in program.functions:
            self.statements(function[3], {parameter: "cualquiera" for parameter in function[2]})

    def statements(self, statements: list[Stmt], types: dict[str, str]) -> None:
        for statement in statements:
            kind = statement[0]
            if kind == "let":
                inferred = self.expression(statement[3], types)
                declared = statement[2] or inferred
                if statement[2] and inferred != "cualquiera" and statement[2] != inferred:
                    raise CForgevError(
                        f"Inferencia estática: '{statement[1]}' fue declarada {statement[2]} pero recibe {inferred}"
                    )
                types[statement[1]] = declared
            elif kind == "assign":
                inferred = self.expression(statement[2], types)
                expected = types.get(statement[1], "cualquiera")
                if expected != "cualquiera" and inferred != "cualquiera" and expected != inferred:
                    raise CForgevError(
                        f"Inferencia estática: '{statement[1]}' es {expected} y no puede recibir {inferred}"
                    )
            elif kind == "if":
                self.statements(statement[2], types)
                self.statements(statement[3], types)
            elif kind == "while":
                self.statements(statement[2], types)
            elif kind == "try":
                self.statements(statement[1], types)
                handler_types = dict(types)
                handler_types[statement[2]] = "texto"
                self.statements(statement[3], handler_types)
            elif kind == "gpu":
                self.statements(statement[1], types)
            elif kind == "extern":
                validate_foreign_memory(statement[1], statement[2])
            elif kind == "test":
                self.statements(statement[2], dict(types))

    def expression(self, expression: Expr, types: dict[str, str]) -> str:
        kind = expression[0]
        if kind == "number": return "numero"
        if kind == "string": return "texto"
        if kind == "bool": return "booleano"
        if kind == "null": return "nulo"
        if kind == "list": return "lista"
        if kind == "map": return "mapa"
        if kind == "variable": return types.get(expression[1], "cualquiera")
        if kind == "unary": return "booleano" if expression[1] == "no" else "numero"
        if kind == "binary":
            if expression[1] in {"==", "!=", ">", ">=", "<", "<=", "y", "o"}:
                return "booleano"
            left, right = self.expression(expression[2], types), self.expression(expression[3], types)
            return left if left == right else "cualquiera"
        return "cualquiera"


RUNTIME = r'''#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <variant>
#include <vector>
#include <cstring>
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
#endif
#ifdef CFV_WITH_PYTHON
#include <Python.h>
#endif
#ifdef CFV_WITH_JNI
#include <jni.h>
#endif
struct ForgeValue;struct CfvDenseMatrix;
using Value=ForgeValue;using Lista=std::shared_ptr<std::vector<ForgeValue>>;using Mapa=std::shared_ptr<std::map<std::string,ForgeValue>>;using FastArray=std::shared_ptr<std::vector<double>>;using DenseMatrix=std::shared_ptr<CfvDenseMatrix>;
struct CfvDenseMatrix{size_t rows=0,columns=0;std::vector<double>values;};
struct ForgeValue{std::variant<std::monostate,double,std::string,bool,Lista,Mapa,FastArray,DenseMatrix>data;std::string origin="cforgev";ForgeValue()=default;ForgeValue(double v):data(v){}ForgeValue(std::string v):data(std::move(v)){}ForgeValue(const char*v):data(std::string(v)){}ForgeValue(bool v):data(v){}ForgeValue(Lista v):data(std::move(v)){}ForgeValue(Mapa v):data(std::move(v)){}ForgeValue(FastArray v):data(std::move(v)){}ForgeValue(DenseMatrix v):data(std::move(v)){}size_t index()const{return data.index();}};
static ForgeValue cfv_origin(ForgeValue value,std::string origin){value.origin=std::move(origin);return value;}
enum CfvType{CFV_NULL=0,CFV_INTEGER=1,CFV_DECIMAL=2,CFV_TEXT=3};
using CfvReleaseFunction=void(*)(void*);
struct CfvValue{int32_t type;int64_t integer;double decimal;const char* text;void*owner;CfvReleaseFunction release;};
using CfvForeignFunction=int(*)(const CfvValue*,size_t,CfvValue*,char*,size_t);
#ifdef CFV_WITH_JNI
class CfvJvmRuntime{JavaVM*vm_=nullptr;JNIEnv*env_=nullptr;public:CfvJvmRuntime(){JavaVMInitArgs args{};JavaVMOption options[1];options[0].optionString=(char*)"-Djava.class.path=.";args.version=JNI_VERSION_1_8;args.nOptions=1;args.options=options;args.ignoreUnrecognized=JNI_FALSE;if(JNI_CreateJavaVM(&vm_,(void**)&env_,&args)!=JNI_OK)throw std::runtime_error("no se pudo crear JVM");}~CfvJvmRuntime(){if(vm_)vm_->DestroyJavaVM();}JNIEnv*env()const{return env_;}CfvJvmRuntime(const CfvJvmRuntime&)=delete;CfvJvmRuntime&operator=(const CfvJvmRuntime&)=delete;};
#endif
static std::map<std::string,CfvForeignFunction>&cfv_registry(){static std::map<std::string,CfvForeignFunction>value;return value;}
static std::mutex cfv_symbol_mutex;static std::map<std::string,ForgeValue*>cfv_symbols;
static void cfv_share_symbol(const std::string&name,ForgeValue*value){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);cfv_symbols[name]=value;}
static ForgeValue cfv_symbol(const std::string&name){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);auto found=cfv_symbols.find(name);if(found==cfv_symbols.end()||!found->second)throw std::runtime_error("símbolo global desconocido: "+name);return *found->second;}
static ForgeValue cfv_symbol_snapshot(){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);auto map=std::make_shared<std::map<std::string,ForgeValue>>();for(const auto&[name,value]:cfv_symbols)if(value)(*map)[name]=*value;return map;}
#ifdef _WIN32
extern "C" __declspec(dllexport) int cfv_register_function(const char*name,CfvForeignFunction fn){if(!name||!fn)return 1;cfv_registry()[name]=fn;return 0;}
#else
extern "C" __attribute__((visibility("default"))) int cfv_register_function(const char*name,CfvForeignFunction fn){if(!name||!fn)return 1;cfv_registry()[name]=fn;return 0;}
#endif
static double numero(const Value& v) { if (auto p=std::get_if<double>(&v.data)) return *p; throw std::runtime_error("se esperaba un número"); }
static bool verdad(const Value& v) { if (auto p=std::get_if<bool>(&v.data)) return *p; throw std::runtime_error("se esperaba verdadero o falso"); }
static Value suma(const Value&a,const Value&b){ if(a.index()==1&&b.index()==1)return numero(a)+numero(b); if(a.index()==2&&b.index()==2)return std::get<std::string>(a.data)+std::get<std::string>(b.data); throw std::runtime_error("'+' requiere dos números o dos textos"); }
static Value resta(const Value&a,const Value&b){return numero(a)-numero(b);} static Value multiplica(const Value&a,const Value&b){return numero(a)*numero(b);}
static Value divide(const Value&a,const Value&b){double d=numero(b);if(d==0)throw std::runtime_error("no se puede dividir por cero");return numero(a)/d;}
static Value compara(const Value&a,const Value&b,const std::string&o){if(o=="==")return a.data==b.data;if(o=="!=")return a.data!=b.data;if(a.index()==1&&b.index()==1){double x=numero(a),y=numero(b);if(o==">")return x>y;if(o==">=")return x>=y;if(o=="<")return x<y;return x<=y;}if(a.index()==2&&b.index()==2){auto x=std::get<std::string>(a.data),y=std::get<std::string>(b.data);if(o==">")return x>y;if(o==">=")return x>=y;if(o=="<")return x<y;return x<=y;}throw std::runtime_error("comparación entre tipos incompatibles");}
static std::string cfv_number_text(double value){std::ostringstream stream;if(std::floor(value)==value)stream<<(long long)value;else stream<<value;return stream.str();}
static std::string texto(const Value&v){if(v.index()==0)return "nulo";if(auto p=std::get_if<double>(&v.data))return cfv_number_text(*p);if(auto p=std::get_if<std::string>(&v.data))return *p;if(auto p=std::get_if<bool>(&v.data))return *p?"verdadero":"falso";if(auto p=std::get_if<Lista>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=", ";s+=texto((*p)->at(i));}return s+"]";}if(auto p=std::get_if<Mapa>(&v.data)){std::string s="{";bool first=true;for(auto&[k,x]:**p){if(!first)s+=", ";first=false;s+="\""+k+"\": "+texto(x);}return s+"}";}if(auto p=std::get_if<FastArray>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=", ";s+=cfv_number_text((*p)->at(i));}return s+"]";}if(auto p=std::get_if<DenseMatrix>(&v.data)){std::string s="[";for(size_t row=0;row<(*p)->rows;++row){if(row)s+=", ";s+="[";for(size_t column=0;column<(*p)->columns;++column){if(column)s+=", ";s+=cfv_number_text((*p)->values[row*(*p)->columns+column]);}s+="]";}return s+"]";}throw std::runtime_error("ForgeValue desconocido");}
static void mostrar(const Value&v){std::cout<<texto(v)<<'\n';}
static Value cfv_leer(Value mensaje=Value{std::string("")}){if(mensaje.index()!=2)throw std::runtime_error("el mensaje de leer debe ser texto");std::cout<<std::get<std::string>(mensaje.data);std::string s;std::getline(std::cin,s);return s;}
static Value cfv_a_numero(const Value&v){try{if(auto p=std::get_if<double>(&v.data))return *p;if(auto p=std::get_if<std::string>(&v.data))return std::stod(*p);}catch(...){ }throw std::runtime_error("no se puede convertir a número");}
static Value cfv_a_texto(const Value&v){return texto(v);}
static Value cfv_longitud(const Value&v){if(auto p=std::get_if<std::string>(&v.data))return (double)p->size();if(auto p=std::get_if<Lista>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<Mapa>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<FastArray>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<DenseMatrix>(&v.data))return (double)(*p)->rows;throw std::runtime_error("longitud requiere una colección");}
static Value cfv_agregar(Value lista,Value valor){if(auto p=std::get_if<Lista>(&lista.data)){(*p)->push_back(std::move(valor));return Value{};}throw std::runtime_error("agregar requiere una lista");}
static Value cfv_sys_run(const Value&command){if(command.index()!=2)throw std::runtime_error("sys_run requiere un comando de texto");std::string shell=std::get<std::string>(command.data)+" 2>&1";
#ifdef _WIN32
FILE*pipe=_popen(shell.c_str(),"r");
#else
FILE*pipe=popen(shell.c_str(),"r");
#endif
if(!pipe)throw std::runtime_error("sys_run no pudo iniciar el comando");std::string output;char buffer[4096];while(std::fgets(buffer,sizeof(buffer),pipe))output+=buffer;
#ifdef _WIN32
int status=_pclose(pipe);
#else
int raw=pclose(pipe);int status=WIFEXITED(raw)?WEXITSTATUS(raw):raw;
#endif
auto result=std::make_shared<std::map<std::string,Value>>();(*result)["estado"]=(double)status;(*result)["salida"]=output;(*result)["error"]=std::string("");return result;}
static Value cfv_sys_info(){uint64_t memory=0;
#ifdef __APPLE__
size_t memory_size=sizeof(memory);sysctlbyname("hw.memsize",&memory,&memory_size,nullptr,0);
#elif !defined(_WIN32)
long pages=sysconf(_SC_PHYS_PAGES),page_size=sysconf(_SC_PAGE_SIZE);if(pages>0&&page_size>0)memory=(uint64_t)pages*(uint64_t)page_size;
#endif
auto result=std::make_shared<std::map<std::string,Value>>();(*result)["nucleos"]=(double)std::max(1u,std::thread::hardware_concurrency());(*result)["ram_bytes"]=(double)memory;
#if defined(__aarch64__) || defined(__arm64__)
(*result)["cpu"]=std::string("arm64");
#elif defined(__x86_64__) || defined(_M_X64)
(*result)["cpu"]=std::string("x86_64");
#else
(*result)["cpu"]=std::string("desconocido");
#endif
#ifdef __APPLE__
(*result)["sistema"]=std::string("macOS");
#elif defined(_WIN32)
(*result)["sistema"]=std::string("Windows");
#else
(*result)["sistema"]=std::string("Linux");
#endif
return result;}
static std::filesystem::path cfv_base_archivos;
static std::string ruta_archivo(const Value&v){if(v.index()!=2)throw std::runtime_error("la ruta debe ser texto");auto p=std::filesystem::path(std::get<std::string>(v.data));return (p.is_absolute()?p:cfv_base_archivos/p).string();}
static Value cfv_leer_archivo(const Value&ruta){std::ifstream f(ruta_archivo(ruta),std::ios::binary);if(!f)throw std::runtime_error("no se pudo abrir el archivo");std::ostringstream s;s<<f.rdbuf();return s.str();}
static Value cfv_escribir_archivo(const Value&ruta,const Value&contenido){if(contenido.index()!=2)throw std::runtime_error("el contenido debe ser texto");auto p=std::filesystem::path(ruta_archivo(ruta));if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);if(!f)throw std::runtime_error("no se pudo escribir el archivo");f<<std::get<std::string>(contenido.data);return Value{};}
static Value cfv_file_read(const Value&ruta){return cfv_leer_archivo(ruta);}
static Value cfv_file_write(const Value&ruta,const Value&contenido){return cfv_escribir_archivo(ruta,contenido);}
static Value cfv_file_append(const Value&ruta,const Value&contenido){if(contenido.index()!=2)throw std::runtime_error("el contenido debe ser texto");auto p=std::filesystem::path(ruta_archivo(ruta));if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary|std::ios::app);if(!f)throw std::runtime_error("no se pudo anexar al archivo");f<<std::get<std::string>(contenido.data);return Value{};}
static Value cfv_existe_archivo(const Value&ruta){return std::filesystem::exists(ruta_archivo(ruta));}
static Value cfv_array_fast(const Value&input){auto list=std::get_if<Lista>(&input.data);if(!list)throw std::runtime_error("array_fast requiere una lista numérica");auto output=std::make_shared<std::vector<double>>();output->reserve((*list)->size());for(const auto&value:**list)output->push_back(numero(value));return output;}
static Value cfv_matrix(const Value&rows_value,const Value&columns_value,const Value&fill_value=Value{0.0}){double rows_number=numero(rows_value),columns_number=numero(columns_value),fill=numero(fill_value);if(rows_number<0||columns_number<0||std::floor(rows_number)!=rows_number||std::floor(columns_number)!=columns_number||rows_number*columns_number>10000000.0)throw std::runtime_error("dimensiones de matrix inválidas");auto matrix=std::make_shared<CfvDenseMatrix>();matrix->rows=(size_t)rows_number;matrix->columns=(size_t)columns_number;matrix->values.assign(matrix->rows*matrix->columns,fill);return matrix;}
#ifdef _WIN32
static Value cfv_net_send(const Value&,const Value&,const Value&){throw std::runtime_error("net_send requiere backend Winsock");}
static Value cfv_net_listen(const Value&,const Value&=Value{5000.0}){throw std::runtime_error("net_listen requiere backend Winsock");}
#else
struct CfvSocket{int value=-1;explicit CfvSocket(int descriptor=-1):value(descriptor){}~CfvSocket(){if(value>=0)::close(value);}CfvSocket(const CfvSocket&)=delete;CfvSocket&operator=(const CfvSocket&)=delete;};
static Value cfv_net_send(const Value&host_value,const Value&port_value,const Value&data_value){if(host_value.index()!=2||data_value.index()!=2)throw std::runtime_error("net_send requiere host, puerto y texto");int port=(int)numero(port_value);if(port<1||port>65535)throw std::runtime_error("puerto inválido");addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;addrinfo*raw=nullptr;auto port_text=std::to_string(port);if(getaddrinfo(std::get<std::string>(host_value.data).c_str(),port_text.c_str(),&hints,&raw)!=0)throw std::runtime_error("no se pudo resolver el host");std::unique_ptr<addrinfo,decltype(&freeaddrinfo)>addresses(raw,freeaddrinfo);int descriptor=-1;for(auto*entry=raw;entry;entry=entry->ai_next){descriptor=socket(entry->ai_family,entry->ai_socktype,entry->ai_protocol);if(descriptor>=0&&connect(descriptor,entry->ai_addr,entry->ai_addrlen)==0)break;if(descriptor>=0)::close(descriptor);descriptor=-1;}CfvSocket connection(descriptor);if(descriptor<0)throw std::runtime_error("net_send no pudo conectar");const auto&data=std::get<std::string>(data_value.data);size_t sent=0;while(sent<data.size()){ssize_t count=send(descriptor,data.data()+sent,data.size()-sent,0);if(count<=0)throw std::runtime_error("net_send perdió la conexión");sent+=(size_t)count;}return (double)sent;}
static Value cfv_net_listen(const Value&port_value,const Value&timeout_value=Value{5000.0}){int port=(int)numero(port_value),timeout=(int)numero(timeout_value);if(port<1||port>65535||timeout<0)throw std::runtime_error("puerto o timeout inválido");CfvSocket server(socket(AF_INET,SOCK_STREAM,0));if(server.value<0)throw std::runtime_error("net_listen no pudo crear socket");int reuse=1;setsockopt(server.value,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));sockaddr_in address{};address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);address.sin_port=htons((uint16_t)port);if(bind(server.value,(sockaddr*)&address,sizeof(address))!=0||listen(server.value,1)!=0)throw std::runtime_error("net_listen no pudo abrir el puerto");fd_set set;FD_ZERO(&set);FD_SET(server.value,&set);timeval wait{timeout/1000,(timeout%1000)*1000};int ready=select(server.value+1,&set,nullptr,nullptr,&wait);if(ready==0)throw std::runtime_error("net_listen agotó el tiempo de espera");if(ready<0)throw std::runtime_error("net_listen falló esperando conexión");sockaddr_storage peer{};socklen_t peer_size=sizeof(peer);CfvSocket client(accept(server.value,(sockaddr*)&peer,&peer_size));if(client.value<0)throw std::runtime_error("net_listen no pudo aceptar conexión");std::string data;char buffer[65536];for(;;){ssize_t count=recv(client.value,buffer,sizeof(buffer),0);if(count<0)throw std::runtime_error("net_listen falló recibiendo datos");if(count==0)break;data.append(buffer,(size_t)count);}char host[NI_MAXHOST]={0};getnameinfo((sockaddr*)&peer,peer_size,host,sizeof(host),nullptr,0,NI_NUMERICHOST);auto result=std::make_shared<std::map<std::string,Value>>();(*result)["datos"]=data;(*result)["host"]=std::string(host);(*result)["puerto"]=(double)port;return result;}
#endif
static Value cfv_raiz(const Value&v){double n=numero(v);if(n<0)throw std::runtime_error("no existe raíz real negativa");return std::sqrt(n);}static Value cfv_absoluto(const Value&v){return std::abs(numero(v));}static Value cfv_redondear(const Value&v){return std::round(numero(v));}static Value cfv_potencia(const Value&a,const Value&b){return std::pow(numero(a),numero(b));}
static Value cfv_tiempo_actual(){using namespace std::chrono;return duration<double>(system_clock::now().time_since_epoch()).count();}
static Value cfv_argumentos_global;
static Value cfv_argumentos(){return cfv_argumentos_global;}
static std::mutex cfv_jit_mutex;
static std::map<std::string,size_t>cfv_jit_counts;
static void cfv_jit_hit(const std::string&name){std::lock_guard<std::mutex>lock(cfv_jit_mutex);++cfv_jit_counts[name];}
static Value cfv_jit_estado(const Value&name){if(name.index()!=2)throw std::runtime_error("jit_estado requiere texto");std::lock_guard<std::mutex>lock(cfv_jit_mutex);return (double)cfv_jit_counts[std::get<std::string>(name.data)];}
static Value cfv_jit_caliente(const Value&name){return numero(cfv_jit_estado(name))>=1000.0;}
static std::mutex cfv_cluster_mutex;
static std::map<std::string,std::string>cfv_cluster_symbols;
static void cfv_cluster_register(const std::string&name,const std::string&kind){std::lock_guard<std::mutex>lock(cfv_cluster_mutex);cfv_cluster_symbols[name]=kind;}
static Value cfv_cluster_estado(){std::lock_guard<std::mutex>lock(cfv_cluster_mutex);auto values=std::make_shared<std::vector<Value>>();for(const auto&entry:cfv_cluster_symbols)values->push_back(entry.second+":"+entry.first);return values;}
static Value cfv_afirmar(const Value&condition,const Value&message=Value{std::string("la condición es falsa")}){if(condition.index()!=3)throw std::runtime_error("afirmar requiere booleano");if(!verdad(condition))throw std::runtime_error("afirmación fallida: "+texto(message));return Value{};}
static Value cfv_parallel_unary(const std::function<Value(Value)>&fn,const Value&jobs){auto list=std::get_if<Lista>(&jobs.data);if(!list)throw std::runtime_error("paralelo requiere una lista");std::vector<std::future<Value>>running;running.reserve((*list)->size());for(const auto&job:**list)running.push_back(std::async(std::launch::async,[fn,job]{return fn(job);}));auto results=std::make_shared<std::vector<Value>>();results->reserve(running.size());for(auto&future:running)results->push_back(future.get());return results;}
static Value cfv_nuget_path(const std::string&package){std::vector<std::filesystem::path>paths={cfv_base_archivos/(package+".dylib"),cfv_base_archivos/"build"/(package+".dylib"),cfv_base_archivos.parent_path()/"build"/package/(package+".dylib"),cfv_base_archivos.parent_path()/"build"/"csharp-native"/(package+".dylib")};for(const auto&path:paths)if(std::filesystem::exists(path))return path.string();return paths.front().string();}
static std::vector<CfvValue> cfv_to_abi(const Value&args,std::vector<std::string>&storage){auto p=std::get_if<Lista>(&args.data);if(!p)throw std::runtime_error("los argumentos extranjeros deben ser una lista");std::vector<CfvValue>out;storage.reserve((*p)->size());for(const auto&v:**p){if(v.index()==0)out.push_back({CFV_NULL,0,0,nullptr,nullptr,nullptr});else if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n)out.push_back({CFV_INTEGER,(int64_t)*n,0,nullptr,nullptr,nullptr});else out.push_back({CFV_DECIMAL,0,*n,nullptr,nullptr,nullptr});}else if(auto s=std::get_if<std::string>(&v.data)){storage.push_back(*s);out.push_back({CFV_TEXT,0,0,storage.back().c_str(),nullptr,nullptr});}else throw std::runtime_error("ABI extranjero solo acepta números, textos y nulo");}return out;}
struct CfvResultGuard{CfvValue*value;~CfvResultGuard(){if(value&&value->release){auto release=value->release;auto owner=value->owner;value->release=nullptr;release(owner);}}};
static Value cfv_from_abi(const CfvValue&v){if(v.type==CFV_NULL)return Value{};if(v.type==CFV_INTEGER)return (double)v.integer;if(v.type==CFV_DECIMAL)return v.decimal;if(v.type==CFV_TEXT)return std::string(v.text?v.text:"");throw std::runtime_error("tipo ABI de retorno desconocido");}
static Value cfv_invoke_foreign(CfvForeignFunction fn,const Value&args){std::vector<std::string>storage;auto abi=cfv_to_abi(args,storage);CfvValue result{CFV_NULL,0,0,nullptr,nullptr,nullptr};CfvResultGuard guard{&result};char error[1024]={0};int status=0;try{status=fn(abi.data(),abi.size(),&result,error,sizeof(error));}catch(const std::exception&e){throw std::runtime_error(std::string("excepción C++: ")+e.what());}catch(...){throw std::runtime_error("excepción nativa desconocida");}if(status!=0)throw std::runtime_error(error[0]?error:"la función extranjera falló");return cfv_from_abi(result);}
static Value cfv_use_cpp(const Value&name,const Value&args){if(name.index()!=2)throw std::runtime_error("el nombre C++ debe ser texto");auto key=std::get<std::string>(name.data);auto&registry=cfv_registry();auto it=registry.find(key);if(it==registry.end())throw std::runtime_error("función C++ no registrada: "+key);return cfv_invoke_foreign(it->second,args);}
static std::map<std::string,void*>cfv_libraries;
static Value cfv_use_native(const Value&path,const Value&symbol,const Value&args){if(path.index()!=2||symbol.index()!=2)throw std::runtime_error("ruta y símbolo deben ser texto");auto raw=std::filesystem::path(std::get<std::string>(path.data));auto file=(raw.is_absolute()?raw:cfv_base_archivos/raw).string();auto sym=std::get<std::string>(symbol.data);void*handle=nullptr;auto found=cfv_libraries.find(file);if(found!=cfv_libraries.end())handle=found->second;else{
#ifdef _WIN32
handle=(void*)LoadLibraryA(file.c_str());
#else
handle=dlopen(file.c_str(),RTLD_NOW|RTLD_LOCAL);
#endif
if(!handle)throw std::runtime_error("no se pudo cargar la librería: "+file);cfv_libraries[file]=handle;}
#ifdef _WIN32
auto fn=(CfvForeignFunction)GetProcAddress((HMODULE)handle,sym.c_str());
#else
auto fn=(CfvForeignFunction)dlsym(handle,sym.c_str());
#endif
if(!fn)throw std::runtime_error("símbolo extranjero no encontrado: "+sym);return cfv_invoke_foreign(fn,args);}
#ifdef CFV_WITH_PYTHON
class PyRef{PyObject*object_=nullptr;public:explicit PyRef(PyObject*object=nullptr):object_(object){}~PyRef(){Py_XDECREF(object_);}PyRef(const PyRef&)=delete;PyRef&operator=(const PyRef&)=delete;PyRef(PyRef&&other)noexcept:object_(other.object_){other.object_=nullptr;}PyObject*get()const{return object_;}PyObject*release(){auto*out=object_;object_=nullptr;return out;}explicit operator bool()const{return object_!=nullptr;}};
static std::string cfv_python_error(const std::string&context){if(!PyErr_Occurred())return context;PyObject*type=nullptr;PyObject*value=nullptr;PyObject*traceback=nullptr;PyErr_Fetch(&type,&value,&traceback);PyErr_NormalizeException(&type,&value,&traceback);PyRef type_ref(type),value_ref(value),traceback_ref(traceback);PyRef text(PyObject_Str(value?value:Py_None));const char*message=text?PyUnicode_AsUTF8(text.get()):nullptr;return context+(message?std::string(": ")+message:"");}
static PyRef cfv_to_python(const Value&v){if(v.index()==0){Py_INCREF(Py_None);return PyRef(Py_None);}if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n)return PyRef(PyLong_FromLongLong((long long)*n));return PyRef(PyFloat_FromDouble(*n));}if(auto s=std::get_if<std::string>(&v.data))return PyRef(PyUnicode_FromString(s->c_str()));if(auto b=std::get_if<bool>(&v.data))return PyRef(PyBool_FromLong(*b));if(auto list=std::get_if<Lista>(&v.data)){PyRef out(PyList_New((*list)->size()));for(size_t i=0;i<(*list)->size();++i){auto item=cfv_to_python((*list)->at(i));PyList_SET_ITEM(out.get(),i,item.release());}return out;}if(auto map=std::get_if<Mapa>(&v.data)){PyRef out(PyDict_New());for(const auto&[key,value]:**map){auto item=cfv_to_python(value);if(PyDict_SetItemString(out.get(),key.c_str(),item.get())!=0)throw std::runtime_error(cfv_python_error("mapa Python inválido"));}return out;}if(auto array=std::get_if<FastArray>(&v.data)){PyRef out(PyList_New((*array)->size()));for(size_t i=0;i<(*array)->size();++i)PyList_SET_ITEM(out.get(),i,PyFloat_FromDouble((*array)->at(i)));return out;}if(auto matrix=std::get_if<DenseMatrix>(&v.data)){PyRef out(PyList_New((*matrix)->rows));for(size_t row=0;row<(*matrix)->rows;++row){PyObject*values=PyList_New((*matrix)->columns);for(size_t column=0;column<(*matrix)->columns;++column)PyList_SET_ITEM(values,column,PyFloat_FromDouble((*matrix)->values[row*(*matrix)->columns+column]));PyList_SET_ITEM(out.get(),row,values);}return out;}throw std::runtime_error("tipo no compatible con Python");}
static Value cfv_from_python(PyObject*o){if(o==Py_None)return Value{};if(PyBool_Check(o))return Value{o==Py_True};if(PyLong_Check(o)){auto value=PyLong_AsLongLong(o);if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("entero Python inválido"));return (double)value;}if(PyFloat_Check(o)){auto value=PyFloat_AsDouble(o);if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("decimal Python inválido"));return value;}if(PyUnicode_Check(o)){auto text=PyUnicode_AsUTF8(o);if(!text)throw std::runtime_error(cfv_python_error("texto Python inválido"));return std::string(text);}if(PyList_Check(o)||PyTuple_Check(o)){auto out=std::make_shared<std::vector<Value>>();Py_ssize_t size=PySequence_Size(o);for(Py_ssize_t i=0;i<size;++i){PyRef item(PySequence_GetItem(o,i));out->push_back(cfv_from_python(item.get()));}return out;}if(PyDict_Check(o)){auto out=std::make_shared<std::map<std::string,Value>>();PyObject*key;PyObject*value;Py_ssize_t pos=0;while(PyDict_Next(o,&pos,&key,&value)){if(!PyUnicode_Check(key))throw std::runtime_error("claves extranjeras deben ser texto");(*out)[PyUnicode_AsUTF8(key)]=cfv_from_python(value);}return out;}throw std::runtime_error("Python devolvió un tipo no compatible");}
static Value cfv_use_python(const Value&module,const Value&function,const Value&args){if(module.index()!=2||function.index()!=2)throw std::runtime_error("módulo y función Python deben ser texto");if(!Py_IsInitialized())Py_Initialize();auto context=cfv_to_python(cfv_symbol_snapshot());if(!context||PyDict_SetItemString(PyEval_GetBuiltins(),"ForgeSymbols",context.get())!=0)throw std::runtime_error(cfv_python_error("no se pudo publicar ForgeSymbols"));if(PyRun_SimpleString("import sys,types,builtins\nif 'cforgev_runtime' not in sys.modules:\n m=types.ModuleType('cforgev_runtime');m.get=lambda name: builtins.ForgeSymbols[name];m.snapshot=lambda: dict(builtins.ForgeSymbols);sys.modules['cforgev_runtime']=m")!=0)throw std::runtime_error(cfv_python_error("no se pudo publicar cforgev_runtime"));PyRef m(PyImport_ImportModule(std::get<std::string>(module.data).c_str()));if(!m)throw std::runtime_error(cfv_python_error("no se pudo importar el módulo Python"));PyRef f(PyObject_GetAttrString(m.get(),std::get<std::string>(function.data).c_str()));if(!f)throw std::runtime_error(cfv_python_error("función Python inexistente"));if(!PyCallable_Check(f.get()))throw std::runtime_error("el atributo Python no es invocable");auto list=std::get_if<Lista>(&args.data);if(!list)throw std::runtime_error("argumentos Python deben ser lista");PyRef tuple(PyTuple_New((*list)->size()));if(!tuple)throw std::runtime_error(cfv_python_error("no se pudo crear argumentos Python"));for(size_t i=0;i<(*list)->size();++i){auto argument=cfv_to_python((*list)->at(i));if(!argument)throw std::runtime_error(cfv_python_error("no se pudo convertir argumento Python"));PyTuple_SET_ITEM(tuple.get(),i,argument.release());}PyRef result(PyObject_CallObject(f.get(),tuple.get()));if(!result)throw std::runtime_error(cfv_python_error("la llamada Python falló"));return cfv_origin(cfv_from_python(result.get()),"python");}
static void cfv_exec_python_code(const std::string&code){std::cout.flush();if(!Py_IsInitialized())Py_Initialize();if(PyRun_SimpleString(code.c_str())!=0)throw std::runtime_error(cfv_python_error("extern Python falló"));PyRun_SimpleString("import sys; sys.stdout.flush(); sys.stderr.flush()");}
static void cfv_prepare_polyglot(){static bool ready=false;if(ready)return;if(!Py_IsInitialized())Py_Initialize();const char*code=R"CFVPY(
import hashlib, json, subprocess, tempfile, pathlib, urllib.request
def _cfv_hash(value):
    raw=json.dumps(value,ensure_ascii=False,sort_keys=True,separators=(",",":")).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()
def _cfv_json_parse(text):
    return json.loads(text)
def _cfv_fetch(url):
    if not url.startswith(("https://","http://")): raise ValueError("sys_fetch solo acepta HTTP o HTTPS")
    request=urllib.request.Request(url,headers={"User-Agent":"C-Forge/native"})
    with urllib.request.urlopen(request,timeout=15) as response:
        payload=response.read(16*1024*1024+1)
        if len(payload)>16*1024*1024: raise ValueError("sys_fetch superó el límite de 16 MiB")
        return payload.decode(response.headers.get_content_charset() or "utf-8")
def _cfv_js(module, function, args, context):
    script=f"""(async()=>{{globalThis.ForgeSymbols={json.dumps(context)};const m=require({json.dumps(module)});const f=m[{json.dumps(function)}]??m.default?.[{json.dumps(function)}];if(typeof f!=="function")throw new Error("función JS inexistente");const r=await f(...{json.dumps(args)});process.stdout.write("__CFV__"+JSON.stringify(r===undefined?null:r));}})().catch(e=>{{console.error(e.stack??String(e));process.exit(1)}})"""
    run=subprocess.run(["node","-e",script],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr.strip())
    return json.loads(run.stdout.rsplit("__CFV__",1)[1])
def _cfv_exec_js(code, typescript=False):
    with tempfile.TemporaryDirectory() as directory:
        path=pathlib.Path(directory)/("extern.ts" if typescript else "extern.js");path.write_text(code)
        run=subprocess.run(["node",str(path)],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr.strip())
    print(run.stdout,end="")
def _cfv_exec_java(code):
    with tempfile.TemporaryDirectory() as directory:
        path=pathlib.Path(directory)/"CForgevExtern.java";path.write_text("public final class CForgevExtern { public static void main(String[] a) throws Exception {\n"+code+"\n}}")
        build=subprocess.run(["javac",str(path)],capture_output=True,text=True)
        if build.returncode: raise RuntimeError(build.stderr.strip())
        run=subprocess.run(["java","-cp",directory,"CForgevExtern"],capture_output=True,text=True)
    if run.returncode: raise RuntimeError(run.stderr.strip())
    print(run.stdout,end="")
)CFVPY";if(PyRun_SimpleString(code)!=0)throw std::runtime_error(cfv_python_error("no se pudo preparar puente políglota"));ready=true;}
static Value cfv_forge_hash(const Value&value){cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(value);return cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_hash")},Value{args}),"cforgev");}
static Value cfv_json_parse(const Value&text){if(text.index()!=2)throw std::runtime_error("json_parse requiere texto");cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(text);return cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_json_parse")},Value{args}),"cforgev");}
static Value cfv_sys_fetch(const Value&url){if(url.index()!=2)throw std::runtime_error("sys_fetch requiere una URL");cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(url);return cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_fetch")},Value{args}),"cforgev");}
static Value cfv_use_javascript(const Value&module,const Value&function,const Value&args){cfv_prepare_polyglot();Value resolved=module;if(module.index()==2){auto raw=std::filesystem::path(std::get<std::string>(module.data));if(!raw.is_absolute()&&raw.string().find('/')!=std::string::npos)resolved=(cfv_base_archivos/raw).string();}auto packed=std::make_shared<std::vector<Value>>();packed->push_back(resolved);packed->push_back(function);packed->push_back(args);packed->push_back(cfv_symbol_snapshot());return cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_js")},Value{packed}),"javascript");}
static Value cfv_use_java(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("use_java requiere un JDK instalado; puente JNI/JAR preparado pero JVM no disponible");}
static void cfv_exec_javascript_code(const std::string&code,bool typescript){std::cout.flush();cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(code);args->push_back(typescript);(void)cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_exec_js")},Value{args});PyRun_SimpleString("import sys; sys.stdout.flush(); sys.stderr.flush()");}
static void cfv_exec_java_code(const std::string&code){cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(code);(void)cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_exec_java")},Value{args});}
#else
static Value cfv_use_python(const Value&,const Value&,const Value&){throw std::runtime_error("este ejecutable no fue enlazado con Python");}
static Value cfv_forge_hash(const Value&){throw std::runtime_error("forge_hash requiere el núcleo ForgeValue");}
static Value cfv_json_parse(const Value&){throw std::runtime_error("json_parse requiere el núcleo ForgeValue");}
static Value cfv_sys_fetch(const Value&){throw std::runtime_error("sys_fetch requiere el conector HTTP");}
static void cfv_exec_python_code(const std::string&){throw std::runtime_error("extern Python requiere Python embebido");}
static Value cfv_use_javascript(const Value&,const Value&,const Value&){throw std::runtime_error("JavaScript requiere soporte políglota");}
static Value cfv_use_java(const Value&,const Value&,const Value&,const Value&){throw std::runtime_error("Java requiere soporte políglota");}
static void cfv_exec_javascript_code(const std::string&,bool){throw std::runtime_error("JavaScript requiere soporte políglota");}
static void cfv_exec_java_code(const std::string&){throw std::runtime_error("Java requiere soporte políglota");}
#endif
static Value cfv_forge_bench(const std::function<Value()>&function,const Value&count){long long iterations=(long long)numero(count);if(iterations<1||iterations>10000000)throw std::runtime_error("forge_bench requiere 1..10.000.000 iteraciones");Value result;auto started=std::chrono::steady_clock::now();for(long long i=0;i<iterations;++i)result=function();double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();auto report=std::make_shared<std::map<std::string,Value>>();(*report)["resultado"]=result;(*report)["iteraciones"]=(double)iterations;(*report)["segundos"]=seconds;(*report)["por_segundo"]=seconds>0?iterations/seconds:0;return report;}
static Value crear_lista(std::initializer_list<Value>v){return std::make_shared<std::vector<Value>>(v);}static Value crear_mapa(std::initializer_list<std::pair<const std::string,Value>>v){return std::make_shared<std::map<std::string,Value>>(v);}
static Value indice(const Value&v,const Value&k){double n=0;if(auto p=std::get_if<Lista>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de lista inválido");return (*p)->at((size_t)n);}if(auto p=std::get_if<Mapa>(&v.data)){if(k.index()!=2)throw std::runtime_error("la clave debe ser texto");auto it=(*p)->find(std::get<std::string>(k.data));if(it==(*p)->end())throw std::runtime_error("clave inexistente");return it->second;}if(auto p=std::get_if<FastArray>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de array_fast inválido");return (*p)->at((size_t)n);}if(auto p=std::get_if<DenseMatrix>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->rows)throw std::runtime_error("fila de matrix inválida");auto row=std::make_shared<std::vector<double>>((*p)->values.begin()+(size_t)n*(*p)->columns,(*p)->values.begin()+((size_t)n+1)*(*p)->columns);return row;}throw std::runtime_error("el valor no admite índices");}
static void asignar_campo(Value&obj,const std::string&campo,Value valor,size_t tipo){auto p=std::get_if<Mapa>(&obj.data);if(!p||(*p)->find(campo)==(*p)->end())throw std::runtime_error("campo desconocido '"+campo+"'");if(tipo!=99&&valor.index()!=tipo)throw std::runtime_error("tipo incompatible para campo '"+campo+"'");(**p)[campo]=std::move(valor);}
static void asignar(Value&destino,size_t tipo,Value valor,const std::string&nombre){if(tipo!=99&&valor.index()!=tipo)throw std::runtime_error("tipo incompatible al asignar '"+nombre+"'");destino=std::move(valor);}
'''


def safe(name: str) -> str:
    return "cfv_" + re.sub(r"[^A-Za-z0-9_]", "_", name)


class Generator:
    def __init__(self, program: Program, base_dir: Path) -> None:
        self.program = program
        self.base_dir = base_dir
        self.structures = {
            statement[1]: statement[2]
            for statement in program.statements
            if statement[0] == "structure"
        }
        self.classes = {
            statement[1]: (statement[2], statement[3])
            for statement in program.statements if statement[0] == "class"
        }
        self.universal_imports = {
            statement[2]: (statement[1], statement[2])
            for statement in program.statements if statement[0] == "universal_import"
        }
        self.active_class: str | None = None
        self.register_next_global = False

    def generate(self) -> str:
        prototypes = [self.structure_prototype(name, fields) + ";" for name, fields in self.structures.items()]
        prototypes += [self.structure_prototype(name, fields) + ";" for name, (fields, _) in self.classes.items()]
        prototypes += [self.method_prototype(method) + ";" for _, methods in self.classes.values() for method in methods]
        prototypes += [self.prototype(function) + ";" for function in self.program.functions]
        functions = [self.structure_function(name, fields) for name, fields in self.structures.items()]
        functions += [self.class_function(name, fields) for name, (fields, _) in self.classes.items()]
        functions += [self.method_function(method) for _, methods in self.classes.values() for method in methods]
        functions += [self.function(function) for function in self.program.functions]
        functions.append(self.method_dispatcher())
        main_parts: list[str] = []
        for top_level_statement in self.program.statements:
            self.register_next_global = True
            main_parts.append(self.statement(top_level_statement, 2))
        self.register_next_global = False
        main = "".join(main_parts)
        cluster_functions = "".join(
            f'    cfv_cluster_register("{function[1]}", "funcion");\n'
            for function in self.program.functions if len(function) > 4 and function[4] is True
        )
        base = json.dumps(str(self.base_dir), ensure_ascii=False)
        return RUNTIME + "\n" + "\n".join(prototypes) + "\n" + "\n".join(functions) + f'''\nint main(int argc, char** argv){{
  try {{
    cfv_base_archivos = std::filesystem::path({base});
    auto cfv_args_lista = std::make_shared<std::vector<Value>>();
    for(int i=1;i<argc;++i) cfv_args_lista->push_back(Value{{std::string(argv[i])}});
    cfv_argumentos_global = Value{{cfv_args_lista}};
{cluster_functions}{main}
    return 0;
  }} catch(const std::exception& e) {{ std::cerr << "[C-Forge Runtime Exception] " << e.what() << '\\n'; return 1; }}
  catch(...) {{ std::cerr << "[C-Forge Runtime Exception] excepción nativa desconocida\\n"; return 1; }}
}}
'''

    def prototype(self, function: Stmt) -> str:
        return f"Value {safe(function[1])}(" + ", ".join(f"Value {safe(p)}" for p in function[2]) + ")"

    def structure_prototype(self, name: str, fields: list[tuple[str, str]]) -> str:
        return f"Value {safe(name)}(" + ", ".join(f"Value {safe(field)}" for field, _ in fields) + ")"

    def structure_function(self, name: str, fields: list[tuple[str, str]]) -> str:
        indexes = {"nulo": 0, "numero": 1, "texto": 2, "booleano": 3, "lista": 4, "mapa": 5, "cualquiera": 99}
        checks = ""
        pairs = []
        for field, field_type in fields:
            expected = indexes.get(field_type, 5)
            if expected != 99:
                checks += f'  if ({safe(field)}.index() != {expected}) throw std::runtime_error("tipo incompatible para {field}");\n'
            pairs.append('{"' + field + '", ' + safe(field) + '}')
        return self.structure_prototype(name, fields) + " {\n" + checks + "  return crear_mapa({" + ", ".join(pairs) + "});\n}"

    def class_function(self, name: str, fields: list[tuple[str, str]]) -> str:
        base = self.structure_function(name, fields)
        return base.replace("return crear_mapa({", f'return crear_mapa({{{{"__clase", Value{{std::string("{name}")}}}}, ', 1)

    def method_prototype(self, method: Stmt) -> str:
        _, class_name, name, parameters, _ = method
        params = ["Value cfv_este", *(f"Value {safe(p)}" for p in parameters)]
        return f"Value {safe(class_name + '_' + name)}(" + ", ".join(params) + ")"

    def method_function(self, method: Stmt) -> str:
        trackers = "  size_t cfv_este_tipo = cfv_este.index();\n" + "".join(
            f"  size_t {safe(p)}_tipo = {safe(p)}.index();\n" for p in method[3]
        )
        previous = self.active_class
        self.active_class = method[1]
        body = self.statements(method[4], 2)
        self.active_class = previous
        return self.method_prototype(method) + " {\n" + trackers + body + "  return Value{};\n}"

    def method_dispatcher(self) -> str:
        lines = ["Value cfv_llamar_metodo(Value objeto,const std::string& nombre,std::vector<Value> args){", '  auto clase=texto(indice(objeto,Value{std::string("__clase")}));']
        for class_name, (_, methods) in self.classes.items():
            for method in methods:
                params = ", ".join(["objeto", *(f"args[{i}]" for i in range(len(method[3])))])
                lines.append(f'  if(clase=="{class_name}"&&nombre=="{method[2]}"){{if(args.size()!={len(method[3])})throw std::runtime_error("cantidad incorrecta de argumentos");return {safe(class_name + "_" + method[2])}({params});}}')
        lines.append('  throw std::runtime_error("método desconocido");\n}')
        return "\n".join(lines)

    def function(self, function: Stmt) -> str:
        trackers = "".join(
            f"  size_t {safe(parameter)}_tipo = {safe(parameter)}.index();\n"
            for parameter in function[2]
        )
        hot = f'  cfv_jit_hit("{function[1]}");\n'
        return self.prototype(function) + " {\n" + hot + trackers + self.statements(function[3], 2) + "  return Value{};\n}"

    def statements(self, statements: list[Stmt], indent: int) -> str:
        return "".join(self.statement(statement, indent) for statement in statements)

    def statement(self, statement: Stmt, indent: int) -> str:
        pad = " " * indent
        kind = statement[0]
        register_global = self.register_next_global
        self.register_next_global = False
        if kind == "let":
            type_indexes = {"nulo": 0, "numero": 1, "texto": 2, "booleano": 3, "lista": 4, "mapa": 5, "cualquiera": 99}
            name, declared, expression = statement[1], statement[2], self.expr(statement[3])
            type_value = str(type_indexes.get(declared, 5)) if declared else f"{safe(name)}.index()"
            validation = ""
            if declared and declared != "cualquiera":
                validation = f'{pad}if ({safe(name)}.index() != {type_value}) throw std::runtime_error("tipo incompatible para {name}");\n'
            cluster = f'{pad}cfv_cluster_register("{name}", "variable");\n' if len(statement) > 4 and statement[4] is True else ""
            shared = f'{pad}cfv_share_symbol("{name}", &{safe(name)});\n' if register_global else ""
            return f"{pad}Value {safe(name)} = {expression};\n{validation}{pad}size_t {safe(name)}_tipo = {type_value};\n{shared}{cluster}"
        if kind == "assign":
            return f'{pad}asignar({safe(statement[1])}, {safe(statement[1])}_tipo, {self.expr(statement[2])}, "{statement[1]}");\n'
        if kind == "field_assign":
            expected = 99
            if statement[1] == "este" and self.active_class:
                for field, field_type in self.classes[self.active_class][0]:
                    if field == statement[2]:
                        expected = {"nulo":0,"numero":1,"texto":2,"booleano":3,"lista":4,"mapa":5,"cualquiera":99}.get(field_type, 5)
            return f'{pad}asignar_campo({safe(statement[1])}, "{statement[2]}", {self.expr(statement[3])}, {expected});\n'
        if kind == "print":
            return f"{pad}mostrar({self.expr(statement[1])});\n"
        if kind == "expression":
            return f"{pad}(void)({self.expr(statement[1])});\n"
        if kind == "return":
            return f"{pad}return {self.expr(statement[1])};\n"
        if kind == "if":
            yes = self.statements(statement[2], indent + 2)
            result = f"{pad}if (verdad({self.expr(statement[1])})) {{\n{yes}{pad}}}"
            if statement[3]:
                no = self.statements(statement[3], indent + 2)
                result += f" else {{\n{no}{pad}}}"
            return result + "\n"
        if kind == "while":
            body = self.statements(statement[2], indent + 2)
            return f"{pad}while (verdad({self.expr(statement[1])})) {{\n{body}{pad}}}\n"
        if kind == "gpu":
            body = self.statements(statement[1], indent + 4)
            return (
                f"{pad}{{ // gpu: backend CPU paralelo; punto de extensión Metal/CUDA\n"
                f"{pad}  auto cfv_gpu_task = std::async(std::launch::async, [&]() {{\n"
                f"{body}{pad}  }});\n{pad}  cfv_gpu_task.get();\n{pad}}}\n"
            )
        if kind == "extern":
            if statement[1] == "python":
                code = textwrap.dedent(statement[2]).strip("\n") + "\n"
                return f'{pad}cfv_exec_python_code(R"CFV_EXTERN({code})CFV_EXTERN");\n'
            if statement[1] in {"javascript", "typescript"}:
                code = textwrap.dedent(statement[2]).strip("\n") + "\n"
                typescript = "true" if statement[1] == "typescript" else "false"
                return f'{pad}cfv_exec_javascript_code(R"CFV_EXTERN({code})CFV_EXTERN", {typescript});\n'
            if statement[1] == "java":
                code = textwrap.dedent(statement[2]).strip("\n") + "\n"
                return f'{pad}cfv_exec_java_code(R"CFV_EXTERN({code})CFV_EXTERN");\n'
            return f"{pad}[&]() {{\n{statement[2]}\n{pad}}}();\n"
        if kind == "test":
            body = self.statements(statement[2], indent + 2)
            return f'{pad}{{ // test: {statement[1]}\n{body}{pad}}}\n'
        if kind == "try":
            protected = self.statements(statement[1], indent + 2)
            handler = self.statements(statement[3], indent + 2)
            name = safe(statement[2])
            return (
                f"{pad}try {{\n{protected}{pad}}} catch(const std::exception& cfv_error_nativo) {{\n"
                f"{pad}  Value {name} = std::string(cfv_error_nativo.what());\n"
                f"{pad}  size_t {name}_tipo = {name}.index();\n{handler}{pad}}}\n"
            )
        if kind in {"structure", "class", "universal_import"}:
            return ""
        if kind == "import":
            raise CForgevError("La importación no fue resuelta antes de generar código")
        raise CForgevError(f"Instrucción no compilable: {kind}")

    def expr(self, expression: Expr) -> str:
        kind = expression[0]
        if kind == "number": return f"Value{{{expression[1]}.0}}" if "." not in expression[1] else f"Value{{{expression[1]}}}"
        if kind == "string": return f"Value{{std::string({expression[1]})}}"
        if kind == "bool": return "Value{true}" if expression[1] else "Value{false}"
        if kind == "null": return "Value{}"
        if kind == "variable": return safe(expression[1])
        if kind == "list": return "crear_lista({" + ", ".join(self.expr(item) for item in expression[1]) + "})"
        if kind == "map":
            pairs = []
            for key, value in expression[1]:
                if key[0] != "string": raise CForgevError("Las claves de mapa deben ser textos")
                pairs.append("{" + key[1] + ", " + self.expr(value) + "}")
            return "crear_mapa({" + ", ".join(pairs) + "})"
        if kind == "index": return f"indice({self.expr(expression[1])}, {self.expr(expression[2])})"
        if kind == "field": return f'indice({self.expr(expression[1])}, Value{{std::string("{expression[2]}")}})'
        if kind == "method_call":
            receiver = expression[1]
            if receiver[0] == "variable" and receiver[1] in self.universal_imports:
                ecosystem, package = self.universal_imports[receiver[1]]
                args = "crear_lista({" + ", ".join(self.expr(arg) for arg in expression[3]) + "})"
                if ecosystem == "pip":
                    return f'cfv_use_python(Value{{std::string("{package}")}}, Value{{std::string("{expression[2]}")}}, {args})'
                if ecosystem == "npm":
                    return f'cfv_use_javascript(Value{{std::string("{package}")}}, Value{{std::string("{expression[2]}")}}, {args})'
                if ecosystem == "maven":
                    if expression[2] != "call" or len(expression[3]) != 3:
                        raise CForgevError("maven usa paquete.call(clase, método, argumentos)")
                    jar = f'Value{{(cfv_base_archivos / "build" / "maven" / "{package}.jar").string()}}'
                    return f'cfv_use_java({jar}, {self.expr(expression[3][0])}, {self.expr(expression[3][1])}, {self.expr(expression[3][2])})'
                library = f'cfv_nuget_path("{package}")'
                return f'cfv_use_native({library}, Value{{std::string("{expression[2]}")}}, {args})'
            return f'cfv_llamar_metodo({self.expr(receiver)}, "{expression[2]}", std::vector<Value>{{' + ", ".join(self.expr(arg) for arg in expression[3]) + "})"
        if kind == "call":
            aliases = {"use_csharp": "use_native", "use_typescript": "use_javascript"}
            call_name = aliases.get(expression[1], expression[1])
            if call_name == "jit_estado":
                return "cfv_jit_estado(" + ", ".join(self.expr(arg) for arg in expression[2]) + ")"
            if call_name == "jit_caliente":
                return "cfv_jit_caliente(" + ", ".join(self.expr(arg) for arg in expression[2]) + ")"
            if call_name == "cluster_estado":
                return "cfv_cluster_estado()"
            if call_name == "paralelo" and len(expression[2]) == 2 and expression[2][0][0] == "string":
                function_name = json.loads(expression[2][0][1])
                return f'cfv_parallel_unary([](Value cfv_job){{return {safe(function_name)}(cfv_job);}}, {self.expr(expression[2][1])})'
            if call_name == "forge_bench":
                if len(expression[2]) not in {2, 3} or expression[2][0][0] != "string":
                    raise CForgevError(
                        "forge_bench requiere nombre, iteraciones y una lista de argumentos opcional"
                    )
                function_name = json.loads(expression[2][0][1])
                function = next(
                    (item for item in self.program.functions if item[1] == function_name), None
                )
                if function is None:
                    raise CForgevError(f"forge_bench no conoce la función '{function_name}'")
                provided = expression[2][2][1] if len(expression[2]) == 3 and expression[2][2][0] == "list" else []
                if len(expression[2]) == 3 and expression[2][2][0] != "list":
                    raise CForgevError("forge_bench requiere una lista de argumentos")
                if len(provided) != len(function[2]):
                    raise CForgevError("forge_bench recibió una cantidad incorrecta de argumentos")
                invocation = f"{safe(function_name)}(" + ", ".join(self.expr(arg) for arg in provided) + ")"
                return (
                    f"cfv_forge_bench([&](){{return {invocation};}}, "
                    f"{self.expr(expression[2][1])})"
                )
            return f"{safe(call_name)}(" + ", ".join(self.expr(arg) for arg in expression[2]) + ")"
        if kind == "unary":
            if expression[1] == "no": return f"Value{{!verdad({self.expr(expression[2])})}}"
            return f"Value{{-numero({self.expr(expression[2])})}}"
        if kind == "binary":
            functions = {"+":"suma","-":"resta","*":"multiplica","/":"divide"}
            op, left, right = expression[1], self.expr(expression[2]), self.expr(expression[3])
            if op == "y": return f"Value{{verdad({left}) && verdad({right})}}"
            if op == "o": return f"Value{{verdad({left}) || verdad({right})}}"
            return f"{functions[op]}({left}, {right})" if op in functions else f'compara({left}, {right}, "{op}")'
        raise CForgevError(f"Expresión no compilable: {kind}")


def compile_native(
    source_path: Path,
    output_path: Path,
    extra_sources: list[Path] | None = None,
) -> Path:
    try:
        source = source_path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {source_path}: {error}") from error
    program = resolve_imports(Parser(tokenize(source)).program(), source_path.resolve().parent, set())
    StaticTypeAnalyzer().analyze(program)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    cpp_path = output_path.with_suffix(".cpp")
    generated = Generator(program, source_path.resolve().parent).generate()
    linked_sources = list(extra_sources or [])
    automatic_sources = discover_cpp_sources(source_path, program, linked_sources)
    linked_sources.extend(automatic_sources)
    if automatic_sources:
        print(
            "C-Forge Auto-Link C++: "
            + ", ".join(str(path) for path in automatic_sources)
        )
    if linked_sources:
        generated += "\n#ifndef CFV_EXPORT\n#ifdef _WIN32\n#define CFV_EXPORT __declspec(dllexport)\n#else\n#define CFV_EXPORT __attribute__((visibility(\"default\")))\n#endif\n#endif\n#define CFORGEV_FFI_H\n"
        for linked_source in linked_sources:
            try:
                linked_code = linked_source.read_text(encoding="utf-8")
            except OSError as error:
                raise CForgevError(
                    f"No se pudo vincular {linked_source}: {error}"
                ) from error
            generated += f"\n// Fuente C++ unificada: {linked_source}\n{linked_code}\n"
    cpp_path.write_text(generated, encoding="utf-8")
    command = ["clang++", "-std=c++17", "-O2", str(cpp_path)]
    project_include = Path(__file__).resolve().parent / "include"
    command += ["-I", str(project_include)]
    for linked_source in linked_sources:
        command += ["-I", str(linked_source.resolve().parent)]
    if ("'use_python'" in repr(program) or "'universal_import', 'pip'" in repr(program)
            or any(name in repr(program) for name in ("'forge_hash'", "'json_parse'", "'sys_fetch'"))
            or "'extern', 'python'" in repr(program) or "'use_javascript'" in repr(program)
            or "'use_typescript'" in repr(program) or "'universal_import', 'npm'" in repr(program)
            or "'use_java'" in repr(program) or "'universal_import', 'maven'" in repr(program)
            or "'extern', 'javascript'" in repr(program) or "'extern', 'typescript'" in repr(program)
            or "'extern', 'java'" in repr(program)):
        command += python_embedding_flags()
        command += ["-DCFV_WITH_PYTHON"]
    if sys.platform.startswith("linux"):
        command += ["-ldl"]
    command += ["-o", str(output_path)]
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as error:
        raise CForgevError(
            "No se encontró clang++; instala las herramientas de desarrollo de C++"
        ) from error
    if result.returncode != 0:
        raise CForgevError("El compilador C++ rechazó el programa:\n" + result.stderr)
    return output_path


def _cpp_symbols(node: object) -> set[str]:
    """Extrae nombres literales usados por use_cpp del AST completo."""
    symbols: set[str] = set()
    if isinstance(node, Program):
        return _cpp_symbols(node.functions) | _cpp_symbols(node.statements)
    if isinstance(node, (tuple, list)):
        if (
            len(node) >= 3 and node[0] == "call" and node[1] == "use_cpp"
            and isinstance(node[2], list) and node[2]
            and isinstance(node[2][0], tuple) and node[2][0][0] == "string"
        ):
            symbols.add(json.loads(node[2][0][1]))
        for child in node:
            symbols.update(_cpp_symbols(child))
    return symbols


def discover_cpp_sources(
    source_path: Path,
    program: Program,
    explicit_sources: list[Path],
) -> list[Path]:
    """Descubre fuentes registrables para use_cpp sin enlazar código arbitrario."""
    required = _cpp_symbols(program)
    if not required:
        return []
    explicit_resolved = {path.resolve() for path in explicit_sources}
    roots = [
        source_path.resolve().parent / "interop",
        source_path.resolve().parent / "native",
        source_path.resolve().parent / "cpp",
        source_path.resolve().parent / "ejemplos" / "interop",
        source_path.resolve().parent.parent / "ejemplos" / "interop",
        Path(__file__).resolve().parent / "ejemplos" / "interop",
    ]
    candidates: list[Path] = []
    seen: set[Path] = set()
    for root in roots:
        if not root.is_dir():
            continue
        for candidate in sorted(root.glob("*.cpp")):
            resolved = candidate.resolve()
            if resolved not in seen and resolved not in explicit_resolved:
                seen.add(resolved)
                candidates.append(resolved)

    selected: list[Path] = []
    covered: set[str] = set()
    pattern = re.compile(r'cfv_register_function\s*\(\s*"([^"]+)"')
    for candidate in candidates:
        try:
            registered = set(pattern.findall(candidate.read_text(encoding="utf-8")))
        except OSError:
            continue
        if registered & required:
            selected.append(candidate)
            covered.update(registered)

    explicit_covered: set[str] = set()
    for path in explicit_sources:
        try:
            explicit_covered.update(pattern.findall(path.read_text(encoding="utf-8")))
        except OSError:
            pass
    missing = required - covered - explicit_covered
    if missing:
        names = ", ".join(sorted(missing))
        raise CForgevError(
            "Auto-Link C++ no encontró implementaciones para: " + names
            + ". Coloca una fuente .cpp registrable en interop/, native/ o cpp/, "
              "o usa --vincular explícitamente."
        )
    return selected


def python_embedding_flags() -> list[str]:
    framework_root = Path(
        "/Library/Developer/CommandLineTools/Library/Frameworks"
    )
    headers = framework_root / "Python3.framework/Headers"
    binary = framework_root / "Python3.framework/Python3"
    if headers.joinpath("Python.h").exists() and binary.exists():
        return [
            "-I", str(headers), "-F", str(framework_root), "-framework", "Python3",
            f"-Wl,-rpath,{framework_root}",
        ]
    include = Path(sysconfig.get_config_var("INCLUDEPY") or "")
    library_dir = Path(sysconfig.get_config_var("LIBDIR") or "")
    library = sysconfig.get_config_var("LDLIBRARY") or ""
    match = re.match(r"lib(.+?)\.(?:so|dylib|a)", library)
    if include.joinpath("Python.h").exists() and match:
        return [
            "-I", str(include), "-L", str(library_dir), f"-l{match.group(1)}",
            f"-Wl,-rpath,{library_dir}",
        ]
    raise CForgevError(
        "use_python requiere Python.h y una biblioteca Python embebible"
    )


def resolve_imports(program: Program, base_dir: Path, loaded: set[Path]) -> Program:
    functions = list(program.functions)
    statements: list[Stmt] = []
    for statement in program.statements:
        if statement[0] != "import":
            statements.append(statement)
            continue
        module_path = (base_dir / statement[1]).resolve()
        if module_path in loaded:
            continue
        if module_path.suffix != ".cfv":
            raise CForgevError("Los módulos deben terminar en .cfv")
        loaded.add(module_path)
        try:
            source = module_path.read_text(encoding="utf-8")
        except OSError as error:
            raise CForgevError(f"No se pudo importar '{statement[1]}': {error}") from error
        imported = resolve_imports(
            Parser(tokenize(source)).program(), module_path.parent, loaded
        )
        functions.extend(imported.functions)
        statements.extend(imported.statements)
    return Program(functions, statements)
)CFV3DATA"},
        {R"CFV4DATA(compilador_wasm.py)CFV4DATA", R"CFV5DATA("""Backend WebAssembly inicial de C-Forge: subconjunto numérico -> WAT válido."""

from __future__ import annotations

from pathlib import Path

from cforgev import CForgevError, tokenize
from compilador_nativo import Parser, Program, resolve_imports, safe


class WasmGenerator:
    def __init__(self, program: Program) -> None:
        self.program = program
        self.locals: set[str] = set()

    def generate(self) -> str:
        for statement in self.program.statements:
            if statement[0] == "let":
                self.locals.add(statement[1])
        declarations = "\n".join(
            f"    (local ${safe(name)} f64)" for name in sorted(self.locals)
        )
        body = "\n".join(self.statement(statement) for statement in self.program.statements)
        return (
            ";; C-Forge WebAssembly 0.9 — módulo WAT completo\n"
            "(module\n"
            '  (import "env" "cfv_print_f64" (func $cfv_print_f64 (param f64)))\n'
            '  (func (export "_start")\n'
            f"{declarations}\n{body}\n"
            "  )\n"
            ")\n"
        )

    def statement(self, statement: tuple) -> str:
        kind = statement[0]
        if kind == "let":
            return self.expr(statement[3]) + f"\n    local.set ${safe(statement[1])}"
        if kind == "assign":
            if statement[1] not in self.locals:
                raise CForgevError(f"Wasm: variable no declarada '{statement[1]}'")
            return self.expr(statement[2]) + f"\n    local.set ${safe(statement[1])}"
        if kind == "print":
            return self.expr(statement[1]) + "\n    call $cfv_print_f64"
        if kind in {"universal_import", "structure", "class"}:
            return "    ;; declaración sin código Wasm"
        raise CForgevError(
            f"Wasm 0.9 todavía no admite la instrucción '{kind}'; usa números, variables y mostrar"
        )

    def expr(self, expression: tuple) -> str:
        kind = expression[0]
        if kind == "number":
            return f"    f64.const {float(expression[1])}"
        if kind == "variable":
            if expression[1] not in self.locals:
                raise CForgevError(f"Wasm: variable desconocida '{expression[1]}'")
            return f"    local.get ${safe(expression[1])}"
        if kind == "unary" and expression[1] == "-":
            return "    f64.const -1\n" + self.expr(expression[2]) + "\n    f64.mul"
        if kind == "binary" and expression[1] in {"+", "-", "*", "/"}:
            opcode = {"+": "add", "-": "sub", "*": "mul", "/": "div"}[expression[1]]
            return self.expr(expression[2]) + "\n" + self.expr(expression[3]) + f"\n    f64.{opcode}"
        raise CForgevError(f"Wasm 0.9 todavía no puede traducir la expresión '{kind}'")


def compile_wasm(source_path: Path, output_path: Path) -> Path:
    if output_path.suffix not in {".wat", ".wast"}:
        raise CForgevError("El backend Wasm 0.9 genera texto WebAssembly; usa extensión .wat")
    try:
        source = source_path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {source_path}: {error}") from error
    program = resolve_imports(Parser(tokenize(source)).program(), source_path.resolve().parent, set())
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(WasmGenerator(program).generate(), encoding="utf-8")
    return output_path
)CFV5DATA"},
        {R"CFV6DATA(include/cforgev_ffi.h)CFV6DATA", R"CFV7DATA(#ifndef CFORGEV_FFI_H
#define CFORGEV_FFI_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define CFV_EXPORT __declspec(dllexport)
#else
#define CFV_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum CfvType {
    CFV_NULL = 0,
    CFV_INTEGER = 1,
    CFV_DECIMAL = 2,
    CFV_TEXT = 3
} CfvType;

typedef void (*CfvReleaseFunction)(void* owner);

typedef struct CfvValue {
    int32_t type;
    int64_t integer;
    double decimal;
    const char* text;
    void* owner;
    CfvReleaseFunction release;
} CfvValue;

typedef int (*CfvForeignFunction)(
    const CfvValue* arguments,
    size_t argument_count,
    CfvValue* result,
    char* error_buffer,
    size_t error_buffer_size
);

CFV_EXPORT int cfv_register_function(const char* name, CfvForeignFunction function);

#ifdef __cplusplus
}
#endif

#endif
)CFV7DATA"},
        {R"CFV8DATA(herramientas/cforgev_ffi_runner.cpp)CFV8DATA", R"CFV9DATA(#include "cforgev_ffi.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

int main(int argc, char** argv) {
    if (argc < 3) return 2;
#ifdef _WIN32
    HMODULE library = LoadLibraryA(argv[1]);
    auto function = library ? reinterpret_cast<CfvForeignFunction>(GetProcAddress(library, argv[2])) : nullptr;
#else
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    auto function = library ? reinterpret_cast<CfvForeignFunction>(dlsym(library, argv[2])) : nullptr;
#endif
    if (!function) { std::cerr << "no se pudo cargar la función extranjera"; return 3; }
    std::vector<std::string> texts;
    std::vector<CfvValue> values;
    texts.reserve(argc - 3);
    values.reserve(argc - 3);
    for (int i = 3; i < argc; ++i) {
        std::string value = argv[i];
        if (value == "n:") values.push_back({CFV_NULL, 0, 0, nullptr});
        else if (value.rfind("i:", 0) == 0) values.push_back({CFV_INTEGER, std::stoll(value.substr(2)), 0, nullptr});
        else if (value.rfind("d:", 0) == 0) values.push_back({CFV_DECIMAL, 0, std::stod(value.substr(2)), nullptr});
        else if (value.rfind("s:", 0) == 0) { texts.push_back(value.substr(2)); values.push_back({CFV_TEXT, 0, 0, texts.back().c_str()}); }
        else { std::cerr << "argumento ABI inválido"; return 4; }
    }
    CfvValue result{CFV_NULL, 0, 0, nullptr, nullptr, nullptr};
    char error[1024] = {};
    int status = function(values.data(), values.size(), &result, error, sizeof(error));
    if (status) { std::cerr << (error[0] ? error : "función extranjera falló"); return status; }
    std::cout << result.type << '\n';
    if (result.type == CFV_INTEGER) std::cout << result.integer;
    else if (result.type == CFV_DECIMAL) std::cout << result.decimal;
    else if (result.type == CFV_TEXT && result.text) std::cout << result.text;
    std::cout.flush();
    if (result.release) result.release(result.owner);
    return 0;
}
)CFV9DATA"},
        {R"CFV10DATA(herramientas/cforge_cli.cpp)CFV10DATA", R"CFV11DATA(#include <cerrno>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {

std::filesystem::path find_engine(const char* executable) {
    const auto binary = std::filesystem::absolute(executable).parent_path();
    const std::vector<std::filesystem::path> candidates = {
        std::filesystem::current_path() / "cforgev.py",
        binary / "cforgev.py",
        binary.parent_path() / "cforgev.py",
    };
    for (const auto& candidate : candidates) {
        if (std::filesystem::exists(candidate)) {
            return std::filesystem::canonical(candidate);
        }
    }
    throw std::runtime_error("no se encontró cforgev.py junto al proyecto");
}

void print_help() {
    std::cout
        << "C-Forge Toolchain 1.4.0\n"
        << "Uso:\n"
        << "  cforge fmt archivo.cfv\n"
        << "  cforge test archivo.cfv\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        print_help();
        return 0;
    }
    if (argc != 3 || (std::string(argv[1]) != "fmt" && std::string(argv[1]) != "test")) {
        print_help();
        return 2;
    }
    try {
        const auto engine = find_engine(argv[0]).string();
        std::vector<std::string> owned = {"python3", engine, argv[1], argv[2]};
        std::vector<char*> arguments;
        for (auto& value : owned) {
            arguments.push_back(value.data());
        }
        arguments.push_back(nullptr);
#ifdef _WIN32
        const int status = _spawnvp(_P_WAIT, "python3", arguments.data());
        if (status < 0) {
            throw std::runtime_error("no se pudo iniciar python3");
        }
        return status;
#else
        execvp("python3", arguments.data());
        throw std::runtime_error("no se pudo iniciar python3: error " + std::to_string(errno));
#endif
    } catch (const std::exception& error) {
        std::cerr << "[C-Forge Toolchain Exception] " << error.what() << '\n';
        return 1;
    }
}
)CFV11DATA"},
        {R"CFV12DATA(herramientas/vscode-cforgev/package.json)CFV12DATA", R"CFV13DATA({
  "name": "cforgev-language",
  "displayName": "C-Forge Language Support",
  "description": "Resaltado de sintaxis y configuración oficial para el lenguaje C-Forge (.cfv)",
  "version": "1.3.2",
  "publisher": "vemoris-group",
  "author": { "name": "Vemoris Group" },
  "license": "SEE LICENSE IN LICENSE",
  "icon": "images/icon.png",
  "preview": true,
  "pricing": "Free",
  "engines": { "vscode": "^1.80.0" },
  "categories": ["Programming Languages"],
  "keywords": ["c-forge", "cforge", "cforgev", "cfv", "programming language", "syntax highlighting", "vemoris", "compiler"],
  "galleryBanner": { "color": "#FF6A32", "theme": "light" },
  "repository": { "type": "git", "url": "https://github.com/VemorisGroup/C-Forge.git" },
  "homepage": "https://github.com/VemorisGroup/C-Forge#readme",
  "bugs": { "url": "https://github.com/VemorisGroup/C-Forge/issues" },
  "scripts": { "package": "vsce package" },
  "devDependencies": { "@vscode/vsce": "^3.6.2" },
  "contributes": {
    "languages": [{
      "id": "cforgev",
      "aliases": ["C-Forge", "cforgev"],
      "extensions": [".cfv"],
      "configuration": "./language-configuration.json",
      "icon": {
        "light": "./images/icon.png",
        "dark": "./images/icon.png"
      }
    }],
    "grammars": [{
      "language": "cforgev",
      "scopeName": "source.cforgev",
      "path": "./syntaxes/cforgev.tmLanguage.json"
    }]
  }
}
)CFV13DATA"},
        {R"CFV14DATA(herramientas/vscode-cforgev/language-configuration.json)CFV14DATA", R"CFV15DATA({
  "comments": { "lineComment": "//" },
  "brackets": [["{", "}"], ["[", "]"], ["(", ")"]],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"" }
  ]
}
)CFV15DATA"},
        {R"CFV16DATA(herramientas/vscode-cforgev/syntaxes/cforgev.tmLanguage.json)CFV16DATA", R"CFV17DATA({
  "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
  "name": "C-Forge",
  "scopeName": "source.cforgev",
  "patterns": [
    { "include": "#comments" },
    { "include": "#externPython" },
    { "include": "#externCpp" },
    { "include": "#externJavaScript" },
    { "include": "#externJava" },
    { "include": "#strings" },
    { "include": "#numbers" },
    { "include": "#types" },
    { "include": "#keywords" },
    { "include": "#functions" }
  ],
  "repository": {
    "comments": { "patterns": [{ "name": "comment.line.double-slash.cforgev", "match": "//.*$" }] },
    "strings": { "patterns": [{ "name": "string.quoted.double.cforgev", "begin": "\"", "end": "\"", "patterns": [{ "name": "constant.character.escape.cforgev", "match": "\\\\." }] }] },
    "numbers": { "patterns": [{ "name": "constant.numeric.cforgev", "match": "\\b\\d+(?:\\.\\d+)?\\b" }] },
    "types": { "patterns": [{ "name": "storage.type.cforgev", "match": "\\b(numero|texto|booleano|lista|mapa|nulo|cualquiera)\\b" }] },
    "externPython": { "patterns": [{ "name": "meta.embedded.block.python.cforgev", "begin": "\\bextern\\s*\\(\\s*\"python\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.python" }] },
    "externCpp": { "patterns": [{ "name": "meta.embedded.block.cpp.cforgev", "begin": "\\bextern\\s*\\(\\s*\"cpp\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.cpp" }] },
    "externJavaScript": { "patterns": [{ "name": "meta.embedded.block.javascript.cforgev", "begin": "\\bextern\\s*\\(\\s*\"(?:javascript|typescript)\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.js" }] },
    "externJava": { "patterns": [{ "name": "meta.embedded.block.java.cforgev", "begin": "\\bextern\\s*\\(\\s*\"java\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.java" }] },
    "keywords": { "patterns": [
      { "name": "keyword.control.test.cforgev", "match": "\\b(test|afirmar)\\b" },
      { "name": "keyword.control.gpu.cforgev", "match": "\\bgpu\\b" },
      { "name": "storage.modifier.cluster.cforgev", "match": "\\bcluster\\b" },
      { "name": "keyword.control.cforgev", "match": "\\b(sea|si|sino|mientras|funcion|retornar|estructura|clase|campo|metodo|este|usar|import|pip|nuget|npm|maven|extern|intentar|capturar|verdadero|falso|y|o|no)\\b" }
    ] },
    "functions": { "patterns": [{ "name": "support.function.cforgev", "match": "\\b(mostrar|print|leer|leer_archivo|escribir_archivo|existe_archivo|file_read|file_write|file_append|sys_run|sys_info|net_listen|net_send|matrix|array_fast|longitud|agregar|a_numero|a_texto|raiz|potencia|absoluto|redondear|tiempo_actual|argumentos|use_python|use_csharp|use_native|use_cpp|use_javascript|use_typescript|use_java|cluster_estado|jit_estado|jit_caliente|paralelo)\\b" }] }
  }
}
)CFV17DATA"}
    };
    return resources;
}

void materialize(const std::filesystem::path& root) {
    for (const auto& [relative, content] : embedded_resources()) {
        const auto destination = root / relative;
        if (destination.has_parent_path()) std::filesystem::create_directories(destination.parent_path());
        std::ofstream stream(destination, std::ios::binary);
        if (!stream) throw std::runtime_error("no se pudo desplegar " + relative);
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream) throw std::runtime_error("escritura incompleta de " + relative);
    }
}

std::vector<wchar_t*> decode_arguments(int argc, char** argv) {
    std::vector<wchar_t*> decoded;
    decoded.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index) {
        wchar_t* value = Py_DecodeLocale(argv[index], nullptr);
        if (!value) {
            for (auto* previous : decoded) PyMem_RawFree(previous);
            throw std::runtime_error("argumento CLI no convertible a Unicode");
        }
        decoded.push_back(value);
    }
    return decoded;
}

int run_toolchain(int argc, char** argv, const std::filesystem::path& root) {
    auto decoded = decode_arguments(argc, argv);
    PySys_SetArgvEx(argc, decoded.data(), 0);
    for (auto* value : decoded) PyMem_RawFree(value);

    PyObject* path = PySys_GetObject("path");  // referencia prestada
    PyOwned root_text(PyUnicode_FromString(root.string().c_str()));
    if (!path || !root_text || PyList_Insert(path, 0, root_text.get()) != 0) {
        throw std::runtime_error("no se pudo configurar sys.path");
    }

    PyOwned module(PyImport_ImportModule("cforgev"));
    if (!module) { PyErr_Print(); throw std::runtime_error("no se pudo importar el frontend embebido"); }
    PyOwned main_function(PyObject_GetAttrString(module.get(), "main"));
    if (!main_function || !PyCallable_Check(main_function.get()))
        throw std::runtime_error("el frontend embebido no exporta main()");
    PyOwned result(PyObject_CallObject(main_function.get(), nullptr));
    if (!result) { PyErr_Print(); return 1; }
    long status = PyLong_AsLong(result.get());
    if (PyErr_Occurred()) { PyErr_Print(); return 1; }
    return static_cast<int>(status);
}

struct ProcessResult final {
    int status = -1;
    std::string output;
};

ProcessResult run_process_captured(const std::string& trusted_command) {
    // Esta función solo recibe comandos construidos por recetas internas.
    const std::string redirected = trusted_command + " 2>&1";
#ifdef _WIN32
    FILE* pipe = _popen(redirected.c_str(), "r");
#else
    FILE* pipe = popen(redirected.c_str(), "r");
#endif
    if (!pipe) throw std::runtime_error("no se pudo iniciar el gestor de dependencias");
    std::string output;
    char chunk[4096];
    while (std::fgets(chunk, sizeof(chunk), pipe)) output += chunk;
#ifdef _WIN32
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    return {status, std::move(output)};
}

std::string branded_process_output(std::string output) {
    static const std::regex internal_install(
        R"((^|
)[^
]*(brew\s+install|pip\s+install|npm\s+install|apt(-get)?\s+install)[^
]*)",
        std::regex::icase);
    return std::regex_replace(
        output,
        internal_install,
        "$1[C-Forge Package Manager] Configurando dependencias del núcleo para entorno .cfv...");
}

bool confirm_and_install_dependency(
    const std::string& public_name,
    const std::string& trusted_command
) {
    std::cout << "[C-Forge] Para usar esta función, se requiere el módulo del sistema "
              << public_name << ".\n"
              << "Componente del sistema que se instalará:\n  " << trusted_command << "\n"
              << "¿Deseas instalarlo automáticamente ahora? (S/N): " << std::flush;
    std::string answer;
    if (!std::getline(std::cin, answer)) return false;
    if (answer != "S" && answer != "s" && answer != "SI" && answer != "si") {
        std::cout << "[C-Forge] Instalación cancelada por el usuario.\n";
        return false;
    }
    std::cout << "[C-Forge Package Manager] Configurando dependencias del núcleo "
                 "para entorno .cfv...\n";
    const auto result = run_process_captured(trusted_command);
    if (result.status == 0) {
        std::cout << "[C-Forge Package Manager] Progreso: [████████████████████] 100%\n";
        std::cout << "[C-Forge Package Manager] " << public_name << " quedó disponible.\n";
        return true;
    }
    std::cerr << "[C-Forge Package Manager] La instalación no pudo completarse.\n";
    const auto details = branded_process_output(result.output);
    if (!details.empty()) std::cerr << details << '\n';
    return false;
}

bool command_available(const std::string& command) {
    const std::string probe = "command -v " + command + " >/dev/null 2>&1";
    return std::system(probe.c_str()) == 0;
}

int setup_environment() {
    std::cout << "C-Forge Setup 1.4.0\n";
    const bool clang = command_available("clang++");
    const bool python = command_available("python3");
#ifdef __APPLE__
    const bool java = std::system("/usr/libexec/java_home >/dev/null 2>&1") == 0;
#else
    const bool java = command_available("java") && command_available("javac");
#endif
    const bool node = command_available("node");
    std::cout << (clang ? "[OK] C++: clang++ disponible\n" : "[FALTA] C++: instala las herramientas de desarrollo\n");
    std::cout << (python ? "[OK] Python 3 disponible\n" : "[FALTA] Python 3\n");
    std::cout << (node ? "[OK] JavaScript/TypeScript: Node.js disponible\n" : "[OPCIONAL] Node.js no instalado\n");
    if (java) {
        std::cout << "[OK] Java: JDK y JVM disponibles\n";
    } else {
        std::cout << "[FALTA] Java: instala un JDK con:\n"
                  << "  brew install --cask temurin\n"
                  << "Si no tienes Homebrew: https://adoptium.net/temurin/releases/\n";
    }
    if (!clang) std::cout << "En macOS ejecuta: xcode-select --install\n";
    std::cout << "Setup finalizado; no se realizaron instalaciones sin autorización.\n";
    return clang && python ? 0 : 1;
}

int install_globally(const char* executable) {
#ifdef _WIN32
    (void)executable;
    std::cerr << "--install actualmente está diseñado para macOS/Linux.\n";
    return 1;
#else
    std::error_code error;
    const auto source = std::filesystem::canonical(std::filesystem::absolute(executable), error);
    if (error || !std::filesystem::is_regular_file(source))
        throw std::runtime_error("no se pudo localizar el ejecutable actual");
    const std::filesystem::path directory = "/usr/local/bin";
    const std::filesystem::path destination = directory / "cforge";
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::cerr << "C-Forge necesita permisos para crear " << directory << ".\n"
                  << "Ejecuta: sudo \"" << source.string() << "\" --install\n";
        return 1;
    }
    std::filesystem::copy_file(source, destination,
        std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        std::cerr << "No se pudo instalar en " << destination << ": " << error.message() << "\n"
                  << "Ejecuta: sudo \"" << source.string() << "\" --install\n";
        return 1;
    }
    if (::chmod(destination.c_str(), 0755) != 0)
        throw std::runtime_error("instalado, pero no se pudo marcar como ejecutable");
    std::cout << "C-Forge instalado globalmente en " << destination << "\n"
              << "Ya puedes ejecutar: cforge --version\n";
    return 0;
#endif
}

}  // namespace cforgev

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--setup")
            return cforgev::setup_environment();
        if (argc == 2 && std::string(argv[1]) == "--install")
            return cforgev::install_globally(argv[0]);
        cforgev::TemporaryWorkspace workspace;
        cforgev::materialize(workspace.path());
        cforgev::PythonRuntime python;
        return cforgev::run_toolchain(argc, argv, workspace.path());
    } catch (const std::exception& error) {
        std::cerr << "[C-Forge Bootstrap Exception] " << error.what() << '\n';
        return 1;
    } catch (...) {
        std::cerr << "[C-Forge Bootstrap Exception] error desconocido\n";
        return 1;
    }
}
