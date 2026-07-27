// C-Forge 1.6.0 Developer Preview — distribución monolítica generada.
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
import queue
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

# Los backends cargados bajo demanda deben reutilizar esta misma instancia del
# módulo cuando el CLI se ejecuta como script. Así comparten CForgevError.
if __name__ == "__main__":
    sys.modules.setdefault("cforgev", sys.modules[__name__])

VERSION = "1.6.0-developer-preview"

CONNECTOR_CATALOG = {
    "ia_": "python",
    "ui_": "java",
    "web_": "javascript",
}


def connector_engine(name: str) -> str | None:
    """Resuelve conectores por prefijo sin heurísticas ambiguas."""
    return next(
        (engine for prefix, engine in CONNECTOR_CATALOG.items() if name.startswith(prefix)),
        None,
    )


class CForgevError(Exception):
    pass


@dataclass
class ExternExecutionPolicy:
    """Consentimiento compartido para fronteras que ejecutan código del host."""
    allow_all: bool = False
    approved_languages: set[str] = dc_field(default_factory=set)

    def authorize(self, language: str, line: int) -> None:
        if self.allow_all or language in self.approved_languages:
            return
        warning = (
            f'[C-Forge Security] extern("{language}") ejecutará código extranjero '
            "con los permisos de tu usuario, incluyendo acceso a archivos, red y "
            "procesos del sistema."
        )
        if not sys.stdin.isatty():
            raise CForgevError(
                f"Línea {line}: {warning} Ejecución bloqueada; "
                "usa --allow-extern únicamente si confías en el archivo."
            )
        print(warning, file=sys.stderr)
        answer = input("¿Deseas autorizar este lenguaje durante la ejecución? (S/N): ")
        if answer.strip().lower() not in {"s", "si", "sí", "y", "yes"}:
            raise CForgevError(f"Línea {line}: ejecución extern cancelada por el usuario")
        self.approved_languages.add(language)


class ReturnSignal(Exception):
    def __init__(self, value: object) -> None:
        self.value = value


@dataclass(frozen=True)
class Function:
    name: str
    parameters: list[str]
    body: list[Token]
    parameter_types: list[str] = dc_field(default_factory=list)
    return_type: str = "cualquiera"
    is_async: bool = False


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
class ForgeOption:
    has_value: bool
    value: object = None

@dataclass
class ForgeTask:
    future: concurrent.futures.Future[object]

@dataclass
class ForgeChannel:
    values: queue.Queue[object]
    closed: bool = False

_FORGE_EXECUTOR = concurrent.futures.ThreadPoolExecutor(
    max_workers=max(2, min(32, os.cpu_count() or 2)), thread_name_prefix="cforge"
)


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
    r"(?P<IDENT>[A-Za-z_][A-Za-z0-9_]*)|(?P<OP><<|==|!=|>=|<=|[+\-*/=(),;.:{}<>\[\]])|(?P<BAD>.)"
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
        extern_policy: ExternExecutionPolicy | None = None,
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
        self.extern_policy = extern_policy or ExternExecutionPolicy()

    def run(self) -> None:
        while not self.check("EOF"):
            self.statement()

    def statement(self) -> None:
        if self.match_ident("region") or self.match_ident("unsafe"):
            body = self.block()
            Interpreter(
                body, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures,
                self.program_arguments, self.jit_counts, self.cluster_symbols,
                extern_policy=self.extern_policy,
            ).run()
            return
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
        if self.match_ident("interfaz"):
            self.interface_declaration()
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
        if self.match_ident("async"):
            keyword = self.consume("IDENT", "Se esperaba 'funcion' después de async")
            if keyword.value != "funcion":
                raise CForgevError(f"Línea {keyword.line}: se esperaba 'funcion' después de async")
            self.function_declaration(True)
            return
        if self.match_ident("funcion"):
            self.function_declaration(False)
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
                declared_type = self.consume_type()
                declared_base = declared_type.split("<", 1)[0]
                if declared_base not in {"numero", "texto", "booleano", "lista", "mapa", "tupla", "conjunto", "opcion", "nulo", "cualquiera"} and declared_base not in self.structures:
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
        if self._match_dotted_print(("console", "log")) or self._match_dotted_print(
            ("System", "out", "println")
        ):
            value = self.expression()
            self.consume_value(")", "Se esperaba ')' después del texto")
            self.optional_semicolon()
            print(format_value(value))
            return
        if self._match_cout():
            value = self.expression()
            if self.match_value("<<"):
                if self.match_ident("std"):
                    self.consume_value(":", "Se esperaba std::endl")
                    self.consume_value(":", "Se esperaba std::endl")
                endl = self.consume("IDENT", "Solo se admite endl después de la salida")
                if endl.value != "endl":
                    raise CForgevError(f"Línea {endl.line}: solo se admite endl")
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
                "numero", "texto", "booleano", "lista", "mapa", "tupla", "conjunto", "nulo", "cualquiera"
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
        if self.match_ident("implementa"):
            while True:
                self.consume("IDENT", "Se esperaba una interfaz")
                if not self.match_value(","):
                    break
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
                        if self.match_value(":"):
                            self.consume_type()
                        if not self.match_value(","):
                            break
                self.consume_value(")", "Se esperaba ')'")
                if self.match_value(":"):
                    self.consume_type()
                methods[method_name.value] = Function(method_name.value, parameters, self.block())
                continue
            raise CForgevError(f"Línea {self.peek().line}: se esperaba 'campo' o 'metodo'")
        self.consume_value("}", "Falta '}' para cerrar la clase")
        self.structures[name.value] = Structure(name.value, fields, methods)

    def interface_declaration(self) -> None:
        self.consume("IDENT", "Se esperaba el nombre de la interfaz")
        self.consume_value("{", "Se esperaba '{'")
        while self.peek().value != "}" and not self.check("EOF"):
            keyword = self.consume("IDENT", "Se esperaba 'metodo'")
            if keyword.value != "metodo":
                raise CForgevError(f"Línea {keyword.line}: se esperaba 'metodo'")
            self.consume("IDENT", "Se esperaba el nombre del método")
            self.consume_value("(", "Se esperaba '('")
            if self.peek().value != ")":
                while True:
                    self.consume("IDENT", "Se esperaba un parámetro")
                    if self.match_value(":"):
                        self.consume_type()
                    if not self.match_value(","):
                        break
            self.consume_value(")", "Se esperaba ')'")
            if self.match_value(":"):
                self.consume_type()
            self.optional_semicolon()
        self.consume_value("}", "Falta '}' para cerrar la interfaz")

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
            module_path.parent, self.imported_modules, self.structures, self.program_arguments,
            extern_policy=self.extern_policy,
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
        self.extern_policy.authorize(language, body.line)
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
        if shutil.which("clang++") is None:
            raise CForgevError(
                f'Línea {body.line}: extern("cpp"): clang++ no está disponible. '
                "Instálalo para usar este bloque."
            )
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source_path, executable = root / "extern.cpp", root / "extern"
            source_path.write_text(
                "#include <iostream>\n#include <string>\nint main(){\n" + body.value + "\n}\n",
                encoding="utf-8",
            )
            try:
                built = subprocess.run(
                    ["clang++", "-std=c++17", str(source_path), "-o", str(executable)],
                    capture_output=True, text=True,
                )
            except FileNotFoundError as error:
                raise CForgevError(
                    f'Línea {body.line}: extern("cpp"): clang++ no está disponible. '
                    "Instálalo para usar este bloque."
                ) from error
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
                extern_policy=self.extern_policy,
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
                extern_policy=self.extern_policy,
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
                self.base_dir, self.imported_modules, self.structures, self.program_arguments,
                extern_policy=self.extern_policy,
            ).run()
        except CForgevError as error:
            self.variables[error_name.value] = str(error)
            self.variable_types[error_name.value] = "texto"
            Interpreter(
                handler, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments,
                extern_policy=self.extern_policy,
            ).run()

    def function_declaration(self, is_async: bool = False) -> None:
        name = self.consume("IDENT", "Se esperaba el nombre de la función")
        if self.match_value("<"):
            while True:
                self.consume("IDENT", "Se esperaba un parámetro de tipo")
                if not self.match_value(","):
                    break
            self.consume_value(">", "Se esperaba '>'")
        self.consume_value("(", "Se esperaba '(' después del nombre de la función")
        parameters: list[str] = []
        parameter_types: list[str] = []
        if self.peek().value != ")":
            while True:
                parameter = self.consume("IDENT", "Se esperaba el nombre de un parámetro")
                if parameter.value in parameters:
                    raise CForgevError(
                        f"Línea {parameter.line}: parámetro repetido '{parameter.value}'"
                    )
                parameters.append(parameter.value)
                parameter_type = "cualquiera"
                if self.match_value(":"):
                    parameter_type = self.consume_type()
                parameter_types.append(parameter_type)
                if not self.match_value(","):
                    break
        self.consume_value(")", "Se esperaba ')' después de los parámetros")
        return_type = "cualquiera"
        if self.match_value(":"):
            return_type = self.consume_type()
        body = self.block()
        self.functions[name.value] = Function(
            name.value, parameters, body, parameter_types, return_type, is_async
        )
        if self.cluster_mode:
            self.cluster_symbols[name.value] = "funcion"

    def consume_type(self) -> str:
        name = self.consume("IDENT", "Se esperaba un tipo").value
        if not self.match_value("<"):
            return name
        arguments: list[str] = []
        while True:
            arguments.append(self.consume_type())
            if not self.match_value(","):
                break
        self.consume_value(">", "Se esperaba '>' para cerrar el tipo genérico")
        return f"{name}<{','.join(arguments)}>"

    def while_statement(self) -> None:
        condition_tokens = self.parenthesized("después de 'mientras'")
        body = self.block()
        iterations = 0
        while True:
            condition = Interpreter(
                condition_tokens, self.variables, self.functions, self.variable_types,
                self.base_dir, self.imported_modules, self.structures, self.program_arguments,
                extern_policy=self.extern_policy,
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
                self.base_dir, self.imported_modules, self.structures, self.program_arguments,
                extern_policy=self.extern_policy,
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
                self.base_dir, self.imported_modules, self.structures, self.program_arguments,
                extern_policy=self.extern_policy,
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
        if self.match_ident("await"):
            token = self.previous()
            task = self.unary()
            if not isinstance(task, ForgeTask):
                raise CForgevError(f"Línea {token.line}: await requiere una tarea")
            try: return task.future.result()
            except concurrent.futures.CancelledError as error:
                raise CForgevError(f"Línea {token.line}: tarea cancelada") from error
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
                if isinstance(value, (str, list, tuple)):
                    if not isinstance(key, int) or isinstance(key, bool):
                        raise CForgevError(f"Línea {bracket.line}: el índice de colección debe ser entero")
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
            if field.value == "length" and isinstance(value, (str, list, dict, tuple, set)):
                value = len(value)
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
        if name.value in {"append", "push"}:
            if not isinstance(instance, list) or len(arguments) != 1:
                raise CForgevError(
                    f"Línea {name.line}: {name.value} requiere una lista y un elemento"
                )
            instance.append(arguments[0])
            return None
        if name.value in {"length", "len"}:
            if arguments or not isinstance(instance, (str, list, dict)):
                raise CForgevError(
                    f"Línea {name.line}: {name.value} requiere texto, lista o mapa"
                )
            return len(instance)
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
            self.imported_modules, self.structures, self.program_arguments,
            extern_policy=self.extern_policy,
        )
        try:
            interpreter.run()
        except ReturnSignal as signal:
            return signal.value
        return None

    def _match_dotted_print(self, names: tuple[str, ...]) -> bool:
        needed = len(names) * 2
        if self.current + needed > len(self.tokens):
            return False
        cursor = self.current
        for index, name in enumerate(names):
            if self.tokens[cursor].kind != "IDENT" or self.tokens[cursor].value != name:
                return False
            cursor += 1
            if index + 1 < len(names):
                if self.tokens[cursor].value != ".":
                    return False
                cursor += 1
        if self.tokens[cursor].value != "(":
            return False
        self.current = cursor + 1
        return True

    def _match_cout(self) -> bool:
        cursor = self.current
        if self.tokens[cursor].kind == "IDENT" and self.tokens[cursor].value == "std":
            if self.tokens[cursor + 1].value != ":" or self.tokens[cursor + 2].value != ":":
                return False
            cursor += 3
        if self.tokens[cursor].kind != "IDENT" or self.tokens[cursor].value != "cout":
            return False
        if self.tokens[cursor + 1].value != "<<":
            return False
        self.current = cursor + 2
        return True

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
            if self.match_value(")"):
                return ()
            first = self.expression()
            if not self.match_value(","):
                self.consume_value(")", "Se esperaba ')'")
                return first
            values = [first]
            while self.peek().value != ")":
                values.append(self.expression())
                if not self.match_value(","):
                    break
            self.consume_value(")", "Se esperaba ')' para cerrar la tupla")
            return tuple(values)
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
        if name.value in {"mover", "prestar", "prestar_mut", "soltar_prestamo", "destruir"}:
            if len(arguments) != 1:
                raise CForgevError(f"Línea {name.line}: {name.value} requiere 1 argumento")
            return arguments[0] if name.value not in {"soltar_prestamo", "destruir"} else None
        if name.value == "conjunto":
            try:
                return set(arguments)
            except TypeError as error:
                raise CForgevError(
                    f"Línea {name.line}: los elementos del conjunto deben ser inmutables"
                ) from error
        if name.value == "algunos":
            if len(arguments) != 1:
                raise CForgevError(f"Línea {name.line}: algunos requiere 1 argumento")
            return ForgeOption(True, arguments[0])
        if name.value == "ninguno":
            if arguments:
                raise CForgevError(f"Línea {name.line}: ninguno no recibe argumentos")
            return ForgeOption(False)
        if name.value == "es_algunos":
            if len(arguments) != 1 or not isinstance(arguments[0], ForgeOption):
                raise CForgevError(f"Línea {name.line}: es_algunos requiere una opcion")
            return arguments[0].has_value
        if name.value == "desenvolver":
            if len(arguments) != 1 or not isinstance(arguments[0], ForgeOption):
                raise CForgevError(f"Línea {name.line}: desenvolver requiere una opcion")
            if not arguments[0].has_value:
                raise CForgevError(f"Línea {name.line}: no se puede desenvolver ninguno")
            return arguments[0].value
        if name.value == "tarea":
            if len(arguments) not in {1, 2} or not isinstance(arguments[0], str):
                raise CForgevError(f"Línea {name.line}: tarea requiere nombre y lista de argumentos opcional")
            provided = arguments[1] if len(arguments) == 2 else []
            if not isinstance(provided, list):
                raise CForgevError(f"Línea {name.line}: los argumentos de tarea deben ser una lista")
            function = self.functions.get(arguments[0])
            if function is None: raise CForgevError(f"Línea {name.line}: función de tarea desconocida")
            return ForgeTask(_FORGE_EXECUTOR.submit(self.invoke_user_function, function, list(provided), name.line))
        if name.value == "esperar":
            if len(arguments) not in {1, 2} or not isinstance(arguments[0], ForgeTask):
                raise CForgevError(f"Línea {name.line}: esperar requiere una tarea")
            timeout = None if len(arguments) == 1 else float(arguments[1]) / 1000.0
            try: return arguments[0].future.result(timeout=timeout)
            except concurrent.futures.TimeoutError as error:
                raise CForgevError(f"Línea {name.line}: tiempo de espera agotado") from error
            except concurrent.futures.CancelledError as error:
                raise CForgevError(f"Línea {name.line}: tarea cancelada") from error
        if name.value == "cancelar":
            if len(arguments) != 1 or not isinstance(arguments[0], ForgeTask):
                raise CForgevError(f"Línea {name.line}: cancelar requiere una tarea")
            return arguments[0].future.cancel()
        if name.value == "canal":
            if len(arguments) > 1: raise CForgevError(f"Línea {name.line}: canal acepta capacidad opcional")
            capacity = int(arguments[0]) if arguments else 0
            if capacity < 0: raise CForgevError(f"Línea {name.line}: capacidad inválida")
            return ForgeChannel(queue.Queue(maxsize=capacity))
        if name.value == "enviar":
            if len(arguments) not in {2, 3} or not isinstance(arguments[0], ForgeChannel):
                raise CForgevError(f"Línea {name.line}: enviar requiere canal y valor")
            if arguments[0].closed: raise CForgevError(f"Línea {name.line}: canal cerrado")
            timeout = None if len(arguments) == 2 else float(arguments[2]) / 1000.0
            try: arguments[0].values.put(arguments[1], timeout=timeout)
            except queue.Full as error: raise CForgevError(f"Línea {name.line}: canal lleno") from error
            return None
        if name.value == "recibir":
            if len(arguments) not in {1, 2} or not isinstance(arguments[0], ForgeChannel):
                raise CForgevError(f"Línea {name.line}: recibir requiere un canal")
            timeout = None if len(arguments) == 1 else float(arguments[1]) / 1000.0
            try: return arguments[0].values.get(timeout=timeout)
            except queue.Empty as error: raise CForgevError(f"Línea {name.line}: canal vacío") from error
        if name.value == "cerrar_canal":
            if len(arguments) != 1 or not isinstance(arguments[0], ForgeChannel):
                raise CForgevError(f"Línea {name.line}: cerrar_canal requiere un canal")
            arguments[0].closed = True
            return None
        if name.value == "forge_catalogo":
            if arguments:
                raise CForgevError(f"Línea {name.line}: forge_catalogo no recibe argumentos")
            return dict(CONNECTOR_CATALOG)
        if name.value == "forge_hash":
            if len(arguments) != 1 or not is_universal_data(arguments[0]):
                raise CForgevError(
                    f"Línea {name.line}: forge_hash requiere un ForgeValue serializable"
                )
            canonical = json.dumps(
                universal_json_value(arguments[0]), ensure_ascii=False, sort_keys=True,
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
            if len(arguments) != 1 or not isinstance(arguments[0], (str, list, dict, tuple, set)):
                raise CForgevError(f"Línea {name.line}: 'longitud' requiere texto o colección")
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
            engine = connector_engine(name.value)
            if engine is not None:
                setting = {
                    "python": "CFORGE_IA_MODULE",
                    "java": "CFORGE_UI_ADAPTER",
                    "javascript": "CFORGE_WEB_MODULE",
                }[engine]
                raise CForgevError(
                    f"Línea {name.line}: conector '{name.value}' enrutado a {engine}; "
                    f"configura su adaptador mediante {setting}"
                )
            raise CForgevError(f"Línea {name.line}: función desconocida '{name.value}'")
        if function.is_async:
            return ForgeTask(
                _FORGE_EXECUTOR.submit(self.invoke_user_function, function, arguments, name.line)
            )
        return self.invoke_user_function(function, arguments, name.line)

    def invoke_user_function(self, function: Function, arguments: list[object], line: int) -> object:
        if len(arguments) != len(function.parameters):
            raise CForgevError(
                f"Línea {line}: '{function.name}' requiere {len(function.parameters)} "
                f"argumentos, pero recibió {len(arguments)}"
            )
        expected_parameters = function.parameter_types or ["cualquiera"] * len(function.parameters)
        for parameter, expected, argument in zip(function.parameters, expected_parameters, arguments):
            ensure_type(parameter, expected, argument, line)
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
            extern_policy=self.extern_policy,
        )
        try:
            interpreter.run()
        except ReturnSignal as signal:
            ensure_type(function.name, function.return_type, signal.value, line)
            return signal.value
        if function.return_type not in {"cualquiera", "nulo"}:
            raise CForgevError(
                f"Línea {line}: '{function.name}' debe retornar {function.return_type}"
            )
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
                command.append("b:true" if value else "b:false")
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
        if result_type == 4:
            if payload not in {"verdadero", "falso"}:
                raise CForgevError(f"Línea {line}: booleano ABI inválido")
            return payload == "verdadero"
        raise CForgevError(f"Línea {line}: tipo ABI de retorno desconocido")

    def invoke_javascript(
        self, module: str, function: str, arguments: list[object], line: int
    ) -> object:
        if "/" in module and not Path(module).is_absolute():
            module = str((self.base_dir / module).resolve())
        marker = "__CFORGEV_JS_RESULT__"
        script = f'''(async () => {{
globalThis.ForgeSymbols = {json.dumps({key: universal_json_value(value) for key, value in self.variables.items() if is_universal_data(value)}, ensure_ascii=False)};
const target = require({json.dumps(module)});
const callable = target[{json.dumps(function)}] ?? target.default?.[{json.dumps(function)}];
if (typeof callable !== "function") throw new Error("función JavaScript inexistente");
const result = await callable(...{json.dumps(universal_json_value(arguments), ensure_ascii=False)});
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
            try:
                compiled = subprocess.run(
                    ["javac", str(source)], capture_output=True, text=True
                )
            except FileNotFoundError as error:
                raise CForgevError(
                    f"Línea {line}: JNI/JDK no disponible; no se encontró javac"
                ) from error
            if compiled.returncode:
                raise CForgevError(f"Línea {line}: JNI/JDK no disponible: {compiled.stderr.strip()}")
            try:
                invoked = subprocess.run(
                    ["java", "-cp", str(root), "CForgevJavaBridge", jar, class_name, method, *tagged],
                    capture_output=True, text=True,
                )
            except FileNotFoundError as error:
                raise CForgevError(
                    f"Línea {line}: JVM no disponible; no se encontró java"
                ) from error
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
    if isinstance(value, ForgeOption):
        return f"algunos({format_value(value.value)})" if value.has_value else "ninguno"
    if isinstance(value, bool):
        return "verdadero" if value else "falso"
    if isinstance(value, float) and value.is_integer():
        return str(int(value))
    if value is None:
        return "nulo"
    if isinstance(value, list):
        return "[" + ", ".join(format_value(item) for item in value) + "]"
    if isinstance(value, tuple):
        suffix = "," if len(value) == 1 else ""
        return "(" + ", ".join(format_value(item) for item in value) + suffix + ")"
    if isinstance(value, set):
        return "conjunto(" + ", ".join(sorted(format_value(item) for item in value)) + ")"
    if isinstance(value, dict):
        return "{" + ", ".join(f'"{key}": {format_value(item)}' for key, item in value.items()) + "}"
    return str(value)


def value_type(value: object) -> str:
    if isinstance(value, ForgeOption):
        return "opcion"
    if isinstance(value, bool):
        return "booleano"
    if isinstance(value, (int, float)):
        return "numero"
    if isinstance(value, str):
        return "texto"
    if isinstance(value, list):
        return "lista"
    if isinstance(value, tuple):
        return "tupla"
    if isinstance(value, set):
        return "conjunto"
    if isinstance(value, dict):
        return value.structure_name if isinstance(value, StructureValue) else "mapa"
    if value is None:
        return "nulo"
    return "cualquiera"


def is_universal_data(value: object) -> bool:
    if value is None or isinstance(value, (bool, int, float, str)):
        return True
    if isinstance(value, (list, tuple, set)):
        return all(is_universal_data(item) for item in value)
    if isinstance(value, dict):
        return all(isinstance(key, str) and is_universal_data(item) for key, item in value.items())
    return False


def universal_json_value(value: object) -> object:
    """Convierte colecciones C-Forge a una forma JSON estable para puentes externos."""
    if isinstance(value, tuple):
        return [universal_json_value(item) for item in value]
    if isinstance(value, set):
        return [universal_json_value(item) for item in sorted(value, key=format_value)]
    if isinstance(value, list):
        return [universal_json_value(item) for item in value]
    if isinstance(value, dict):
        return {key: universal_json_value(item) for key, item in value.items()}
    return value


def ensure_type(name: str, expected: str, value: object, line: int) -> None:
    actual = value_type(value)
    expected_base = expected.split("<", 1)[0]
    if expected not in {"cualquiera", actual} and expected_base != actual and (
        len(expected) != 1 or not expected.isupper()
    ):
        raise CForgevError(
            f"Línea {line}: '{name}' es {expected} y no puede recibir {actual}"
        )


def require_bool(value: object, line: int) -> bool:
    if not isinstance(value, bool):
        raise CForgevError(f"Línea {line}: la operación lógica requiere booleanos")
    return value


def execute(
    path: Path,
    program_arguments: list[str] | None = None,
    allow_extern: bool = False,
) -> None:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {path}: {error}") from error
    try:
        from compilador_nativo import Parser, StaticTypeAnalyzer
        from cforge_memory import MemorySafetyAnalyzer
        checked = Parser(tokenize(source)).program()
        StaticTypeAnalyzer().analyze(checked)
        MemorySafetyAnalyzer().analyze(checked)
        Interpreter(
            tokenize(source), base_dir=path.resolve().parent,
            program_arguments=program_arguments,
            extern_policy=ExternExecutionPolicy(allow_all=allow_extern),
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


def run_test_file(path: Path, allow_extern: bool = False) -> int:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {path}: {error.strerror or error}") from error
    results: list[str] = []
    Interpreter(
        tokenize(source),
        base_dir=path.resolve().parent,
        test_results=results,
        extern_policy=ExternExecutionPolicy(allow_all=allow_extern),
    ).run()
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
    if len(sys.argv) in {2, 3} and sys.argv[1] == "capabilities":
        if len(sys.argv) == 3 and sys.argv[2] != "--json":
            print("Uso: cforge capabilities [--json]", file=sys.stderr)
            return 2
        manifest = Path(__file__).resolve().parent / "capabilities.json"
        try:
            data = json.loads(manifest.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            print(f"[C-Forge Capability Gate] {error}", file=sys.stderr)
            return 1
        if len(sys.argv) == 3:
            print(json.dumps(data, ensure_ascii=False, indent=2))
        else:
            print(f"C-Forge {data['language_version']} — {data['overall_status']}")
            for item in data["capabilities"]:
                print(f"[{item['status']}] {item['name']}: {item['scope']}")
        return 0
    if len(sys.argv) >= 2 and sys.argv[1] in {"fmt", "test"}:
        command = sys.argv[1]
        allow_extern = command == "test" and "--allow-extern" in sys.argv[3:]
        expected = 4 if allow_extern else 3
        if len(sys.argv) != expected:
            suffix = " [--allow-extern]" if command == "test" else ""
            print(f"Uso: cforge {command} archivo.cfv{suffix}", file=sys.stderr)
            return 2
        path = Path(sys.argv[2])
        try:
            if command == "fmt":
                changed = format_file(path)
                print(f"C-Forge fmt: {'formateado' if changed else 'sin cambios'} — {path}")
            else:
                run_test_file(path, allow_extern=allow_extern)
        except CForgevError as error:
            print(f"[C-Forge Runtime Exception] {error}", file=sys.stderr)
            return 1
        return 0
    if len(sys.argv) >= 2 and sys.argv[1] == "check":
        import json as json_module
        from cforge_diagnostics import analyze_file
        if len(sys.argv) not in {3, 4} or (len(sys.argv) == 4 and sys.argv[3] != "--json"):
            print("Uso: cforge check archivo.cfv [--json]", file=sys.stderr); return 2
        diagnostics = analyze_file(Path(sys.argv[2]))
        if len(sys.argv) == 4:
            print(json_module.dumps([item.to_dict() for item in diagnostics], ensure_ascii=False))
        else:
            for item in diagnostics:
                print(f"{sys.argv[2]}:{item.line}:{item.column}: {item.severity} {item.code}: {item.message}")
            if not diagnostics: print(f"[OK] {sys.argv[2]}: sin errores")
        return 1 if any(item.severity == "error" for item in diagnostics) else 0
    if len(sys.argv) >= 2 and sys.argv[1] == "parity":
        from cforge_parity import compare_file, format_report, reports_json
        arguments = sys.argv[2:]
        json_output = "--json" in arguments
        arguments = [argument for argument in arguments if argument != "--json"]
        if not arguments:
            print("Uso: cforge parity archivo.cfv [...] [--json]", file=sys.stderr)
            return 2
        try:
            reports = [compare_file(Path(argument)) for argument in arguments]
        except CForgevError as error:
            print(f"[C-Forge Parity] {error}", file=sys.stderr)
            return 1
        if json_output:
            print(reports_json(reports))
        else:
            print("\n\n".join(format_report(report) for report in reports))
        return 0 if all(report.equal for report in reports) else 1
    if len(sys.argv) >= 2 and sys.argv[1] in {"vm", "bytecode", "debug"}:
        from cforge_vm import VirtualMachine, compile_file, compile_source, disassemble, execute_file, save_bytecode
        if len(sys.argv) < 3:
            print(f"Uso: cforge {sys.argv[1]} archivo.cfv [--break offset]", file=sys.stderr); return 2
        try:
            path = Path(sys.argv[2])
            if sys.argv[1] == "vm": execute_file(path)
            elif sys.argv[1] == "bytecode":
                program = compile_file(path)
                if len(sys.argv) == 5 and sys.argv[3] == "-o":
                    destination = save_bytecode(program, Path(sys.argv[4]))
                    print(f"Bytecode C-Forge creado: {destination}")
                elif len(sys.argv) == 3:
                    print(disassemble(program))
                else:
                    raise CForgevError("Uso: cforge bytecode archivo.cfv [-o programa.cfb]")
            else:
                breakpoints: set[int] = set()
                cursor = 3
                while cursor < len(sys.argv):
                    if sys.argv[cursor] != "--break" or cursor + 1 >= len(sys.argv):
                        raise CForgevError("debug acepta únicamente --break offset")
                    breakpoints.add(int(sys.argv[cursor + 1])); cursor += 2
                program = compile_source(path.read_text(encoding="utf-8"))
                def trace(chunk, offset, instruction, scope):
                    if breakpoints and offset not in breakpoints: return
                    variables = ", ".join(f"{key}={value!r}" for key, value in sorted(scope.items()))
                    marker = "breakpoint" if offset in breakpoints else "debug"
                    print(f"[{marker}] {chunk}:{offset:04d} {instruction.op} {instruction.arg!r} | {variables}", file=sys.stderr)
                    if offset in breakpoints and sys.stdin.isatty(): input("[C-Forge Debug] Enter para continuar...")
                VirtualMachine(program, trace=trace).run()
        except (CForgevError, OSError) as error:
            print(f"[C-Forge VM Exception] {error}", file=sys.stderr); return 1
        return 0
    if len(sys.argv) >= 2 and sys.argv[1] == "lsp":
        if len(sys.argv) != 2:
            print("Uso: cforge lsp", file=sys.stderr); return 2
        from cforge_lsp import run
        return run()
    if len(sys.argv) >= 2 and sys.argv[1] == "dap":
        if len(sys.argv) != 2:
            print("Uso: cforge dap", file=sys.stderr); return 2
        from cforge_dap import run
        return run()
    if len(sys.argv) >= 2 and sys.argv[1] == "pkg":
        from cforge_packages import (add, build_package, generate_keypair, init,
                                     install_registry, list_packages, remove,
                                     search_registry, sign_package)
        action = sys.argv[2] if len(sys.argv) > 2 else ""
        try:
            if action == "init" and len(sys.argv) in {3, 4}:
                init(Path.cwd(), sys.argv[3] if len(sys.argv) == 4 else None)
                print(f"Proyecto C-Forge creado: {Path.cwd() / 'cforge.json'}")
            elif action == "add" and len(sys.argv) == 5:
                add(Path.cwd(), sys.argv[3], sys.argv[4]); print(f"Dependencia fijada: {Path.cwd() / 'cforge.lock'}")
            elif action == "remove" and len(sys.argv) == 4:
                remove(Path.cwd(), sys.argv[3]); print(f"Dependencia eliminada: {sys.argv[3]}")
            elif action == "list" and len(sys.argv) == 3:
                packages = list_packages(Path.cwd())
                print("\n".join(f"{name} -> {path}" for name, path in packages) or "Sin dependencias")
            elif action == "search" and len(sys.argv) == 4:
                matches = search_registry(sys.argv[3])
                print("\n".join(f"{name}@{version} — {description}" for name, version, description in matches) or "Sin resultados")
            elif action == "install" and len(sys.argv) in {4, 5}:
                destination = install_registry(Path.cwd(), sys.argv[3], sys.argv[4] if len(sys.argv) == 5 else None)
                print(f"Paquete verificado e instalado: {destination}")
            elif action == "build" and len(sys.argv) in {3, 4}:
                archive, digest = build_package(Path.cwd(), Path(sys.argv[3]) if len(sys.argv) == 4 else Path("dist"))
                print(f"Paquete creado: {archive}\nSHA-256: {digest}")
            elif action == "keygen" and len(sys.argv) in {3, 4}:
                directory = Path(sys.argv[3]) if len(sys.argv) == 4 else Path.home() / ".cforge" / "keys"
                key_id = generate_keypair(directory / "publisher.pem", directory / "publisher.pub.pem")
                print(f"Identidad Ed25519 creada: {directory}\nKey ID: {key_id}")
            elif action == "sign" and len(sys.argv) in {7, 8}:
                signature = sign_package(Path(sys.argv[3]), Path(sys.argv[4]), sys.argv[5], sys.argv[6],
                                         Path(sys.argv[7]) if len(sys.argv) == 8 else None)
                print(f"Firma de paquete creada: {signature}")
            else:
                print("Uso: cforge pkg init [nombre] | add nombre ruta | remove nombre | list | search texto | install nombre [versión] | build [salida] | keygen [directorio] | sign archivo clave nombre versión [salida]", file=sys.stderr); return 2
        # Los módulos opcionales pueden estar importados bajo el nombre de paquete
        # mientras este archivo se ejecuta como __main__; captura sus errores de
        # dominio sin exponer un traceback interno al usuario.
        except Exception as error:
            print(f"[C-Forge Package Manager] {error}", file=sys.stderr); return 1
        return 0
    parser = argparse.ArgumentParser(prog="cforge", description="Intérprete de C-Forge")
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
    parser.add_argument("--llvm", action="store_true", help="exportar LLVM IR textual real (.ll)")
    parser.add_argument("--compilar-llvm", action="store_true", help="compilar el núcleo numérico mediante LLVM IR y Clang")
    parser.add_argument(
        "--allow-extern", action="store_true",
        help="autorizar bloques extern de código extranjero en archivos confiables",
    )
    args, program_arguments = parser.parse_known_args()
    if args.archivo is None:
        if args.compilar or args.salida or args.vincular or args.reparar or args.vigilar or args.wasm or args.llvm or args.compilar_llvm:
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
        selected_backends = sum(bool(value) for value in (args.wasm, args.llvm, args.compilar_llvm, args.compilar))
        if selected_backends > 1:
            raise CForgevError("selecciona solo un backend: --compilar, --llvm, --compilar-llvm o --wasm")
        if args.llvm:
            from compilador_llvm import emit_file
            output = args.salida or args.archivo.with_suffix(".ll")
            emit_file(args.archivo, output)
            print(f"LLVM IR C-Forge creado: {output}")
        elif args.compilar_llvm:
            from compilador_llvm import compile_native as compile_llvm_native
            output = args.salida or args.archivo.with_suffix("")
            compile_llvm_native(args.archivo, output, linked_sources=args.vincular)
            print(f"Ejecutable LLVM C-Forge creado: {output}")
        elif args.wasm:
            from compilador_wasm import compile_wasm
            output = args.salida or args.archivo.with_suffix(".wat")
            compile_wasm(args.archivo, output)
            print(f"Módulo WebAssembly C-Forge creado: {output}")
        elif args.vigilar:
            execute_watch(args.archivo, program_arguments, args.intervalo)
        elif args.compilar:
            from compilador_nativo import compile_native

            output = args.salida or args.archivo.with_suffix("")
            compile_native(
                args.archivo, output, args.vincular,
                allow_extern=args.allow_extern,
            )
            print(f"Ejecutable C-Forge creado: {output}")
        else:
            execute(args.archivo, program_arguments, allow_extern=args.allow_extern)
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
    print("C-Forge Setup 1.6.0 Developer Preview")
    clang = shutil.which("clang++") is not None
    python = bool(getattr(sys, "frozen", False)) or shutil.which("python3") is not None
    node = shutil.which("node") is not None
    try:
        import cryptography  # noqa: F401
        package_signatures = True
    except ImportError:
        package_signatures = False
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
    print("[OK] Paquetes: firmas Ed25519 disponibles" if package_signatures else
          "[FALTA] Paquetes firmados: instala el componente cryptography")
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
    locations: dict[int, int] = None

    def __post_init__(self) -> None:
        if self.locations is None:
            self.locations = {}


class Parser:
    def __init__(self, tokens: list[Token]) -> None:
        self.tokens = tokens
        self.current = 0
        self.structures: set[str] = set()
        self.declared: set[str] = set()
        self.universal_imports: dict[str, tuple[str, str]] = {}
        self.locations: dict[int, int] = {}

    def program(self) -> Program:
        functions: list[Stmt] = []
        statements: list[Stmt] = []
        while self.peek().kind != "EOF":
            statement = self.statement()
            (functions if statement[0] == "function" else statements).append(statement)
        return Program(functions, statements, self.locations)

    def statement(self) -> Stmt:
        line = self.peek().line
        statement = self._statement()
        self.locations[id(statement)] = line
        return statement

    def _statement(self) -> Stmt:
        if self.word("extern_c"):
            checked = self.word("segura")
            if not self.word("funcion"):
                raise CForgevError("'extern_c' debe declarar una función (opcionalmente segura)")
            name = self.ident("Se esperaba el símbolo C")
            self.value("(", "Se esperaba '('")
            parameters: list[str] = []
            parameter_types: list[str] = []
            if self.peek().value != ")":
                while True:
                    parameters.append(self.ident("Se esperaba un parámetro FFI"))
                    self.value(":", "Los parámetros extern_c requieren tipo")
                    parameter_types.append(self.parse_type())
                    if not self.take(","):
                        break
            self.value(")", "Se esperaba ')'")
            self.value(":", "extern_c requiere un tipo de retorno")
            return_type = self.parse_type()
            self.take(";")
            self.declared.add(name)
            return ("ffi_function", name, parameters, parameter_types, return_type, checked)
        if self.word("region"):
            return ("region", self.block())
        if self.word("unsafe"):
            return ("unsafe", self.block())
        if self.word("cluster"):
            declaration = self.statement()
            if declaration[0] == "let":
                return declaration + (True,)
            if declaration[0] == "function":
                while len(declaration) < 7:
                    declaration += (False,)
                return declaration[:7] + (True,)
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
        if self.word("interfaz"):
            name = self.ident("Se esperaba el nombre de la interfaz")
            self.value("{", "Se esperaba '{'")
            methods: list[Stmt] = []
            while self.peek().value != "}" and self.peek().kind != "EOF":
                if not self.word("metodo"):
                    raise CForgevError(f"Línea {self.peek().line}: se esperaba 'metodo'")
                method = self.ident("Se esperaba el nombre del método")
                self.value("(", "Se esperaba '('")
                parameter_types: list[str] = []
                if self.peek().value != ")":
                    while True:
                        self.ident("Se esperaba un parámetro")
                        parameter_types.append(self.parse_type() if self.take(":") else "cualquiera")
                        if not self.take(","):
                            break
                self.value(")", "Se esperaba ')'")
                return_type = self.parse_type() if self.take(":") else "cualquiera"
                self.take(";")
                methods.append(("interface_method", method, parameter_types, return_type))
            self.value("}", "Falta '}' para cerrar la interfaz")
            self.structures.add(name)
            return ("interface", name, methods)
        if self.word("clase"):
            name = self.ident("Se esperaba el nombre de la clase")
            interfaces: list[str] = []
            if self.word("implementa"):
                while True:
                    interfaces.append(self.ident("Se esperaba una interfaz"))
                    if not self.take(","):
                        break
            self.value("{", "Se esperaba '{'")
            fields: list[tuple[str, str]] = []
            methods: list[Stmt] = []
            while self.peek().value != "}" and self.peek().kind != "EOF":
                if self.word("campo"):
                    field = self.ident("Se esperaba el nombre del campo")
                    self.value(":", "Se esperaba ':'")
                    fields.append((field, self.parse_type()))
                    self.take(";")
                    continue
                if self.word("metodo"):
                    method = self.ident("Se esperaba el nombre del método")
                    self.value("(", "Se esperaba '('")
                    parameters: list[str] = []
                    parameter_types: list[str] = []
                    if self.peek().value != ")":
                        while True:
                            parameters.append(self.ident("Se esperaba un parámetro"))
                            parameter_types.append(self.parse_type() if self.take(":") else "cualquiera")
                            if not self.take(","):
                                break
                    self.value(")", "Se esperaba ')'")
                    return_type = self.parse_type() if self.take(":") else "cualquiera"
                    methods.append(("method", name, method, parameters, self.block(), parameter_types, return_type))
                    continue
                raise CForgevError(f"Línea {self.peek().line}: se esperaba 'campo' o 'metodo'")
            self.value("}", "Falta '}' para cerrar la clase")
            self.structures.add(name)
            return ("class", name, fields, methods, interfaces)
        if self.word("estructura"):
            name = self.ident("Se esperaba el nombre de la estructura")
            self.value("{", "Se esperaba '{'")
            fields: list[tuple[str, str]] = []
            while self.peek().value != "}" and self.peek().kind != "EOF":
                field = self.ident("Se esperaba el nombre del campo")
                self.value(":", "Se esperaba ':'")
                field_type = self.parse_type()
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
        async_function = False
        if self.word("async"):
            if not self.word("funcion"):
                raise CForgevError("'async' debe preceder una función")
            async_function = True
        elif self.word("funcion"):
            async_function = False
        else:
            async_function = None
        if async_function is not None:
            name = self.ident("Se esperaba el nombre de la función")
            type_parameters: list[str] = []
            if self.take("<"):
                while True:
                    type_parameters.append(self.ident("Se esperaba un parámetro de tipo"))
                    if not self.take(","):
                        break
                self.value(">", "Se esperaba '>'")
            self.value("(", "Se esperaba '('")
            parameters: list[str] = []
            parameter_types: list[str] = []
            if self.peek().value != ")":
                while True:
                    parameters.append(self.ident("Se esperaba un parámetro"))
                    parameter_types.append(self.parse_type() if self.take(":") else "cualquiera")
                    if not self.take(","):
                        break
            self.value(")", "Se esperaba ')'")
            return_type = self.parse_type() if self.take(":") else "cualquiera"
            return ("function", name, parameters, self.block(), parameter_types, return_type, async_function, False, type_parameters)
        if self.word("sea"):
            name = self.ident("Se esperaba el nombre de la variable")
            self.declared.add(name)
            declared_type = None
            if self.take(":"):
                declared_type = self.parse_type()
                base_type = declared_type.split("<", 1)[0]
                if base_type not in {
                    "numero", "texto", "booleano", "lista", "mapa", "tupla", "conjunto",
                    "opcion", "nulo", "cualquiera"
                } and base_type not in self.structures:
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
        if self.dotted_print(("console", "log")) or self.dotted_print(
            ("System", "out", "println")
        ):
            expression = self.expression()
            self.value(")", "Se esperaba ')' después del texto")
            self.take(";")
            return ("print", expression)
        if self.cout_print():
            expression = self.expression()
            if self.take("<<"):
                if self.word("std"):
                    self.value(":", "Se esperaba std::endl")
                    self.value(":", "Se esperaba std::endl")
                if self.ident("Solo se admite endl después de la salida") != "endl":
                    raise CForgevError("Solo se admite endl después de la salida")
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

    def dotted_print(self, names: tuple[str, ...]) -> bool:
        cursor = self.current
        for index, name in enumerate(names):
            if cursor >= len(self.tokens) or self.tokens[cursor].kind != "IDENT" or self.tokens[cursor].value != name:
                return False
            cursor += 1
            if index + 1 < len(names):
                if cursor >= len(self.tokens) or self.tokens[cursor].value != ".":
                    return False
                cursor += 1
        if cursor >= len(self.tokens) or self.tokens[cursor].value != "(":
            return False
        self.current = cursor + 1
        return True

    def cout_print(self) -> bool:
        cursor = self.current
        if self.tokens[cursor].kind == "IDENT" and self.tokens[cursor].value == "std":
            if self.tokens[cursor + 1].value != ":" or self.tokens[cursor + 2].value != ":":
                return False
            cursor += 3
        if self.tokens[cursor].kind != "IDENT" or self.tokens[cursor].value != "cout":
            return False
        if self.tokens[cursor + 1].value != "<<":
            return False
        self.current = cursor + 2
        return True

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
        if self.word("await"):
            return ("await", self.unary())
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
                if token.value == "conjunto":
                    return ("set", arguments)
                return ("call", token.value, arguments)
            return ("variable", token.value)
        if token.value == "(":
            if self.take(")"):
                return ("tuple", [])
            first = self.expression()
            if not self.take(","):
                self.value(")", "Se esperaba ')'")
                return first
            values = [first]
            while self.peek().value != ")":
                values.append(self.expression())
                if not self.take(","):
                    break
            self.value(")", "Se esperaba ')' para cerrar la tupla")
            return ("tuple", values)
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

    def parse_type(self) -> str:
        """Lee tipos nominales y aplicaciones genéricas en una forma canónica."""
        name = self.ident("Se esperaba un tipo")
        if not self.take("<"):
            return name
        arguments: list[str] = []
        while True:
            arguments.append(self.parse_type())
            if not self.take(","):
                break
        self.value(">", "Se esperaba '>' para cerrar el tipo genérico")
        return f"{name}<{','.join(arguments)}>"

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

    def __init__(self) -> None:
        self.signatures: dict[str, tuple[list[str], str, bool, list[str]]] = {}
        self.expected_return = "cualquiera"
        self.interfaces: dict[str, dict[str, tuple[list[str], str]]] = {}

    def analyze(self, program: Program) -> None:
        self.signatures = {
            function[1]: (
                list(function[4]) if len(function) > 4 else ["cualquiera"] * len(function[2]),
                function[5] if len(function) > 5 else "cualquiera",
                bool(function[6]) if len(function) > 6 else False,
                list(function[8]) if len(function) > 8 else [],
            ) for function in program.functions
        }
        for declaration in program.statements:
            if declaration[0] == "ffi_function":
                self.signatures[declaration[1]] = (
                    list(declaration[3]), declaration[4], False, []
                )
        self.interfaces = {
            statement[1]: {
                method[1]: (list(method[2]), method[3]) for method in statement[2]
            }
            for statement in program.statements if statement[0] == "interface"
        }
        self._validate_interfaces(program.statements)
        global_types: dict[str, str] = {}
        self.statements(program.statements, global_types)
        for function in program.functions:
            function_types = dict(global_types)
            parameter_types, return_type, _, _ = self.signatures[function[1]]
            function_types.update(dict(zip(function[2], parameter_types)))
            previous = self.expected_return
            self.expected_return = return_type
            self.statements(function[3], function_types)
            self.expected_return = previous

    @staticmethod
    def _same_contract(wanted: str, actual: str) -> bool:
        return wanted == "cualquiera" or actual == "cualquiera" or wanted == actual

    @staticmethod
    def _compatible(wanted: str, actual: str) -> bool:
        if wanted == "cualquiera" or actual == "cualquiera" or wanted == actual:
            return True
        wanted_base, actual_base = wanted.split("<", 1)[0], actual.split("<", 1)[0]
        if wanted_base != actual_base:
            return False
        if "<" not in wanted or "<" not in actual:
            return True
        # Un constructor vacío como ninguno() conserva el contenedor, pero deja
        # que la anotación del destino determine el parámetro concreto.
        return actual == f"{actual_base}<cualquiera>"

    def _validate_interfaces(self, statements: list[Stmt]) -> None:
        for statement in statements:
            if statement[0] != "class":
                continue
            implemented = statement[4] if len(statement) > 4 else []
            methods = {method[2]: method for method in statement[3]}
            for interface_name in implemented:
                contract = self.interfaces.get(interface_name)
                if contract is None:
                    raise CForgevError(
                        f"Inferencia estática: interfaz desconocida '{interface_name}'"
                    )
                for method_name, (wanted_parameters, wanted_return) in contract.items():
                    method = methods.get(method_name)
                    if method is None:
                        raise CForgevError(
                            f"Inferencia estática: clase '{statement[1]}' no implementa "
                            f"'{interface_name}.{method_name}'"
                        )
                    actual_parameters = list(method[5]) if len(method) > 5 else ["cualquiera"] * len(method[3])
                    actual_return = method[6] if len(method) > 6 else "cualquiera"
                    if len(actual_parameters) != len(wanted_parameters) or any(
                        not self._same_contract(wanted, actual)
                        for wanted, actual in zip(wanted_parameters, actual_parameters)
                    ) or not self._same_contract(wanted_return, actual_return):
                        raise CForgevError(
                            f"Inferencia estática: '{statement[1]}.{method_name}' no cumple "
                            f"el contrato de '{interface_name}'"
                        )

    def statements(self, statements: list[Stmt], types: dict[str, str]) -> None:
        for statement in statements:
            kind = statement[0]
            if kind == "let":
                inferred = self.expression(statement[3], types)
                declared = statement[2] or inferred
                if statement[2] and not self._compatible(statement[2], inferred):
                    raise CForgevError(
                        f"Inferencia estática: '{statement[1]}' fue declarada {statement[2]} pero recibe {inferred}"
                    )
                if statement[2] and "<" not in statement[2] and inferred.startswith(statement[2] + "<"):
                    declared = inferred
                types[statement[1]] = declared
            elif kind == "assign":
                inferred = self.expression(statement[2], types)
                expected = types.get(statement[1], "cualquiera")
                if not self._compatible(expected, inferred):
                    raise CForgevError(
                        f"Inferencia estática: '{statement[1]}' es {expected} y no puede recibir {inferred}"
                    )
            elif kind == "if":
                self.expression(statement[1], types)
                self.statements(statement[2], types)
                self.statements(statement[3], types)
            elif kind == "while":
                self.expression(statement[1], types)
                self.statements(statement[2], types)
            elif kind == "try":
                self.statements(statement[1], types)
                handler_types = dict(types)
                handler_types[statement[2]] = "texto"
                self.statements(statement[3], handler_types)
            elif kind == "gpu":
                self.statements(statement[1], types)
            elif kind in {"region", "unsafe"}:
                self.statements(statement[1], types)
            elif kind == "extern":
                validate_foreign_memory(statement[1], statement[2])
            elif kind == "test":
                self.statements(statement[2], dict(types))
            elif kind in {"print", "return", "expression"}:
                inferred = self.expression(statement[1], types)
                if kind == "return" and self.expected_return not in {"cualquiera", inferred} and inferred != "cualquiera":
                    raise CForgevError(
                        f"Inferencia estática: retorno {inferred}, se esperaba {self.expected_return}"
                    )
            elif kind == "universal_import":
                types[statement[2]] = "cualquiera"
            elif kind == "ffi_function":
                continue

    def expression(self, expression: Expr, types: dict[str, str]) -> str:
        kind = expression[0]
        if kind == "number": return "numero"
        if kind == "string": return "texto"
        if kind == "bool": return "booleano"
        if kind == "null": return "nulo"
        if kind == "list":
            elements = [self.expression(value, types) for value in expression[1]]
            concrete = {value for value in elements if value != "cualquiera"}
            if len(concrete) > 1:
                return "lista<cualquiera>"
            return f"lista<{next(iter(concrete), 'cualquiera')}>"
        if kind == "tuple":
            elements = [self.expression(value, types) for value in expression[1]]
            return f"tupla<{','.join(elements)}>"
        if kind == "set":
            elements = [self.expression(value, types) for value in expression[1]]
            concrete = {value for value in elements if value != "cualquiera"}
            if len(concrete) > 1:
                raise CForgevError("Inferencia estática: un conjunto requiere elementos homogéneos")
            return f"conjunto<{next(iter(concrete), 'cualquiera')}>"
        if kind == "map":
            values: list[str] = []
            for key, value in expression[1]:
                key_type = self.expression(key, types)
                if key_type not in {"texto", "cualquiera"}:
                    raise CForgevError("Inferencia estática: la clave de un mapa debe ser texto")
                values.append(self.expression(value, types))
            concrete = {value for value in values if value != "cualquiera"}
            return f"mapa<{next(iter(concrete))}>" if len(concrete) == 1 else "mapa"
        if kind == "variable": return types.get(expression[1], "cualquiera")
        if kind == "unary":
            value_type = self.expression(expression[2], types)
            if expression[1] == "-" and value_type not in {"numero", "cualquiera"}:
                raise CForgevError("Inferencia estática: '-' requiere un número")
            return "booleano" if expression[1] == "no" else "numero"
        if kind == "await":
            inner = expression[1]
            if inner[0] == "call" and inner[1] in self.signatures:
                expected, returned, asynchronous, type_parameters = self.signatures[inner[1]]
                if not asynchronous:
                    raise CForgevError(f"Inferencia estática: await requiere una función async")
                # Reutiliza la validación de argumentos de la llamada.
                self.expression(inner, types)
                return returned
            self.expression(inner, types)
            return "cualquiera"
        if kind == "binary":
            left, right = self.expression(expression[2], types), self.expression(expression[3], types)
            if expression[1] in {"==", "!=", ">", ">=", "<", "<=", "y", "o"}:
                return "booleano"
            if expression[1] in {"-", "*", "/"} and any(
                value not in {"numero", "cualquiera"} for value in (left, right)
            ):
                raise CForgevError(
                    f"Inferencia estática: '{expression[1]}' requiere números, recibió {left} y {right}"
                )
            if expression[1] == "+" and left != "cualquiera" and right != "cualquiera" and left != right:
                raise CForgevError(
                    f"Inferencia estática: '+' no puede combinar {left} con {right}"
                )
            return left if left == right else "cualquiera"
        if kind == "call":
            argument_types = [self.expression(argument, types) for argument in expression[2]]
            if expression[1] == "algunos":
                if len(argument_types) != 1:
                    raise CForgevError("Inferencia estática: algunos requiere un argumento")
                return f"opcion<{argument_types[0]}>"
            if expression[1] == "ninguno":
                if argument_types:
                    raise CForgevError("Inferencia estática: ninguno no recibe argumentos")
                return "opcion<cualquiera>"
            if expression[1] == "es_algunos":
                if len(argument_types) != 1 or not argument_types[0].startswith("opcion"):
                    raise CForgevError("Inferencia estática: es_algunos requiere una opcion")
                return "booleano"
            if expression[1] == "desenvolver":
                if len(argument_types) != 1 or not argument_types[0].startswith("opcion"):
                    raise CForgevError("Inferencia estática: desenvolver requiere una opcion")
                option_type = argument_types[0]
                return option_type[7:-1] if option_type.startswith("opcion<") else "cualquiera"
            if expression[1] in self.signatures:
                expected, returned, asynchronous, type_parameters = self.signatures[expression[1]]
                if len(expected) != len(argument_types):
                    raise CForgevError(
                        f"Inferencia estática: '{expression[1]}' requiere {len(expected)} argumentos"
                    )
                substitutions: dict[str, str] = {}
                for index, (wanted, actual) in enumerate(zip(expected, argument_types), 1):
                    if wanted in type_parameters:
                        previous = substitutions.setdefault(wanted, actual)
                        if previous != "cualquiera" and actual != "cualquiera" and previous != actual:
                            raise CForgevError(
                                f"Inferencia estática: el genérico '{wanted}' recibió {previous} y {actual}"
                            )
                    elif not self._compatible(wanted, actual):
                        raise CForgevError(
                            f"Inferencia estática: argumento {index} de '{expression[1]}' requiere {wanted}, recibió {actual}"
                        )
                resolved_return = substitutions.get(returned, returned)
                return "tarea" if asynchronous else resolved_return
            return "cualquiera"
        if kind == "method_call":
            self.expression(expression[1], types)
            for argument in expression[3]: self.expression(argument, types)
            return "cualquiera"
        if kind == "field":
            self.expression(expression[1], types); return "cualquiera"
        if kind == "index":
            owner = self.expression(expression[1], types)
            index = self.expression(expression[2], types)
            if owner.startswith("mapa<") and owner.endswith(">"):
                return owner[5:-1]
            if owner.startswith("tupla<") and owner.endswith(">"):
                if expression[2][0] != "number" or "." in expression[2][1]:
                    raise CForgevError("Inferencia estática: el índice de tupla debe ser constante entero")
                elements = owner[6:-1].split(",") if owner[6:-1] else []
                position = int(expression[2][1])
                if position < 0 or position >= len(elements):
                    raise CForgevError("Inferencia estática: índice de tupla fuera de rango")
                return elements[position]
            return "cualquiera"
        return "cualquiera"


RUNTIME = r'''#include <algorithm>
#include <atomic>
#include "cforge_shared_arena.h"
#include <cmath>
#include <chrono>
#include <condition_variable>
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
#include <deque>
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
struct ForgeValue;struct CfvDenseMatrix;struct CfvTuple;struct CfvSet;
using Value=ForgeValue;using Lista=std::shared_ptr<std::vector<ForgeValue>>;using Mapa=std::shared_ptr<std::map<std::string,ForgeValue>>;using FastArray=std::shared_ptr<std::vector<double>>;using DenseMatrix=std::shared_ptr<CfvDenseMatrix>;using Tupla=std::shared_ptr<CfvTuple>;using Conjunto=std::shared_ptr<CfvSet>;
struct CfvDenseMatrix{size_t rows=0,columns=0;std::vector<double>values;};
struct ForgeValue{std::variant<std::monostate,double,std::string,bool,Lista,Mapa,FastArray,DenseMatrix,Tupla,Conjunto>data;std::string origin="cforgev";ForgeValue()=default;ForgeValue(double v):data(v){}ForgeValue(std::string v):data(std::move(v)){}ForgeValue(const char*v):data(std::string(v)){}ForgeValue(bool v):data(v){}ForgeValue(Lista v):data(std::move(v)){}ForgeValue(Mapa v):data(std::move(v)){}ForgeValue(FastArray v):data(std::move(v)){}ForgeValue(DenseMatrix v):data(std::move(v)){}ForgeValue(Tupla v):data(std::move(v)){}ForgeValue(Conjunto v):data(std::move(v)){}size_t index()const{return data.index();}};
struct CfvTuple{std::vector<ForgeValue>values;};struct CfvSet{std::vector<ForgeValue>values;};
static ForgeValue cfv_origin(ForgeValue value,std::string origin){value.origin=std::move(origin);return value;}
enum CfvType{CFV_NULL=0,CFV_INTEGER=1,CFV_DECIMAL=2,CFV_TEXT=3,CFV_BOOLEAN=4,CFV_LIST=5,CFV_MAP=6,CFV_RECORD=7};
using CfvReleaseFunction=void(*)(void*);
struct CfvValue{int32_t type;int64_t integer;double decimal;const char* text;void*owner;CfvReleaseFunction release;};
using CfvForeignFunction=int(*)(const CfvValue*,size_t,CfvValue*,char*,size_t);
static constexpr uint32_t CFV_ABI_V2=0x00020000u;
static constexpr uint64_t CFV_V2_BORROWED=0x00000001ull,CFV_V2_OWNED=0x00000002ull;
static constexpr uint32_t CFV_V2_MAX_DEPTH=64u;
struct CfvValueV2{uint32_t struct_size;uint32_t type;uint64_t flags;uint64_t length;int64_t integer;double decimal;const void*data;void*owner;CfvReleaseFunction release;};
struct CfvMapEntryV2{CfvValueV2 key;CfvValueV2 value;};
struct CfvRecordFieldV2{const char*name;uint64_t name_length;CfvValueV2 value;};
struct CfvRecordV2{const char*type_name;uint64_t type_name_length;const CfvRecordFieldV2*fields;uint64_t field_count;};
using CfvForeignFunctionV2=int(*)(uint32_t,const CfvValueV2*,size_t,CfvValueV2*,char*,size_t);
#ifdef CFV_WITH_JNI
class CfvJvmRuntime{JavaVM*vm_=nullptr;JNIEnv*env_=nullptr;public:CfvJvmRuntime(){JavaVMInitArgs args{};JavaVMOption options[1];options[0].optionString=(char*)"-Djava.class.path=.";args.version=JNI_VERSION_1_8;args.nOptions=1;args.options=options;args.ignoreUnrecognized=JNI_FALSE;if(JNI_CreateJavaVM(&vm_,(void**)&env_,&args)!=JNI_OK)throw std::runtime_error("no se pudo crear JVM");}~CfvJvmRuntime(){if(vm_)vm_->DestroyJavaVM();}JNIEnv*env()const{return env_;}CfvJvmRuntime(const CfvJvmRuntime&)=delete;CfvJvmRuntime&operator=(const CfvJvmRuntime&)=delete;};
#endif
static std::map<std::string,CfvForeignFunction>&cfv_registry(){static std::map<std::string,CfvForeignFunction>value;return value;}
static std::map<std::string,CfvForeignFunctionV2>&cfv_registry_v2(){static std::map<std::string,CfvForeignFunctionV2>value;return value;}
static std::mutex cfv_symbol_mutex;static std::map<std::string,ForgeValue*>cfv_symbols;
static void cfv_share_symbol(const std::string&name,ForgeValue*value){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);cfv_symbols[name]=value;}
static ForgeValue cfv_symbol(const std::string&name){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);auto found=cfv_symbols.find(name);if(found==cfv_symbols.end()||!found->second)throw std::runtime_error("símbolo global desconocido: "+name);return *found->second;}
static ForgeValue cfv_symbol_snapshot(){std::lock_guard<std::mutex>lock(cfv_symbol_mutex);auto map=std::make_shared<std::map<std::string,ForgeValue>>();for(const auto&[name,value]:cfv_symbols)if(value)(*map)[name]=*value;return map;}
#ifdef _WIN32
extern "C" __declspec(dllexport) int cfv_register_function(const char*name,CfvForeignFunction fn){if(!name||!fn)return 1;cfv_registry()[name]=fn;return 0;}
extern "C" __declspec(dllexport) int cfv_register_function_v2(const char*name,CfvForeignFunctionV2 fn){if(!name||!fn)return 1;cfv_registry_v2()[name]=fn;return 0;}
#else
extern "C" __attribute__((visibility("default"))) int cfv_register_function(const char*name,CfvForeignFunction fn){if(!name||!fn)return 1;cfv_registry()[name]=fn;return 0;}
extern "C" __attribute__((visibility("default"))) int cfv_register_function_v2(const char*name,CfvForeignFunctionV2 fn){if(!name||!fn)return 1;cfv_registry_v2()[name]=fn;return 0;}
#endif
static double numero(const Value& v) { if (auto p=std::get_if<double>(&v.data)) return *p; throw std::runtime_error("se esperaba un número"); }
static bool verdad(const Value& v) { if (auto p=std::get_if<bool>(&v.data)) return *p; throw std::runtime_error("se esperaba verdadero o falso"); }
static Value suma(const Value&a,const Value&b){ if(a.index()==1&&b.index()==1)return numero(a)+numero(b); if(a.index()==2&&b.index()==2)return std::get<std::string>(a.data)+std::get<std::string>(b.data); throw std::runtime_error("'+' requiere dos números o dos textos"); }
static Value resta(const Value&a,const Value&b){return numero(a)-numero(b);} static Value multiplica(const Value&a,const Value&b){return numero(a)*numero(b);}
static Value divide(const Value&a,const Value&b){double d=numero(b);if(d==0)throw std::runtime_error("no se puede dividir por cero");return numero(a)/d;}
static Value compara(const Value&a,const Value&b,const std::string&o){if(o=="==")return a.data==b.data;if(o=="!=")return a.data!=b.data;if(a.index()==1&&b.index()==1){double x=numero(a),y=numero(b);if(o==">")return x>y;if(o==">=")return x>=y;if(o=="<")return x<y;return x<=y;}if(a.index()==2&&b.index()==2){auto x=std::get<std::string>(a.data),y=std::get<std::string>(b.data);if(o==">")return x>y;if(o==">=")return x>=y;if(o=="<")return x<y;return x<=y;}throw std::runtime_error("comparación entre tipos incompatibles");}
static std::string cfv_number_text(double value){std::ostringstream stream;if(std::floor(value)==value)stream<<(long long)value;else stream<<value;return stream.str();}
static std::string texto(const Value&v){if(v.index()==0)return "nulo";if(auto p=std::get_if<double>(&v.data))return cfv_number_text(*p);if(auto p=std::get_if<std::string>(&v.data))return *p;if(auto p=std::get_if<bool>(&v.data))return *p?"verdadero":"falso";if(auto p=std::get_if<Lista>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=", ";s+=texto((*p)->at(i));}return s+"]";}if(auto p=std::get_if<Mapa>(&v.data)){auto marker=(*p)->find("__opcion");if(marker!=(*p)->end()){auto has=(*p)->find("tiene");if(has==(*p)->end()||!verdad(has->second))return "ninguno";return "algunos("+texto((*p)->at("valor"))+")";}std::string s="{";bool first=true;for(auto&[k,x]:**p){if(!first)s+=", ";first=false;s+="\""+k+"\": "+texto(x);}return s+"}";}if(auto p=std::get_if<FastArray>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=", ";s+=cfv_number_text((*p)->at(i));}return s+"]";}if(auto p=std::get_if<DenseMatrix>(&v.data)){std::string s="[";for(size_t row=0;row<(*p)->rows;++row){if(row)s+=", ";s+="[";for(size_t column=0;column<(*p)->columns;++column){if(column)s+=", ";s+=cfv_number_text((*p)->values[row*(*p)->columns+column]);}s+="]";}return s+"]";}if(auto p=std::get_if<Tupla>(&v.data)){std::string s="(";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=", ";s+=texto((*p)->values[i]);}if((*p)->values.size()==1)s+=",";return s+")";}if(auto p=std::get_if<Conjunto>(&v.data)){std::string s="conjunto(";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=", ";s+=texto((*p)->values[i]);}return s+")";}throw std::runtime_error("ForgeValue desconocido");}
static std::string cfv_json_escape(const std::string&input){std::string out="\"";for(unsigned char c:input){switch(c){case '\"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:if(c<32){char b[7];std::snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=(char)c;}}return out+"\"";}
static std::string cfv_canonical_json(const Value&v){if(v.index()==0)return "null";if(auto p=std::get_if<double>(&v.data))return cfv_number_text(*p);if(auto p=std::get_if<std::string>(&v.data))return cfv_json_escape(*p);if(auto p=std::get_if<bool>(&v.data))return *p?"true":"false";if(auto p=std::get_if<Lista>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->at(i));}return s+"]";}if(auto p=std::get_if<Mapa>(&v.data)){std::string s="{";bool first=true;for(const auto&[k,x]:**p){if(!first)s+=",";first=false;s+=cfv_json_escape(k)+":"+cfv_canonical_json(x);}return s+"}";}if(auto p=std::get_if<Tupla>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->values[i]);}return s+"]";}if(auto p=std::get_if<Conjunto>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->values.size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->values[i]);}return s+"]";}return cfv_json_escape(texto(v));}
struct CfvArenaRuntime{std::filesystem::path path;std::unique_ptr<cforge::arena::ForgeSharedArena>arena;std::mutex mutex;std::map<std::string,cforge::arena::Offset>latest;CfvArenaRuntime(){auto id=
#ifdef _WIN32
(unsigned long long)GetCurrentProcessId();
#else
(unsigned long long)getpid();
#endif
path=std::filesystem::temp_directory_path()/("cforge-arena-"+std::to_string(id)+".bin");arena=std::make_unique<cforge::arena::ForgeSharedArena>(cforge::arena::ForgeSharedArena::create(path,64ULL*1024ULL*1024ULL));}~CfvArenaRuntime(){std::error_code error;arena.reset();std::filesystem::remove(path,error);}};
static CfvArenaRuntime&cfv_arena_runtime(){static CfvArenaRuntime runtime;return runtime;}
static Value cfv_arena_stage(Value value,const std::string&connector){auto&runtime=cfv_arena_runtime();auto json=cfv_canonical_json(value);std::lock_guard<std::mutex>guard(runtime.mutex);runtime.latest[connector]=runtime.arena->store_text(cforge::arena::ValueType::Json,json);return value;}
static Value cfv_arena_estado(){auto&runtime=cfv_arena_runtime();auto out=std::make_shared<std::map<std::string,Value>>();(*out)["ruta"]=runtime.path.string();(*out)["capacidad"]=(double)runtime.arena->capacity();(*out)["usado"]=(double)runtime.arena->used();(*out)["registros_vivos"]=(double)runtime.arena->live_records();auto offsets=std::make_shared<std::map<std::string,Value>>();{std::lock_guard<std::mutex>guard(runtime.mutex);for(const auto&[name,offset]:runtime.latest)(*offsets)[name]=(double)offset;}(*out)["offsets"]=offsets;return out;}
static Value cfv_catalogo(){auto out=std::make_shared<std::map<std::string,Value>>();(*out)["ia_"]=std::string("python");(*out)["ui_"]=std::string("java");(*out)["web_"]=std::string("javascript");return out;}
static Value cfv_catalog_dispatch(const std::string&,const std::string&,const Value&);
static Value cfv_compat_append(Value collection,Value item){auto list=std::get_if<Lista>(&collection.data);if(!list)throw std::runtime_error("append/push requiere una lista");(*list)->push_back(std::move(item));cfv_arena_stage(collection,"compat_collection");return Value{};}
static Value cfv_compat_length(Value collection){cfv_arena_stage(collection,"compat_length");if(auto p=std::get_if<std::string>(&collection.data))return (double)p->size();if(auto p=std::get_if<Lista>(&collection.data))return (double)(*p)->size();if(auto p=std::get_if<Mapa>(&collection.data))return (double)(*p)->size();if(auto p=std::get_if<FastArray>(&collection.data))return (double)(*p)->size();if(auto p=std::get_if<DenseMatrix>(&collection.data))return (double)(*p)->rows;if(auto p=std::get_if<Tupla>(&collection.data))return (double)(*p)->values.size();if(auto p=std::get_if<Conjunto>(&collection.data))return (double)(*p)->values.size();throw std::runtime_error("length/len requiere texto o colección");}
static void mostrar(const Value&v){std::cout<<texto(v)<<'\n';}
static Value cfv_leer(Value mensaje=Value{std::string("")}){if(mensaje.index()!=2)throw std::runtime_error("el mensaje de leer debe ser texto");std::cout<<std::get<std::string>(mensaje.data);std::string s;std::getline(std::cin,s);return s;}
static Value cfv_a_numero(const Value&v){try{if(auto p=std::get_if<double>(&v.data))return *p;if(auto p=std::get_if<std::string>(&v.data))return std::stod(*p);}catch(...){ }throw std::runtime_error("no se puede convertir a número");}
static Value cfv_a_texto(const Value&v){return texto(v);}
static Value cfv_longitud(const Value&v){if(auto p=std::get_if<std::string>(&v.data))return (double)p->size();if(auto p=std::get_if<Lista>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<Mapa>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<FastArray>(&v.data))return (double)(*p)->size();if(auto p=std::get_if<DenseMatrix>(&v.data))return (double)(*p)->rows;if(auto p=std::get_if<Tupla>(&v.data))return (double)(*p)->values.size();if(auto p=std::get_if<Conjunto>(&v.data))return (double)(*p)->values.size();throw std::runtime_error("longitud requiere una colección");}
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
static Value cfv_file_read(const Value&ruta){return cfv_arena_stage(cfv_leer_archivo(ruta),"file_read");}
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
static std::vector<CfvValue> cfv_to_abi(const Value&args,std::vector<std::string>&storage){auto p=std::get_if<Lista>(&args.data);if(!p)throw std::runtime_error("los argumentos extranjeros deben ser una lista");std::vector<CfvValue>out;storage.reserve((*p)->size());for(const auto&v:**p){if(v.index()==0)out.push_back({CFV_NULL,0,0,nullptr,nullptr,nullptr});else if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n)out.push_back({CFV_INTEGER,(int64_t)*n,0,nullptr,nullptr,nullptr});else out.push_back({CFV_DECIMAL,0,*n,nullptr,nullptr,nullptr});}else if(auto s=std::get_if<std::string>(&v.data)){storage.push_back(*s);out.push_back({CFV_TEXT,0,0,storage.back().c_str(),nullptr,nullptr});}else if(auto b=std::get_if<bool>(&v.data))out.push_back({CFV_BOOLEAN,*b?1:0,0,nullptr,nullptr,nullptr});else throw std::runtime_error("ABI extranjero solo acepta números, textos, booleanos y nulo");}return out;}
struct CfvResultGuard{CfvValue*value;~CfvResultGuard(){if(value&&value->release){auto release=value->release;auto owner=value->owner;value->release=nullptr;release(owner);}}};
static Value cfv_from_abi(const CfvValue&v){if(v.type==CFV_NULL)return Value{};if(v.type==CFV_INTEGER)return (double)v.integer;if(v.type==CFV_DECIMAL)return v.decimal;if(v.type==CFV_TEXT)return std::string(v.text?v.text:"");if(v.type==CFV_BOOLEAN)return Value{v.integer!=0};throw std::runtime_error("tipo ABI de retorno desconocido");}
static Value cfv_invoke_foreign(CfvForeignFunction fn,const Value&args){std::vector<std::string>storage;auto abi=cfv_to_abi(args,storage);CfvValue result{CFV_NULL,0,0,nullptr,nullptr,nullptr};CfvResultGuard guard{&result};char error[1024]={0};int status=0;try{status=fn(abi.data(),abi.size(),&result,error,sizeof(error));}catch(const std::exception&e){throw std::runtime_error(std::string("excepción C++: ")+e.what());}catch(...){throw std::runtime_error("excepción nativa desconocida");}if(status!=0)throw std::runtime_error(error[0]?error:"la función extranjera falló");return cfv_from_abi(result);}
struct CfvAbiStorageV2{
std::deque<std::string>texts;
std::deque<std::vector<CfvValueV2>>lists;
std::deque<std::vector<CfvMapEntryV2>>maps;
std::deque<std::vector<CfvRecordFieldV2>>record_fields;
std::deque<CfvRecordV2>records;
};
static CfvValueV2 cfv_to_abi_v2_value(const Value&v,CfvAbiStorageV2&storage,uint32_t depth=0){
if(depth>CFV_V2_MAX_DEPTH)throw std::runtime_error("ABI V2 excede la profundidad máxima");
CfvValueV2 item{sizeof(CfvValueV2),CFV_NULL,0,0,0,0,nullptr,nullptr,nullptr};
if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n){item.type=CFV_INTEGER;item.integer=(int64_t)*n;}else{item.type=CFV_DECIMAL;item.decimal=*n;}}
else if(auto s=std::get_if<std::string>(&v.data)){storage.texts.push_back(*s);item.type=CFV_TEXT;item.flags=CFV_V2_BORROWED;item.length=storage.texts.back().size();item.data=storage.texts.back().data();}
else if(auto b=std::get_if<bool>(&v.data)){item.type=CFV_BOOLEAN;item.integer=*b?1:0;}
else if(auto list=std::get_if<Lista>(&v.data)){std::vector<CfvValueV2>values;values.reserve((*list)->size());for(const auto&value:**list)values.push_back(cfv_to_abi_v2_value(value,storage,depth+1));storage.lists.push_back(std::move(values));item.type=CFV_LIST;item.flags=CFV_V2_BORROWED;item.length=storage.lists.back().size();item.data=storage.lists.back().data();}
else if(auto map=std::get_if<Mapa>(&v.data)){
auto class_marker=(*map)->find("__clase");
if(class_marker!=(*map)->end()&&class_marker->second.index()==2){
std::vector<CfvRecordFieldV2>fields;fields.reserve((*map)->size()-1);
for(const auto&[key,value]:**map){if(key=="__clase")continue;storage.texts.push_back(key);auto&name=storage.texts.back();fields.push_back({name.data(),name.size(),cfv_to_abi_v2_value(value,storage,depth+1)});}
storage.record_fields.push_back(std::move(fields));storage.texts.push_back(std::get<std::string>(class_marker->second.data));auto&type=storage.texts.back();storage.records.push_back({type.data(),type.size(),storage.record_fields.back().data(),storage.record_fields.back().size()});item.type=CFV_RECORD;item.flags=CFV_V2_BORROWED;item.length=storage.records.back().field_count;item.data=&storage.records.back();
}else{
std::vector<CfvMapEntryV2>entries;entries.reserve((*map)->size());
for(const auto&[key,value]:**map){storage.texts.push_back(key);CfvValueV2 encoded_key{sizeof(CfvValueV2),CFV_TEXT,CFV_V2_BORROWED,storage.texts.back().size(),0,0,storage.texts.back().data(),nullptr,nullptr};entries.push_back({encoded_key,cfv_to_abi_v2_value(value,storage,depth+1)});}
storage.maps.push_back(std::move(entries));item.type=CFV_MAP;item.flags=CFV_V2_BORROWED;item.length=storage.maps.back().size();item.data=storage.maps.back().data();
}}
else throw std::runtime_error("tipo no compatible con ABI V2");
return item;
}
static std::vector<CfvValueV2>cfv_to_abi_v2(const Value&args,CfvAbiStorageV2&storage){auto p=std::get_if<Lista>(&args.data);if(!p)throw std::runtime_error("los argumentos ABI V2 deben ser una lista");std::vector<CfvValueV2>out;out.reserve((*p)->size());for(const auto&v:**p)out.push_back(cfv_to_abi_v2_value(v,storage));return out;}
struct CfvResultGuardV2{CfvValueV2*value;~CfvResultGuardV2(){if(value&&value->release){auto release=value->release;auto owner=value->owner;value->release=nullptr;release(owner);}}};
static Value cfv_from_abi_v2(const CfvValueV2&v,uint32_t depth=0){
if(depth>CFV_V2_MAX_DEPTH)throw std::runtime_error("resultado ABI V2 excede la profundidad máxima");
if(v.struct_size<sizeof(CfvValueV2))throw std::runtime_error("resultado ABI V2 usa una estructura incompleta");
if(v.type==CFV_NULL)return Value{};if(v.type==CFV_INTEGER)return (double)v.integer;if(v.type==CFV_DECIMAL)return v.decimal;if(v.type==CFV_BOOLEAN)return Value{v.integer!=0};
if(v.type==CFV_TEXT){if(!v.data&&v.length)throw std::runtime_error("texto ABI V2 tiene puntero nulo");return std::string(v.data?static_cast<const char*>(v.data):"",(size_t)v.length);}
if(v.length>10000000ull)throw std::runtime_error("colección ABI V2 excede el límite de elementos");
if(v.type==CFV_LIST){if(!v.data&&v.length)throw std::runtime_error("lista ABI V2 tiene puntero nulo");auto values=std::make_shared<std::vector<Value>>();values->reserve(v.length);auto raw=static_cast<const CfvValueV2*>(v.data);for(uint64_t i=0;i<v.length;++i){if(raw[i].release)throw std::runtime_error("elemento ABI V2 anidado no puede tener liberador");values->push_back(cfv_from_abi_v2(raw[i],depth+1));}return values;}
if(v.type==CFV_MAP){if(!v.data&&v.length)throw std::runtime_error("mapa ABI V2 tiene puntero nulo");auto values=std::make_shared<std::map<std::string,Value>>();auto raw=static_cast<const CfvMapEntryV2*>(v.data);for(uint64_t i=0;i<v.length;++i){if(raw[i].key.type!=CFV_TEXT)throw std::runtime_error("clave ABI V2 debe ser texto");if(raw[i].key.release||raw[i].value.release)throw std::runtime_error("entrada ABI V2 anidada no puede tener liberador");auto key=cfv_from_abi_v2(raw[i].key,depth+1);(*values)[std::get<std::string>(key.data)]=cfv_from_abi_v2(raw[i].value,depth+1);}return values;}
if(v.type==CFV_RECORD){if(!v.data)throw std::runtime_error("registro ABI V2 tiene puntero nulo");auto record=static_cast<const CfvRecordV2*>(v.data);if(record->field_count>1000000ull||(!record->fields&&record->field_count))throw std::runtime_error("registro ABI V2 inválido");auto values=std::make_shared<std::map<std::string,Value>>();(*values)["__clase"]=std::string(record->type_name?record->type_name:"",record->type_name_length);for(uint64_t i=0;i<record->field_count;++i){const auto&field=record->fields[i];if(field.value.release)throw std::runtime_error("campo ABI V2 anidado no puede tener liberador");(*values)[std::string(field.name?field.name:"",field.name_length)]=cfv_from_abi_v2(field.value,depth+1);}return values;}
throw std::runtime_error("tipo ABI V2 de retorno desconocido");
}
static Value cfv_invoke_foreign_v2(CfvForeignFunctionV2 fn,const Value&args){CfvAbiStorageV2 storage;auto abi=cfv_to_abi_v2(args,storage);CfvValueV2 result{sizeof(CfvValueV2),CFV_NULL,0,0,0,0,nullptr,nullptr,nullptr};CfvResultGuardV2 guard{&result};char error[1024]={0};int status=0;try{status=fn(CFV_ABI_V2,abi.data(),abi.size(),&result,error,sizeof(error));}catch(const std::exception&e){throw std::runtime_error(std::string("excepción C++ ABI V2: ")+e.what());}catch(...){throw std::runtime_error("excepción nativa ABI V2 desconocida");}if(status!=0)throw std::runtime_error(error[0]?error:"la función extranjera ABI V2 falló");return cfv_from_abi_v2(result);}
static Value cfv_use_cpp(const Value&name,const Value&args){if(name.index()!=2)throw std::runtime_error("el nombre C++ debe ser texto");auto key=std::get<std::string>(name.data);auto&registry2=cfv_registry_v2();auto modern=registry2.find(key);if(modern!=registry2.end())return cfv_invoke_foreign_v2(modern->second,args);auto&registry=cfv_registry();auto it=registry.find(key);if(it==registry.end())throw std::runtime_error("función C++ no registrada: "+key);return cfv_invoke_foreign(it->second,args);}
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
static PyRef cfv_to_python(const Value&v){if(v.index()==0){Py_INCREF(Py_None);return PyRef(Py_None);}if(auto n=std::get_if<double>(&v.data)){if(std::floor(*n)==*n)return PyRef(PyLong_FromLongLong((long long)*n));return PyRef(PyFloat_FromDouble(*n));}if(auto s=std::get_if<std::string>(&v.data))return PyRef(PyUnicode_DecodeUTF8(s->data(),(Py_ssize_t)s->size(),"strict"));if(auto b=std::get_if<bool>(&v.data))return PyRef(PyBool_FromLong(*b));if(auto list=std::get_if<Lista>(&v.data)){PyRef out(PyList_New((*list)->size()));for(size_t i=0;i<(*list)->size();++i){auto item=cfv_to_python((*list)->at(i));PyList_SET_ITEM(out.get(),i,item.release());}return out;}if(auto tuple=std::get_if<Tupla>(&v.data)){PyRef out(PyTuple_New((*tuple)->values.size()));for(size_t i=0;i<(*tuple)->values.size();++i){auto item=cfv_to_python((*tuple)->values[i]);PyTuple_SET_ITEM(out.get(),i,item.release());}return out;}if(auto set=std::get_if<Conjunto>(&v.data)){PyRef out(PySet_New(nullptr));for(const auto&value:(*set)->values){auto item=cfv_to_python(value);if(PySet_Add(out.get(),item.get())!=0)throw std::runtime_error(cfv_python_error("conjunto Python inválido"));}return out;}if(auto map=std::get_if<Mapa>(&v.data)){PyRef out(PyDict_New());for(const auto&[key,value]:**map){PyRef py_key(PyUnicode_DecodeUTF8(key.data(),(Py_ssize_t)key.size(),"strict"));auto item=cfv_to_python(value);if(!py_key||PyDict_SetItem(out.get(),py_key.get(),item.get())!=0)throw std::runtime_error(cfv_python_error("mapa Python inválido"));}return out;}if(auto array=std::get_if<FastArray>(&v.data)){PyRef out(PyList_New((*array)->size()));for(size_t i=0;i<(*array)->size();++i)PyList_SET_ITEM(out.get(),i,PyFloat_FromDouble((*array)->at(i)));return out;}if(auto matrix=std::get_if<DenseMatrix>(&v.data)){PyRef out(PyList_New((*matrix)->rows));for(size_t row=0;row<(*matrix)->rows;++row){PyObject*values=PyList_New((*matrix)->columns);for(size_t column=0;column<(*matrix)->columns;++column)PyList_SET_ITEM(values,column,PyFloat_FromDouble((*matrix)->values[row*(*matrix)->columns+column]));PyList_SET_ITEM(out.get(),row,values);}return out;}throw std::runtime_error("tipo no compatible con Python");}
static Value cfv_from_python(PyObject*o){if(o==Py_None)return Value{};if(PyBool_Check(o))return Value{o==Py_True};if(PyLong_Check(o)){auto value=PyLong_AsLongLong(o);if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("entero Python inválido"));return (double)value;}if(PyFloat_Check(o)){auto value=PyFloat_AsDouble(o);if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("decimal Python inválido"));return value;}if(PyUnicode_Check(o)){Py_ssize_t size=0;auto text=PyUnicode_AsUTF8AndSize(o,&size);if(!text)throw std::runtime_error(cfv_python_error("texto Python inválido"));return std::string(text,(size_t)size);}if(PyList_Check(o)){auto out=std::make_shared<std::vector<Value>>();Py_ssize_t size=PyList_Size(o);for(Py_ssize_t i=0;i<size;++i)out->push_back(cfv_from_python(PyList_GetItem(o,i)));return out;}if(PyTuple_Check(o)){auto out=std::make_shared<CfvTuple>();Py_ssize_t size=PyTuple_Size(o);for(Py_ssize_t i=0;i<size;++i)out->values.push_back(cfv_from_python(PyTuple_GetItem(o,i)));return out;}if(PySet_Check(o)){auto out=std::make_shared<CfvSet>();PyRef iterator(PyObject_GetIter(o));while(true){PyRef item(PyIter_Next(iterator.get()));if(!item)break;out->values.push_back(cfv_from_python(item.get()));}if(PyErr_Occurred())throw std::runtime_error(cfv_python_error("conjunto Python inválido"));std::sort(out->values.begin(),out->values.end(),[](const Value&a,const Value&b){return cfv_canonical_json(a)<cfv_canonical_json(b);});return out;}if(PyDict_Check(o)){auto out=std::make_shared<std::map<std::string,Value>>();PyObject*key;PyObject*value;Py_ssize_t pos=0;while(PyDict_Next(o,&pos,&key,&value)){if(!PyUnicode_Check(key))throw std::runtime_error("claves extranjeras deben ser texto");Py_ssize_t size=0;const char*text=PyUnicode_AsUTF8AndSize(key,&size);if(!text)throw std::runtime_error(cfv_python_error("clave Python inválida"));(*out)[std::string(text,(size_t)size)]=cfv_from_python(value);}return out;}throw std::runtime_error("Python devolvió un tipo no compatible");}
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
static Value cfv_json_parse(const Value&text){if(text.index()!=2)throw std::runtime_error("json_parse requiere texto");cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(text);return cfv_arena_stage(cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_json_parse")},Value{args}),"cforgev"),"json_parse");}
static Value cfv_sys_fetch(const Value&url){if(url.index()!=2)throw std::runtime_error("sys_fetch requiere una URL");cfv_prepare_polyglot();auto args=std::make_shared<std::vector<Value>>();args->push_back(url);return cfv_arena_stage(cfv_origin(cfv_use_python(Value{std::string("__main__")},Value{std::string("_cfv_fetch")},Value{args}),"cforgev"),"sys_fetch");}
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
static Value cfv_catalog_dispatch(const std::string&engine,const std::string&name,const Value&arguments){Value staged=cfv_arena_stage(arguments,name);const char*setting=std::getenv(engine=="python"?"CFORGE_IA_MODULE":engine=="javascript"?"CFORGE_WEB_MODULE":"CFORGE_UI_ADAPTER");if(!setting||!*setting)throw std::runtime_error("conector "+name+" enrutado a "+engine+", pero su adaptador no está configurado");if(engine=="python")return cfv_use_python(Value{std::string(setting)},Value{name},staged);if(engine=="javascript")return cfv_use_javascript(Value{std::string(setting)},Value{name},staged);throw std::runtime_error("conector "+name+" requiere el adaptador Java declarado en CFORGE_UI_ADAPTER");}
static Value cfv_forge_bench(const std::function<Value()>&function,const Value&count){long long iterations=(long long)numero(count);if(iterations<1||iterations>10000000)throw std::runtime_error("forge_bench requiere 1..10.000.000 iteraciones");Value result;auto started=std::chrono::steady_clock::now();for(long long i=0;i<iterations;++i)result=function();double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count();auto report=std::make_shared<std::map<std::string,Value>>();(*report)["resultado"]=result;(*report)["iteraciones"]=(double)iterations;(*report)["segundos"]=seconds;(*report)["por_segundo"]=seconds>0?iterations/seconds:0;return report;}
static Value crear_lista(std::initializer_list<Value>v){return std::make_shared<std::vector<Value>>(v);}static Value crear_mapa(std::initializer_list<std::pair<const std::string,Value>>v){return std::make_shared<std::map<std::string,Value>>(v);}static Value crear_tupla(std::initializer_list<Value>v){auto out=std::make_shared<CfvTuple>();out->values.assign(v);return out;}static Value crear_conjunto(std::initializer_list<Value>v){auto out=std::make_shared<CfvSet>();for(const auto&item:v){auto key=cfv_canonical_json(item);bool exists=false;for(const auto&current:out->values)if(cfv_canonical_json(current)==key){exists=true;break;}if(!exists)out->values.push_back(item);}std::sort(out->values.begin(),out->values.end(),[](const Value&a,const Value&b){return cfv_canonical_json(a)<cfv_canonical_json(b);});return out;}
static Value cfv_mover(Value value){return value;}static Value cfv_prestar(Value value){return value;}static Value cfv_prestar_mut(Value value){return value;}static Value cfv_soltar_prestamo(const Value&){return Value{};}static Value cfv_destruir(const Value&){return Value{};}
static Value cfv_algunos(Value value){return crear_mapa({{"__opcion",Value{true}},{"tiene",Value{true}},{"valor",std::move(value)}});}static Value cfv_ninguno(){return crear_mapa({{"__opcion",Value{true}},{"tiene",Value{false}},{"valor",Value{}}});}
static Mapa cfv_opcion_mapa(const Value&value){auto map=std::get_if<Mapa>(&value.data);if(!map||(*map)->find("__opcion")==(*map)->end())throw std::runtime_error("se esperaba una opcion");return *map;}static Value cfv_es_algunos(const Value&value){auto map=cfv_opcion_mapa(value);return verdad(map->at("tiene"));}static Value cfv_desenvolver(const Value&value){auto map=cfv_opcion_mapa(value);if(!verdad(map->at("tiene")))throw std::runtime_error("no se puede desenvolver ninguno");return map->at("valor");}
static std::atomic<long long>cfv_next_task{1};static std::mutex cfv_task_mutex;static std::map<long long,std::shared_future<Value>>cfv_tasks;
static Value cfv_tarea(std::function<Value()>job){auto id=cfv_next_task.fetch_add(1);auto future=std::async(std::launch::async,std::move(job)).share();{std::lock_guard<std::mutex>lock(cfv_task_mutex);cfv_tasks.emplace(id,std::move(future));}return (double)id;}
static std::shared_future<Value>cfv_task_future(const Value&handle){auto id=(long long)numero(handle);std::lock_guard<std::mutex>lock(cfv_task_mutex);auto found=cfv_tasks.find(id);if(found==cfv_tasks.end())throw std::runtime_error("tarea desconocida");return found->second;}
static Value cfv_esperar(const Value&handle){return cfv_task_future(handle).get();}static Value cfv_esperar(const Value&handle,const Value&timeout){auto future=cfv_task_future(handle);if(future.wait_for(std::chrono::milliseconds((long long)numero(timeout)))!=std::future_status::ready)throw std::runtime_error("tiempo de espera agotado");return future.get();}static Value cfv_cancelar(const Value&handle){(void)cfv_task_future(handle);return false;}
struct CfvChannel{size_t capacity=0;bool closed=false;std::deque<Value>values;std::mutex mutex;std::condition_variable readable,writable;};static std::atomic<long long>cfv_next_channel{1};static std::mutex cfv_channel_mutex;static std::map<long long,std::shared_ptr<CfvChannel>>cfv_channels;
static std::shared_ptr<CfvChannel>cfv_channel(const Value&handle){auto id=(long long)numero(handle);std::lock_guard<std::mutex>lock(cfv_channel_mutex);auto found=cfv_channels.find(id);if(found==cfv_channels.end())throw std::runtime_error("canal desconocido");return found->second;}
static Value cfv_canal(const Value&size){auto capacity=(long long)numero(size);if(capacity<0)throw std::runtime_error("capacidad de canal inválida");auto id=cfv_next_channel.fetch_add(1);auto channel=std::make_shared<CfvChannel>();channel->capacity=(size_t)capacity;{std::lock_guard<std::mutex>lock(cfv_channel_mutex);cfv_channels[id]=channel;}return (double)id;}static Value cfv_canal(){return cfv_canal(Value{0.0});}
static Value cfv_enviar(const Value&handle,Value value){auto channel=cfv_channel(handle);std::unique_lock<std::mutex>lock(channel->mutex);channel->writable.wait(lock,[&]{return channel->closed||channel->capacity==0||channel->values.size()<channel->capacity;});if(channel->closed)throw std::runtime_error("canal cerrado");channel->values.push_back(std::move(value));lock.unlock();channel->readable.notify_one();return Value{};}static Value cfv_recibir(const Value&handle){auto channel=cfv_channel(handle);std::unique_lock<std::mutex>lock(channel->mutex);channel->readable.wait(lock,[&]{return channel->closed||!channel->values.empty();});if(channel->values.empty())throw std::runtime_error("canal cerrado y vacío");Value result=std::move(channel->values.front());channel->values.pop_front();lock.unlock();channel->writable.notify_one();return result;}static Value cfv_cerrar_canal(const Value&handle){auto channel=cfv_channel(handle);{std::lock_guard<std::mutex>lock(channel->mutex);channel->closed=true;}channel->readable.notify_all();channel->writable.notify_all();return Value{};}
static Value indice(const Value&v,const Value&k){double n=0;if(auto p=std::get_if<std::string>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=p->size())throw std::runtime_error("índice de texto inválido");return std::string(1,p->at((size_t)n));}if(auto p=std::get_if<Lista>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de lista inválido");return (*p)->at((size_t)n);}if(auto p=std::get_if<Mapa>(&v.data)){if(k.index()!=2)throw std::runtime_error("la clave debe ser texto");auto it=(*p)->find(std::get<std::string>(k.data));if(it==(*p)->end())throw std::runtime_error("clave inexistente");return it->second;}if(auto p=std::get_if<Tupla>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->values.size())throw std::runtime_error("índice de tupla inválido");return (*p)->values.at((size_t)n);}if(auto p=std::get_if<FastArray>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->size())throw std::runtime_error("índice de array_fast inválido");return (*p)->at((size_t)n);}if(auto p=std::get_if<DenseMatrix>(&v.data)){n=numero(k);if(n<0||std::floor(n)!=n||(size_t)n>=(*p)->rows)throw std::runtime_error("fila de matrix inválida");auto row=std::make_shared<std::vector<double>>((*p)->values.begin()+(size_t)n*(*p)->columns,(*p)->values.begin()+((size_t)n+1)*(*p)->columns);return row;}throw std::runtime_error("el valor no admite índices");}
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
            for function in self.program.functions if len(function) > 7 and function[7] is True
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
        indexes = {"nulo": 0, "numero": 1, "texto": 2, "booleano": 3, "lista": 4, "mapa": 5, "tupla": 8, "conjunto": 9, "cualquiera": 99}
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
        class_name, name, parameters = method[1], method[2], method[3]
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
            type_indexes = {"nulo": 0, "numero": 1, "texto": 2, "booleano": 3, "lista": 4, "mapa": 5, "tupla": 8, "conjunto": 9, "cualquiera": 99}
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
                        expected = {"nulo":0,"numero":1,"texto":2,"booleano":3,"lista":4,"mapa":5,"tupla":8,"conjunto":9,"cualquiera":99}.get(field_type, 5)
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
        if kind in {"region", "unsafe"}:
            label = "región léxica" if kind == "region" else "frontera unsafe explícita"
            return f"{pad}{{ // {label}\n{self.statements(statement[1], indent + 2)}{pad}}}\n"
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
        if kind in {"structure", "class", "interface", "universal_import"}:
            return ""
        if kind == "import":
            raise CForgevError("La importación no fue resuelta antes de generar código")
        raise CForgevError(f"Instrucción no compilable: {kind}")

    def expr(self, expression: Expr) -> str:
        kind = expression[0]
        if kind == "number": return f"Value{{{expression[1]}.0}}" if "." not in expression[1] else f"Value{{{expression[1]}}}"
        if kind == "string": return f"Value{{{self.cpp_string(expression[1])}}}"
        if kind == "bool": return "Value{true}" if expression[1] else "Value{false}"
        if kind == "null": return "Value{}"
        if kind == "variable": return safe(expression[1])
        if kind == "list": return "crear_lista({" + ", ".join(self.expr(item) for item in expression[1]) + "})"
        if kind == "tuple": return "crear_tupla({" + ", ".join(self.expr(item) for item in expression[1]) + "})"
        if kind == "set": return "crear_conjunto({" + ", ".join(self.expr(item) for item in expression[1]) + "})"
        if kind == "map":
            pairs = []
            for key, value in expression[1]:
                if key[0] != "string": raise CForgevError("Las claves de mapa deben ser textos")
                pairs.append("{" + self.cpp_string(key[1]) + ", " + self.expr(value) + "}")
            return "crear_mapa({" + ", ".join(pairs) + "})"
        if kind == "index": return f"indice({self.expr(expression[1])}, {self.expr(expression[2])})"
        if kind == "field":
            if expression[2] == "length":
                return f"cfv_compat_length({self.expr(expression[1])})"
            return f'indice({self.expr(expression[1])}, Value{{std::string("{expression[2]}")}})'
        if kind == "method_call":
            receiver = expression[1]
            if expression[2] in {"append", "push"}:
                if len(expression[3]) != 1:
                    raise CForgevError(f"{expression[2]} requiere exactamente un elemento")
                return f"cfv_compat_append({self.expr(receiver)}, {self.expr(expression[3][0])})"
            if expression[2] in {"length", "len"}:
                if expression[3]:
                    raise CForgevError(f"{expression[2]} no recibe argumentos")
                return f"cfv_compat_length({self.expr(receiver)})"
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
            declared_function = next(
                (item for item in self.program.functions if item[1] == call_name), None
            )
            if declared_function is not None and len(declared_function) > 6 and declared_function[6]:
                invocation = f"{safe(call_name)}(" + ", ".join(
                    self.expr(argument) for argument in expression[2]
                ) + ")"
                return f"cfv_tarea([=](){{return {invocation};}})"
            connector = next(
                ((prefix, engine) for prefix, engine in (
                    ("ia_", "python"), ("ui_", "java"), ("web_", "javascript")
                ) if call_name.startswith(prefix)),
                None,
            )
            if connector is not None:
                arguments = "crear_lista({" + ", ".join(
                    self.expr(argument) for argument in expression[2]
                ) + "})"
                return (
                    f'cfv_catalog_dispatch("{connector[1]}", "{call_name}", '
                    f'{arguments})'
                )
            if call_name == "forge_catalogo":
                if expression[2]:
                    raise CForgevError("forge_catalogo no recibe argumentos")
                return "cfv_catalogo()"
            if call_name == "forge_arena_estado":
                if expression[2]:
                    raise CForgevError("forge_arena_estado no recibe argumentos")
                return "cfv_arena_estado()"
            if call_name == "jit_estado":
                return "cfv_jit_estado(" + ", ".join(self.expr(arg) for arg in expression[2]) + ")"
            if call_name == "jit_caliente":
                return "cfv_jit_caliente(" + ", ".join(self.expr(arg) for arg in expression[2]) + ")"
            if call_name == "cluster_estado":
                return "cfv_cluster_estado()"
            if call_name == "paralelo" and len(expression[2]) == 2 and expression[2][0][0] == "string":
                function_name = json.loads(expression[2][0][1])
                return f'cfv_parallel_unary([](Value cfv_job){{return {safe(function_name)}(cfv_job);}}, {self.expr(expression[2][1])})'
            if call_name == "tarea":
                if len(expression[2]) not in {1, 2} or expression[2][0][0] != "string":
                    raise CForgevError("tarea requiere el nombre literal de una función y argumentos opcionales")
                function_name = json.loads(expression[2][0][1])
                function = next((item for item in self.program.functions if item[1] == function_name), None)
                if function is None: raise CForgevError(f"tarea no conoce la función '{function_name}'")
                provided = expression[2][1][1] if len(expression[2]) == 2 and expression[2][1][0] == "list" else []
                if len(expression[2]) == 2 and expression[2][1][0] != "list":
                    raise CForgevError("tarea requiere una lista de argumentos")
                if len(provided) != len(function[2]):
                    raise CForgevError("tarea recibió una cantidad incorrecta de argumentos")
                invocation = f"{safe(function_name)}(" + ", ".join(self.expr(arg) for arg in provided) + ")"
                return f"cfv_tarea([=](){{return {invocation};}})"
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
        if kind == "await":
            return f"cfv_esperar({self.expr(expression[1])})"
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

    @staticmethod
    def cpp_string(token: str) -> str:
        value = json.loads(token)
        literal = json.dumps(value, ensure_ascii=False)
        return f"std::string({literal}, {len(value.encode('utf-8'))})"


def compile_native(
    source_path: Path,
    output_path: Path,
    extra_sources: list[Path] | None = None,
    allow_extern: bool = False,
) -> Path:
    try:
        source = source_path.read_text(encoding="utf-8")
    except OSError as error:
        raise CForgevError(f"No se pudo abrir {source_path}: {error}") from error
    program = resolve_imports(Parser(tokenize(source)).program(), source_path.resolve().parent, set())
    if _ast_contains(program, lambda node: node[0] == "extern") and not allow_extern:
        raise CForgevError(
            "el programa contiene bloques extern; vuelve a compilar con "
            "--allow-extern únicamente si confías en todo su código extranjero"
        )
    StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
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


def _ast_contains(node: object, predicate) -> bool:
    """Recorre el AST estructuralmente sin depender de su representación textual."""
    if isinstance(node, Program):
        return _ast_contains(node.functions, predicate) or _ast_contains(node.statements, predicate)
    if isinstance(node, tuple):
        if node and isinstance(node[0], str) and predicate(node):
            return True
        return any(_ast_contains(child, predicate) for child in node)
    if isinstance(node, list):
        return any(_ast_contains(child, predicate) for child in node)
    return False


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
    pattern = re.compile(r'cfv_register_function(?:_v2)?\s*\(\s*"([^"]+)"')
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
    locations = dict(program.locations)
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
        locations.update(imported.locations)
    return Program(functions, statements, locations)
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
        {R"CFV6DATA(compilador_llvm.py)CFV6DATA", R"CFV7DATA("""Backend LLVM IR real de C-Forge para el núcleo numérico tipado.

Emite LLVM IR textual verificable por Clang. Rechaza explícitamente cualquier
construcción no soportada para impedir que un backend parcial parezca completo.
"""

from __future__ import annotations

import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

from cforgev import CForgevError, tokenize
from compilador_nativo import Parser, Program, StaticTypeAnalyzer, resolve_imports


@dataclass(frozen=True)
class IRValue:
    ref: str
    type: str
    semantic: str | None = None


class LLVMGenerator:
    def __init__(self) -> None:
        self.lines: list[str] = []
        self.counter = 0
        self.block = 0
        self.variables: dict[str, tuple[str, str, str | None]] = {}
        self.globals: list[str] = []
        self.string_counter = 0
        self.signatures: dict[str, tuple[list[str], str]] = {}
        self.structures: dict[str, list[tuple[str, str]]] = {}
        self.classes: dict[str, tuple[list[tuple[str, str]], list[tuple]]] = {}
        self.generic_functions: dict[str, tuple] = {}
        self.specializations: dict[tuple[str, tuple[str, ...]], str] = {}
        self.pending_specializations: list[tuple[tuple, dict[str, str], str]] = []
        self.return_type = "double"
        self.exception_targets: list[tuple[str, str]] = []
        self.ffi_functions: dict[str, tuple[list[str], str, bool]] = {}

    def llvm_type(self, name: str) -> str:
        if name in self.structures or name in self.classes: return "ptr"
        if name.startswith(("lista<", "mapa:", "mapa<", "tupla<", "conjunto<", "opcion")): return "ptr"
        return {"numero": "double", "booleano": "i1", "texto": "ptr", "lista": "ptr",
                "tupla": "ptr", "conjunto": "ptr"}.get(name, "double")

    @staticmethod
    def map_value_semantic(semantic: str | None) -> str | None:
        if semantic and semantic.startswith("mapa:"):
            return semantic.split(":", 1)[1]
        if semantic and semantic.startswith("mapa<") and semantic.endswith(">"):
            return semantic[5:-1]
        return None

    @staticmethod
    def is_list_semantic(semantic: str | None) -> bool:
        return semantic == "lista" or bool(semantic and semantic.startswith("lista<"))

    def fields(self, name: str) -> list[tuple[str, str]]:
        if name in self.structures: return self.structures[name]
        if name in self.classes: return self.classes[name][0]
        raise CForgevError(f"LLVM: tipo nominal desconocido '{name}'")

    def ffi_call_argument(self, declared: str, value: IRValue) -> str:
        """Baja un valor interno al contrato C ABI sin transferir propiedad."""
        if declared == "lista<numero>":
            length = self.temporary()
            data_pointer = self.temporary()
            data = self.temporary()
            view = self.temporary()
            view_data = self.temporary()
            view_length = self.temporary()
            self.emit(f"{length} = call i64 @cfv_list_length(ptr {value.ref})")
            self.emit(f"{data_pointer} = getelementptr %CfvList, ptr {value.ref}, i32 0, i32 2")
            self.emit(f"{data} = load ptr, ptr {data_pointer}")
            self.emit(f"{view} = alloca %CfvNumberSlice")
            self.emit(f"{view_data} = getelementptr %CfvNumberSlice, ptr {view}, i32 0, i32 0")
            self.emit(f"{view_length} = getelementptr %CfvNumberSlice, ptr {view}, i32 0, i32 1")
            self.emit(f"store ptr {data}, ptr {view_data}")
            self.emit(f"store i64 {length}, ptr {view_length}")
            return f"ptr {view}"
        if declared == "mapa<numero>":
            length_slot = self.temporary(); length = self.temporary()
            keys_slot = self.temporary(); keys = self.temporary()
            values_slot = self.temporary(); values = self.temporary()
            view = self.temporary(); view_keys = self.temporary()
            view_values = self.temporary(); view_length = self.temporary()
            self.emit(f"{length_slot} = getelementptr %CfvMap, ptr {value.ref}, i32 0, i32 0")
            self.emit(f"{length} = load i64, ptr {length_slot}")
            self.emit(f"{keys_slot} = getelementptr %CfvMap, ptr {value.ref}, i32 0, i32 1")
            self.emit(f"{keys} = load ptr, ptr {keys_slot}")
            self.emit(f"{values_slot} = getelementptr %CfvMap, ptr {value.ref}, i32 0, i32 2")
            self.emit(f"{values} = load ptr, ptr {values_slot}")
            self.emit(f"{view} = alloca %CfvNumberMapView")
            self.emit(f"{view_keys} = getelementptr %CfvNumberMapView, ptr {view}, i32 0, i32 0")
            self.emit(f"{view_values} = getelementptr %CfvNumberMapView, ptr {view}, i32 0, i32 1")
            self.emit(f"{view_length} = getelementptr %CfvNumberMapView, ptr {view}, i32 0, i32 2")
            self.emit(f"store ptr {keys}, ptr {view_keys}")
            self.emit(f"store ptr {values}, ptr {view_values}")
            self.emit(f"store i64 {length}, ptr {view_length}")
            return f"ptr {view}"
        if declared in self.structures or declared in self.classes:
            fields = self.fields(declared)
            count = len(fields)
            array = self.temporary()
            self.emit(f"{array} = alloca [{count} x %CfvRecordField]")
            for index, (field_name, field_type) in enumerate(fields):
                source_slot = self.temporary(); source_value = self.temporary()
                field = self.temporary(); name_slot = self.temporary(); abi_value = self.temporary()
                self.emit(
                    f"{source_slot} = getelementptr {self.type_symbol(declared)}, "
                    f"ptr {value.ref}, i32 0, i32 {index}"
                )
                llvm_field_type = self.llvm_type(field_type)
                self.emit(f"{source_value} = load {llvm_field_type}, ptr {source_slot}")
                self.emit(
                    f"{field} = getelementptr [{count} x %CfvRecordField], "
                    f"ptr {array}, i64 0, i64 {index}"
                )
                self.emit(f"{name_slot} = getelementptr %CfvRecordField, ptr {field}, i32 0, i32 0")
                field_name_value = self.string(field_name)
                self.emit(f"store ptr {field_name_value.ref}, ptr {name_slot}")
                self.emit(f"{abi_value} = getelementptr %CfvRecordField, ptr {field}, i32 0, i32 1")
                self.emit(f"store %CfvAbiValue zeroinitializer, ptr {abi_value}")
                tag_slot = self.temporary()
                self.emit(f"{tag_slot} = getelementptr %CfvAbiValue, ptr {abi_value}, i32 0, i32 0")
                tag = {"numero": 2, "texto": 3, "booleano": 4}[field_type]
                self.emit(f"store i32 {tag}, ptr {tag_slot}")
                if field_type == "numero":
                    data_slot = self.temporary()
                    self.emit(f"{data_slot} = getelementptr %CfvAbiValue, ptr {abi_value}, i32 0, i32 3")
                    self.emit(f"store double {source_value}, ptr {data_slot}")
                elif field_type == "texto":
                    data_slot = self.temporary()
                    self.emit(f"{data_slot} = getelementptr %CfvAbiValue, ptr {abi_value}, i32 0, i32 4")
                    self.emit(f"store ptr {source_value}, ptr {data_slot}")
                else:
                    integer = self.temporary(); data_slot = self.temporary()
                    self.emit(f"{integer} = zext i1 {source_value} to i64")
                    self.emit(f"{data_slot} = getelementptr %CfvAbiValue, ptr {abi_value}, i32 0, i32 2")
                    self.emit(f"store i64 {integer}, ptr {data_slot}")
            view = self.temporary(); type_slot = self.temporary()
            fields_slot = self.temporary(); length_slot = self.temporary(); first = self.temporary()
            self.emit(f"{view} = alloca %CfvRecordView")
            self.emit(f"{type_slot} = getelementptr %CfvRecordView, ptr {view}, i32 0, i32 0")
            type_name = self.string(declared)
            self.emit(f"store ptr {type_name.ref}, ptr {type_slot}")
            self.emit(f"{fields_slot} = getelementptr %CfvRecordView, ptr {view}, i32 0, i32 1")
            self.emit(f"{first} = getelementptr [{count} x %CfvRecordField], ptr {array}, i64 0, i64 0")
            self.emit(f"store ptr {first}, ptr {fields_slot}")
            self.emit(f"{length_slot} = getelementptr %CfvRecordView, ptr {view}, i32 0, i32 2")
            self.emit(f"store i64 {count}, ptr {length_slot}")
            return f"ptr {view}"
        return f"{value.type} {value.ref}"

    @staticmethod
    def type_symbol(name: str) -> str:
        return "%Cfv." + name

    def string(self, value: str) -> IRValue:
        encoded = value.encode("utf-8") + b"\0"
        escaped = "".join(chr(byte) if 32 <= byte < 127 and byte not in {34, 92}
                          else f"\\{byte:02X}" for byte in encoded)
        self.string_counter += 1; name = f"@.str.{self.string_counter}"
        self.globals.append(
            f'{name} = private unnamed_addr constant [{len(encoded)} x i8] c"{escaped}"'
        )
        return IRValue(
            f"getelementptr inbounds ([{len(encoded)} x i8], ptr {name}, i64 0, i64 0)",
            "ptr", "texto"
        )

    def temporary(self) -> str:
        self.counter += 1
        return f"%t{self.counter}"

    def label(self, prefix: str) -> str:
        self.block += 1
        return f"{prefix}.{self.block}"

    def emit(self, value: str) -> None:
        self.lines.append("  " + value)

    def checked_failure(self, condition: str, message: str, valid_prefix: str) -> None:
        """Ramifica a un capturador activo o termina con diagnóstico C-Forge."""
        valid = self.label(valid_prefix)
        if self.exception_targets:
            handler, message_slot = self.exception_targets[-1]
            text = self.string(message)
            self.emit(f"store ptr {text.ref}, ptr {message_slot}")
            self.emit(f"br i1 {condition}, label %{handler}, label %{valid}")
        else:
            failed_prefix = (valid_prefix[:-6] + ".error"
                             if valid_prefix.endswith(".valid") else valid_prefix + ".error")
            failed = self.label(failed_prefix)
            self.emit(f"br i1 {condition}, label %{failed}, label %{valid}")
            self.lines.append(f"{failed}:")
            text = self.string("[C-Forge Runtime Exception] " + message)
            ignored = self.temporary()
            self.emit(f"{ignored} = call i32 (ptr, ...) @printf(ptr @.fmt_text, ptr {text.ref})")
            self.emit("call void @abort()")
            self.emit("unreachable")
        self.lines.append(f"{valid}:")

    def checked_failure_value(self, condition: str, message: str, valid_prefix: str) -> None:
        """Versión para un mensaje UTF-8 entregado por una frontera FFI."""
        valid = self.label(valid_prefix)
        if self.exception_targets:
            handler, message_slot = self.exception_targets[-1]
            self.emit(f"store ptr {message}, ptr {message_slot}")
            self.emit(f"br i1 {condition}, label %{handler}, label %{valid}")
        else:
            failed_prefix = (valid_prefix[:-6] + ".error"
                             if valid_prefix.endswith(".valid") else valid_prefix + ".error")
            failed = self.label(failed_prefix)
            self.emit(f"br i1 {condition}, label %{failed}, label %{valid}")
            self.lines.append(f"{failed}:")
            prefix = self.string("[C-Forge Runtime Exception] ")
            combined = self.temporary()
            self.emit(f"{combined} = call ptr @cfv_concat(ptr {prefix.ref}, ptr {message})")
            ignored = self.temporary()
            self.emit(f"{ignored} = call i32 (ptr, ...) @printf(ptr @.fmt_text, ptr {combined})")
            self.emit(f"call void @free(ptr {combined})")
            self.emit("call void @abort()")
            self.emit("unreachable")
        self.lines.append(f"{valid}:")

    def expression(self, node: tuple) -> IRValue:
        kind = node[0]
        if kind == "number": return IRValue(str(float(node[1])), "double", "numero")
        if kind == "bool": return IRValue("1" if node[1] else "0", "i1", "booleano")
        if kind == "string": return self.string(json.loads(node[1]))
        if kind == "variable":
            if node[1] not in self.variables: raise CForgevError(f"LLVM: variable desconocida '{node[1]}'")
            pointer, value_type, semantic = self.variables[node[1]]
            result = self.temporary(); self.emit(f"{result} = load {value_type}, ptr {pointer}")
            return IRValue(result, value_type, semantic)
        if kind == "list":
            values = [self.number(self.expression(item)) for item in node[1]]
            result = self.temporary(); self.emit(f"{result} = call ptr @cfv_list_new(i64 {len(values)})")
            for index, value in enumerate(values):
                self.emit(f"call void @cfv_list_set(ptr {result}, i64 {index}, double {value.ref})")
            return IRValue(result, "ptr", "lista")
        if kind in {"tuple", "set"}:
            values = [self.expression(item) for item in node[1]]
            semantics = [value.semantic for value in values]
            if any(value not in {"numero", "texto", "booleano"} for value in semantics):
                raise CForgevError("LLVM: tuplas y conjuntos admiten escalares tipados")
            if kind == "set" and len(set(semantics)) > 1:
                raise CForgevError("LLVM: un conjunto requiere elementos homogéneos")
            result = self.temporary(); self.emit(
                f"{result} = call ptr @cfv_collection_new(i64 {len(values)})"
            )
            unique = "1" if kind == "set" else "0"
            for value in values:
                if value.semantic == "numero":
                    self.emit(f"call void @cfv_collection_add_number(ptr {result}, double {value.ref}, i1 {unique})")
                elif value.semantic == "texto":
                    self.emit(f"call void @cfv_collection_add_text(ptr {result}, ptr {value.ref}, i1 {unique})")
                else:
                    self.emit(f"call void @cfv_collection_add_bool(ptr {result}, i1 {value.ref}, i1 {unique})")
            semantic = ("tupla<" + ",".join(semantics) + ">") if kind == "tuple" else (
                "conjunto<" + (semantics[0] if semantics else "cualquiera") + ">"
            )
            return IRValue(result, "ptr", semantic)
        if kind == "map":
            entries = [(self.expression(key), self.expression(value)) for key, value in node[1]]
            if any(key.semantic != "texto" for key, _ in entries):
                raise CForgevError("LLVM: los mapas requieren claves de texto")
            semantics = {value.semantic for _, value in entries}
            if not entries:
                value_semantic = "numero"
            elif len(semantics) != 1 or None in semantics:
                raise CForgevError("LLVM: un mapa debe tener valores homogéneos tipados")
            else:
                value_semantic = next(iter(semantics))
            if value_semantic not in {"numero", "texto", "booleano"}:
                raise CForgevError("LLVM: el mapa admite valores numero, texto o booleano")
            value_type = self.llvm_type(value_semantic)
            result = self.temporary()
            self.emit(f"{result} = call ptr @cfv_map_new(i64 {len(entries)}, i64 {8 if value_type in {'double', 'ptr'} else 1})")
            setter = {"numero": "cfv_map_set_number", "texto": "cfv_map_set_text", "booleano": "cfv_map_set_bool"}[value_semantic]
            for index, (key, value) in enumerate(entries):
                self.emit(f"call void @{setter}(ptr {result}, i64 {index}, ptr {key.ref}, {value_type} {value.ref})")
            return IRValue(result, "ptr", f"mapa<{value_semantic}>")
        if kind == "index":
            owner = self.expression(node[1]); index = self.expression(node[2])
            if self.is_list_semantic(owner.semantic):
                index = self.number(index)
                integer = self.temporary(); self.emit(f"{integer} = fptosi double {index.ref} to i64")
                result = self.temporary(); self.emit(f"{result} = call double @cfv_list_get(ptr {owner.ref}, i64 {integer})")
                return IRValue(result, "double", "numero")
            map_semantic = self.map_value_semantic(owner.semantic)
            if map_semantic:
                if index.semantic != "texto": raise CForgevError("LLVM: la clave del mapa debe ser texto")
                value_type = self.llvm_type(map_semantic)
                getter = {"numero": "cfv_map_get_number", "texto": "cfv_map_get_text", "booleano": "cfv_map_get_bool"}[map_semantic]
                result = self.temporary(); self.emit(
                    f"{result} = call {value_type} @{getter}(ptr {owner.ref}, ptr {index.ref})"
                )
                return IRValue(result, value_type, map_semantic)
            if owner.semantic and owner.semantic.startswith("tupla<") and owner.semantic.endswith(">"):
                if node[2][0] != "number" or "." in node[2][1]:
                    raise CForgevError("LLVM: el índice de tupla debe ser constante entero")
                elements = owner.semantic[6:-1].split(",") if owner.semantic[6:-1] else []
                position = int(node[2][1])
                if position < 0 or position >= len(elements):
                    raise CForgevError("LLVM: índice de tupla fuera de rango")
                semantic = elements[position]; value_type = self.llvm_type(semantic)
                getter = {"numero": "cfv_collection_get_number", "texto": "cfv_collection_get_text",
                          "booleano": "cfv_collection_get_bool"}[semantic]
                result = self.temporary(); self.emit(
                    f"{result} = call {value_type} @{getter}(ptr {owner.ref}, i64 {position})"
                )
                return IRValue(result, value_type, semantic)
            raise CForgevError("LLVM: el índice requiere una lista o mapa")
        if kind == "field":
            owner = self.expression(node[1])
            if self.is_list_semantic(owner.semantic) and node[2] in {"length", "len"}:
                length = self.temporary(); self.emit(f"{length} = call i64 @cfv_list_length(ptr {owner.ref})")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if self.map_value_semantic(owner.semantic) and node[2] in {"length", "len"}:
                length = self.temporary(); self.emit(f"{length} = call i64 @cfv_map_length(ptr {owner.ref})")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if owner.semantic and owner.semantic.startswith(("tupla<", "conjunto<")) and node[2] in {"length", "len"}:
                length = self.temporary(); self.emit(f"{length} = call i64 @cfv_collection_length(ptr {owner.ref})")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if owner.semantic in self.structures or owner.semantic in self.classes:
                fields = self.fields(owner.semantic); names = [field[0] for field in fields]
                if node[2] not in names: raise CForgevError(f"LLVM: campo desconocido '{node[2]}'")
                index = names.index(node[2]); field_type = fields[index][1]
                pointer = self.temporary(); self.emit(
                    f"{pointer} = getelementptr {self.type_symbol(owner.semantic)}, ptr {owner.ref}, i32 0, i32 {index}"
                )
                result = self.temporary(); llvm_type = self.llvm_type(field_type)
                self.emit(f"{result} = load {llvm_type}, ptr {pointer}")
                return IRValue(result, llvm_type, field_type)
            raise CForgevError(f"LLVM: miembro no admitido '{node[2]}'")
        if kind == "method_call":
            owner = self.expression(node[1])
            if self.is_list_semantic(owner.semantic) and node[2] in {"append", "push", "agregar"} and len(node[3]) == 1:
                value = self.number(self.expression(node[3][0]))
                self.emit(f"call void @cfv_list_append(ptr {owner.ref}, double {value.ref})")
                return owner
            if self.is_list_semantic(owner.semantic) and node[2] in {"length", "len"} and not node[3]:
                length = self.temporary(); self.emit(f"{length} = call i64 @cfv_list_length(ptr {owner.ref})")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if self.map_value_semantic(owner.semantic) and node[2] in {"length", "len"} and not node[3]:
                length = self.temporary(); self.emit(f"{length} = call i64 @cfv_map_length(ptr {owner.ref})")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if owner.semantic and owner.semantic.startswith(("tupla<", "conjunto<")) and node[2] in {"length", "len"} and not node[3]:
                length = self.temporary(); self.emit(f"{length} = call i64 @cfv_collection_length(ptr {owner.ref})")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if owner.semantic in self.classes:
                methods = {method[2]: method for method in self.classes[owner.semantic][1]}
                method = methods.get(node[2])
                if method is None: raise CForgevError(f"LLVM: método desconocido '{node[2]}'")
                arguments = [self.expression(argument) for argument in node[3]]
                expected = list(method[5]) if len(method) > 5 else ["cualquiera"] * len(method[3])
                if len(arguments) != len(expected): raise CForgevError("LLVM: cantidad incorrecta de argumentos")
                for value, wanted in zip(arguments, expected):
                    if wanted != "cualquiera" and value.type != self.llvm_type(wanted):
                        raise CForgevError(f"LLVM: argumento incompatible para método '{node[2]}'")
                returned = method[6] if len(method) > 6 else "cualquiera"
                result_type = self.llvm_type(returned); result = self.temporary()
                params = [f"ptr {owner.ref}", *(f"{value.type} {value.ref}" for value in arguments)]
                self.emit(f"{result} = call {result_type} @{owner.semantic}_{node[2]}(" + ", ".join(params) + ")")
                return IRValue(result, result_type, returned)
            raise CForgevError(f"LLVM: método no admitido '{node[2]}'")
        if kind == "unary":
            value = self.expression(node[2])
            if node[1] == "no":
                condition = self.condition(value); result = self.temporary()
                self.emit(f"{result} = xor i1 {condition.ref}, true"); return IRValue(result, "i1")
            number = self.number(value); result = self.temporary()
            self.emit(f"{result} = fneg double {number.ref}"); return IRValue(result, "double")
        if kind == "binary":
            op = node[1]
            left, right = self.expression(node[2]), self.expression(node[3])
            if op in {"+", "-", "*", "/"}:
                if op == "+" and left.semantic == right.semantic == "texto":
                    result = self.temporary(); self.emit(
                        f"{result} = call ptr @cfv_concat(ptr {left.ref}, ptr {right.ref})"
                    ); return IRValue(result, "ptr")
                left, right = self.number(left), self.number(right)
                if op == "/":
                    zero = self.temporary()
                    self.emit(f"{zero} = fcmp oeq double {right.ref}, 0.0")
                    self.checked_failure(zero, "no se puede dividir por cero", "division.valid")
                result = self.temporary(); opcode = {"+": "fadd", "-": "fsub", "*": "fmul", "/": "fdiv"}[op]
                self.emit(f"{result} = {opcode} double {left.ref}, {right.ref}"); return IRValue(result, "double")
            if op in {">", ">=", "<", "<=", "==", "!="}:
                if left.semantic == right.semantic == "texto":
                    if op not in {"==", "!="}: raise CForgevError("LLVM: textos solo admiten == y !=")
                    compared = self.temporary(); self.emit(
                        f"{compared} = call i32 @strcmp(ptr {left.ref}, ptr {right.ref})"
                    )
                    result = self.temporary(); predicate = "eq" if op == "==" else "ne"
                    self.emit(f"{result} = icmp {predicate} i32 {compared}, 0")
                    return IRValue(result, "i1")
                left, right = self.number(left), self.number(right)
                predicate = {">": "ogt", ">=": "oge", "<": "olt", "<=": "ole", "==": "oeq", "!=": "one"}[op]
                result = self.temporary(); self.emit(f"{result} = fcmp {predicate} double {left.ref}, {right.ref}")
                return IRValue(result, "i1")
            if op in {"y", "o"}:
                left, right = self.condition(left), self.condition(right)
                result = self.temporary(); self.emit(f"{result} = {'and' if op == 'y' else 'or'} i1 {left.ref}, {right.ref}")
                return IRValue(result, "i1")
        if kind == "call":
            if node[1] in {"mover", "prestar", "prestar_mut"} and len(node[2]) == 1:
                return self.expression(node[2][0])
            if node[1] in {"soltar_prestamo", "destruir"} and len(node[2]) == 1:
                value = self.expression(node[2][0])
                if node[1] == "destruir" and self.is_list_semantic(value.semantic):
                    self.emit(f"call void @cfv_list_free(ptr {value.ref})")
                elif node[1] == "destruir" and self.map_value_semantic(value.semantic):
                    self.emit(f"call void @cfv_map_free(ptr {value.ref})")
                elif node[1] == "destruir" and value.semantic and value.semantic.startswith(("tupla<", "conjunto<")):
                    self.emit(f"call void @cfv_collection_free(ptr {value.ref})")
                elif node[1] == "destruir" and value.semantic and value.semantic.startswith("opcion"):
                    self.emit(f"call void @cfv_option_free(ptr {value.ref})")
                elif node[1] == "destruir" and (value.semantic in self.structures or value.semantic in self.classes):
                    self.emit(f"call void @cfv_drop_{value.semantic}(ptr {value.ref})")
                return IRValue("0.0", "double")
            if node[1] == "algunos":
                if len(node[2]) != 1: raise CForgevError("LLVM: algunos requiere un argumento")
                value = self.expression(node[2][0])
                tags = {"numero": 1, "texto": 2, "booleano": 3}
                if value.semantic not in tags:
                    raise CForgevError("LLVM: opcion admite escalares numero, texto o booleano")
                result = self.temporary(); self.emit(f"{result} = call ptr @cfv_option_new(i8 {tags[value.semantic]})")
                setter = {"numero": "cfv_option_set_number", "texto": "cfv_option_set_text",
                          "booleano": "cfv_option_set_bool"}[value.semantic]
                self.emit(f"call void @{setter}(ptr {result}, {value.type} {value.ref})")
                return IRValue(result, "ptr", f"opcion<{value.semantic}>")
            if node[1] == "ninguno":
                if node[2]: raise CForgevError("LLVM: ninguno no recibe argumentos")
                result = self.temporary(); self.emit(f"{result} = call ptr @cfv_option_new(i8 0)")
                return IRValue(result, "ptr", "opcion<cualquiera>")
            if node[1] == "es_algunos":
                if len(node[2]) != 1: raise CForgevError("LLVM: es_algunos requiere una opcion")
                value = self.expression(node[2][0])
                if not value.semantic or not value.semantic.startswith("opcion"):
                    raise CForgevError("LLVM: es_algunos requiere una opcion")
                result = self.temporary(); self.emit(f"{result} = call i1 @cfv_option_has_value(ptr {value.ref})")
                return IRValue(result, "i1", "booleano")
            if node[1] == "desenvolver":
                if len(node[2]) != 1: raise CForgevError("LLVM: desenvolver requiere una opcion")
                value = self.expression(node[2][0])
                if not value.semantic or not value.semantic.startswith("opcion<"):
                    raise CForgevError("LLVM: desenvolver requiere una opcion tipada")
                contained = value.semantic[7:-1]
                if contained not in {"numero", "texto", "booleano"}:
                    raise CForgevError("LLVM: no se puede desenvolver una opcion sin tipo escalar concreto")
                has_value = self.temporary(); self.emit(
                    f"{has_value} = call i1 @cfv_option_has_value(ptr {value.ref})"
                )
                missing = self.temporary(); self.emit(f"{missing} = xor i1 {has_value}, true")
                self.checked_failure(missing, "no se puede desenvolver ninguno", "option.valid")
                getter = {"numero": "cfv_option_get_number", "texto": "cfv_option_get_text",
                          "booleano": "cfv_option_get_bool"}[contained]
                result_type = self.llvm_type(contained); result = self.temporary()
                self.emit(f"{result} = call {result_type} @{getter}(ptr {value.ref})")
                return IRValue(result, result_type, contained)
            if node[1] in {"longitud", "len"} and len(node[2]) == 1:
                owner = self.expression(node[2][0])
                if self.is_list_semantic(owner.semantic):
                    length = self.temporary(); self.emit(f"{length} = call i64 @cfv_list_length(ptr {owner.ref})")
                elif self.map_value_semantic(owner.semantic):
                    length = self.temporary(); self.emit(f"{length} = call i64 @cfv_map_length(ptr {owner.ref})")
                elif owner.semantic and owner.semantic.startswith(("tupla<", "conjunto<")):
                    length = self.temporary(); self.emit(f"{length} = call i64 @cfv_collection_length(ptr {owner.ref})")
                else: raise CForgevError("LLVM: longitud requiere lista o mapa")
                result = self.temporary(); self.emit(f"{result} = uitofp i64 {length} to double")
                return IRValue(result, "double", "numero")
            if node[1] in self.structures or node[1] in self.classes:
                fields = self.fields(node[1]); arguments = [self.expression(argument) for argument in node[2]]
                if len(arguments) != len(fields): raise CForgevError(f"LLVM: '{node[1]}' requiere {len(fields)} campos")
                result = self.temporary(); size_ptr = self.temporary(); size = self.temporary()
                symbol = self.type_symbol(node[1])
                self.emit(f"{size_ptr} = getelementptr {symbol}, ptr null, i32 1")
                self.emit(f"{size} = ptrtoint ptr {size_ptr} to i64")
                self.emit(f"{result} = call ptr @malloc(i64 {size})")
                for index, ((_, field_type), value) in enumerate(zip(fields, arguments)):
                    expected = self.llvm_type(field_type)
                    if value.type != expected: raise CForgevError(f"LLVM: campo {index + 1} incompatible para '{node[1]}'")
                    slot = self.temporary(); self.emit(f"{slot} = getelementptr {symbol}, ptr {result}, i32 0, i32 {index}")
                    self.emit(f"store {expected} {value.ref}, ptr {slot}")
                return IRValue(result, "ptr", node[1])
            if node[1] in self.ffi_functions and self.ffi_functions[node[1]][2]:
                expected, returned, _ = self.ffi_functions[node[1]]
                arguments = [self.expression(argument) for argument in node[2]]
                llvm_expected = [self.llvm_type(value) for value in expected]
                if len(arguments) != len(llvm_expected):
                    raise CForgevError(f"LLVM FFI: cantidad incorrecta para '{node[1]}'")
                if any(value.type != wanted for value, wanted in zip(arguments, llvm_expected)):
                    raise CForgevError(f"LLVM FFI: argumentos incompatibles para '{node[1]}'")
                result_type = self.llvm_type(returned)
                output = self.temporary(); error = self.temporary()
                if returned == "lista<numero>":
                    self.emit(f"{output} = alloca %CfvOwnedNumberList")
                    self.emit(f"store %CfvOwnedNumberList zeroinitializer, ptr {output}")
                elif returned == "texto":
                    self.emit(f"{output} = alloca %CfvOwnedText")
                    self.emit(f"store %CfvOwnedText zeroinitializer, ptr {output}")
                else:
                    self.emit(f"{output} = alloca {result_type}")
                self.emit(f"{error} = alloca ptr")
                self.emit(f"store ptr null, ptr {error}")
                status = self.temporary()
                call_arguments = [*(self.ffi_call_argument(declared, value)
                                    for declared, value in zip(expected, arguments)),
                                  f"ptr {output}", f"ptr {error}"]
                self.emit(f"{status} = call i32 @{node[1]}(" + ", ".join(call_arguments) + ")")
                failed = self.temporary(); self.emit(f"{failed} = icmp ne i32 {status}, 0")
                raw_message = self.temporary(); self.emit(f"{raw_message} = load ptr, ptr {error}")
                missing_message = self.temporary(); self.emit(
                    f"{missing_message} = icmp eq ptr {raw_message}, null"
                )
                fallback = self.string(f"{node[1]} falló sin mensaje")
                message = self.temporary(); self.emit(
                    f"{message} = select i1 {missing_message}, ptr {fallback.ref}, ptr {raw_message}"
                )
                self.checked_failure_value(failed, message, "ffi.valid")
                result = self.temporary()
                if returned == "lista<numero>":
                    data_slot = self.temporary(); data = self.temporary()
                    length_slot = self.temporary(); length = self.temporary()
                    missing = self.temporary(); nonempty = self.temporary(); invalid = self.temporary()
                    self.emit(f"{data_slot} = getelementptr %CfvOwnedNumberList, ptr {output}, i32 0, i32 0")
                    self.emit(f"{data} = load ptr, ptr {data_slot}")
                    self.emit(f"{length_slot} = getelementptr %CfvOwnedNumberList, ptr {output}, i32 0, i32 1")
                    self.emit(f"{length} = load i64, ptr {length_slot}")
                    self.emit(f"{missing} = icmp eq ptr {data}, null")
                    self.emit(f"{nonempty} = icmp ne i64 {length}, 0")
                    self.emit(f"{invalid} = and i1 {missing}, {nonempty}")
                    invalid_message = self.string(f"{node[1]} devolvió una lista ABI inválida")
                    self.emit(f"call void @cfv_ffi_release_if(i1 {invalid}, ptr {output})")
                    self.checked_failure_value(invalid, invalid_message.ref, "ffi.list.valid")
                    self.emit(f"{result} = call ptr @cfv_list_from_ffi(ptr {output})")
                elif returned == "texto":
                    data_slot = self.temporary(); data = self.temporary()
                    length_slot = self.temporary(); length = self.temporary(); invalid = self.temporary()
                    self.emit(f"{data_slot} = getelementptr %CfvOwnedText, ptr {output}, i32 0, i32 0")
                    self.emit(f"{data} = load ptr, ptr {data_slot}")
                    self.emit(f"{length_slot} = getelementptr %CfvOwnedText, ptr {output}, i32 0, i32 1")
                    self.emit(f"{length} = load i64, ptr {length_slot}")
                    self.emit(f"{invalid} = call i1 @cfv_ffi_text_invalid(ptr {data}, i64 {length})")
                    invalid_message = self.string(f"{node[1]} devolvió texto UTF-8 ABI inválido")
                    self.emit(f"call void @cfv_ffi_release_if(i1 {invalid}, ptr {output})")
                    self.checked_failure_value(invalid, invalid_message.ref, "ffi.text.valid")
                    self.emit(f"{result} = call ptr @cfv_text_from_ffi(ptr {output})")
                else:
                    self.emit(f"{result} = load {result_type}, ptr {output}")
                return IRValue(result, result_type, returned)
            if node[1] in self.ffi_functions:
                expected, returned, _ = self.ffi_functions[node[1]]
                arguments = [self.expression(argument) for argument in node[2]]
                llvm_expected = [self.llvm_type(value) for value in expected]
                if len(arguments) != len(llvm_expected):
                    raise CForgevError(f"LLVM FFI: cantidad incorrecta para '{node[1]}'")
                if any(value.type != wanted for value, wanted in zip(arguments, llvm_expected)):
                    raise CForgevError(f"LLVM FFI: argumentos incompatibles para '{node[1]}'")
                result_type = self.llvm_type(returned)
                result = self.temporary()
                call_arguments = [self.ffi_call_argument(declared, value)
                                  for declared, value in zip(expected, arguments)]
                self.emit(f"{result} = call {result_type} @{node[1]}(" + ", ".join(call_arguments) + ")")
                return IRValue(result, result_type, returned)
            if node[1] in self.generic_functions:
                function = self.generic_functions[node[1]]
                arguments = [self.expression(argument) for argument in node[2]]
                expected = list(function[4]); parameters = list(function[8])
                if len(arguments) != len(expected): raise CForgevError("LLVM: cantidad incorrecta de argumentos genéricos")
                substitutions: dict[str, str] = {}
                for wanted, value in zip(expected, arguments):
                    actual = value.semantic or {"double": "numero", "i1": "booleano", "ptr": "texto"}.get(value.type)
                    if wanted in parameters:
                        previous = substitutions.setdefault(wanted, actual or "cualquiera")
                        if actual and previous != actual: raise CForgevError(f"LLVM: sustitución contradictoria para '{wanted}'")
                    elif value.type != self.llvm_type(wanted):
                        raise CForgevError(f"LLVM: argumento genérico requiere {wanted}")
                if any(parameter not in substitutions or substitutions[parameter] == "cualquiera" for parameter in parameters):
                    raise CForgevError("LLVM: no se pudieron inferir todos los parámetros genéricos")
                key = (node[1], tuple(substitutions[parameter] for parameter in parameters))
                symbol = self.specializations.get(key)
                if symbol is None:
                    suffix = "__".join(substitutions[parameter] for parameter in parameters)
                    symbol = f"{node[1]}__{suffix}"; self.specializations[key] = symbol
                    self.pending_specializations.append((function, substitutions, symbol))
                returned = substitutions.get(function[5], function[5]); result_type = self.llvm_type(returned)
                result = self.temporary(); self.emit(f"{result} = call {result_type} @{symbol}(" + ", ".join(
                    f"{value.type} {value.ref}" for value in arguments) + ")")
                return IRValue(result, result_type, returned)
            if node[1] not in self.signatures:
                raise CForgevError(f"LLVM: función desconocida '{node[1]}'")
            expected, returned = self.signatures[node[1]]
            arguments = [self.expression(argument) for argument in node[2]]
            llvm_expected = [self.llvm_type(value) for value in expected]
            if len(arguments) != len(llvm_expected): raise CForgevError("LLVM: cantidad incorrecta de argumentos")
            if any(value.type != wanted for value, wanted in zip(arguments, llvm_expected)):
                raise CForgevError(f"LLVM: argumentos incompatibles para '{node[1]}'")
            result = self.temporary()
            result_type = self.llvm_type(returned)
            self.emit(f"{result} = call {result_type} @{node[1]}(" + ", ".join(f"{value.type} {value.ref}" for value in arguments) + ")")
            return IRValue(result, result_type, returned)
        raise CForgevError(f"LLVM 1.0 todavía no admite la expresión '{kind}'")

    def number(self, value: IRValue) -> IRValue:
        if value.type == "double": return value
        if value.type != "i1": raise CForgevError("LLVM: esta operación requiere un número")
        result = self.temporary(); self.emit(f"{result} = uitofp i1 {value.ref} to double"); return IRValue(result, "double")

    def condition(self, value: IRValue) -> IRValue:
        if value.type == "i1": return value
        if value.type == "ptr":
            result = self.temporary(); self.emit(f"{result} = icmp ne ptr {value.ref}, null")
            return IRValue(result, "i1")
        result = self.temporary(); self.emit(f"{result} = fcmp one double {value.ref}, 0.0"); return IRValue(result, "i1")

    def statements(self, statements: list[tuple]) -> bool:
        terminated = False
        for statement in statements:
            if terminated: break
            kind = statement[0]
            if kind == "let":
                value = self.expression(statement[3]); pointer = f"%v.{statement[1]}.{self.counter}"
                self.emit(f"{pointer} = alloca {value.type}"); self.emit(f"store {value.type} {value.ref}, ptr {pointer}")
                semantic = value.semantic
                if (statement[2] and statement[2].startswith("opcion<")
                        and value.semantic == "opcion<cualquiera>"):
                    semantic = statement[2]
                self.variables[statement[1]] = (pointer, value.type, semantic)
            elif kind == "assign":
                if statement[1] not in self.variables: raise CForgevError(f"LLVM: variable desconocida '{statement[1]}'")
                pointer, expected, semantic = self.variables[statement[1]]; value = self.expression(statement[2])
                if value.type != expected: raise CForgevError(f"LLVM: asignación incompatible para '{statement[1]}'")
                if semantic and value.semantic and semantic != value.semantic:
                    raise CForgevError(f"LLVM: asignación semánticamente incompatible para '{statement[1]}'")
                self.emit(f"store {expected} {value.ref}, ptr {pointer}")
            elif kind == "field_assign":
                owner = self.expression(("variable", statement[1]))
                if owner.semantic not in self.structures and owner.semantic not in self.classes:
                    raise CForgevError("LLVM: asignación de campo requiere un objeto nominal")
                fields = self.fields(owner.semantic); names = [field[0] for field in fields]
                if statement[2] not in names: raise CForgevError(f"LLVM: campo desconocido '{statement[2]}'")
                index = names.index(statement[2]); field_type = fields[index][1]
                value = self.expression(statement[3]); expected = self.llvm_type(field_type)
                if value.type != expected: raise CForgevError(f"LLVM: asignación incompatible para campo '{statement[2]}'")
                slot = self.temporary(); self.emit(
                    f"{slot} = getelementptr {self.type_symbol(owner.semantic)}, ptr {owner.ref}, i32 0, i32 {index}"
                ); self.emit(f"store {expected} {value.ref}, ptr {slot}")
            elif kind == "print":
                value = self.expression(statement[1]); result = self.temporary()
                if self.is_list_semantic(value.semantic):
                    self.emit(f"call void @cfv_list_print(ptr {value.ref})")
                elif value.semantic and value.semantic.startswith(("tupla<", "conjunto<")):
                    is_set = "1" if value.semantic.startswith("conjunto<") else "0"
                    self.emit(f"call void @cfv_collection_print(ptr {value.ref}, i1 {is_set})")
                elif value.semantic and value.semantic.startswith("opcion"):
                    self.emit(f"call void @cfv_option_print(ptr {value.ref})")
                elif value.type == "ptr":
                    self.emit(f"{result} = call i32 (ptr, ...) @printf(ptr @.fmt_text, ptr {value.ref})")
                elif value.type == "i1":
                    selected = self.temporary(); self.emit(
                        f"{selected} = select i1 {value.ref}, ptr @.true_text, ptr @.false_text"
                    )
                    self.emit(f"{result} = call i32 (ptr, ...) @printf(ptr @.fmt_text, ptr {selected})")
                else:
                    value = self.number(value)
                    self.emit(f"{result} = call i32 (ptr, ...) @printf(ptr @.fmt_number, double {value.ref})")
            elif kind == "expression": self.expression(statement[1])
            elif kind == "return":
                value = self.expression(statement[1])
                if value.type != self.return_type: raise CForgevError("LLVM: tipo de retorno incompatible")
                self.emit(f"ret {value.type} {value.ref}"); terminated = True
            elif kind == "if":
                condition = self.condition(self.expression(statement[1])); yes, no, end = self.label("if.yes"), self.label("if.no"), self.label("if.end")
                self.emit(f"br i1 {condition.ref}, label %{yes}, label %{no}")
                self.lines.append(f"{yes}:"); yes_term = self.statements(statement[2])
                if not yes_term: self.emit(f"br label %{end}")
                self.lines.append(f"{no}:"); no_term = self.statements(statement[3])
                if not no_term: self.emit(f"br label %{end}")
                if yes_term and no_term: terminated = True
                else: self.lines.append(f"{end}:")
            elif kind == "while":
                test, body, end = self.label("while.test"), self.label("while.body"), self.label("while.end")
                self.emit(f"br label %{test}"); self.lines.append(f"{test}:")
                condition = self.condition(self.expression(statement[1])); self.emit(f"br i1 {condition.ref}, label %{body}, label %{end}")
                self.lines.append(f"{body}:"); body_term = self.statements(statement[2])
                if not body_term: self.emit(f"br label %{test}")
                self.lines.append(f"{end}:")
            elif kind == "try":
                handler, end = self.label("try.catch"), self.label("try.end")
                outer_variables = dict(self.variables)
                message_slot = f"%try.error.{self.counter}.{self.block}"
                self.emit(f"{message_slot} = alloca ptr")
                self.exception_targets.append((handler, message_slot))
                protected_term = self.statements(statement[1])
                self.exception_targets.pop()
                if not protected_term: self.emit(f"br label %{end}")
                self.lines.append(f"{handler}:")
                self.variables = dict(outer_variables)
                self.variables[statement[2]] = (message_slot, "ptr", "texto")
                handler_term = self.statements(statement[3])
                if not handler_term: self.emit(f"br label %{end}")
                self.variables = outer_variables
                if protected_term and handler_term:
                    terminated = True
                else:
                    self.lines.append(f"{end}:")
            elif kind in {"region", "unsafe"}:
                terminated = self.statements(statement[1])
            elif kind in {"structure", "class", "interface", "ffi_function"}:
                continue
            else: raise CForgevError(f"LLVM 1.0 todavía no admite la sentencia '{kind}'")
        return terminated

    def function(self, statement: tuple) -> str:
        self.lines, self.counter, self.block = [], 0, 0
        self.variables = {}
        self.exception_targets = []
        parameters = statement[2]
        parameter_types = statement[4] if len(statement) > 4 else ["numero"] * len(parameters)
        llvm_parameters = [self.llvm_type(value) for value in parameter_types]
        self.return_type = self.llvm_type(statement[5] if len(statement) > 5 else "numero")
        signature = ", ".join(f"{value_type} %arg.{name}" for name, value_type in zip(parameters, llvm_parameters))
        self.lines.append(f"define {self.return_type} @{statement[1]}({signature}) {{")
        for name, value_type in zip(parameters, llvm_parameters):
            pointer = f"%v.{name}.arg"; self.emit(f"{pointer} = alloca {value_type}")
            semantic = parameter_types[parameters.index(name)]
            self.emit(f"store {value_type} %arg.{name}, ptr {pointer}"); self.variables[name] = (pointer, value_type, semantic)
        if not self.statements(statement[3]):
            default = "null" if self.return_type == "ptr" else ("0" if self.return_type == "i1" else "0.0")
            self.emit(f"ret {self.return_type} {default}")
        self.lines.append("}"); return "\n".join(self.lines)

    def method_function(self, class_name: str, method: tuple) -> str:
        parameter_types = list(method[5]) if len(method) > 5 else ["cualquiera"] * len(method[3])
        return_type = method[6] if len(method) > 6 else "cualquiera"
        lowered = ("function", f"{class_name}_{method[2]}", ["este", *method[3]],
                   method[4], [class_name, *parameter_types], return_type, False, False, [])
        return self.function(lowered)

    def specialized_function(self, function: tuple, substitutions: dict[str, str], symbol: str) -> str:
        parameter_types = [substitutions.get(value, value) for value in function[4]]
        return_type = substitutions.get(function[5], function[5])
        lowered = ("function", symbol, function[2], function[3], parameter_types,
                   return_type, False, False, [])
        return self.function(lowered)

    def drop_function(self, name: str, fields: list[tuple[str, str]]) -> str:
        lines = [f"define void @cfv_drop_{name}(ptr %object) {{"]
        for index, (_, field_type) in enumerate(fields):
            destructor = None
            if field_type == "lista": destructor = "cfv_list_free"
            elif field_type == "mapa" or field_type.startswith("mapa<"): destructor = "cfv_map_free"
            elif field_type in {"tupla", "conjunto"} or field_type.startswith(("tupla<", "conjunto<")):
                destructor = "cfv_collection_free"
            elif field_type == "opcion" or field_type.startswith("opcion<"):
                destructor = "cfv_option_free"
            elif field_type in self.structures or field_type in self.classes:
                destructor = f"cfv_drop_{field_type}"
            if destructor:
                slot = f"%drop.slot.{index}"
                value = f"%drop.value.{index}"
                lines.append(
                    f"  {slot} = getelementptr {self.type_symbol(name)}, ptr %object, i32 0, i32 {index}"
                )
                lines.append(f"  {value} = load ptr, ptr {slot}")
                lines.append(f"  call void @{destructor}(ptr {value})")
        lines.extend(["  call void @free(ptr %object)", "  ret void", "}"])
        return "\n".join(lines)

    def generate(self, program: Program) -> str:
        if any(len(function) > 6 and function[6] for function in program.functions):
            raise CForgevError("LLVM 1.0 todavía no admite funciones async; usa VM o backend C++")
        self.structures = {statement[1]: statement[2] for statement in program.statements
                           if statement[0] == "structure"}
        self.classes = {statement[1]: (statement[2], statement[3]) for statement in program.statements
                        if statement[0] == "class"}
        self.generic_functions = {function[1]: function for function in program.functions
                                  if len(function) > 8 and function[8]}
        self.ffi_functions = {
            statement[1]: (list(statement[3]), statement[4], bool(statement[5]))
            for statement in program.statements if statement[0] == "ffi_function"
        }
        allowed_ffi_parameters = {
            "numero", "booleano", "texto", "lista<numero>", "mapa<numero>"
        }
        allowed_ffi_returns = {"numero", "booleano", "texto", "lista<numero>"}
        for name, (parameters, returned, checked) in self.ffi_functions.items():
            unsafe_nominals = [
                value for value in parameters
                if value in self.structures or value in self.classes
                if any(field_type not in {"numero", "texto", "booleano"}
                       for _, field_type in self.fields(value))
            ]
            unsupported = [
                value for value in parameters
                if value not in allowed_ffi_parameters
                and value not in self.structures and value not in self.classes
            ]
            if unsafe_nominals:
                raise CForgevError(
                    f"LLVM FFI: '{name}' no puede prestar {unsafe_nominals[0]} porque contiene "
                    "campos no escalares"
                )
            if returned not in allowed_ffi_returns or unsupported or (
                returned == "lista<numero>" and not checked
            ):
                raise CForgevError(
                    f"LLVM FFI: '{name}' admite escalares, vistas numéricas y objetos escalares; "
                    "los retornos propietarios requieren extern_c segura"
                )
        self.signatures = {
            function[1]: (
                list(function[4]) if len(function) > 4 else ["numero"] * len(function[2]),
                function[5] if len(function) > 5 else "numero",
            ) for function in program.functions if function[1] not in self.generic_functions
        }
        self.signatures.update({
            name: (parameters, returned)
            for name, (parameters, returned, _) in self.ffi_functions.items()
        })
        functions = [self.function(function) for function in program.functions
                     if function[1] not in self.generic_functions]
        functions += [self.method_function(class_name, method)
                      for class_name, (_, methods) in self.classes.items() for method in methods]
        functions += [self.drop_function(name, fields) for name, fields in self.structures.items()]
        functions += [self.drop_function(name, definition[0]) for name, definition in self.classes.items()]
        self.lines, self.counter, self.block, self.variables = ["define i32 @main() {"], 0, 0, {}
        self.exception_targets = []
        terminated = self.statements(program.statements)
        if not terminated: self.emit("ret i32 0")
        self.lines.append("}")
        main_ir = "\n".join(self.lines)
        cursor = 0
        while cursor < len(self.pending_specializations):
            function, substitutions, symbol = self.pending_specializations[cursor]; cursor += 1
            functions.append(self.specialized_function(function, substitutions, symbol))
        nominal_types = "\n".join(
            f"{self.type_symbol(name)} = type {{" + ", ".join(
                self.llvm_type(field_type) for _, field_type in fields
            ) + "}"
            for name, fields in [
                *self.structures.items(),
                *((name, definition[0]) for name, definition in self.classes.items()),
            ]
        )
        prelude = '''; C-Forge LLVM IR 1.4
@.fmt_number = private unnamed_addr constant [7 x i8] c"%.15g\\0A\\00"
@.fmt_text = private unnamed_addr constant [4 x i8] c"%s\\0A\\00"
@.true_text = private unnamed_addr constant [10 x i8] c"verdadero\\00"
@.false_text = private unnamed_addr constant [6 x i8] c"falso\\00"
@.fmt_raw = private unnamed_addr constant [3 x i8] c"%s\\00"
@.fmt_list_number = private unnamed_addr constant [6 x i8] c"%.15g\\00"
@.list_open = private unnamed_addr constant [2 x i8] c"[\\00"
@.list_separator = private unnamed_addr constant [3 x i8] c", \\00"
@.list_close = private unnamed_addr constant [3 x i8] c"]\\0A\\00"
@.tuple_open = private unnamed_addr constant [2 x i8] c"(\\00"
@.set_open = private unnamed_addr constant [10 x i8] c"conjunto(\\00"
@.collection_close = private unnamed_addr constant [3 x i8] c")\\0A\\00"
@.option_open = private unnamed_addr constant [9 x i8] c"algunos(\\00"
@.option_none = private unnamed_addr constant [8 x i8] c"ninguno\\00"
%CfvList = type { i64, i64, ptr }
%CfvNumberSlice = type { ptr, i64 }
%CfvOwnedNumberList = type { ptr, i64, ptr, ptr }
%CfvOwnedText = type { ptr, i64, ptr, ptr }
%CfvNumberMapView = type { ptr, ptr, i64 }
%CfvAbiValue = type { i32, i32, i64, double, ptr, ptr, ptr }
%CfvRecordField = type { ptr, %CfvAbiValue }
%CfvRecordView = type { ptr, ptr, i64 }
%CfvMap = type { i64, ptr, ptr, i64 }
%CfvScalarValue = type { i8, double, ptr }
%CfvScalarCollection = type { i64, i64, ptr }
%CfvOption = type { i8, double, ptr }
declare i32 @printf(ptr, ...)
declare i64 @strlen(ptr)
declare ptr @malloc(i64)
declare ptr @realloc(ptr, i64)
declare void @free(ptr)
declare ptr @memcpy(ptr, ptr, i64)
declare void @abort()
declare ptr @strcpy(ptr, ptr)
declare ptr @strcat(ptr, ptr)
declare i32 @strcmp(ptr, ptr)
define ptr @cfv_concat(ptr %a, ptr %b) {
  %la = call i64 @strlen(ptr %a)
  %lb = call i64 @strlen(ptr %b)
  %sum = add i64 %la, %lb
  %size = add i64 %sum, 1
  %out = call ptr @malloc(i64 %size)
  %copy = call ptr @strcpy(ptr %out, ptr %a)
  %append = call ptr @strcat(ptr %out, ptr %b)
  ret ptr %out
}
define void @cfv_ffi_release_if(i1 %condition, ptr %foreign) {
entry:
  br i1 %condition, label %inspect, label %done
inspect:
  %ownerp = getelementptr %CfvOwnedText, ptr %foreign, i32 0, i32 2
  %releasep = getelementptr %CfvOwnedText, ptr %foreign, i32 0, i32 3
  %owner = load ptr, ptr %ownerp
  %release = load ptr, ptr %releasep
  %hasrelease = icmp ne ptr %release, null
  br i1 %hasrelease, label %release.call, label %done
release.call:
  call void %release(ptr %owner)
  br label %done
done:
  ret void
}
define i1 @cfv_ffi_text_invalid(ptr %data, i64 %len) {
entry:
  %missing = icmp eq ptr %data, null
  br i1 %missing, label %missing.case, label %scan.test
missing.case:
  %nonempty = icmp ne i64 %len, 0
  ret i1 %nonempty
scan.test:
  %i = phi i64 [ 0, %entry ], [ %next, %scan.next ]
  %more = icmp ult i64 %i, %len
  br i1 %more, label %scan.body, label %valid
scan.body:
  %slot = getelementptr i8, ptr %data, i64 %i
  %byte = load i8, ptr %slot
  %nul = icmp eq i8 %byte, 0
  br i1 %nul, label %invalid, label %scan.next
scan.next:
  %next = add i64 %i, 1
  br label %scan.test
invalid:
  ret i1 true
valid:
  ret i1 false
}
define ptr @cfv_text_from_ffi(ptr %foreign) {
entry:
  %datap = getelementptr %CfvOwnedText, ptr %foreign, i32 0, i32 0
  %lenp = getelementptr %CfvOwnedText, ptr %foreign, i32 0, i32 1
  %ownerp = getelementptr %CfvOwnedText, ptr %foreign, i32 0, i32 2
  %releasep = getelementptr %CfvOwnedText, ptr %foreign, i32 0, i32 3
  %data = load ptr, ptr %datap
  %len = load i64, ptr %lenp
  %owner = load ptr, ptr %ownerp
  %release = load ptr, ptr %releasep
  %size = add i64 %len, 1
  %text = call ptr @malloc(i64 %size)
  %nonempty = icmp ne i64 %len, 0
  br i1 %nonempty, label %copy, label %terminate
copy:
  %copied = call ptr @memcpy(ptr %text, ptr %data, i64 %len)
  br label %terminate
terminate:
  %end = getelementptr i8, ptr %text, i64 %len
  store i8 0, ptr %end
  %hasrelease = icmp ne ptr %release, null
  br i1 %hasrelease, label %release.call, label %done
release.call:
  call void %release(ptr %owner)
  br label %done
done:
  ret ptr %text
}
define ptr @cfv_list_new(i64 %count) {
  %object = call ptr @malloc(i64 24)
  %has = icmp ugt i64 %count, 0
  %capacity = select i1 %has, i64 %count, i64 1
  %bytes = mul i64 %capacity, 8
  %data = call ptr @malloc(i64 %bytes)
  %lenp = getelementptr %CfvList, ptr %object, i32 0, i32 0
  %capp = getelementptr %CfvList, ptr %object, i32 0, i32 1
  %datap = getelementptr %CfvList, ptr %object, i32 0, i32 2
  store i64 %count, ptr %lenp
  store i64 %capacity, ptr %capp
  store ptr %data, ptr %datap
  ret ptr %object
}
define i64 @cfv_list_length(ptr %list) {
  %lenp = getelementptr %CfvList, ptr %list, i32 0, i32 0
  %len = load i64, ptr %lenp
  ret i64 %len
}
define void @cfv_list_set(ptr %list, i64 %index, double %value) {
  %len = call i64 @cfv_list_length(ptr %list)
  %ok = icmp ult i64 %index, %len
  br i1 %ok, label %valid, label %invalid
valid:
  %datap = getelementptr %CfvList, ptr %list, i32 0, i32 2
  %data = load ptr, ptr %datap
  %slot = getelementptr double, ptr %data, i64 %index
  store double %value, ptr %slot
  ret void
invalid:
  call void @abort()
  unreachable
}
define double @cfv_list_get(ptr %list, i64 %index) {
  %len = call i64 @cfv_list_length(ptr %list)
  %ok = icmp ult i64 %index, %len
  br i1 %ok, label %valid, label %invalid
valid:
  %datap = getelementptr %CfvList, ptr %list, i32 0, i32 2
  %data = load ptr, ptr %datap
  %slot = getelementptr double, ptr %data, i64 %index
  %value = load double, ptr %slot
  ret double %value
invalid:
  call void @abort()
  unreachable
}
define void @cfv_list_append(ptr %list, double %value) {
  %lenp = getelementptr %CfvList, ptr %list, i32 0, i32 0
  %capp = getelementptr %CfvList, ptr %list, i32 0, i32 1
  %datap = getelementptr %CfvList, ptr %list, i32 0, i32 2
  %len = load i64, ptr %lenp
  %cap = load i64, ptr %capp
  %full = icmp uge i64 %len, %cap
  br i1 %full, label %grow, label %store
grow:
  %next = mul i64 %cap, 2
  %bytes = mul i64 %next, 8
  %old = load ptr, ptr %datap
  %new = call ptr @realloc(ptr %old, i64 %bytes)
  store ptr %new, ptr %datap
  store i64 %next, ptr %capp
  br label %store
store:
  %data = load ptr, ptr %datap
  %slot = getelementptr double, ptr %data, i64 %len
  store double %value, ptr %slot
  %updated = add i64 %len, 1
  store i64 %updated, ptr %lenp
  ret void
}
define ptr @cfv_list_from_ffi(ptr %foreign) {
entry:
  %datap = getelementptr %CfvOwnedNumberList, ptr %foreign, i32 0, i32 0
  %lenp = getelementptr %CfvOwnedNumberList, ptr %foreign, i32 0, i32 1
  %ownerp = getelementptr %CfvOwnedNumberList, ptr %foreign, i32 0, i32 2
  %releasep = getelementptr %CfvOwnedNumberList, ptr %foreign, i32 0, i32 3
  %data = load ptr, ptr %datap
  %len = load i64, ptr %lenp
  %owner = load ptr, ptr %ownerp
  %release = load ptr, ptr %releasep
  %list = call ptr @cfv_list_new(i64 %len)
  br label %copy.test
copy.test:
  %i = phi i64 [ 0, %entry ], [ %next, %copy.body ]
  %more = icmp ult i64 %i, %len
  br i1 %more, label %copy.body, label %release.test
copy.body:
  %slot = getelementptr double, ptr %data, i64 %i
  %value = load double, ptr %slot
  call void @cfv_list_set(ptr %list, i64 %i, double %value)
  %next = add i64 %i, 1
  br label %copy.test
release.test:
  %hasrelease = icmp ne ptr %release, null
  br i1 %hasrelease, label %release.call, label %done
release.call:
  call void %release(ptr %owner)
  br label %done
done:
  ret ptr %list
}
define void @cfv_list_free(ptr %list) {
  %datap = getelementptr %CfvList, ptr %list, i32 0, i32 2
  %data = load ptr, ptr %datap
  call void @free(ptr %data)
  call void @free(ptr %list)
  ret void
}
define void @cfv_list_print(ptr %list) {
entry:
  %open = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.list_open)
  %len = call i64 @cfv_list_length(ptr %list)
  br label %test
test:
  %i = phi i64 [ 0, %entry ], [ %next, %valueblock ]
  %more = icmp ult i64 %i, %len
  br i1 %more, label %body, label %done
body:
  %notfirst = icmp ugt i64 %i, 0
  br i1 %notfirst, label %separator, label %valueblock
separator:
  %sep = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.list_separator)
  br label %valueblock
valueblock:
  %value = call double @cfv_list_get(ptr %list, i64 %i)
  %printed = call i32 (ptr, ...) @printf(ptr @.fmt_list_number, double %value)
  %next = add i64 %i, 1
  br label %test
done:
  %close = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.list_close)
  ret void
}
define ptr @cfv_collection_new(i64 %requested) {
  %object = call ptr @malloc(i64 24)
  %has = icmp ugt i64 %requested, 0
  %capacity = select i1 %has, i64 %requested, i64 1
  %bytes = mul i64 %capacity, 24
  %data = call ptr @malloc(i64 %bytes)
  %lenp = getelementptr %CfvScalarCollection, ptr %object, i32 0, i32 0
  %capp = getelementptr %CfvScalarCollection, ptr %object, i32 0, i32 1
  %datap = getelementptr %CfvScalarCollection, ptr %object, i32 0, i32 2
  store i64 0, ptr %lenp
  store i64 %capacity, ptr %capp
  store ptr %data, ptr %datap
  ret ptr %object
}
define i64 @cfv_collection_length(ptr %collection) {
  %lenp = getelementptr %CfvScalarCollection, ptr %collection, i32 0, i32 0
  %len = load i64, ptr %lenp
  ret i64 %len
}
define i1 @cfv_collection_has(ptr %collection, i8 %wanted_tag, double %wanted_number, ptr %wanted_pointer) {
entry:
  %len = call i64 @cfv_collection_length(ptr %collection)
  %datap = getelementptr %CfvScalarCollection, ptr %collection, i32 0, i32 2
  %data = load ptr, ptr %datap
  br label %test
test:
  %i = phi i64 [ 0, %entry ], [ %next, %continue ]
  %more = icmp ult i64 %i, %len
  br i1 %more, label %body, label %missing
body:
  %slot = getelementptr %CfvScalarValue, ptr %data, i64 %i
  %tagp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 0
  %tag = load i8, ptr %tagp
  %same_tag = icmp eq i8 %tag, %wanted_tag
  br i1 %same_tag, label %compare_kind, label %continue
compare_kind:
  %is_text = icmp eq i8 %tag, 2
  br i1 %is_text, label %compare_text, label %compare_number
compare_text:
  %pointerp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 2
  %pointer = load ptr, ptr %pointerp
  %comparison = call i32 @strcmp(ptr %pointer, ptr %wanted_pointer)
  %text_equal = icmp eq i32 %comparison, 0
  br i1 %text_equal, label %found, label %continue
compare_number:
  %numberp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 1
  %number = load double, ptr %numberp
  %number_equal = fcmp oeq double %number, %wanted_number
  br i1 %number_equal, label %found, label %continue
continue:
  %next = add i64 %i, 1
  br label %test
found:
  ret i1 1
missing:
  ret i1 0
}
define void @cfv_collection_add(ptr %collection, i8 %tag, double %number, ptr %pointer, i1 %unique) {
entry:
  %exists = call i1 @cfv_collection_has(ptr %collection, i8 %tag, double %number, ptr %pointer)
  %skip = and i1 %unique, %exists
  br i1 %skip, label %done, label %store
store:
  %lenp = getelementptr %CfvScalarCollection, ptr %collection, i32 0, i32 0
  %datap = getelementptr %CfvScalarCollection, ptr %collection, i32 0, i32 2
  %len = load i64, ptr %lenp
  %data = load ptr, ptr %datap
  %slot = getelementptr %CfvScalarValue, ptr %data, i64 %len
  %tagp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 0
  %numberp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 1
  %pointerp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 2
  store i8 %tag, ptr %tagp
  store double %number, ptr %numberp
  store ptr %pointer, ptr %pointerp
  %next = add i64 %len, 1
  store i64 %next, ptr %lenp
  br label %done
done:
  ret void
}
define void @cfv_collection_add_number(ptr %collection, double %value, i1 %unique) {
  call void @cfv_collection_add(ptr %collection, i8 1, double %value, ptr null, i1 %unique)
  ret void
}
define void @cfv_collection_add_text(ptr %collection, ptr %value, i1 %unique) {
  call void @cfv_collection_add(ptr %collection, i8 2, double 0.0, ptr %value, i1 %unique)
  ret void
}
define void @cfv_collection_add_bool(ptr %collection, i1 %value, i1 %unique) {
  %number = uitofp i1 %value to double
  call void @cfv_collection_add(ptr %collection, i8 3, double %number, ptr null, i1 %unique)
  ret void
}
define ptr @cfv_collection_slot(ptr %collection, i64 %index) {
  %len = call i64 @cfv_collection_length(ptr %collection)
  %ok = icmp ult i64 %index, %len
  br i1 %ok, label %valid, label %invalid
valid:
  %datap = getelementptr %CfvScalarCollection, ptr %collection, i32 0, i32 2
  %data = load ptr, ptr %datap
  %slot = getelementptr %CfvScalarValue, ptr %data, i64 %index
  ret ptr %slot
invalid:
  call void @abort()
  unreachable
}
define double @cfv_collection_get_number(ptr %collection, i64 %index) {
  %slot = call ptr @cfv_collection_slot(ptr %collection, i64 %index)
  %numberp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 1
  %number = load double, ptr %numberp
  ret double %number
}
define ptr @cfv_collection_get_text(ptr %collection, i64 %index) {
  %slot = call ptr @cfv_collection_slot(ptr %collection, i64 %index)
  %pointerp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 2
  %pointer = load ptr, ptr %pointerp
  ret ptr %pointer
}
define i1 @cfv_collection_get_bool(ptr %collection, i64 %index) {
  %number = call double @cfv_collection_get_number(ptr %collection, i64 %index)
  %value = fcmp one double %number, 0.0
  ret i1 %value
}
define void @cfv_collection_print(ptr %collection, i1 %is_set) {
entry:
  %open_text = select i1 %is_set, ptr @.set_open, ptr @.tuple_open
  %open = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr %open_text)
  %len = call i64 @cfv_collection_length(ptr %collection)
  br label %test
test:
  %i = phi i64 [ 0, %entry ], [ %next, %value_done ]
  %more = icmp ult i64 %i, %len
  br i1 %more, label %body, label %done
body:
  %not_first = icmp ugt i64 %i, 0
  br i1 %not_first, label %separator, label %dispatch
separator:
  %separator_print = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.list_separator)
  br label %dispatch
dispatch:
  %slot = call ptr @cfv_collection_slot(ptr %collection, i64 %i)
  %tagp = getelementptr %CfvScalarValue, ptr %slot, i32 0, i32 0
  %tag = load i8, ptr %tagp
  %is_number = icmp eq i8 %tag, 1
  br i1 %is_number, label %number, label %not_number
not_number:
  %is_text = icmp eq i8 %tag, 2
  br i1 %is_text, label %text, label %boolean
number:
  %number_value = call double @cfv_collection_get_number(ptr %collection, i64 %i)
  %number_print = call i32 (ptr, ...) @printf(ptr @.fmt_list_number, double %number_value)
  br label %value_done
text:
  %text_value = call ptr @cfv_collection_get_text(ptr %collection, i64 %i)
  %text_print = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr %text_value)
  br label %value_done
boolean:
  %bool_value = call i1 @cfv_collection_get_bool(ptr %collection, i64 %i)
  %bool_text = select i1 %bool_value, ptr @.true_text, ptr @.false_text
  %bool_print = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr %bool_text)
  br label %value_done
value_done:
  %next = add i64 %i, 1
  br label %test
done:
  %close = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.collection_close)
  ret void
}
define void @cfv_collection_free(ptr %collection) {
  %datap = getelementptr %CfvScalarCollection, ptr %collection, i32 0, i32 2
  %data = load ptr, ptr %datap
  call void @free(ptr %data)
  call void @free(ptr %collection)
  ret void
}
define ptr @cfv_option_new(i8 %tag) {
  %end = getelementptr %CfvOption, ptr null, i32 1
  %size = ptrtoint ptr %end to i64
  %option = call ptr @malloc(i64 %size)
  %tagp = getelementptr %CfvOption, ptr %option, i32 0, i32 0
  %numberp = getelementptr %CfvOption, ptr %option, i32 0, i32 1
  %pointerp = getelementptr %CfvOption, ptr %option, i32 0, i32 2
  store i8 %tag, ptr %tagp
  store double 0.0, ptr %numberp
  store ptr null, ptr %pointerp
  ret ptr %option
}
define void @cfv_option_set_number(ptr %option, double %value) {
  %slot = getelementptr %CfvOption, ptr %option, i32 0, i32 1
  store double %value, ptr %slot
  ret void
}
define void @cfv_option_set_text(ptr %option, ptr %value) {
  %slot = getelementptr %CfvOption, ptr %option, i32 0, i32 2
  store ptr %value, ptr %slot
  ret void
}
define void @cfv_option_set_bool(ptr %option, i1 %value) {
  %numeric = uitofp i1 %value to double
  %slot = getelementptr %CfvOption, ptr %option, i32 0, i32 1
  store double %numeric, ptr %slot
  ret void
}
define i1 @cfv_option_has_value(ptr %option) {
  %tagp = getelementptr %CfvOption, ptr %option, i32 0, i32 0
  %tag = load i8, ptr %tagp
  %has = icmp ne i8 %tag, 0
  ret i1 %has
}
define double @cfv_option_get_number(ptr %option) {
  %slot = getelementptr %CfvOption, ptr %option, i32 0, i32 1
  %value = load double, ptr %slot
  ret double %value
}
define ptr @cfv_option_get_text(ptr %option) {
  %slot = getelementptr %CfvOption, ptr %option, i32 0, i32 2
  %value = load ptr, ptr %slot
  ret ptr %value
}
define i1 @cfv_option_get_bool(ptr %option) {
  %slot = getelementptr %CfvOption, ptr %option, i32 0, i32 1
  %numeric = load double, ptr %slot
  %value = fcmp one double %numeric, 0.0
  ret i1 %value
}
define void @cfv_option_print(ptr %option) {
  %tagp = getelementptr %CfvOption, ptr %option, i32 0, i32 0
  %tag = load i8, ptr %tagp
  switch i8 %tag, label %none [i8 1, label %number i8 2, label %text i8 3, label %boolean]
none:
  %none_print = call i32 (ptr, ...) @printf(ptr @.fmt_text, ptr @.option_none)
  ret void
number:
  %number_open = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.option_open)
  %number_value = call double @cfv_option_get_number(ptr %option)
  %number_print = call i32 (ptr, ...) @printf(ptr @.fmt_list_number, double %number_value)
  br label %close
text:
  %text_open = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.option_open)
  %text_value = call ptr @cfv_option_get_text(ptr %option)
  %text_print = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr %text_value)
  br label %close
boolean:
  %bool_open = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.option_open)
  %bool_value = call i1 @cfv_option_get_bool(ptr %option)
  %bool_text = select i1 %bool_value, ptr @.true_text, ptr @.false_text
  %bool_print = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr %bool_text)
  br label %close
close:
  %close_print = call i32 (ptr, ...) @printf(ptr @.fmt_raw, ptr @.collection_close)
  ret void
}
define void @cfv_option_free(ptr %option) {
  call void @free(ptr %option)
  ret void
}
define ptr @cfv_map_new(i64 %count, i64 %element_size) {
  %object = call ptr @malloc(i64 32)
  %key_bytes = mul i64 %count, 8
  %value_bytes = mul i64 %count, %element_size
  %keys = call ptr @malloc(i64 %key_bytes)
  %values = call ptr @malloc(i64 %value_bytes)
  %lenp = getelementptr %CfvMap, ptr %object, i32 0, i32 0
  %keysp = getelementptr %CfvMap, ptr %object, i32 0, i32 1
  %valuesp = getelementptr %CfvMap, ptr %object, i32 0, i32 2
  %sizep = getelementptr %CfvMap, ptr %object, i32 0, i32 3
  store i64 %count, ptr %lenp
  store ptr %keys, ptr %keysp
  store ptr %values, ptr %valuesp
  store i64 %element_size, ptr %sizep
  ret ptr %object
}
define i64 @cfv_map_length(ptr %map) {
  %lenp = getelementptr %CfvMap, ptr %map, i32 0, i32 0
  %len = load i64, ptr %lenp
  ret i64 %len
}
define void @cfv_map_set_key(ptr %map, i64 %index, ptr %key) {
  %len = call i64 @cfv_map_length(ptr %map)
  %ok = icmp ult i64 %index, %len
  br i1 %ok, label %valid, label %invalid
valid:
  %keysp = getelementptr %CfvMap, ptr %map, i32 0, i32 1
  %keys = load ptr, ptr %keysp
  %slot = getelementptr ptr, ptr %keys, i64 %index
  store ptr %key, ptr %slot
  ret void
invalid:
  call void @abort()
  unreachable
}
define void @cfv_map_set_number(ptr %map, i64 %index, ptr %key, double %value) {
  call void @cfv_map_set_key(ptr %map, i64 %index, ptr %key)
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %values = load ptr, ptr %valuesp
  %slot = getelementptr double, ptr %values, i64 %index
  store double %value, ptr %slot
  ret void
}
define void @cfv_map_set_text(ptr %map, i64 %index, ptr %key, ptr %value) {
  call void @cfv_map_set_key(ptr %map, i64 %index, ptr %key)
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %values = load ptr, ptr %valuesp
  %slot = getelementptr ptr, ptr %values, i64 %index
  store ptr %value, ptr %slot
  ret void
}
define void @cfv_map_set_bool(ptr %map, i64 %index, ptr %key, i1 %value) {
  call void @cfv_map_set_key(ptr %map, i64 %index, ptr %key)
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %values = load ptr, ptr %valuesp
  %slot = getelementptr i1, ptr %values, i64 %index
  store i1 %value, ptr %slot
  ret void
}
define i64 @cfv_map_find(ptr %map, ptr %key) {
entry:
  %len = call i64 @cfv_map_length(ptr %map)
  %keysp = getelementptr %CfvMap, ptr %map, i32 0, i32 1
  %keys = load ptr, ptr %keysp
  br label %test
test:
  %index = phi i64 [ 0, %entry ], [ %next, %continue ]
  %more = icmp ult i64 %index, %len
  br i1 %more, label %body, label %missing
body:
  %slot = getelementptr ptr, ptr %keys, i64 %index
  %candidate = load ptr, ptr %slot
  %comparison = call i32 @strcmp(ptr %candidate, ptr %key)
  %equal = icmp eq i32 %comparison, 0
  br i1 %equal, label %found, label %continue
continue:
  %next = add i64 %index, 1
  br label %test
found:
  ret i64 %index
missing:
  call void @abort()
  unreachable
}
define double @cfv_map_get_number(ptr %map, ptr %key) {
  %index = call i64 @cfv_map_find(ptr %map, ptr %key)
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %values = load ptr, ptr %valuesp
  %slot = getelementptr double, ptr %values, i64 %index
  %value = load double, ptr %slot
  ret double %value
}
define ptr @cfv_map_get_text(ptr %map, ptr %key) {
  %index = call i64 @cfv_map_find(ptr %map, ptr %key)
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %values = load ptr, ptr %valuesp
  %slot = getelementptr ptr, ptr %values, i64 %index
  %value = load ptr, ptr %slot
  ret ptr %value
}
define i1 @cfv_map_get_bool(ptr %map, ptr %key) {
  %index = call i64 @cfv_map_find(ptr %map, ptr %key)
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %values = load ptr, ptr %valuesp
  %slot = getelementptr i1, ptr %values, i64 %index
  %value = load i1, ptr %slot
  ret i1 %value
}
define void @cfv_map_free(ptr %map) {
  %keysp = getelementptr %CfvMap, ptr %map, i32 0, i32 1
  %valuesp = getelementptr %CfvMap, ptr %map, i32 0, i32 2
  %keys = load ptr, ptr %keysp
  %values = load ptr, ptr %valuesp
  call void @free(ptr %keys)
  call void @free(ptr %values)
  call void @free(ptr %map)
  ret void
}
'''
        globals_text = "\n".join(self.globals)
        ffi_declarations = "\n".join(
            (f"declare i32 @{name}(" + ", ".join([
                *(self.llvm_type(value) for value in parameters), "ptr", "ptr"
            ]) + ")") if checked else (
                f"declare {self.llvm_type(returned)} @{name}(" + ", ".join(
                    self.llvm_type(value) for value in parameters
                ) + ")"
            )
            for name, (parameters, returned, checked) in self.ffi_functions.items()
        )
        return (prelude + nominal_types + "\n" + globals_text + "\n" + ffi_declarations
                + "\n\n" + "\n\n".join(functions + [main_ir]) + "\n")


def compile_source(source: str) -> str:
    program = Parser(tokenize(source)).program(); StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
    validate_llvm_signatures(program)
    return LLVMGenerator().generate(program)


def compile_file(source: Path) -> str:
    try: text = source.read_text(encoding="utf-8")
    except OSError as error: raise CForgevError(f"No se pudo abrir {source}: {error}") from error
    program = resolve_imports(Parser(tokenize(text)).program(), source.resolve().parent, set())
    StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
    validate_llvm_signatures(program)
    return LLVMGenerator().generate(program)


def validate_llvm_signatures(program: Program) -> None:
    """LLVM exige una ABI inequívoca aunque otros backends sean graduales."""
    for function in program.functions:
        parameters = function[2]
        parameter_types = function[4] if len(function) > 4 else []
        missing = [
            name for index, name in enumerate(parameters)
            if index >= len(parameter_types) or parameter_types[index] == "cualquiera"
        ]
        if missing:
            rendered = ", ".join(missing)
            example = ", ".join(f"{name}: numero" for name in parameters)
            raise CForgevError(
                f"LLVM: la función '{function[1]}' requiere tipos explícitos en "
                f"todos sus parámetros; faltan: {rendered}. "
                f"Usa funcion {function[1]}({example}): numero"
            )


def emit_file(source: Path, output: Path) -> Path:
    text = compile_file(source)
    output.parent.mkdir(parents=True, exist_ok=True); output.write_text(text, encoding="utf-8")
    return output


def compile_native(
    source: Path,
    output: Path,
    clang: str = "clang",
    linked_sources: list[Path] | None = None,
) -> Path:
    llvm_path = output.with_suffix(".ll"); emit_file(source, llvm_path)
    linked_sources = linked_sources or []
    allowed = {".c", ".cc", ".cpp", ".cxx", ".o", ".a", ".so", ".dylib"}
    for path in linked_sources:
        if path.suffix.lower() not in allowed:
            raise CForgevError(f"LLVM FFI: formato de vínculo no permitido: {path}")
        if not path.exists():
            raise CForgevError(f"LLVM FFI: no existe el archivo vinculado: {path}")
    compiler = "clang++" if clang == "clang" and any(
        path.suffix.lower() in {".cc", ".cpp", ".cxx"} for path in linked_sources
    ) else clang
    process = subprocess.run(
        [compiler, "-O2", llvm_path, *(str(path) for path in linked_sources), "-o", output],
        capture_output=True,
        text=True,
    )
    if process.returncode: raise CForgevError("LLVM/Clang rechazó el módulo:\n" + process.stderr.strip())
    return output
)CFV7DATA"},
        {R"CFV8DATA(cforge_diagnostics.py)CFV8DATA", R"CFV9DATA("""Diagnósticos estructurados de C-Forge para CLI, editores y CI."""

from __future__ import annotations

import re
from dataclasses import asdict, dataclass
from pathlib import Path

from cforgev import CForgevError, tokenize
from compilador_nativo import Parser, StaticTypeAnalyzer, resolve_imports
from cforge_memory import MemorySafetyAnalyzer


@dataclass(frozen=True)
class Diagnostic:
    line: int
    column: int
    severity: str
    code: str
    message: str

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def _from_error(error: Exception, code: str) -> Diagnostic:
    message = str(error)
    found = re.search(r"Línea\s+(\d+)", message)
    line = int(found.group(1)) if found else 1
    return Diagnostic(line, 1, "error", code, message)


def analyze_source(source: str) -> list[Diagnostic]:
    try:
        tokens = tokenize(source)
    except CForgevError as error:
        return [_from_error(error, "CF1001")]
    try:
        program = Parser(tokens).program()
    except CForgevError as error:
        return [_from_error(error, "CF1002")]
    try:
        StaticTypeAnalyzer().analyze(program)
    except CForgevError as error:
        return [_from_error(error, "CF2001")]
    try:
        MemorySafetyAnalyzer().analyze(program)
    except CForgevError as error:
        return [_from_error(error, "CF3001")]
    return []


def analyze_file(path: Path) -> list[Diagnostic]:
    try:
        source = path.read_text(encoding="utf-8")
    except OSError as error:
        return [Diagnostic(1, 1, "error", "CF0001", f"No se pudo abrir {path}: {error}")]
    try:
        tokens = tokenize(source)
        program = resolve_imports(Parser(tokens).program(), path.resolve().parent, set())
    except CForgevError as error:
        return [_from_error(error, "CF1002")]
    try:
        StaticTypeAnalyzer().analyze(program)
    except CForgevError as error:
        return [_from_error(error, "CF2001")]
    try:
        MemorySafetyAnalyzer().analyze(program)
    except CForgevError as error:
        return [_from_error(error, "CF3001")]
    return []
)CFV9DATA"},
        {R"CFV10DATA(cforge_lsp.py)CFV10DATA", R"CFV11DATA("""Servidor LSP 3.17 mínimo de C-Forge mediante JSON-RPC por stdio."""

from __future__ import annotations

import json
import re
import sys
from typing import BinaryIO

from cforge_diagnostics import analyze_source
from cforgev import format_source


KEYWORDS = [
    "sea", "si", "sino", "mientras", "funcion", "async", "await", "retornar", "estructura", "interfaz", "implementa",
    "clase", "campo", "metodo", "intentar", "capturar", "gpu", "cluster",
    "test", "verdadero", "falso", "nulo", "mostrar", "print", "console.log",
    "System.out.println", "file_read", "file_write", "json_parse", "sys_fetch",
    "forge_hash", "forge_bench", "forge_catalogo", "forge_arena_estado",
    "region", "unsafe", "mover", "prestar", "prestar_mut",
    "soltar_prestamo", "destruir", "opcion", "algunos", "ninguno",
    "es_algunos", "desenvolver",
    "numero", "texto", "booleano", "lista", "mapa", "tupla", "conjunto",
    "tarea", "esperar", "cancelar", "canal", "enviar", "recibir",
    "cerrar_canal",
    "extern_c", "segura",
]


def _read(stream: BinaryIO) -> dict[str, object] | None:
    headers: dict[str, str] = {}
    while True:
        line = stream.readline()
        if not line:
            return None
        if line in {b"\r\n", b"\n"}:
            break
        key, _, value = line.decode("ascii").partition(":")
        headers[key.lower()] = value.strip()
    size = int(headers.get("content-length", "0"))
    if size <= 0:
        return None
    return json.loads(stream.read(size).decode("utf-8"))


def _write(stream: BinaryIO, payload: dict[str, object]) -> None:
    raw = json.dumps(payload, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    stream.write(f"Content-Length: {len(raw)}\r\n\r\n".encode("ascii") + raw)
    stream.flush()


def _publish(output: BinaryIO, uri: str, source: str) -> None:
    diagnostics = []
    for item in analyze_source(source):
        line = max(0, item.line - 1)
        column = max(0, item.column - 1)
        diagnostics.append({
            "range": {
                "start": {"line": line, "character": column},
                "end": {"line": line, "character": column + 1},
            },
            "severity": 1 if item.severity == "error" else 2,
            "code": item.code,
            "source": "C-Forge",
            "message": item.message,
        })
    _write(output, {
        "jsonrpc": "2.0", "method": "textDocument/publishDiagnostics",
        "params": {"uri": uri, "diagnostics": diagnostics},
    })


def _position_offset(source: str, position: dict[str, object]) -> int:
    lines = source.splitlines(keepends=True)
    line = max(0, int(position.get("line", 0)))
    character = max(0, int(position.get("character", 0)))
    return sum(len(value) for value in lines[:line]) + min(character, len(lines[line]) if line < len(lines) else 0)


def _word_at(source: str, position: dict[str, object]) -> str:
    offset = _position_offset(source, position)
    for match in re.finditer(r"[A-Za-z_][A-Za-z0-9_]*", source):
        if match.start() <= offset <= match.end():
            return match.group(0)
    return ""


def _locations(uri: str, source: str, word: str) -> list[dict[str, object]]:
    if not word: return []
    result = []
    for line_number, line in enumerate(source.splitlines()):
        for match in re.finditer(rf"\b{re.escape(word)}\b", line):
            result.append({"uri": uri, "range": {
                "start": {"line": line_number, "character": match.start()},
                "end": {"line": line_number, "character": match.end()},
            }})
    return result


def _definitions(uri: str, source: str, word: str) -> list[dict[str, object]]:
    for declaration in _declarations(source):
        if declaration["name"] == word:
            return [{"uri": uri, "range": declaration["range"]}]
    return []


def _infer_expression_type(expression: str) -> str:
    value = expression.strip()
    if re.match(r'^"', value): return "texto"
    if re.match(r"^-?\d+(?:\.\d+)?\b", value): return "numero"
    if re.match(r"^(?:verdadero|falso)\b", value): return "booleano"
    if value.startswith("["): return "lista"
    if value.startswith("{"): return "mapa"
    if value.startswith("conjunto("): return "conjunto"
    option = re.match(r"algunos\((.*)\)\s*$", value)
    if option: return f"opcion<{_infer_expression_type(option.group(1))}>"
    if value.startswith("ninguno("): return "opcion<cualquiera>"
    return "cualquiera"


def _declarations(source: str) -> list[dict[str, object]]:
    """Extrae símbolos y firmas sin ejecutar código; tolera archivos incompletos."""
    result: list[dict[str, object]] = []
    type_pattern = r"[A-Za-z_][A-Za-z0-9_]*(?:<[^>\n]+>)?"
    function_pattern = re.compile(
        rf"^\s*(?P<prefix>(?:extern_c\s+(?:segura\s+)?)|(?:cluster\s+)?(?:async\s+)?)"
        rf"funcion\s+(?P<name>[A-Za-z_]\w*)\s*\((?P<params>[^)]*)\)"
        rf"(?:\s*:\s*(?P<return>{type_pattern}))?"
    )
    variable_pattern = re.compile(
        rf"^\s*(?:cluster\s+)?sea\s+(?P<name>[A-Za-z_]\w*)"
        rf"(?:\s*:\s*(?P<type>{type_pattern}))?\s*=\s*(?P<value>.*)$"
    )
    nominal_pattern = re.compile(r"^\s*(estructura|clase|interfaz)\s+([A-Za-z_]\w*)")
    for line_number, line in enumerate(source.splitlines()):
        function = function_pattern.search(line)
        if function:
            name = function.group("name"); prefix = function.group("prefix").strip()
            returned = function.group("return") or "cualquiera"
            parameters = function.group("params").strip()
            marker = "extern C ABI segura" if prefix.startswith("extern_c segura") else (
                "extern C ABI" if prefix.startswith("extern_c") else "función"
            )
            detail = f"{marker} {name}({parameters}): {returned}"
            start = function.start("name")
            result.append({"name": name, "kind": 12, "detail": detail, "range": {
                "start": {"line": line_number, "character": start},
                "end": {"line": line_number, "character": function.end("name")},
            }})
            continue
        variable = variable_pattern.search(line)
        if variable:
            name = variable.group("name")
            value_type = variable.group("type") or _infer_expression_type(variable.group("value"))
            start = variable.start("name")
            result.append({"name": name, "kind": 13, "detail": f"{name}: {value_type}", "range": {
                "start": {"line": line_number, "character": start},
                "end": {"line": line_number, "character": variable.end("name")},
            }})
            continue
        nominal = nominal_pattern.search(line)
        if nominal:
            name = nominal.group(2); category = nominal.group(1)
            kind = {"estructura": 23, "clase": 5, "interfaz": 11}[category]
            result.append({"name": name, "kind": kind, "detail": f"{category} {name}", "range": {
                "start": {"line": line_number, "character": nominal.start(2)},
                "end": {"line": line_number, "character": nominal.end(2)},
            }})
    return result


def _symbols(source: str) -> list[dict[str, object]]:
    return [{"name": item["name"], "kind": item["kind"], "detail": item["detail"],
             "range": item["range"], "selectionRange": item["range"]}
            for item in _declarations(source)]


def run(input_stream: BinaryIO | None = None, output_stream: BinaryIO | None = None) -> int:
    input_stream = input_stream or sys.stdin.buffer
    output_stream = output_stream or sys.stdout.buffer
    documents: dict[str, str] = {}
    while True:
        message = _read(input_stream)
        if message is None:
            return 0
        method = message.get("method")
        request_id = message.get("id")
        params = message.get("params", {})
        if method == "initialize":
            _write(output_stream, {
                "jsonrpc": "2.0", "id": request_id,
                "result": {"serverInfo": {"name": "C-Forge LSP", "version": "1.6.0"},
                           "capabilities": {"textDocumentSync": 1,
                                            "completionProvider": {"triggerCharacters": ["."]},
                                            "hoverProvider": True,
                                            "definitionProvider": True,
                                            "referencesProvider": True,
                                            "renameProvider": {"prepareProvider": False},
                                            "documentSymbolProvider": True,
                                            "documentFormattingProvider": True}},
            })
        elif method == "shutdown":
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": None})
        elif method == "exit":
            return 0
        elif method in {"textDocument/didOpen", "textDocument/didChange"}:
            assert isinstance(params, dict)
            document = params.get("textDocument", {})
            assert isinstance(document, dict)
            uri = str(document.get("uri", ""))
            if method.endswith("didOpen"):
                source = str(document.get("text", ""))
            else:
                changes = params.get("contentChanges", [])
                source = str(changes[-1].get("text", "")) if isinstance(changes, list) and changes else ""
            documents[uri] = source
            _publish(output_stream, uri, source)
        elif method == "textDocument/completion":
            symbols = {
                str(item["name"]): int(item["kind"])
                for source in documents.values() for item in _declarations(source)
            }
            completion = [{"label": word, "kind": 14} for word in KEYWORDS]
            completion.extend({"label": name, "kind": kind, "detail": "símbolo C-Forge del proyecto"}
                              for name, kind in sorted(symbols.items()))
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": completion})
        elif method == "textDocument/hover":
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); position = params.get("position", {})
            assert isinstance(document, dict) and isinstance(position, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            word = _word_at(source, position)
            detail = next((str(item["detail"]) for text in documents.values()
                           for item in _declarations(text) if item["name"] == word), None)
            value = f"```cforge\n{detail}\n```" if detail else "**C-Forge 1.6.0** — ForgeValue y sintaxis `.cfv`."
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": {"contents": {"kind": "markdown",
                                                            "value": value}}})
        elif method in {"textDocument/definition", "textDocument/references", "textDocument/rename"}:
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); position = params.get("position", {})
            assert isinstance(document, dict) and isinstance(position, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            word = _word_at(source, position)
            if method.endswith("definition"):
                result = next((found for target_uri, text in documents.items()
                               if (found := _definitions(target_uri, text, word))), [])
            elif method.endswith("references"):
                result = [location for target_uri, text in documents.items()
                          for location in _locations(target_uri, text, word)]
            else:
                new_name = str(params.get("newName", ""))
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", new_name):
                    _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                           "error": {"code": -32602, "message": "Nombre C-Forge inválido"}})
                    continue
                changes = {}
                for target_uri, text in documents.items():
                    edits = [{"range": item["range"], "newText": new_name}
                             for item in _locations(target_uri, text, word)]
                    if edits: changes[target_uri] = edits
                result = {"changes": changes}
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": result})
        elif method == "textDocument/documentSymbol":
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); assert isinstance(document, dict)
            uri = str(document.get("uri", ""))
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id,
                                   "result": _symbols(documents.get(uri, ""))})
        elif method == "textDocument/formatting":
            assert isinstance(params, dict)
            document = params.get("textDocument", {}); assert isinstance(document, dict)
            uri = str(document.get("uri", "")); source = documents.get(uri, "")
            end_line = len(source.splitlines()) + 1
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": [{
                "range": {"start": {"line": 0, "character": 0},
                          "end": {"line": end_line, "character": 0}},
                "newText": format_source(source),
            }]})
        elif request_id is not None:
            _write(output_stream, {"jsonrpc": "2.0", "id": request_id, "result": None})


if __name__ == "__main__":
    raise SystemExit(run())
)CFV11DATA"},
        {R"CFV12DATA(cforge_dap.py)CFV12DATA", R"CFV13DATA("""Adaptador Debug Adapter Protocol para la VM de C-Forge."""
from __future__ import annotations

import json
import ast
import re
import sys
import threading
from pathlib import Path
from typing import Any, BinaryIO

from cforgev import CForgevError
from cforge_vm import BytecodeProgram, Instruction, VirtualMachine, compile_file


class DAPSession:
    def __init__(self, reader: BinaryIO, writer: BinaryIO) -> None:
        self.reader, self.writer = reader, writer
        self.sequence = 1
        self.send_lock = threading.Lock()
        self.program: BytecodeProgram | None = None
        self.source: Path | None = None
        self.breakpoints: dict[int, dict[str, Any]] = {}
        self.breakpoint_hits: dict[int, int] = {}
        self.condition = threading.Condition()
        self.paused = False
        self.stepping = False
        self.terminated = False
        self.snapshot: dict[str, Any] = {}
        self.chunk = "<main>"; self.line = 1
        self.frames: list[dict[str, Any]] = []
        self.references: dict[int, Any] = {}
        self.frame_references: dict[int, int] = {}
        self.next_reference = 1
        self.vm: VirtualMachine | None = None
        self.last_location: tuple[int, int] | None = None
        self.resume_location: tuple[int, int] | None = None
        self.step_mode: str | None = None
        self.step_depth = 0
        self.worker: threading.Thread | None = None

    def send(self, message: dict[str, Any]) -> None:
        with self.send_lock:
            message.setdefault("seq", self.sequence); self.sequence += 1
            body = json.dumps(message, ensure_ascii=False, separators=(",", ":")).encode()
            self.writer.write(f"Content-Length: {len(body)}\r\n\r\n".encode() + body)
            self.writer.flush()

    def response(self, request: dict[str, Any], body: dict[str, Any] | None = None,
                 success: bool = True, message: str | None = None) -> None:
        result: dict[str, Any] = {"type": "response", "request_seq": request.get("seq", 0),
                                  "command": request.get("command", ""), "success": success}
        if body is not None: result["body"] = body
        if message: result["message"] = message
        self.send(result)

    def event(self, name: str, body: dict[str, Any] | None = None) -> None:
        message: dict[str, Any] = {"type": "event", "event": name}
        if body is not None: message["body"] = body
        self.send(message)

    def _all_lines(self) -> set[int]:
        if self.program is None: return set()
        chunks = [self.program.main, *self.program.functions.values()]
        for definition in self.program.classes.values():
            chunks.extend(definition["methods"].values())
        return {instruction.line for chunk in chunks for instruction in chunk.code
                if instruction.line is not None}

    def trace(self, chunk: str, offset: int, instruction: Instruction,
              scope: dict[str, Any]) -> None:
        line = instruction.line or self.line
        frames = self.vm.debug_frames() if self.vm is not None else [
            {"name": chunk, "line": line, "offset": offset, "scope": scope}
        ]
        location = (len(frames), line)
        changed = location != self.resume_location
        if self.resume_location is not None and changed:
            self.resume_location = None
        breakpoint = self.breakpoints.get(line)
        should_pause = bool(breakpoint and changed)
        if breakpoint and changed:
            self.breakpoint_hits[line] = self.breakpoint_hits.get(line, 0) + 1
            condition = str(breakpoint.get("condition") or "").strip()
            if condition:
                try: should_pause = bool(self._evaluate_safe(condition, scope))
                except Exception as error:
                    should_pause = True
                    self.event("output", {"category": "stderr",
                        "output": f"[C-Forge Debug] Condición inválida en línea {line}: {error}\n"})
            hit_condition = str(breakpoint.get("hitCondition") or "").strip()
            if should_pause and hit_condition:
                try: should_pause = self._hit_matches(hit_condition, self.breakpoint_hits[line])
                except Exception as error:
                    should_pause = True
                    self.event("output", {"category": "stderr",
                        "output": f"[C-Forge Debug] Hit condition inválida en línea {line}: {error}\n"})
            log_message = str(breakpoint.get("logMessage") or "")
            if should_pause and log_message:
                try: rendered = self._format_logpoint(log_message, scope)
                except Exception as error: rendered = f"[C-Forge Debug] Logpoint inválido: {error}"
                self.event("output", {"category": "console", "output": rendered + "\n"})
                should_pause = False
        if self.step_mode == "in" and changed: should_pause = True
        elif self.step_mode == "over" and len(frames) <= self.step_depth and changed: should_pause = True
        elif self.step_mode == "out" and len(frames) < self.step_depth: should_pause = True
        if not should_pause: return
        with self.condition:
            self.chunk, self.line, self.snapshot = chunk, line, dict(scope)
            self.frames = frames; self._rebuild_references()
            self.paused = True; self.stepping = False; self.step_mode = None
            self.last_location = location
            self.event("stopped", {"reason": "breakpoint" if line in self.breakpoints else "step",
                                   "threadId": 1, "allThreadsStopped": True})
            self.condition.wait_for(lambda: not self.paused or self.terminated)

    def start(self) -> None:
        if self.program is None or self.worker is not None: return
        def execute() -> None:
            try:
                vm = VirtualMachine(self.program,
                    lambda text: self.event("output", {"category": "stdout", "output": text + "\n"}),
                    base_dir=(self.source.parent if self.source else Path.cwd()), trace=self.trace)
                self.vm = vm; vm.run()
            except Exception as error:
                self.event("output", {"category": "stderr", "output": f"[C-Forge Debug] {error}\n"})
            finally:
                self.terminated = True; self.event("terminated")
                with self.condition:
                    self.paused = False; self.condition.notify_all()
        self.worker = threading.Thread(target=execute, name="cforge-dap-vm", daemon=True)
        self.worker.start()

    @staticmethod
    def value(value: Any) -> str:
        if value is None: return "nulo"
        if isinstance(value, bool): return "verdadero" if value else "falso"
        return repr(value)

    def _reference(self, value: Any) -> int:
        if not isinstance(value, (dict, list, tuple, set)) and not hasattr(value, "__dict__"):
            return 0
        for reference, existing in self.references.items():
            if existing is value: return reference
        reference = self.next_reference; self.next_reference += 1
        self.references[reference] = value
        return reference

    def _rebuild_references(self) -> None:
        self.references = {}; self.frame_references = {}; self.next_reference = 1
        for frame_id, frame in enumerate(reversed(self.frames), 1):
            self.frame_references[frame_id] = self._reference(frame["scope"])

    def _children(self, value: Any) -> list[tuple[str, Any]]:
        if isinstance(value, dict): return [(str(key), item) for key, item in value.items()]
        if isinstance(value, (list, tuple)): return [(f"[{index}]", item) for index, item in enumerate(value)]
        if isinstance(value, set): return [(f"[{index}]", item) for index, item in enumerate(sorted(value, key=repr))]
        if hasattr(value, "__dict__"): return list(vars(value).items())
        return []

    def _variable(self, name: str, value: Any) -> dict[str, Any]:
        return {"name": name, "value": self.value(value), "type": type(value).__name__,
                "variablesReference": self._reference(value)}

    def _inspect(self, expression: str) -> Any:
        match = re.match(r"^[A-Za-z_]\w*", expression)
        if not match: raise CForgevError("Solo se permiten variables, campos e índices")
        name = match.group(); value = self.snapshot.get(name, self.vm.globals.get(name) if self.vm else None)
        if name not in self.snapshot and (self.vm is None or name not in self.vm.globals):
            raise CForgevError(f"Variable desconocida '{name}'")
        cursor = match.end()
        while cursor < len(expression):
            if expression[cursor] == ".":
                field = re.match(r"[A-Za-z_]\w*", expression[cursor + 1:])
                if not field: raise CForgevError("Campo inválido")
                key = field.group(); cursor += 1 + len(key)
                if not isinstance(value, dict) or key not in value: raise CForgevError(f"Campo desconocido '{key}'")
                value = value[key]
            elif expression[cursor] == "[":
                end = expression.find("]", cursor)
                if end < 0: raise CForgevError("Índice sin cerrar")
                raw = expression[cursor + 1:end].strip()
                try: key = json.loads(raw) if raw.startswith('"') else int(raw)
                except Exception as error: raise CForgevError("Índice inválido") from error
                value = value[key]; cursor = end + 1
            else: raise CForgevError("La evaluación no permite llamadas ni operadores")
        return value

    @classmethod
    def _evaluate_safe(cls, expression: str, scope: dict[str, Any]) -> Any:
        """Evalúa condiciones DAP sin llamadas, asignaciones ni acceso arbitrario."""
        operators = {
            ast.Add: lambda a, b: a + b, ast.Sub: lambda a, b: a - b,
            ast.Mult: lambda a, b: a * b, ast.Div: lambda a, b: a / b,
            ast.Mod: lambda a, b: a % b, ast.Eq: lambda a, b: a == b,
            ast.NotEq: lambda a, b: a != b, ast.Lt: lambda a, b: a < b,
            ast.LtE: lambda a, b: a <= b, ast.Gt: lambda a, b: a > b,
            ast.GtE: lambda a, b: a >= b,
        }
        def visit(node: ast.AST) -> Any:
            if isinstance(node, ast.Expression): return visit(node.body)
            if isinstance(node, ast.Constant) and isinstance(node.value, (str, int, float, bool, type(None))):
                return node.value
            if isinstance(node, ast.Name):
                if node.id not in scope: raise CForgevError(f"variable desconocida '{node.id}'")
                return scope[node.id]
            if isinstance(node, ast.BinOp) and type(node.op) in operators:
                return operators[type(node.op)](visit(node.left), visit(node.right))
            if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.Not, ast.USub, ast.UAdd)):
                value = visit(node.operand)
                return not value if isinstance(node.op, ast.Not) else (-value if isinstance(node.op, ast.USub) else +value)
            if isinstance(node, ast.BoolOp) and isinstance(node.op, (ast.And, ast.Or)):
                values = [bool(visit(value)) for value in node.values]
                return all(values) if isinstance(node.op, ast.And) else any(values)
            if isinstance(node, ast.Compare):
                left = visit(node.left)
                for operator, comparator in zip(node.ops, node.comparators):
                    if type(operator) not in operators: raise CForgevError("operador de comparación no permitido")
                    right = visit(comparator)
                    if not operators[type(operator)](left, right): return False
                    left = right
                return True
            if isinstance(node, ast.Subscript):
                container = visit(node.value); key = visit(node.slice)
                return container[key]
            if isinstance(node, ast.Attribute):
                container = visit(node.value)
                if not isinstance(container, dict) or node.attr not in container:
                    raise CForgevError(f"campo desconocido '{node.attr}'")
                return container[node.attr]
            raise CForgevError("la condición contiene una operación no permitida")
        expression = re.sub(r"\bverdadero\b", "True", expression)
        expression = re.sub(r"\bfalso\b", "False", expression)
        expression = re.sub(r"\bnulo\b", "None", expression)
        expression = re.sub(r"\by\b", "and", expression)
        expression = re.sub(r"\bo\b", "or", expression)
        expression = re.sub(r"\bno\b", "not", expression)
        try: tree = ast.parse(expression, mode="eval")
        except SyntaxError as error: raise CForgevError("sintaxis de condición inválida") from error
        return visit(tree)

    @staticmethod
    def _hit_matches(expression: str, count: int) -> bool:
        if expression.isdigit(): return count == int(expression)
        match = re.fullmatch(r"(>=|>|==|%)(\d+)", expression.replace(" ", ""))
        if not match: raise CForgevError("hitCondition debe ser N, >=N, >N, ==N o %N")
        operator, raw = match.groups(); target = int(raw)
        if operator == ">=": return count >= target
        if operator == ">": return count > target
        if operator == "==": return count == target
        return target > 0 and count % target == 0

    @classmethod
    def _format_logpoint(cls, template: str, scope: dict[str, Any]) -> str:
        def replace(match: re.Match[str]) -> str:
            return cls.value(cls._evaluate_safe(match.group(1), scope))
        return re.sub(r"\{([^{}]+)\}", replace, template)

    def handle(self, request: dict[str, Any]) -> bool:
        command = request.get("command"); arguments = request.get("arguments") or {}
        try:
            if command == "initialize":
                self.response(request, {"supportsConfigurationDoneRequest": True,
                    "supportsTerminateRequest": True, "supportsStepBack": False,
                    "supportsEvaluateForHovers": True,
                    "supportsConditionalBreakpoints": True,
                    "supportsHitConditionalBreakpoints": True,
                    "supportsLogPoints": True})
                self.event("initialized")
            elif command == "launch":
                path = Path(str(arguments.get("program", ""))).resolve()
                if path.suffix != ".cfv": raise CForgevError("DAP requiere un archivo .cfv")
                self.program, self.source = compile_file(path), path
                self.response(request)
            elif command == "setBreakpoints":
                specifications = list(arguments.get("breakpoints", []))
                requested = [int(item.get("line", 0)) for item in specifications]
                available = self._all_lines()
                self.breakpoints = {int(item.get("line", 0)): dict(item) for item in specifications
                                    if int(item.get("line", 0)) in available}
                self.breakpoint_hits = {}
                self.response(request, {"breakpoints": [
                    {"verified": line in available, "line": line,
                     **({} if line in available else {"message": "No hay instrucción ejecutable en esta línea"})}
                    for line in requested]})
            elif command == "configurationDone": self.response(request); self.start()
            elif command == "threads":
                self.response(request, {"threads": [{"id": 1, "name": "C-Forge VM"}]})
            elif command == "stackTrace":
                frames = list(reversed(self.frames)) or [{"name": self.chunk, "line": self.line}]
                self.response(request, {"stackFrames": [{"id": index, "name": frame["name"],
                    "line": frame["line"], "column": 1,
                    "source": {"name": self.source.name if self.source else "programa.cfv",
                               "path": str(self.source) if self.source else ""}}
                    for index, frame in enumerate(frames, 1)], "totalFrames": len(frames)})
            elif command == "scopes":
                self.response(request, {"scopes": [{"name": "Variables C-Forge",
                    "variablesReference": self.frame_references.get(int(arguments.get("frameId", 1)), 0),
                    "expensive": False}]})
            elif command == "variables":
                value = self.references.get(int(arguments.get("variablesReference", 0)))
                self.response(request, {"variables": [self._variable(name, item)
                    for name, item in self._children(value)]})
            elif command == "evaluate":
                value = self._inspect(str(arguments.get("expression", "")))
                self.response(request, {"result": self.value(value),
                    "type": type(value).__name__, "variablesReference": self._reference(value)})
            elif command in {"continue", "next", "stepIn", "stepOut"}:
                with self.condition:
                    self.resume_location = self.last_location
                    self.step_mode = {"next": "over", "stepIn": "in", "stepOut": "out"}.get(command)
                    self.step_depth = len(self.frames)
                    self.stepping = command != "continue"; self.paused = False; self.condition.notify_all()
                self.response(request, {"allThreadsContinued": True})
            elif command in {"disconnect", "terminate"}:
                self.terminated = True
                with self.condition: self.paused = False; self.condition.notify_all()
                self.response(request); return False
            else: self.response(request, success=False, message=f"Comando DAP no soportado: {command}")
        except Exception as error:
            self.response(request, success=False, message=str(error))
        return True


def _read_message(reader: BinaryIO) -> dict[str, Any] | None:
    length = None
    while True:
        line = reader.readline()
        if not line: return None
        if line in {b"\r\n", b"\n"}: break
        key, _, value = line.decode("ascii").partition(":")
        if key.lower() == "content-length": length = int(value.strip())
    if length is None: raise CForgevError("DAP: falta Content-Length")
    payload = reader.read(length)
    if len(payload) != length: raise CForgevError("DAP: mensaje truncado")
    value = json.loads(payload)
    if not isinstance(value, dict): raise CForgevError("DAP: mensaje inválido")
    return value


def run(reader: BinaryIO | None = None, writer: BinaryIO | None = None) -> int:
    session = DAPSession(reader or sys.stdin.buffer, writer or sys.stdout.buffer)
    while True:
        message = _read_message(session.reader)
        if message is None or not session.handle(message): break
    return 0


if __name__ == "__main__": raise SystemExit(run())
)CFV13DATA"},
        {R"CFV14DATA(cforge_packages.py)CFV14DATA", R"CFV15DATA("""Gestor local, reproducible y seguro de paquetes C-Forge."""

from __future__ import annotations

import hashlib
import base64
import gzip
import json
import re
import tarfile
import tempfile
import urllib.request
from pathlib import Path

from cforgev import CForgevError


MANIFEST = "cforge.json"
LOCKFILE = "cforge.lock"
NAME_RE = re.compile(r"^[A-Za-z][A-Za-z0-9_-]{0,63}$")
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[A-Za-z0-9.-]+)?$")
DEFAULT_REGISTRY = "https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/registry/index.json"
MAX_PACKAGE_BYTES = 32 * 1024 * 1024


def _crypto():
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
    except ImportError as error:
        raise CForgevError(
            "Las firmas Ed25519 requieren el componente oficial 'cryptography'"
        ) from error
    return serialization, Ed25519PrivateKey, Ed25519PublicKey


def _signature_message(name: str, version: str, digest: str) -> bytes:
    if not NAME_RE.fullmatch(name) or not VERSION_RE.fullmatch(version):
        raise CForgevError("Nombre o versión inválidos para firmar")
    if not re.fullmatch(r"[0-9a-f]{64}", digest):
        raise CForgevError("SHA-256 inválido para firmar")
    return b"C-FORGE-PACKAGE-V1\0" + name.encode() + b"\0" + version.encode() + b"\0" + bytes.fromhex(digest)


def generate_keypair(private_path: Path, public_path: Path) -> str:
    """Genera una identidad Ed25519; nunca sobrescribe material existente."""
    if private_path.exists() or public_path.exists():
        raise CForgevError("La clave ya existe; no se sobrescribirá")
    serialization, Private, _ = _crypto()
    private = Private.generate(); public = private.public_key()
    private_path.parent.mkdir(parents=True, exist_ok=True)
    private_path.write_bytes(private.private_bytes(
        serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    ))
    public_path.write_bytes(public.public_bytes(
        serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo,
    ))
    try: private_path.chmod(0o600)
    except OSError: pass
    raw = public.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    return hashlib.sha256(raw).hexdigest()


def sign_package(archive: Path, private_path: Path, name: str, version: str,
                 output: Path | None = None) -> Path:
    serialization, Private, _ = _crypto()
    try:
        private = serialization.load_pem_private_key(private_path.read_bytes(), password=None)
    except (OSError, ValueError) as error:
        raise CForgevError(f"Clave privada inválida: {error}") from error
    if not isinstance(private, Private): raise CForgevError("La clave no es Ed25519")
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    public = private.public_key()
    raw_public = public.public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    document = {
        "format": 1, "algorithm": "Ed25519", "name": name, "version": version,
        "sha256": digest,
        "public_key": base64.b64encode(raw_public).decode("ascii"),
        "key_id": hashlib.sha256(raw_public).hexdigest(),
        "signature": base64.b64encode(private.sign(_signature_message(name, version, digest))).decode("ascii"),
    }
    destination = output or archive.with_suffix(archive.suffix + ".sig.json")
    destination.write_text(json.dumps(document, sort_keys=True, indent=2) + "\n", encoding="utf-8")
    return destination


def verify_package_signature(payload: bytes, release: dict[str, object], name: str,
                             version: str, revocations: set[str] | None = None) -> str:
    _, _, Public = _crypto()
    digest = hashlib.sha256(payload).hexdigest()
    expected = str(release.get("sha256", "")).lower()
    key_id = str(release.get("key_id", "")).lower()
    if digest != expected: raise CForgevError("La suma SHA-256 del paquete no coincide")
    revoked = revocations or set()
    if f"{name}@{version}" in revoked or key_id in revoked:
        raise CForgevError(f"Paquete o clave revocados: {name}@{version}")
    try:
        raw_public = base64.b64decode(str(release["public_key"]), validate=True)
        signature = base64.b64decode(str(release["signature"]), validate=True)
        calculated_id = hashlib.sha256(raw_public).hexdigest()
        if key_id != calculated_id: raise CForgevError("Identificador de clave inválido")
        Public.from_public_bytes(raw_public).verify(
            signature, _signature_message(name, version, digest)
        )
    except CForgevError: raise
    except Exception as error:
        raise CForgevError("Firma Ed25519 inválida") from error
    return key_id


def verify_publisher(index: dict[str, object], metadata: dict[str, object], key_id: str) -> str:
    publisher = metadata.get("publisher")
    publishers = index.get("publishers", {})
    account = publishers.get(publisher) if isinstance(publishers, dict) else None
    if not isinstance(publisher, str) or not isinstance(account, dict):
        raise CForgevError("El paquete no posee un publicador registrado")
    keys = account.get("keys", [])
    if account.get("status") != "active" or not isinstance(keys, list) or key_id not in keys:
        raise CForgevError("La identidad o clave del publicador no está autorizada")
    return publisher


def _load(root: Path) -> dict[str, object]:
    path = root / MANIFEST
    if not path.exists():
        raise CForgevError("No existe cforge.json; ejecuta 'cforge pkg init'")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise CForgevError(f"Manifiesto inválido: {error}") from error
    if not isinstance(value, dict) or not isinstance(value.get("dependencies", {}), dict):
        raise CForgevError("cforge.json no posee un mapa dependencies válido")
    return value


def init(root: Path, name: str | None = None) -> None:
    project = name or root.name.lower().replace(" ", "-")
    if not NAME_RE.fullmatch(project):
        project = "proyecto-cforge"
    path = root / MANIFEST
    if path.exists():
        raise CForgevError(f"{MANIFEST} ya existe")
    value = {"name": project, "version": "0.1.0", "language": ">=1.6.0", "dependencies": {}}
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    _write_lock(root, value)


def add(root: Path, name: str, source: str) -> None:
    if not NAME_RE.fullmatch(name):
        raise CForgevError("Nombre de paquete inválido")
    manifest = _load(root)
    candidate = Path(source).expanduser()
    if not candidate.is_absolute():
        candidate = (root / candidate).resolve()
    if not candidate.exists():
        raise CForgevError(f"La dependencia local no existe: {candidate}")
    dependencies = manifest.setdefault("dependencies", {})
    assert isinstance(dependencies, dict)
    dependencies[name] = {"path": str(candidate)}
    (root / MANIFEST).write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    _write_lock(root, manifest)


def remove(root: Path, name: str) -> None:
    manifest = _load(root)
    dependencies = manifest.get("dependencies", {})
    assert isinstance(dependencies, dict)
    if name not in dependencies:
        raise CForgevError(f"Paquete no registrado: {name}")
    del dependencies[name]
    (root / MANIFEST).write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
    _write_lock(root, manifest)


def list_packages(root: Path) -> list[tuple[str, str]]:
    manifest = _load(root)
    dependencies = manifest.get("dependencies", {})
    assert isinstance(dependencies, dict)
    return sorted(
        (name, str(spec.get("path", "")))
        for name, spec in dependencies.items() if isinstance(spec, dict)
    )


def fetch_registry(url: str = DEFAULT_REGISTRY) -> dict[str, object]:
    if not url.startswith("https://"):
        raise CForgevError("El registro debe usar HTTPS")
    try:
        with urllib.request.urlopen(url, timeout=15) as response:
            raw = response.read(MAX_PACKAGE_BYTES + 1)
    except Exception as error:
        raise CForgevError(f"No se pudo consultar el registro: {error}") from error
    if len(raw) > MAX_PACKAGE_BYTES:
        raise CForgevError("Respuesta del registro demasiado grande")
    try: value = json.loads(raw.decode("utf-8"))
    except (UnicodeError, json.JSONDecodeError) as error:
        raise CForgevError(f"Índice del registro inválido: {error}") from error
    if not isinstance(value, dict) or value.get("format") not in {1, 2} or not isinstance(value.get("packages"), dict):
        raise CForgevError("Formato de registro C-Forge incompatible")
    return value


def search_registry(query: str, url: str = DEFAULT_REGISTRY) -> list[tuple[str, str, str]]:
    packages = fetch_registry(url)["packages"]
    assert isinstance(packages, dict)
    lowered = query.lower()
    result = []
    for name, metadata in sorted(packages.items()):
        if not isinstance(metadata, dict): continue
        description = str(metadata.get("description", ""))
        if lowered in name.lower() or lowered in description.lower():
            versions = metadata.get("versions", {})
            latest = sorted(versions, reverse=True)[0] if isinstance(versions, dict) and versions else ""
            result.append((name, latest, description))
    return result


def install_registry(root: Path, name: str, version: str | None = None,
                     url: str = DEFAULT_REGISTRY) -> Path:
    if not NAME_RE.fullmatch(name): raise CForgevError("Nombre de paquete inválido")
    index = fetch_registry(url); packages = index["packages"]; assert isinstance(packages, dict)
    metadata = packages.get(name)
    if not isinstance(metadata, dict): raise CForgevError(f"Paquete no encontrado: {name}")
    versions = metadata.get("versions", {})
    if not isinstance(versions, dict) or not versions: raise CForgevError(f"Paquete sin versiones: {name}")
    selected = version or max(versions, key=lambda item: tuple(int(part) for part in item.split("-")[0].split(".")))
    release = versions.get(selected)
    if not isinstance(release, dict): raise CForgevError(f"Versión no encontrada: {name}@{selected}")
    download_url, expected = str(release.get("url", "")), str(release.get("sha256", "")).lower()
    if not download_url.startswith("https://") or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise CForgevError("Metadatos de descarga inseguros")
    try:
        with urllib.request.urlopen(download_url, timeout=30) as response:
            payload = response.read(MAX_PACKAGE_BYTES + 1)
    except Exception as error:
        raise CForgevError(f"No se pudo descargar {name}: {error}") from error
    if len(payload) > MAX_PACKAGE_BYTES or hashlib.sha256(payload).hexdigest() != expected:
        raise CForgevError("El paquete excede el límite o su SHA-256 no coincide")
    if index.get("format") == 2:
        revocations_value = index.get("revocations", [])
        if not isinstance(revocations_value, list) or not all(isinstance(item, str) for item in revocations_value):
            raise CForgevError("Lista de revocaciones inválida")
        key_id = verify_package_signature(payload, release, name, selected, set(revocations_value))
        verify_publisher(index, metadata, key_id)
    destination = root / ".cforge" / "packages" / name / selected
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(suffix=".tar.gz") as temporary:
        temporary.write(payload); temporary.flush()
        with tarfile.open(temporary.name, "r:gz") as archive:
            base = destination.resolve()
            for member in archive.getmembers():
                target = (destination / member.name).resolve()
                if base != target and base not in target.parents:
                    raise CForgevError("Paquete rechazado: ruta fuera del destino")
                if member.issym() or member.islnk():
                    raise CForgevError("Paquete rechazado: enlaces no permitidos")
            archive.extractall(destination)
    add(root, name, str(destination))
    return destination


def build_package(root: Path, output: Path) -> tuple[Path, str]:
    manifest = _load(root)
    name, version = str(manifest.get("name", "")), str(manifest.get("version", ""))
    if not NAME_RE.fullmatch(name) or not VERSION_RE.fullmatch(version):
        raise CForgevError("El manifiesto requiere name y version semántica válidos")
    output.mkdir(parents=True, exist_ok=True)
    target = output / f"{name}-{version}.tar.gz"
    output_resolved = output.resolve()
    files = []
    for path in sorted(root.rglob("*")):
        if not path.is_file() or path.is_symlink():
            continue
        resolved = path.resolve()
        if output_resolved == resolved or output_resolved in resolved.parents:
            continue
        if ".cforge" in path.parts or ".git" in path.parts:
            continue
        files.append(path)
    with target.open("wb") as stream:
        with gzip.GzipFile(filename="", mode="wb", fileobj=stream, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
                for path in files:
                    info = archive.gettarinfo(str(path), str(Path(name) / path.relative_to(root)))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    info.mode = 0o755 if path.stat().st_mode & 0o111 else 0o644
                    with path.open("rb") as source:
                        archive.addfile(info, source)
    return target, hashlib.sha256(target.read_bytes()).hexdigest()


def _write_lock(root: Path, manifest: dict[str, object]) -> None:
    dependencies = manifest.get("dependencies", {})
    assert isinstance(dependencies, dict)
    locked: dict[str, object] = {"format": 1, "dependencies": {}}
    target = locked["dependencies"]
    assert isinstance(target, dict)
    for name, spec in sorted(dependencies.items()):
        if not isinstance(spec, dict):
            continue
        path = Path(str(spec.get("path", "")))
        digest = hashlib.sha256()
        files = sorted(path.rglob("*.cfv")) if path.is_dir() else [path]
        for file in files:
            if file.is_file():
                digest.update(file.read_bytes())
        target[name] = {"path": str(path), "sha256": digest.hexdigest()}
    (root / LOCKFILE).write_text(
        json.dumps(locked, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
    )
)CFV15DATA"},
        {R"CFV16DATA(cforge_vm.py)CFV16DATA", R"CFV17DATA("""Compilador de bytecode y máquina virtual alojada de C-Forge.

El formato es propio de C-Forge; esta primera versión se aloja en Python para
facilitar su auditoría y portabilidad. No ejecuta código extranjero.
"""

from __future__ import annotations

import json
import hashlib
import concurrent.futures
import math
import os
import platform
import socket
import struct
import subprocess
import queue
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from cforgev import CForgevError, ForgeOption, tokenize
from compilador_nativo import Parser, Program, StaticTypeAnalyzer, resolve_imports

BYTECODE_MAGIC = b"CFBC"
BYTECODE_VERSION = (1, 1)
BYTECODE_HEADER = struct.Struct(">4sBBQ32s")
MAX_BYTECODE_PAYLOAD = 256 * 1024 * 1024
MAX_BYTECODE_NESTING = 256
VALID_OPCODES = {
    "CONST", "LOAD", "STORE", "POP", "PRINT", "BUILD_LIST", "BUILD_TUPLE",
    "BUILD_SET", "BUILD_MAP",
    "UNARY", "BINARY", "INDEX", "FIELD", "METHOD", "CALL", "TRY",
    "JUMP", "JUMP_IF_FALSE", "RETURN", "HALT", "SET_FIELD", "AWAIT",
}


@dataclass(frozen=True)
class Instruction:
    op: str
    arg: Any = None
    line: int | None = None


@dataclass
class Chunk:
    name: str
    parameters: list[str] = field(default_factory=list)
    code: list[Instruction] = field(default_factory=list)
    is_async: bool = False
    current_line: int | None = field(default=None, repr=False)

    def emit(self, op: str, arg: Any = None, line: int | None = None) -> int:
        self.code.append(Instruction(op, arg, self.current_line if line is None else line))
        return len(self.code) - 1

    def patch(self, position: int, target: int) -> None:
        old = self.code[position]
        self.code[position] = Instruction(old.op, target, old.line)


@dataclass
class BytecodeProgram:
    main: Chunk
    functions: dict[str, Chunk]
    structures: dict[str, list[list[str]]] = field(default_factory=dict)
    classes: dict[str, dict[str, Any]] = field(default_factory=dict)


@dataclass
class VMTask:
    future: concurrent.futures.Future[Any]


@dataclass
class VMChannel:
    values: queue.Queue[Any] = field(default_factory=queue.Queue)
    closed: bool = False


def _encode_chunk(chunk: Chunk) -> dict[str, Any]:
    instructions = []
    for instruction in chunk.code:
        argument = instruction.arg
        if instruction.op == "TRY":
            protected, error_name, handler = argument
            argument = {"protected": _encode_chunk(protected), "error": error_name,
                        "handler": _encode_chunk(handler)}
        encoded = {"op": instruction.op, "arg": argument}
        if instruction.line is not None: encoded["line"] = instruction.line
        instructions.append(encoded)
    return {"name": chunk.name, "parameters": chunk.parameters,
            "async": chunk.is_async, "code": instructions}


def _decode_chunk(document: dict[str, Any], depth: int = 0) -> Chunk:
    if depth > MAX_BYTECODE_NESTING:
        raise CForgevError("Bytecode: anidamiento excesivo")
    if not isinstance(document, dict) or not isinstance(document.get("name"), str):
        raise CForgevError("Bytecode: chunk inválido")
    parameters = document.get("parameters", [])
    code = document.get("code", [])
    if not isinstance(parameters, list) or not all(isinstance(x, str) for x in parameters):
        raise CForgevError("Bytecode: parámetros inválidos")
    if not isinstance(code, list) or len(code) > 10_000_000:
        raise CForgevError("Bytecode: tabla de instrucciones inválida")
    asynchronous = document.get("async", False)
    if not isinstance(asynchronous, bool): raise CForgevError("Bytecode: marca async inválida")
    chunk = Chunk(document["name"], parameters, is_async=asynchronous)
    for encoded in code:
        if not isinstance(encoded, dict) or encoded.get("op") not in VALID_OPCODES:
            raise CForgevError("Bytecode: opcode desconocido o inválido")
        operation, argument = encoded["op"], encoded.get("arg")
        if operation == "TRY":
            if not isinstance(argument, dict): raise CForgevError("Bytecode: TRY inválido")
            if not isinstance(argument.get("error"), str):
                raise CForgevError("Bytecode: nombre de error TRY inválido")
            argument = (_decode_chunk(argument.get("protected"), depth + 1), argument["error"],
                        _decode_chunk(argument.get("handler"), depth + 1))
        elif operation in {"CALL", "METHOD"} and isinstance(argument, list):
            argument = tuple(argument)
        line = encoded.get("line")
        if line is not None and (not isinstance(line, int) or line < 1):
            raise CForgevError("Bytecode: línea de depuración inválida")
        chunk.emit(operation, argument, line)
    _verify_chunk(chunk)
    return chunk


def _verify_chunk(chunk: Chunk) -> None:
    """Rechaza operandos estructuralmente inválidos antes de ejecutar bytecode."""
    size = len(chunk.code)
    for offset, instruction in enumerate(chunk.code):
        op, argument = instruction.op, instruction.arg
        if op in {"JUMP", "JUMP_IF_FALSE"}:
            if not isinstance(argument, int) or isinstance(argument, bool) or not 0 <= argument <= size:
                raise CForgevError(
                    f"Bytecode: destino de salto inválido en {chunk.name}:{offset}"
                )
        elif op in {"BUILD_LIST", "BUILD_TUPLE", "BUILD_SET", "BUILD_MAP"}:
            if not isinstance(argument, int) or isinstance(argument, bool) or argument < 0:
                raise CForgevError(f"Bytecode: cantidad inválida para {op}")
        elif op in {"LOAD", "STORE", "SET_FIELD", "FIELD", "UNARY", "BINARY"}:
            if not isinstance(argument, str):
                raise CForgevError(f"Bytecode: operando inválido para {op}")
        elif op in {"CALL", "METHOD"}:
            if (not isinstance(argument, tuple) or len(argument) != 2
                    or not isinstance(argument[0], str)
                    or not isinstance(argument[1], int) or isinstance(argument[1], bool)
                    or argument[1] < 0):
                raise CForgevError(f"Bytecode: invocación inválida para {op}")
        elif op == "TRY":
            if (not isinstance(argument, tuple) or len(argument) != 3
                    or not isinstance(argument[0], Chunk)
                    or not isinstance(argument[1], str)
                    or not isinstance(argument[2], Chunk)):
                raise CForgevError("Bytecode: TRY inválido")


def serialize(program: BytecodeProgram) -> bytes:
    document = {
        "format": "C-Forge Bytecode",
        "version": list(BYTECODE_VERSION),
        "main": _encode_chunk(program.main),
        "functions": {name: _encode_chunk(chunk) for name, chunk in sorted(program.functions.items())},
        "structures": program.structures,
        "classes": {
            name: {"fields": definition["fields"],
                   "methods": {method: _encode_chunk(chunk) for method, chunk in sorted(definition["methods"].items())}}
            for name, definition in sorted(program.classes.items())
        },
    }
    payload = json.dumps(document, ensure_ascii=False, sort_keys=True,
                         separators=(",", ":")).encode("utf-8")
    return BYTECODE_HEADER.pack(BYTECODE_MAGIC, *BYTECODE_VERSION, len(payload),
                                hashlib.sha256(payload).digest()) + payload


def deserialize(data: bytes) -> BytecodeProgram:
    if len(data) < BYTECODE_HEADER.size: raise CForgevError("Bytecode: archivo truncado")
    magic, major, minor, length, expected = BYTECODE_HEADER.unpack_from(data)
    if magic != BYTECODE_MAGIC: raise CForgevError("Bytecode: firma CFBC inválida")
    if major != BYTECODE_VERSION[0] or minor > BYTECODE_VERSION[1]:
        raise CForgevError(f"Bytecode: versión {major}.{minor} incompatible")
    if length > MAX_BYTECODE_PAYLOAD:
        raise CForgevError("Bytecode: carga excede el límite de 256 MiB")
    payload = data[BYTECODE_HEADER.size:]
    if length != len(payload): raise CForgevError("Bytecode: longitud inválida")
    if hashlib.sha256(payload).digest() != expected:
        raise CForgevError("Bytecode: suma SHA-256 inválida")
    try: document = json.loads(payload)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise CForgevError("Bytecode: contenido JSON inválido") from error
    if document.get("format") != "C-Forge Bytecode": raise CForgevError("Bytecode: formato desconocido")
    if document.get("version") != [major, minor]:
        raise CForgevError("Bytecode: versión interna no coincide con la cabecera")
    functions = document.get("functions")
    if not isinstance(functions, dict): raise CForgevError("Bytecode: funciones inválidas")
    structures = document.get("structures", {})
    classes_document = document.get("classes", {})
    if not isinstance(structures, dict) or not isinstance(classes_document, dict):
        raise CForgevError("Bytecode: tabla de tipos inválida")
    classes = {}
    for name, definition in classes_document.items():
        if not isinstance(name, str) or not isinstance(definition, dict):
            raise CForgevError("Bytecode: clase inválida")
        methods = definition.get("methods", {})
        fields = definition.get("fields", [])
        if not isinstance(methods, dict) or not isinstance(fields, list):
            raise CForgevError("Bytecode: definición de clase inválida")
        classes[name] = {"fields": fields,
                         "methods": {method: _decode_chunk(chunk) for method, chunk in methods.items()}}
    return BytecodeProgram(_decode_chunk(document["main"]),
                           {name: _decode_chunk(chunk) for name, chunk in functions.items()},
                           structures, classes)


def save_bytecode(program: BytecodeProgram, path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(serialize(program)); return path


def load_bytecode(path: Path) -> BytecodeProgram:
    try: return deserialize(path.read_bytes())
    except OSError as error: raise CForgevError(f"No se pudo abrir {path}: {error}") from error


class BytecodeCompiler:
    """Baja el AST verificado a instrucciones de pila de C-Forge."""

    def compile(self, program: Program) -> BytecodeProgram:
        self.locations = program.locations
        functions: dict[str, Chunk] = {}
        for node in program.functions:
            chunk = Chunk(node[1], list(node[2]), is_async=bool(node[6]) if len(node) > 6 else False)
            self._statements(chunk, node[3])
            chunk.emit("CONST", None)
            chunk.emit("RETURN")
            functions[node[1]] = chunk
        structures: dict[str, list[list[str]]] = {}
        classes: dict[str, dict[str, Any]] = {}
        for statement in program.statements:
            if statement[0] == "structure":
                structures[statement[1]] = [list(field) for field in statement[2]]
            elif statement[0] == "class":
                methods = {}
                for method in statement[3]:
                    method_chunk = Chunk(f"{statement[1]}.{method[2]}", ["este", *method[3]])
                    self._statements(method_chunk, method[4])
                    method_chunk.emit("CONST", None); method_chunk.emit("RETURN")
                    methods[method[2]] = method_chunk
                classes[statement[1]] = {
                    "fields": [list(field) for field in statement[2]], "methods": methods
                }
        main = Chunk("<main>")
        self._statements(main, program.statements)
        main.emit("HALT")
        return BytecodeProgram(main, functions, structures, classes)

    def _statements(self, chunk: Chunk, statements: list[tuple]) -> None:
        for statement in statements:
            chunk.current_line = self.locations.get(id(statement), chunk.current_line)
            kind = statement[0]
            if kind == "let":
                self._expression(chunk, statement[3]); chunk.emit("STORE", statement[1])
            elif kind == "assign":
                self._expression(chunk, statement[2]); chunk.emit("STORE", statement[1])
            elif kind == "field_assign":
                self._expression(chunk, statement[3]); chunk.emit("SET_FIELD", statement[2])
            elif kind == "print":
                self._expression(chunk, statement[1]); chunk.emit("PRINT")
            elif kind == "expression":
                self._expression(chunk, statement[1]); chunk.emit("POP")
            elif kind == "return":
                self._expression(chunk, statement[1]); chunk.emit("RETURN")
            elif kind == "if":
                self._expression(chunk, statement[1])
                false_jump = chunk.emit("JUMP_IF_FALSE", -1)
                self._statements(chunk, statement[2])
                end_jump = chunk.emit("JUMP", -1)
                chunk.patch(false_jump, len(chunk.code))
                self._statements(chunk, statement[3])
                chunk.patch(end_jump, len(chunk.code))
            elif kind == "while":
                start = len(chunk.code)
                self._expression(chunk, statement[1])
                done = chunk.emit("JUMP_IF_FALSE", -1)
                self._statements(chunk, statement[2])
                chunk.emit("JUMP", start)
                chunk.patch(done, len(chunk.code))
            elif kind in {"gpu", "test"}:
                self._statements(chunk, statement[-1])
            elif kind in {"region", "unsafe"}:
                self._statements(chunk, statement[1])
            elif kind == "try":
                protected = Chunk(f"{chunk.name}:try")
                handler = Chunk(f"{chunk.name}:catch")
                self._statements(protected, statement[1]); protected.emit("CONST", None); protected.emit("RETURN")
                self._statements(handler, statement[3]); handler.emit("CONST", None); handler.emit("RETURN")
                chunk.emit("TRY", (protected, statement[2], handler))
            elif kind in {"structure", "class", "interface", "import", "universal_import"}:
                continue
            else:
                raise CForgevError(f"Bytecode 1.0 todavía no admite la sentencia '{kind}'")

    def _expression(self, chunk: Chunk, expression: tuple) -> None:
        kind = expression[0]
        if kind == "number":
            raw = expression[1]; chunk.emit("CONST", float(raw) if "." in raw else int(raw))
        elif kind == "string": chunk.emit("CONST", json.loads(expression[1]))
        elif kind == "bool": chunk.emit("CONST", expression[1])
        elif kind == "null": chunk.emit("CONST", None)
        elif kind == "variable": chunk.emit("LOAD", expression[1])
        elif kind == "list":
            for item in expression[1]: self._expression(chunk, item)
            chunk.emit("BUILD_LIST", len(expression[1]))
        elif kind == "tuple":
            for item in expression[1]: self._expression(chunk, item)
            chunk.emit("BUILD_TUPLE", len(expression[1]))
        elif kind == "set":
            for item in expression[1]: self._expression(chunk, item)
            chunk.emit("BUILD_SET", len(expression[1]))
        elif kind == "map":
            for key, value in expression[1]:
                self._expression(chunk, key); self._expression(chunk, value)
            chunk.emit("BUILD_MAP", len(expression[1]))
        elif kind == "unary":
            self._expression(chunk, expression[2]); chunk.emit("UNARY", expression[1])
        elif kind == "await":
            self._expression(chunk, expression[1]); chunk.emit("AWAIT")
        elif kind == "binary":
            self._expression(chunk, expression[2]); self._expression(chunk, expression[3])
            chunk.emit("BINARY", expression[1])
        elif kind == "index":
            self._expression(chunk, expression[1]); self._expression(chunk, expression[2]); chunk.emit("INDEX")
        elif kind == "field":
            self._expression(chunk, expression[1]); chunk.emit("FIELD", expression[2])
        elif kind == "method_call":
            self._expression(chunk, expression[1])
            for argument in expression[3]: self._expression(chunk, argument)
            chunk.emit("METHOD", (expression[2], len(expression[3])))
        elif kind == "call":
            for argument in expression[2]: self._expression(chunk, argument)
            chunk.emit("CALL", (expression[1], len(expression[2])))
        else:
            raise CForgevError(f"Bytecode 1.0 todavía no admite la expresión '{kind}'")


class VirtualMachine:
    """VM de pila determinista, con límite de instrucciones y ámbitos aislados."""

    def __init__(self, program: BytecodeProgram, output: Callable[[str], None] = print,
                 max_steps: int = 10_000_000,
                 trace: Callable[[str, int, Instruction, dict[str, Any]], None] | None = None,
                 base_dir: Path | None = None, permissions: set[str] | None = None) -> None:
        self.program, self.output, self.max_steps = program, output, max_steps
        self.globals: dict[str, Any] = {}
        self.steps = 0
        self._step_lock = threading.Lock()
        self._debug_local = threading.local()
        self._executor = concurrent.futures.ThreadPoolExecutor(
            max_workers=max(2, min(32, os.cpu_count() or 2)),
            thread_name_prefix="cforge-vm",
        )
        self.trace = trace
        self.base_dir = (base_dir or Path.cwd()).resolve()
        self.permissions = permissions if permissions is not None else {
            item.strip() for item in os.environ.get(
                "CFORGE_PERMISSIONS", "fs-read,fs-write"
            ).split(",") if item.strip()
        }
        self.builtins: dict[str, Callable[..., Any]] = {
            "longitud": len, "len": len, "raiz": math.sqrt, "potencia": pow,
            "absoluto": abs, "redondear": round, "a_texto": str,
            "a_numero": lambda value: float(value) if "." in str(value) else int(value),
            "agregar": self._append, "afirmar": self._assert,
            "mover": lambda value: value, "prestar": lambda value: value,
            "prestar_mut": lambda value: value, "soltar_prestamo": lambda value: None,
            "destruir": lambda value: None,
            "algunos": lambda value: ForgeOption(True, value),
            "ninguno": lambda: ForgeOption(False),
            "es_algunos": lambda value: isinstance(value, ForgeOption) and value.has_value,
            "desenvolver": self._unwrap,
            "file_read": self._file_read, "leer_archivo": self._file_read,
            "file_write": self._file_write, "escribir_archivo": self._file_write,
            "file_append": self._file_append, "existe_archivo": self._file_exists,
            "sys_run": self._sys_run, "sys_info": self._sys_info,
            "net_send": self._net_send, "net_listen": self._net_listen,
            "tarea": self._task, "esperar": self._await_task,
            "cancelar": self._cancel_task, "canal": self._channel,
            "enviar": self._send_channel, "recibir": self._receive_channel,
            "cerrar_canal": self._close_channel,
        }

    def run(self) -> Any:
        return self._run_chunk(self.program.main, self.globals)

    def debug_frames(self) -> list[dict[str, Any]]:
        """Instantánea inmutable de la pila del hilo que está ejecutando el trace."""
        frames = getattr(self._debug_local, "frames", [])
        return [{"name": frame["name"], "line": frame["line"],
                 "offset": frame["offset"], "scope": dict(frame["scope"])}
                for frame in frames]

    def _run_chunk(self, chunk: Chunk, scope: dict[str, Any]) -> Any:
        stack: list[Any] = []; ip = 0
        frames = getattr(self._debug_local, "frames", None)
        if frames is None:
            frames = []; self._debug_local.frames = frames
        frame = {"name": chunk.name, "line": 1, "offset": 0, "scope": scope}
        frames.append(frame)
        try:
            while ip < len(chunk.code):
                with self._step_lock:
                    self.steps += 1
                    if self.steps > self.max_steps: raise CForgevError("VM: límite de instrucciones excedido")
                instruction = chunk.code[ip]; ip += 1
                frame["line"], frame["offset"] = instruction.line or frame["line"], ip - 1
                if self.trace is not None:
                    self.trace(chunk.name, ip - 1, instruction, dict(scope))
                op, arg = instruction.op, instruction.arg
                result, returned = self._execute_instruction(op, arg, stack, scope, chunk, ip)
                if returned: return result
                if op == "JUMP": ip = arg
                elif op == "JUMP_IF_FALSE" and result is not None: ip = result
            return None
        finally:
            frames.pop()

    def _execute_instruction(self, op: str, arg: Any, stack: list[Any],
                             scope: dict[str, Any], chunk: Chunk, ip: int) -> tuple[Any, bool]:
            if op == "CONST": stack.append(arg)
            elif op == "LOAD":
                if arg in scope: stack.append(scope[arg])
                elif arg in self.globals: stack.append(self.globals[arg])
                else: raise CForgevError(f"VM: variable desconocida '{arg}'")
            elif op == "STORE": scope[arg] = stack.pop()
            elif op == "SET_FIELD":
                owner = scope.get("este")
                if not isinstance(owner, dict) or arg not in owner:
                    raise CForgevError(f"VM: campo desconocido '{arg}'")
                owner[arg] = stack.pop()
            elif op == "POP": stack.pop()
            elif op == "PRINT": self.output(self._display(stack.pop()))
            elif op == "BUILD_LIST":
                values = stack[-arg:] if arg else []; self._drop(stack, arg); stack.append(values)
            elif op == "BUILD_TUPLE":
                values = stack[-arg:] if arg else []; self._drop(stack, arg); stack.append(tuple(values))
            elif op == "BUILD_SET":
                values = stack[-arg:] if arg else []; self._drop(stack, arg)
                try: stack.append(set(values))
                except TypeError as error: raise CForgevError("VM: los elementos del conjunto deben ser inmutables") from error
            elif op == "BUILD_MAP":
                values = stack[-2 * arg:] if arg else []; self._drop(stack, 2 * arg)
                stack.append({values[i]: values[i + 1] for i in range(0, len(values), 2)})
            elif op == "UNARY": stack.append((not stack.pop()) if arg == "no" else -stack.pop())
            elif op == "BINARY":
                right, left = stack.pop(), stack.pop(); stack.append(self._binary(arg, left, right))
            elif op == "INDEX":
                key, owner = stack.pop(), stack.pop(); stack.append(owner[key])
            elif op == "FIELD": stack.append(self._field(stack.pop(), arg))
            elif op == "METHOD":
                name, count = arg; args = stack[-count:] if count else []; self._drop(stack, count)
                owner = stack.pop(); stack.append(self._method(owner, name, args))
            elif op == "CALL":
                name, count = arg; args = stack[-count:] if count else []; self._drop(stack, count)
                stack.append(self._call(name, args))
            elif op == "AWAIT": stack.append(self._await_task(stack.pop()))
            elif op == "TRY":
                protected, error_name, handler = arg
                try:
                    self._run_chunk(protected, scope)
                except Exception as error:
                    scope[error_name] = str(error)
                    self._run_chunk(handler, scope)
            elif op == "JUMP": pass
            elif op == "JUMP_IF_FALSE":
                if not stack.pop(): return arg, False
            elif op == "RETURN": return stack.pop(), True
            elif op == "HALT": return (stack[-1] if stack else None), True
            else: raise CForgevError(f"VM: opcode desconocido '{op}'")
            return None, False

    @staticmethod
    def _drop(stack: list[Any], count: int, keep: bool = False) -> None:
        if count:
            if keep:
                value = stack[-count:]
                del stack[-count:]
                stack.append(value)
            else: del stack[-count:]

    def _call(self, name: str, args: list[Any]) -> Any:
        if name in self.builtins: return self.builtins[name](*args)
        definition = self.program.structures.get(name)
        class_definition = self.program.classes.get(name)
        fields = definition if definition is not None else (
            class_definition["fields"] if class_definition is not None else None
        )
        if fields is not None:
            if len(args) != len(fields):
                raise CForgevError(f"VM: '{name}' requiere {len(fields)} campos")
            value = {field[0]: argument for field, argument in zip(fields, args)}
            if class_definition is not None: value["__clase"] = name
            return value
        if name not in self.program.functions: raise CForgevError(f"VM: función desconocida '{name}'")
        chunk = self.program.functions[name]
        if len(args) != len(chunk.parameters): raise CForgevError(f"VM: '{name}' requiere {len(chunk.parameters)} argumentos")
        scope = dict(zip(chunk.parameters, args))
        if chunk.is_async:
            return VMTask(self._executor.submit(self._run_chunk, chunk, scope))
        return self._run_chunk(chunk, scope)

    @staticmethod
    def _binary(op: str, left: Any, right: Any) -> Any:
        if op == "+": return left + right
        if op == "-": return left - right
        if op == "*": return left * right
        if op == "/":
            if right == 0: raise CForgevError("VM: no se puede dividir por cero")
            return left / right
        if op == "==": return left == right
        if op == "!=": return left != right
        if op == ">": return left > right
        if op == ">=": return left >= right
        if op == "<": return left < right
        if op == "<=": return left <= right
        if op == "y": return bool(left and right)
        if op == "o": return bool(left or right)
        raise CForgevError(f"VM: operador desconocido '{op}'")

    @staticmethod
    def _field(owner: Any, name: str) -> Any:
        if name in {"length", "len"}: return len(owner)
        if isinstance(owner, dict) and name in owner: return owner[name]
        raise CForgevError(f"VM: miembro desconocido '{name}'")

    def _method(self, owner: Any, name: str, args: list[Any]) -> Any:
        if name in {"append", "push", "agregar"} and isinstance(owner, list) and len(args) == 1:
            owner.append(args[0]); return owner
        if name in {"length", "len"} and not args: return len(owner)
        if isinstance(owner, dict) and "__clase" in owner:
            definition = self.program.classes.get(owner["__clase"])
            method = definition["methods"].get(name) if definition else None
            if method is None: raise CForgevError(f"VM: método desconocido '{name}'")
            if len(args) + 1 != len(method.parameters):
                raise CForgevError(f"VM: '{name}' recibió una cantidad incorrecta de argumentos")
            return self._run_chunk(method, dict(zip(method.parameters, [owner, *args])))
        raise CForgevError(f"VM: método incompatible '{name}'")

    @staticmethod
    def _append(owner: list[Any], value: Any) -> list[Any]: owner.append(value); return owner

    @staticmethod
    def _assert(condition: Any, message: str = "afirmación fallida") -> bool:
        if not condition: raise CForgevError(message)
        return True

    @staticmethod
    def _unwrap(value: Any) -> Any:
        if not isinstance(value, ForgeOption): raise CForgevError("VM: desenvolver requiere una opcion")
        if not value.has_value: raise CForgevError("VM: no se puede desenvolver ninguno")
        return value.value

    def _permit(self, permission: str) -> None:
        if permission not in self.permissions:
            raise CForgevError(
                f"VM Sandbox: falta permiso '{permission}'. "
                "Usa CFORGE_PERMISSIONS para concederlo explícitamente"
            )

    def _path(self, raw: Any) -> Path:
        if not isinstance(raw, str): raise CForgevError("VM: la ruta debe ser texto")
        path = (self.base_dir / raw).resolve()
        try: path.relative_to(self.base_dir)
        except ValueError as error: raise CForgevError("VM Sandbox: ruta fuera del proyecto") from error
        return path

    def _file_read(self, raw: Any) -> str:
        self._permit("fs-read")
        try: return self._path(raw).read_text(encoding="utf-8")
        except OSError as error: raise CForgevError(f"VM: no se pudo leer archivo: {error}") from error

    def _file_write(self, raw: Any, content: Any) -> None:
        self._permit("fs-write")
        if not isinstance(content, str): raise CForgevError("VM: el contenido debe ser texto")
        path = self._path(raw); path.parent.mkdir(parents=True, exist_ok=True)
        try: path.write_text(content, encoding="utf-8")
        except OSError as error: raise CForgevError(f"VM: no se pudo escribir archivo: {error}") from error

    def _file_append(self, raw: Any, content: Any) -> None:
        self._permit("fs-write")
        if not isinstance(content, str): raise CForgevError("VM: el contenido debe ser texto")
        path = self._path(raw); path.parent.mkdir(parents=True, exist_ok=True)
        try:
            with path.open("a", encoding="utf-8") as stream: stream.write(content)
        except OSError as error: raise CForgevError(f"VM: no se pudo anexar archivo: {error}") from error

    def _file_exists(self, raw: Any) -> bool:
        self._permit("fs-read"); return self._path(raw).exists()

    def _sys_run(self, command: Any) -> dict[str, Any]:
        self._permit("process")
        if not isinstance(command, str): raise CForgevError("VM: sys_run requiere texto")
        try:
            completed = subprocess.run(command, shell=True, cwd=self.base_dir,
                                       capture_output=True, text=True, timeout=30)
        except (OSError, subprocess.SubprocessError) as error:
            raise CForgevError(f"VM: sys_run falló: {error}") from error
        return {"estado": completed.returncode, "salida": completed.stdout,
                "error": completed.stderr}

    @staticmethod
    def _sys_info() -> dict[str, Any]:
        return {"cpu": platform.machine(), "nucleos": os.cpu_count() or 1,
                "sistema": platform.system()}

    def _net_send(self, host: Any, port: Any, content: Any) -> int:
        self._permit("network")
        if not isinstance(host, str) or not isinstance(content, str):
            raise CForgevError("VM: net_send requiere host, puerto y texto")
        data = content.encode("utf-8")
        try:
            with socket.create_connection((host, int(port)), timeout=5) as connection:
                connection.sendall(data)
        except (OSError, ValueError) as error:
            raise CForgevError(f"VM: net_send falló: {error}") from error
        return len(data)

    def _net_listen(self, port: Any, timeout_ms: Any = 5000) -> str:
        self._permit("network")
        try:
            with socket.socket() as server:
                server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                server.bind(("127.0.0.1", int(port))); server.listen(1)
                server.settimeout(float(timeout_ms) / 1000.0)
                connection, _ = server.accept()
                with connection: return connection.recv(16 * 1024 * 1024).decode("utf-8")
        except (OSError, ValueError, UnicodeError) as error:
            raise CForgevError(f"VM: net_listen falló: {error}") from error

    def _task(self, function_name: Any, arguments: Any = None) -> VMTask:
        if not isinstance(function_name, str) or function_name not in self.program.functions:
            raise CForgevError("VM: tarea requiere el nombre de una función C-Forge")
        if arguments is None: arguments = []
        if not isinstance(arguments, list): raise CForgevError("VM: los argumentos de tarea deben ser una lista")
        chunk = self.program.functions[function_name]
        if len(arguments) != len(chunk.parameters):
            raise CForgevError(f"VM: '{function_name}' requiere {len(chunk.parameters)} argumentos")
        return VMTask(self._executor.submit(
            self._run_chunk, chunk, dict(zip(chunk.parameters, list(arguments)))
        ))

    @staticmethod
    def _await_task(task: Any, timeout_ms: Any = None) -> Any:
        if not isinstance(task, VMTask): raise CForgevError("VM: esperar requiere una tarea")
        timeout = None if timeout_ms is None else float(timeout_ms) / 1000.0
        try: return task.future.result(timeout=timeout)
        except concurrent.futures.TimeoutError as error: raise CForgevError("VM: tiempo de espera agotado") from error
        except concurrent.futures.CancelledError as error: raise CForgevError("VM: tarea cancelada") from error

    @staticmethod
    def _cancel_task(task: Any) -> bool:
        if not isinstance(task, VMTask): raise CForgevError("VM: cancelar requiere una tarea")
        return task.future.cancel()

    @staticmethod
    def _channel(capacity: Any = 0) -> VMChannel:
        size = int(capacity)
        if size < 0: raise CForgevError("VM: capacidad de canal inválida")
        return VMChannel(queue.Queue(maxsize=size))

    @staticmethod
    def _send_channel(channel: Any, value: Any, timeout_ms: Any = None) -> None:
        if not isinstance(channel, VMChannel): raise CForgevError("VM: enviar requiere un canal")
        if channel.closed: raise CForgevError("VM: canal cerrado")
        timeout = None if timeout_ms is None else float(timeout_ms) / 1000.0
        try: channel.values.put(value, timeout=timeout)
        except queue.Full as error: raise CForgevError("VM: canal lleno") from error

    @staticmethod
    def _receive_channel(channel: Any, timeout_ms: Any = None) -> Any:
        if not isinstance(channel, VMChannel): raise CForgevError("VM: recibir requiere un canal")
        timeout = None if timeout_ms is None else float(timeout_ms) / 1000.0
        try: return channel.values.get(timeout=timeout)
        except queue.Empty as error:
            if channel.closed: raise CForgevError("VM: canal cerrado y vacío") from error
            raise CForgevError("VM: tiempo de recepción agotado") from error

    @staticmethod
    def _close_channel(channel: Any) -> None:
        if not isinstance(channel, VMChannel): raise CForgevError("VM: cerrar_canal requiere un canal")
        channel.closed = True

    @staticmethod
    def _display(value: Any) -> str:
        if isinstance(value, ForgeOption):
            return f"algunos({VirtualMachine._display(value.value)})" if value.has_value else "ninguno"
        if value is True: return "verdadero"
        if value is False: return "falso"
        if value is None: return "nulo"
        if isinstance(value, tuple):
            suffix = "," if len(value) == 1 else ""
            return "(" + ", ".join(VirtualMachine._display(item) for item in value) + suffix + ")"
        if isinstance(value, set):
            return "conjunto(" + ", ".join(sorted(VirtualMachine._display(item) for item in value)) + ")"
        return str(value)


def compile_source(source: str) -> BytecodeProgram:
    program = Parser(tokenize(source)).program()
    StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
    return BytecodeCompiler().compile(program)


def compile_file(path: Path) -> BytecodeProgram:
    try: source = path.read_text(encoding="utf-8")
    except OSError as error: raise CForgevError(f"No se pudo abrir {path}: {error.strerror or error}") from error
    program = resolve_imports(Parser(tokenize(source)).program(), path.resolve().parent, set())
    StaticTypeAnalyzer().analyze(program)
    from cforge_memory import MemorySafetyAnalyzer
    MemorySafetyAnalyzer().analyze(program)
    return BytecodeCompiler().compile(program)


def execute_file(path: Path, output: Callable[[str], None] = print) -> VirtualMachine:
    if path.suffix == ".cfb":
        program = load_bytecode(path)
    else:
        program = compile_file(path)
    vm = VirtualMachine(program, output, base_dir=path.resolve().parent)
    vm.run(); return vm


def disassemble(program: BytecodeProgram) -> str:
    chunks = [program.main, *program.functions.values()]; lines: list[str] = []
    for chunk in chunks:
        lines.append(f"== {chunk.name}({', '.join(chunk.parameters)}) ==")
        lines.extend(f"{index:04d} {item.op:<14} {item.arg!r}" for index, item in enumerate(chunk.code))
    return "\n".join(lines)
)CFV17DATA"},
        {R"CFV18DATA(cforge_memory.py)CFV18DATA", R"CFV19DATA("""Análisis estático de ownership, préstamos y efectos interprocedurales."""
from __future__ import annotations
from dataclasses import dataclass, field
from cforgev import CForgevError

@dataclass
class MemoryState:
    moved: bool = False
    shared: int = 0
    mutable: bool = False
    owner: str | None = None
    borrow_kind: str | None = None
    destroyed: bool = False
    owns: set[str] = field(default_factory=set)
    parameter: bool = False
    type_name: str = "cualquiera"
    def clone(self):
        return MemoryState(self.moved, self.shared, self.mutable, self.owner,
                           self.borrow_kind, self.destroyed, set(self.owns), self.parameter,
                           self.type_name)


@dataclass(frozen=True)
class FunctionMemorySummary:
    consumed: frozenset[int] = frozenset()
    returned_borrow: tuple[int, str] | None = None

class MemorySafetyAnalyzer:
    """Rechaza uso tras movimiento y alias mutables antes de generar código."""
    def analyze(self, program):
        self.unsafe_depth = 0
        self.functions = {function[1]: function for function in program.functions}
        constructors = {}
        for statement in program.statements:
            if statement[0] in {"structure", "class"}:
                consumed = frozenset(
                    index for index, (_, field_type) in enumerate(statement[2])
                    if not self.copy_type(field_type)
                )
                constructors[statement[1]] = FunctionMemorySummary(consumed)
        self.constructor_names = set(constructors)
        self.summaries = self.build_summaries(program.functions, constructors)
        states = {}
        self.statements(program.statements, states)
        for function in program.functions:
            local = {name: state.clone() for name, state in states.items()}
            local.update({parameter: MemoryState(parameter=True) for parameter in function[2]})
            self.statements(function[3], local)

    @staticmethod
    def copy_type(type_name):
        return type_name in {"numero", "booleano", "texto", "nulo", "cualquiera"}

    def build_summaries(self, functions, initial=None):
        summaries = dict(initial or {})
        summaries.update({function[1]: FunctionMemorySummary() for function in functions})
        for _ in range(max(1, len(functions) + 1)):
            changed = False
            for function in functions:
                summary = self.summarize_function(function, summaries)
                if summaries[function[1]] != summary:
                    summaries[function[1]] = summary; changed = True
            if not changed: break
        return summaries

    def summarize_function(self, function, summaries):
        parameters = {name: index for index, name in enumerate(function[2])}
        parameter_types = function[4] if len(function) > 4 else []
        # Los valores nominales se pasan por valor. Las colecciones mantienen la
        # semántica histórica de vista prestada; moverlas exige `mover(...)`.
        consumed: set[int] = {
            index for index, type_name in enumerate(parameter_types)
            if type_name in self.constructor_names
        }
        returned: tuple[int, str] | None = None

        def visit_expression(expression, is_return=False):
            nonlocal returned
            kind = expression[0]
            if kind in {"number", "string", "bool", "null", "variable"}: return
            if kind in {"list", "tuple", "set"}:
                for item in expression[1]: visit_expression(item)
                return
            if kind == "map":
                for key, value in expression[1]: visit_expression(key); visit_expression(value)
                return
            if kind == "call":
                name, arguments = expression[1], expression[2]
                if name in {"mover", "destruir"} and len(arguments) == 1 and arguments[0][0] == "variable":
                    index = parameters.get(arguments[0][1])
                    if index is not None: consumed.add(index)
                if is_return and name in {"prestar", "prestar_mut"} and len(arguments) == 1 and arguments[0][0] == "variable":
                    index = parameters.get(arguments[0][1])
                    if index is None:
                        self.error("una función no puede devolver un préstamo de una variable local")
                    candidate = (index, "mutable" if name == "prestar_mut" else "shared")
                    if returned is not None and returned != candidate:
                        self.error("la función devuelve préstamos con tiempos de vida incompatibles")
                    returned = candidate
                callee = summaries.get(name)
                if callee:
                    for position in callee.consumed:
                        if position < len(arguments) and arguments[position][0] == "variable":
                            outer = parameters.get(arguments[position][1])
                            if outer is not None: consumed.add(outer)
                    if is_return and callee.returned_borrow:
                        position, borrow_kind = callee.returned_borrow
                        if position < len(arguments) and arguments[position][0] == "variable":
                            outer = parameters.get(arguments[position][1])
                            if outer is None:
                                self.error("un préstamo devuelto no puede referirse a una variable local")
                            candidate = (outer, borrow_kind)
                            if returned is not None and returned != candidate:
                                self.error("la función devuelve préstamos con tiempos de vida incompatibles")
                            returned = candidate
                for argument in arguments: visit_expression(argument)
                return
            if kind == "unary": visit_expression(expression[2]); return
            if kind == "await": visit_expression(expression[1], is_return); return
            if kind == "binary": visit_expression(expression[2]); visit_expression(expression[3]); return
            if kind in {"field", "index"}:
                visit_expression(expression[1])
                if kind == "index": visit_expression(expression[2])
                return
            if kind == "method_call":
                visit_expression(expression[1])
                for argument in expression[3]: visit_expression(argument)

        def visit_statements(statements):
            for statement in statements:
                kind = statement[0]
                if kind == "let": visit_expression(statement[3])
                elif kind == "assign": visit_expression(statement[2])
                elif kind == "field_assign": visit_expression(statement[3])
                elif kind in {"print", "expression"}: visit_expression(statement[1])
                elif kind == "return": visit_expression(statement[1], True)
                elif kind == "if": visit_expression(statement[1]); visit_statements(statement[2]); visit_statements(statement[3])
                elif kind == "while": visit_expression(statement[1]); visit_statements(statement[2])
                elif kind == "try": visit_statements(statement[1]); visit_statements(statement[3])
                elif kind in {"gpu", "region", "unsafe"}: visit_statements(statement[1])
                elif kind == "test": visit_statements(statement[2])

        visit_statements(function[3])
        if returned and returned[0] in consumed:
            self.error(f"'{function[1]}' no puede consumir y devolver prestado el mismo parámetro")
        return FunctionMemorySummary(frozenset(consumed), returned)

    def error(self, message):
        raise CForgevError(f"Seguridad de memoria: {message}")

    def state(self, name, states):
        if name not in states: self.error(f"variable desconocida '{name}'")
        return states[name]

    def read(self, name, states):
        state = self.state(name, states)
        if (state.moved or state.destroyed) and not self.unsafe_depth:
            self.error(f"uso después de mover la variable '{name}'")
        return state

    def release_alias(self, name, states):
        alias = states.get(name)
        if alias is None or alias.owner is None or alias.borrow_kind is None:
            return False
        owner = states.get(alias.owner)
        if owner is not None:
            if alias.borrow_kind == "mutable": owner.mutable = False
            elif owner.shared > 0: owner.shared -= 1
        alias.owner = None; alias.borrow_kind = None; alias.destroyed = True
        return True

    def consume(self, name, states, action="mover"):
        state = self.read(name, states)
        if state.shared or state.mutable:
            self.error(f"no se puede {action} '{name}' mientras está prestada")
        state.moved = True
        state.destroyed = action == "destruir"
        if state.destroyed: state.owns.clear()

    def owned_references(self, expression):
        kind = expression[0]
        if kind == "variable": return {expression[1]}
        if kind in {"list", "tuple", "set"}:
            result = set()
            for item in expression[1]: result.update(self.owned_references(item))
            return result
        if kind == "map":
            result = set()
            for key, value in expression[1]:
                result.update(self.owned_references(key))
                result.update(self.owned_references(value))
            return result
        if kind == "call" and expression[1] in self.constructor_names:
            summary = self.summaries[expression[1]]
            return {
                expression[2][position][1]
                for position in summary.consumed
                if position < len(expression[2]) and expression[2][position][0] == "variable"
            }
        return set()

    def expression_type(self, expression, states):
        """Conserva el tipo mínimo necesario para decidir copia frente a movimiento."""
        kind = expression[0]
        if kind == "variable":
            return self.state(expression[1], states).type_name
        if kind == "call" and expression[1] in self.constructor_names:
            return expression[1]
        if kind in {"list", "map", "tuple", "set"}:
            return kind
        if kind == "string":
            return "texto"
        if kind == "number":
            return "numero"
        if kind == "bool":
            return "booleano"
        if kind == "null":
            return "nulo"
        return "cualquiera"

    def transfer_variable_initializer(self, expression, declared_type, states):
        """Una asignación de un valor no copiable transfiere ownership implícitamente."""
        if expression[0] != "variable":
            return
        source_name = expression[1]
        source = self.state(source_name, states)
        effective_type = declared_type or source.type_name
        if not self.copy_type(effective_type):
            self.consume(source_name, states)

    def transfer_aggregate_values(self, expression, states):
        """Transfiere, en orden, valores no copiables almacenados en colecciones."""
        kind = expression[0]
        if kind in {"list", "tuple", "set"}:
            values = expression[1]
        elif kind == "map":
            values = [item for pair in expression[1] for item in pair]
        else:
            return
        for value in values:
            if value[0] == "variable":
                state = self.state(value[1], states)
                if not self.copy_type(state.type_name):
                    self.consume(value[1], states)
            else:
                self.transfer_aggregate_values(value, states)

    def reaches(self, source, target, states, visited=None):
        if source == target: return True
        visited = set() if visited is None else visited
        if source in visited: return False
        visited.add(source)
        state = states.get(source)
        if state is None or state.destroyed: return False
        return any(self.reaches(child, target, states, visited) for child in state.owns)

    def attach_ownership(self, owner, children, states, transferred=False):
        state = self.state(owner, states)
        for child in children:
            if transferred: self.state(child, states)
            else: self.read(child, states)
            if not self.unsafe_depth and self.reaches(child, owner, states):
                self.error(f"ciclo de ownership entre '{owner}' y '{child}'")
            state.owns.add(child)

    def returned_alias(self, expression):
        if expression[0] != "call": return None
        name, arguments = expression[1], expression[2]
        if name in {"prestar", "prestar_mut"} and len(arguments) == 1 and arguments[0][0] == "variable":
            return arguments[0][1], "mutable" if name == "prestar_mut" else "shared"
        summary = self.summaries.get(name)
        if summary and summary.returned_borrow:
            position, borrow_kind = summary.returned_borrow
            if position < len(arguments) and arguments[position][0] == "variable":
                return arguments[position][1], borrow_kind
        return None

    def statements(self, statements, states):
        for statement in statements:
            kind = statement[0]
            if kind == "let":
                self.expression(statement[3], states)
                expression = statement[3]
                declared_type = statement[2]
                inferred_type = declared_type or self.expression_type(expression, states)
                self.transfer_variable_initializer(expression, inferred_type, states)
                self.transfer_aggregate_values(expression, states)
                alias = MemoryState(type_name=inferred_type)
                borrowed = self.returned_alias(expression)
                if borrowed:
                    alias.owner, alias.borrow_kind = borrowed
                    if not (expression[0] == "call" and expression[1] in {"prestar", "prestar_mut"}):
                        owner = self.read(alias.owner, states)
                        if alias.borrow_kind == "mutable":
                            if owner.mutable or owner.shared: self.error(f"'{alias.owner}' ya está prestada")
                            owner.mutable = True
                        else:
                            if owner.mutable: self.error(f"'{alias.owner}' ya tiene un préstamo mutable")
                            owner.shared += 1
                alias.owns = self.owned_references(expression)
                states[statement[1]] = alias
                self.attach_ownership(
                    statement[1], alias.owns, states,
                    transferred=(
                        (expression[0] == "call" and expression[1] in self.constructor_names)
                        or (
                            expression[0] == "variable"
                            and not self.copy_type(inferred_type)
                        )
                        or expression[0] in {"list", "map", "tuple", "set"}
                    ),
                )
            elif kind == "assign":
                state = self.state(statement[1], states)
                if state.owner is not None:
                    self.error(f"no se puede reasignar el préstamo '{statement[1]}'")
                if state.shared or state.mutable:
                    self.error(f"no se puede reasignar '{statement[1]}' mientras está prestada")
                self.expression(statement[2], states)
                self.transfer_variable_initializer(statement[2], state.type_name, states)
                self.transfer_aggregate_values(statement[2], states)
                state.moved = False; state.destroyed = False
                state.owns.clear()
                self.attach_ownership(statement[1], self.owned_references(statement[2]), states)
            elif kind in {"print", "return", "expression"}:
                self.expression(statement[1], states)
            elif kind == "if":
                self.expression(statement[1], states)
                yes = {n: s.clone() for n, s in states.items()}
                no = {n: s.clone() for n, s in states.items()}
                self.statements(statement[2], yes); self.statements(statement[3], no)
                for name in states.keys() & yes.keys() & no.keys():
                    states[name].moved = yes[name].moved or no[name].moved
                    states[name].shared = max(yes[name].shared, no[name].shared)
                    states[name].mutable = yes[name].mutable or no[name].mutable
            elif kind == "while":
                self.expression(statement[1], states)
                loop = {n: s.clone() for n, s in states.items()}
                self.statements(statement[2], loop)
                for name in states.keys() & loop.keys():
                    if loop[name].moved and not states[name].moved:
                        self.error(f"'{name}' puede moverse dentro de un ciclo")
            elif kind == "try":
                protected = {n: s.clone() for n, s in states.items()}
                handler = {n: s.clone() for n, s in states.items()}
                handler[statement[2]] = MemoryState()
                self.statements(statement[1], protected); self.statements(statement[3], handler)
                for name in states.keys() & protected.keys() & handler.keys():
                    states[name].moved = protected[name].moved or handler[name].moved
            elif kind in {"gpu", "test"}:
                self.statements(statement[1] if kind == "gpu" else statement[2], states)
            elif kind == "region":
                previous = {name: state.clone() for name, state in states.items()}
                self.statements(statement[1], states)
                for name in list(states):
                    if name not in previous:
                        self.release_alias(name, states)
                        del states[name]
                    else:
                        states[name].shared = previous[name].shared
                        states[name].mutable = previous[name].mutable
            elif kind == "unsafe":
                self.unsafe_depth += 1
                try: self.statements(statement[1], states)
                finally: self.unsafe_depth -= 1
            elif kind == "universal_import":
                states[statement[2]] = MemoryState()

    def expression(self, expression, states):
        kind = expression[0]
        if kind == "variable": self.read(expression[1], states); return
        if kind in {"number", "string", "bool", "null"}: return
        if kind in {"list", "tuple", "set"}:
            for item in expression[1]: self.expression(item, states)
            return
        if kind == "map":
            for key, value in expression[1]:
                self.expression(key, states); self.expression(value, states)
            return
        if kind == "call":
            name, arguments = expression[1], expression[2]
            if name in {"mover", "prestar", "prestar_mut", "soltar_prestamo", "destruir"}:
                if len(arguments) != 1 or arguments[0][0] != "variable":
                    self.error(f"{name} requiere el nombre de una variable")
                variable = arguments[0][1]; state = self.state(variable, states)
                if name != "soltar_prestamo": self.read(variable, states)
                if name == "destruir" and self.release_alias(variable, states):
                    return
                if name in {"mover", "destruir"}:
                    self.consume(variable, states, name)
                elif name == "prestar":
                    if state.mutable: self.error(f"'{variable}' ya tiene un préstamo mutable")
                    state.shared += 1
                elif name == "prestar_mut":
                    if state.mutable or state.shared: self.error(f"'{variable}' ya está prestada")
                    state.mutable = True
                elif self.release_alias(variable, states): pass
                elif state.mutable: state.mutable = False
                elif state.shared: state.shared -= 1
                else: self.error(f"'{variable}' no tiene préstamos activos")
                return
            for argument in arguments: self.expression(argument, states)
            summary = self.summaries.get(name)
            if summary:
                for position in summary.consumed:
                    if position < len(arguments) and arguments[position][0] == "variable":
                        self.consume(arguments[position][1], states)
            return
        if kind == "unary": self.expression(expression[2], states); return
        if kind == "await": self.expression(expression[1], states); return
        if kind == "binary":
            self.expression(expression[2], states); self.expression(expression[3], states); return
        if kind == "method_call":
            if not self.unsafe_depth and expression[2] in {"append", "push"}:
                receiver = expression[1]
                if receiver[0] == "variable":
                    children = {argument[1] for argument in expression[3] if argument[0] == "variable"}
                    self.attach_ownership(receiver[1], children, states)
            self.expression(expression[1], states)
            for argument in expression[3]: self.expression(argument, states)
            return
        if kind == "field": self.expression(expression[1], states); return
        if kind == "index":
            self.expression(expression[1], states); self.expression(expression[2], states)
)CFV19DATA"},
        {R"CFV20DATA(cforge_parity.py)CFV20DATA", R"CFV21DATA("""Matriz de paridad verificable entre los motores de C-Forge.

Una característica solo puede declararse común a intérprete, VM y LLVM cuando
el mismo archivo produce exactamente la misma salida y estado de terminación.
"""

from __future__ import annotations

import contextlib
import io
import json
import shutil
import subprocess
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path

from cforgev import CForgevError, execute
from cforge_vm import VirtualMachine, compile_file as compile_vm_file
from compilador_llvm import compile_native as compile_llvm_native


@dataclass(frozen=True)
class EngineResult:
    engine: str
    supported: bool
    returncode: int
    stdout: str
    stderr: str
    error: str | None = None


@dataclass(frozen=True)
class ParityReport:
    source: str
    equal: bool
    reference: str | None
    results: tuple[EngineResult, ...]

    def to_dict(self) -> dict[str, object]:
        value = asdict(self)
        value["results"] = [asdict(result) for result in self.results]
        return value


def _interpreter(path: Path) -> EngineResult:
    output = io.StringIO()
    errors = io.StringIO()
    try:
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(errors):
            execute(path)
        return EngineResult("interpreter", True, 0, output.getvalue(), errors.getvalue())
    except Exception as error:
        return EngineResult(
            "interpreter", True, 1, output.getvalue(), errors.getvalue(), str(error)
        )


def _vm(path: Path) -> EngineResult:
    output: list[str] = []
    try:
        program = compile_vm_file(path)
        VirtualMachine(program, output.append, base_dir=path.resolve().parent).run()
        stdout = "".join(item + "\n" for item in output)
        return EngineResult("vm", True, 0, stdout, "")
    except Exception as error:
        return EngineResult("vm", True, 1, "".join(item + "\n" for item in output), "", str(error))


def _llvm(path: Path, clang: str, timeout: float) -> EngineResult:
    resolved = shutil.which(clang)
    if resolved is None:
        return EngineResult("llvm", False, 127, "", "", f"no se encontró {clang}")
    try:
        with tempfile.TemporaryDirectory(prefix="cforge-parity-") as directory:
            binary = Path(directory) / "programa"
            compile_llvm_native(path, binary, clang=resolved)
            completed = subprocess.run(
                [str(binary)],
                capture_output=True,
                text=True,
                timeout=timeout,
                cwd=path.resolve().parent,
            )
            return EngineResult(
                "llvm", True, completed.returncode, completed.stdout, completed.stderr
            )
    except subprocess.TimeoutExpired:
        return EngineResult("llvm", True, 124, "", "", "tiempo de ejecución agotado")
    except Exception as error:
        return EngineResult("llvm", False, 1, "", "", str(error))


def compare_file(path: Path, clang: str = "clang", timeout: float = 30.0) -> ParityReport:
    path = path.resolve()
    if path.suffix != ".cfv":
        raise CForgevError("Parity requiere un archivo .cfv")
    if not path.is_file():
        raise CForgevError(f"No se pudo abrir {path}")
    results = (_interpreter(path), _vm(path), _llvm(path, clang, timeout))
    supported = [result for result in results if result.supported]
    reference = supported[0].engine if supported else None
    signatures = {(result.returncode, result.stdout, result.stderr) for result in supported}
    equal = (
        len(supported) == len(results)
        and all(result.error is None for result in supported)
        and len(signatures) == 1
    )
    return ParityReport(str(path), equal, reference, results)


def format_report(report: ParityReport) -> str:
    lines = [f"C-Forge parity: {'OK' if report.equal else 'FALLO'} — {report.source}"]
    for result in report.results:
        state = "OK" if result.supported and result.error is None else (
            "NO SOPORTADO" if not result.supported else "ERROR"
        )
        lines.append(
            f"[{state}] {result.engine}: código={result.returncode}, "
            f"stdout={result.stdout!r}, stderr={result.stderr!r}"
        )
        if result.error:
            lines.append(f"  {result.error}")
    return "\n".join(lines)


def reports_json(reports: list[ParityReport]) -> str:
    return json.dumps(
        {"schema": 1, "ok": all(report.equal for report in reports),
         "reports": [report.to_dict() for report in reports]},
        ensure_ascii=False,
        sort_keys=True,
    )
)CFV21DATA"},
        {R"CFV22DATA(capabilities.json)CFV22DATA", R"CFV23DATA({
  "schema": 1,
  "language_version": "1.6.0-developer-preview",
  "overall_status": "developer-preview",
  "quality_gate": {
    "test_command": "python3 -m unittest discover -s tests -v",
    "required_test_outcome": "zero failures and zero errors",
    "fuzz_command": "python3 tests/fuzz_smoke.py --cases 10000",
    "fuzz_iterations_per_pass": 10000,
    "fuzz_passes": 2,
    "fuzz_generated_cases": 20000
  },
  "statuses": [
    "verified-preview",
    "experimental",
    "partial",
    "planned",
    "not-certified"
  ],
  "capabilities": [
    {
      "id": "reference-interpreter",
      "name": "Intérprete de referencia",
      "status": "verified-preview",
      "scope": "Sintaxis y semántica cubiertas por la suite de intérprete",
      "evidence": ["tests/test_cforgev.py", "ejemplos/hola.cfv"]
    },
    {
      "id": "bytecode-vm",
      "name": "Bytecode y VM propios",
      "status": "verified-preview",
      "scope": "Subconjunto seguro documentado, formato 1.1",
      "evidence": ["cforge_vm.py", "tests/test_professional_tooling.py"]
    },
    {
      "id": "ownership",
      "name": "Ownership y préstamos",
      "status": "experimental",
      "scope": "Movimientos, préstamos, regiones y Option; no es aún un verificador formal completo de tiempos de vida",
      "evidence": ["cforge_memory.py", "docs/TYPE-SYSTEM-1.6.md"]
    },
    {
      "id": "llvm-backend",
      "name": "Backend LLVM",
      "status": "partial",
      "scope": "Escalares, textos, colecciones seleccionadas, objetos, módulos, genéricos básicos y FFI documentada",
      "evidence": ["compilador_llvm.py", "docs/PRODUCTION-READINESS.md"]
    },
    {
      "id": "cpp17-backend",
      "name": "Backend C++17",
      "status": "experimental",
      "scope": "Traducción nativa del subconjunto aceptado por compilador_nativo.py",
      "evidence": ["compilador_nativo.py", "tests/test_cforgev.py"]
    },
    {
      "id": "wasm-backend",
      "name": "Backend WebAssembly",
      "status": "partial",
      "scope": "Emisión WAT para un subconjunto; no cubre todo el lenguaje",
      "evidence": ["compilador_wasm.py", "docs/PRODUCTION-READINESS.md"]
    },
    {
      "id": "polyglot-adapters",
      "name": "Adaptadores políglotas",
      "status": "partial",
      "scope": "C/C++, Python, Java, .NET, JavaScript y TypeScript con alcances y dependencias diferentes",
      "evidence": ["docs/PRODUCTION-READINESS.md", "ejemplos/interop"]
    },
    {
      "id": "async-concurrency",
      "name": "Async y concurrencia",
      "status": "partial",
      "scope": "Tareas, await y canales en backends documentados; paridad total pendiente",
      "evidence": ["ejemplos/async_await_16.cfv", "ejemplos/concurrencia_16.cfv"]
    },
    {
      "id": "developer-tools",
      "name": "LSP, DAP, formatter y diagnósticos",
      "status": "experimental",
      "scope": "Implementaciones iniciales funcionales sin garantía de estabilidad",
      "evidence": ["cforge_lsp.py", "cforge_dap.py", "tests/test_professional_tooling.py"]
    },
    {
      "id": "package-manager",
      "name": "Gestor y registro de paquetes",
      "status": "partial",
      "scope": "Cliente, firmas e índice en repositorio; servicio público operado y cuentas pendientes",
      "evidence": ["cforge_packages.py", "registry/index.json", "registry/README.md"]
    },
    {
      "id": "gpu-jit-cluster",
      "name": "GPU, JIT y cluster",
      "status": "partial",
      "scope": "GPU emulada en CPU, JIT de perfilado y metadatos cluster; backends físicos pendientes",
      "evidence": ["docs/PRODUCTION-READINESS.md", "ejemplos/arquitectura_10.cfv"]
    },
    {
      "id": "multiplatform-distribution",
      "name": "Distribución multiplataforma",
      "status": "partial",
      "scope": "CI y empaquetado preparados; validación física completa y catálogos pendientes",
      "evidence": [".github/workflows/ci.yml", "docs/PLATFORM-VALIDATION.md"]
    },
    {
      "id": "autonomous-native-core",
      "name": "Compilador autoalojado y núcleo autónomo",
      "status": "planned",
      "scope": "B4 verificado: Stage 0 construye un compilador Stage 1 escrito en C-Forge que compila programas Core 0.4 a ejecutables sin Python; Stage 2/3, paridad de lenguaje y runtime autónomo siguen pendientes",
      "evidence": ["docs/COMPLETENESS-POLICY.md", "docs/BOOTSTRAP.md", "docs/CORE-DIRECTION.md", "docs/CORE-GRAMMAR-0.4.ebnf", "docs/C-FORGE-2.0-DRAFT.md", "docs/GRAMMAR-2.0-DRAFT.ebnf", "bootstrap/stage0/cforge_bootstrap.cpp", "bootstrap/fixtures/minimal.cfv", "bootstrap/core_lexer.cfv", "bootstrap/core_ast.cfv", "bootstrap/core_parser.cfv", "bootstrap/core_semantics.cfv", "bootstrap/core_emitter.cfv", "bootstrap/core_driver.cfv", "bootstrap/stage1/cforge_stage1.cfv", "bootstrap/fixtures/parser_b1_driver.cfv", "bootstrap/fixtures/semantics_b2_driver.cfv", "bootstrap/fixtures/emitter_b3_driver.cfv", "tests/test_bootstrap_stage0.py", "tests/test_bootstrap_b1.py", "tests/test_bootstrap_b2.py", "tests/test_bootstrap_b3.py", "tests/test_bootstrap_b4.py", "tests/test_professional_tooling.py"]
    },
    {
      "id": "critical-production",
      "name": "Producción bancaria o crítica",
      "status": "not-certified",
      "scope": "Requiere auditoría profesional, LTS, builds firmados y cumplimiento",
      "evidence": ["docs/EXTERNAL-AUDIT-SCOPE.md", "SECURITY.md"]
    }
  ]
}
)CFV23DATA"},
        {R"CFV24DATA(docs/BOOTSTRAP.md)CFV24DATA", R"CFV25DATA(# C-Forge Core Bootstrap

## Objetivo

C-Forge será autoalojado cuando un compilador escrito íntegramente en `.cfv`
pueda compilar su propio código fuente y las generaciones consecutivas sean
reproducibles. Los puentes políglotas no forman parte de este núcleo.

El lenguaje que deberá implementar el compilador autoalojado está definido por
[`C-FORGE-2.0-DRAFT.md`](C-FORGE-2.0-DRAFT.md) y
[`GRAMMAR-2.0-DRAFT.ebnf`](GRAMMAR-2.0-DRAFT.ebnf). Ambos son contratos
candidatos; el subconjunto Core crece de forma controlada hasta cubrirlos.

## Definiciones verificables

- **Stage 0:** compilador mínimo escrito en C++17. Lee C-Forge Core, genera una
  unidad nativa temporal y usa `clang++` para producir código máquina. Solo
  construye la primera generación y no cuenta como autonomía.
- **Stage 1:** compilador escrito en C-Forge y construido por Stage 0.
- **Stage 2:** Stage 1 compila exactamente las mismas fuentes `.cfv`.
- **Stage 3:** Stage 2 vuelve a compilar esas fuentes.
- **Autoalojado:** Stage 2 y Stage 3 producen artefactos equivalentes mediante
  una comparación reproducible.
- **Autónomo:** el compilador autoalojado, runtime, biblioteca estándar,
  ensamblador/enlazador y gestor básico funcionan en una instalación limpia sin
  Python, C++, JVM, .NET, Node ni LLVM.

No se usará la palabra “autoalojado” antes de superar Stage 2/3. No se usará la
palabra “autónomo” mientras el funcionamiento normal requiera otro runtime.

## C-Forge Core congelado

Stage 0 comienza deliberadamente con este subconjunto:

1. números y textos;
2. declaraciones `sea`, con anotación opcional `numero` o `texto`;
3. expresiones aritméticas y concatenación de textos;
4. `mostrar`/`print`;
5. comentarios de línea y punto y coma opcional;
6. diagnósticos con línea y columna.

Quedan fuera del bootstrap inicial: FFI, `extern`, GPU, cluster, red, paquetes,
interfaces gráficas y adaptadores extranjeros.

El subconjunto crecerá únicamente cuando Stage 1 necesite una construcción para
implementar el propio compilador: funciones, control de flujo, estructuras,
colecciones, archivos y ownership. Cada ampliación debe incluir una prueba que
compile y ejecute el artefacto nativo sin invocar Python.

## Orden de migración

| Hito | Implementación `.cfv` | Criterio de salida |
|---|---|---|
| B0 | Stage 0 C++ + lexer Stage 1 | Stage 0 produce un ejecutable; lexer `.cfv` pasa intérprete y VM |
| B1 | Parser escrito en C-Forge | **Cumplido:** AST canónico igual en Stage 0, intérprete y VM para la suite Core 0.2 |
| B2 | Tipos y ownership escritos en C-Forge | **Cumplido:** tipos, variable no declarada y uso después de mover producen diagnósticos iguales en los tres motores |
| B3 | Emisor nativo escrito en C-Forge | **Cumplido:** Stage 0 compila el emisor `.cfv`, que produce C++17 y un ejecutable nativo verificado |
| B4 | Compilador Stage 1 | **Cumplido:** Stage 0 construye el compilador `.cfv`; este compila programas Core a ejecutables sin Python |
| B5 | Stage 2/3 | Stage 1 compila sus propias fuentes y los artefactos de Stage 2/3 son reproducibles |
| B6 | Runtime autónomo | Funciona sin runtimes externos instalados |

## Estado actual

**B4 completado de forma verificable; B5 es el siguiente hito.**

- `bootstrap/stage0/cforge_bootstrap.cpp` es el compilador inicial C++.
- `bootstrap/fixtures/minimal.cfv` se convierte en un ejecutable de máquina real.
- `bootstrap/core_lexer.cfv` es el primer componente de Stage 1 escrito en
  C-Forge.
- `bootstrap/core_ast.cfv` define el árbol independiente del runtime anfitrión.
- `bootstrap/core_parser.cfv` implementa el parser recursivo descendente.
- `bootstrap/core_semantics.cfv` implementa tipos y ownership Core.
- `bootstrap/core_emitter.cfv` genera una unidad C++17 completa desde el AST
  aprobado por B2.
- `bootstrap/core_driver.cfv` conecta automáticamente lexer, parser, análisis
  semántico, ownership, emisión C++ y compilación nativa.
- `bootstrap/stage1/cforge_stage1.cfv` es la unidad Stage 1 autocontenida,
  escrita íntegramente en C-Forge y construible por Stage 0.
- `docs/CORE-GRAMMAR-0.4.ebnf` congela exactamente el subconjunto aceptado.
- `tests/test_bootstrap_b1.py` compila la unidad B1 con Stage 0 y exige que
  Stage 0, intérprete y VM emitan bytes idénticos para el AST canónico.

B3 cubre toda la gramática **Core 0.4**, no la gramática general de C-Forge
2.0. `numero` es copiable; `texto` tiene ownership y `mover(texto)` invalida el
origen. El analizador detecta tipos incompatibles, variables no declaradas y uso
después de mover. Préstamos, tiempos de vida, regiones, clases del usuario,
módulos y el resto del lenguaje se añadirán solo cuando los hitos posteriores
los necesiten y posean pruebas normativas.

La prueba B4 construye Stage 1 con Stage 0 y usa ese nuevo binario para compilar
un programa `.cfv` Core completo. La ejecución de Stage 1 no carga ni enlaza
Python. También verifica que los errores sintácticos y semánticos detengan la
compilación antes de producir un ejecutable.

Esto completa el **compilador Stage 1 para C-Forge Core 0.4**, pero todavía no
constituye autoalojamiento: el subconjunto que Stage 1 acepta como entrada aún
no cubre todas las construcciones usadas por sus propias fuentes. Esa paridad,
la compilación Stage 2/3 y la reproducibilidad pertenecen a B5.

Construcción manual:

```bash
clang++ -std=c++17 -O2 bootstrap/stage0/cforge_bootstrap.cpp \
  -o build/cforge-bootstrap
./build/cforge-bootstrap bootstrap/fixtures/minimal.cfv -o build/minimal
./build/minimal

python3 herramientas/generar_stage1.py
./build/cforge-bootstrap bootstrap/stage1/cforge_stage1.cfv \
  -o build/cforge-stage1
./build/cforge-stage1 bootstrap/fixtures/minimal.cfv -o build/minimal-stage1
./build/minimal-stage1
```

La salida debe ser:

```text
C-Forge Core Bootstrap
42
```

Stage 0 todavía requiere una toolchain C++ para generar el ejecutable. Esa
dependencia existe solo para arrancar. C-Forge continúa clasificado como
Developer Preview no autoalojado hasta superar Stage 2/3.
)CFV25DATA"},
        {R"CFV26DATA(docs/CORE-DIRECTION.md)CFV26DATA", R"CFV27DATA(# Dirección nativa de C-Forge

## Regla principal

C-Forge Core no se construirá como una capa encima de Python, C++, C#, Java,
JavaScript, TypeScript, JVM, .NET, Node ni otro runtime. Stage 0 usa C++ solo
para arrancar el proceso de autoalojamiento. Stage 1 y generaciones posteriores
se escribirán en C-Forge.

Los puentes históricos permanecen temporalmente en Developer Preview para no
romper ejemplos existentes, pero no forman parte del núcleo autónomo ni definen
la arquitectura futura. No se añadirán nuevos puentes durante el bootstrap.

## Capacidades que deben ser propias

| Área | Implementación futura de C-Forge |
|---|---|
| Control y rendimiento | Código nativo, tipos de tamaño explícito, SIMD, ABI y frontera `unsafe` propias |
| Memoria | Ownership, préstamos, regiones, destructores y asignador de C-Forge |
| Objetos | Clases, estructuras, interfaces, genéricos, despacho y módulos propios |
| Scripting | Compilación rápida incremental, REPL y ejecución de módulos C-Forge |
| Concurrencia | Tareas, canales, cancelación y scheduler del runtime propio |
| Web | Biblioteca HTTP, TLS, sockets, JSON, servidor y target WebAssembly propios |
| Herramientas | Compilador, formatter, LSP, DAP y gestor de paquetes escritos en C-Forge |

“Propio” significa que la característica está implementada por el compilador,
runtime o biblioteca estándar de C-Forge. No significa renombrar una llamada a
otro lenguaje.

## Capas permitidas

1. **Core autónomo:** obligatorio y sin runtimes extranjeros.
2. **Biblioteca estándar:** escrita en C-Forge con primitivas del sistema
   documentadas.
3. **Paquetes C-Forge:** escritos y distribuidos como C-Forge.
4. **Compatibilidad extranjera opcional:** fuera del Core, nunca requerida para
   compilar o ejecutar un programa C-Forge puro.

## Criterio de aceptación

Una instalación limpia debe poder:

1. compilar el compilador desde sus fuentes `.cfv`;
2. recompilarse de Stage 2 a Stage 3 reproduciblemente;
3. compilar y ejecutar programas Core sin Python, C++, JVM, .NET, Node ni LLVM;
4. ejecutar la biblioteca estándar nativa;
5. informar claramente cualquier capacidad todavía no implementada.

Hasta cumplirlo, el estado público seguirá siendo Developer Preview.
)CFV27DATA"},
        {R"CFV28DATA(docs/CORE-GRAMMAR-0.4.ebnf)CFV28DATA", R"CFV29DATA((* C-Forge Core Bootstrap 0.4 — gramática congelada para B3.
   Esta no es la gramática completa de C-Forge 2.0. Es el subconjunto que
   Stage 0 compila para construir las primeras etapas autoalojadas. *)

programa          = { sentencia }, EOF ;

sentencia         = declaracion | impresion ;

declaracion       = "sea", identificador, [ ":", tipo_core ],
                    "=", expresion, [ ";" ] ;

impresion         = ( "mostrar" | "print" ), "(",
                    expresion, ")", [ ";" ] ;

expresion         = producto, { ( "+" | "-" ), producto } ;
producto          = primaria, { ( "*" | "/" ), primaria } ;
primaria          = numero | texto | identificador |
                    "mover", "(", expresion, ")" |
                    "(", expresion, ")" ;

tipo_core         = "numero" | "texto" ;
identificador     = letra_o_guion, { letra_o_guion | digito } ;
numero            = digito, { digito }, [ ".", digito, { digito } ] ;
texto             = '"', { caracter | escape }, '"' ;
escape            = "\", ( "\" | '"' | "n" | "r" | "t" ) ;
letra_o_guion     = "A"…"Z" | "a"…"z" | "_" ;
digito            = "0"…"9" ;

(* AST canónico Core AST 1:
   nodo = tipo, ":", largo_decimal, ":", valor_escapado,
          "@", linea_decimal, "[", [ nodo, { ",", nodo } ], "]" ;

   Tipos de nodo:
   Programa      valor="Core-0.4", hijos=sentencias
   Declaracion   valor="nombre:tipo" (tipo vacío si fue inferido), hijos=[valor]
   Mostrar       valor="mostrar"|"print", hijos=[expresión]
   Binario       valor=operador, hijos=[izquierdo,derecho]
   Numero        valor=lexema, hijos=[]
   Texto         valor=lexema incluyendo comillas, hijos=[]
   Identificador valor=nombre, hijos=[]
   Mover         valor="mover", hijos=[expresión]
*)
)CFV29DATA"},
        {R"CFV30DATA(docs/C-FORGE-2.0-DRAFT.md)CFV30DATA", R"CFV31DATA(# C-Forge 2.0 — especificación de diseño candidata

Estado: **borrador normativo; no representa todavía funcionalidad implementada**.

C-Forge es un lenguaje autónomo, compilado, seguro por defecto y de propósito
general. Su identidad no consiste en ejecutar otros lenguajes. Toma ideas
probadas de distintos paradigmas, las somete a un único modelo semántico y las
expresa con una sintaxis propia.

La meta de 2.0 es que una misma implementación sirva para software de sistemas,
servicios, aplicaciones, herramientas, cálculo, juegos y WebAssembly. Ninguna
carga de trabajo tiene garantizado superar a todos los demás lenguajes: el
rendimiento se demostrará mediante mediciones reproducibles.

## 1. Principios obligatorios

1. **Seguro por defecto, poderoso de forma explícita.** Punteros crudos,
   alias mutable sin comprobar y llamadas ABI inseguras solo existen dentro de
   `unsafe`.
2. **Un programa, una semántica.** Intérprete de desarrollo, VM, compilador
   nativo y WebAssembly deben conservar el mismo resultado observable.
3. **Inferencia sin ambigüedad.** El compilador infiere tipos cuando puede
   probar uno único; nunca adivina una conversión con pérdida.
4. **Coste visible.** Copias, asignaciones, conteo de referencias, despacho
   dinámico y recolección opcional deben poder identificarse con herramientas.
5. **Determinismo local.** Recursos con ownership se destruyen al abandonar su
   alcance, incluso durante propagación de errores.
6. **Concurrencia estructurada.** Una tarea no sobrevive accidentalmente al
   alcance que la creó.
7. **Compatibilidad verificable.** Toda evolución de la especificación declara
   sus rupturas, migraciones y nivel de estabilidad.

## 2. Identidad sintáctica

- Los bloques usan `{}`. La indentación es visual, no semántica.
- El salto de línea termina una sentencia cuando la expresión está completa;
  `;` es válido, pero opcional.
- `sea` crea un enlace inmutable e inferido; `var` crea uno mutable; `const`
  exige evaluación en compilación.
- Las funciones se declaran con `funcion`; los valores faltantes se modelan
  con `T?`, no con referencias nulas.
- Las palabras de control son propias y consistentes: `si`, `sino`, `segun`,
  `para`, `mientras`, `intentar`, `capturar`, `retornar`.

La gramática léxica y sintáctica candidata completa se encuentra en
[`GRAMMAR-2.0-DRAFT.ebnf`](GRAMMAR-2.0-DRAFT.ebnf).

```cfv
modulo ejemplo

publico registro Persona(nombre: texto, edad: u8)

publico funcion saludo(persona: &Persona) -> texto {
    retornar "Hola, " + persona.nombre
}

funcion principal() -> Resultado<unidad, Error> {
    sea persona = Persona(nombre: "Javier", edad: 20)
    mostrar(saludo(&persona))
    retornar correcto(())
}
```

## 3. Modelo unificado de características

| Necesidad | Forma propia de C-Forge |
|---|---|
| Bajo nivel y RAII | ownership, préstamos, regiones, destructores y `unsafe` |
| Objetos empresariales | clases, registros, estructuras, interfaces y propiedades |
| Scripts productivos | inferencia, colecciones literales, comprensiones y `dinamico` explícito |
| Programación funcional | lambdas, cierres, iteradores, tuberías y pattern matching |
| Eventos | delegados tipados, eventos y flujos asíncronos |
| Metaprogramación | genéricos, `comptime`, reflexión opt-in y atributos |
| Web | compilación WebAssembly y biblioteca web nativa por capacidades |
| Portabilidad | perfiles `nativo`, `portable` y `web`, sin cambiar la semántica |

Esto no crea seis dialectos. Por ejemplo, solo existe una operación estándar
para emitir valores, una jerarquía de colecciones y una forma de declarar
funciones. Los alias de sintaxis de lenguajes ajenos quedan fuera del núcleo 2.0.

## 4. Sistema de tipos

### 4.1 Categorías

- Escalares: `bool`, `caracter`, enteros con signo y sin signo de 8 a 64 bits,
  `isize`, `usize`, `f32`, `f64`, `decimal`, `unidad` y `nunca`.
- Valores compuestos: tuplas, arreglos de tamaño fijo, `estructura`, `registro`
  y `enum`.
- Referencias: `&T` y `&mut T`, siempre válidas durante su vida.
- Punteros crudos: `*const T` y `*mut T`, utilizables solo en `unsafe`.
- Objetos nominales: `clase` e `interfaz`.
- Colecciones estándar: `Lista<T>`, `Mapa<K,V>`, `Conjunto<T>`,
  `Vector<T,N>` y vistas prestadas.
- Funciones: `funcion(A, B) -> R` y `async funcion(A) -> R`.
- Ausencia: `T?`, equivalente semánticamente a `Opcion<T>`.
- Resultado: `Resultado<T,E>`.
- Gradualidad explícita: `dinamico`. El valor conserva una etiqueta de tipo y
  toda operación no demostrable se comprueba en ejecución.

`cualquiera` es el supertipo estático para abstracciones; no permite operar
sobre un valor sin acotarlo mediante patrón, interfaz o conversión comprobada.
`dinamico` sí permite despacho en ejecución y, por eso, hace visible ese coste.

### 4.2 Inferencia y conversiones

La inferencia es local con restricciones propagadas desde parámetros, retornos
y genéricos. Las interfaces públicas deben declarar sus tipos. Los literales
enteros se ajustan al tipo contextual si el valor cabe; sin contexto son `i64`.
Los reales son `f64`.

Solo son implícitas:

- ampliaciones enteras sin pérdida;
- coerción de `&mut T` a `&T`;
- conversión de un tipo concreto a una interfaz implementada;
- elevación de `T` a `T?`.

Toda reducción, cambio de signo riesgoso, conversión texto-número o salida de
`dinamico` exige `convertir<T>(valor)`, que retorna `Resultado<T, ErrorTipo>`.

### 4.3 Genéricos y metaprogramación

Los genéricos estáticos se monomorfizan por defecto. Las restricciones se
expresan con interfaces y `donde`. El despacho por interfaz puede ser estático
o dinámico según el tipo utilizado.

`comptime` ejecuta código puro y limitado durante compilación. Puede producir
constantes, tipos o AST higiénico, pero no acceder a red, reloj o sistema de
archivos sin una capacidad declarada. Los tipos condicionales y mapeados se
resuelven en compilación y nunca crean tipos parcialmente válidos en ejecución.

### 4.4 Tipos nominales y estructurales

Clases, estructuras, registros y enumeraciones son nominales. Las interfaces
son nominales al implementarlas, pero pueden existir contratos estructurales
locales marcados `interfaz estructural`. Una clase admite una sola clase base y
múltiples interfaces. Los registros son inmutables por defecto y poseen
igualdad por valor.

Las clases no tienen una cadena de prototipos mutable. La flexibilidad de ese
modelo se ofrece mediante `ObjetoDinamico`, extensiones y delegación explícita,
sin alterar silenciosamente métodos de tipos estáticos.

## 5. Modelo de memoria

Cada valor propietario tiene un único dueño. Asignarlo mueve el valor salvo que
el tipo implemente `Copiable`; `clonar()` hace una duplicación explícita.

- `&T` permite múltiples préstamos de solo lectura.
- `&mut T` es exclusivo.
- Ningún préstamo puede sobrevivir al propietario.
- Un valor movido no puede reutilizarse.
- `destruir()` se ejecuta exactamente una vez y en orden inverso de
  construcción.
- `deferir` y `recurso` cooperan con el desenrollado de errores.

Los objetos compartidos usan `Compartido<T>` con conteo atómico de referencias.
`Debil<T>` rompe ciclos. La biblioteca puede incluir un heap trazado
`Recolectado<T>` como perfil **opcional y aislado**; no cambia el modelo por
defecto ni permite que una referencia prestada escape.

Las regiones (`region`) agrupan asignaciones cuya vida se demuestra de forma
conjunta. Un valor solo sale de la región si se mueve a un dueño externo válido.
El compilador rechaza uso después de mover, doble destrucción, referencias
colgantes y carreras de datos demostrables.

`unsafe` permite desreferenciar punteros, construir una referencia desde una
dirección, acceder a registros de hardware o cruzar una ABI. El bloque no
desactiva el sistema de tipos: únicamente transfiere al programador cinco
obligaciones verificables — validez, alineación, inicialización, alias y vida.

## 6. Semántica de ejecución

- El alcance es léxico.
- Los argumentos y operandos se evalúan de izquierda a derecha.
- Una asignación devuelve `unidad`.
- Los enteros comprueban overflow de forma predeterminada en todos los perfiles.
  `sumar_envuelto`, `sumar_saturado` y `sumar_comprobado` expresan alternativas.
- `==` es igualdad de valor; `identico` compara identidad donde exista.
- Los `enum` y opcionales deben cubrirse exhaustivamente en `segun`.
- El despacho virtual solo ocurre al usar una referencia de interfaz o clase
  virtual; el resto se resuelve estáticamente.
- Los cierres capturan por préstamo cuando es seguro y por movimiento cuando
  se escribe `mover |...|`.
- Los iteradores son perezosos. Una comprensión crea una colección; una tubería
  no lo hace hasta una operación terminal.

La reflexión es de compilación por defecto. La reflexión en ejecución requiere
`@reflejable` y conserva únicamente los metadatos solicitados.

## 7. Módulos, paquetes y capacidades

Cada archivo pertenece a un módulo. Un paquete contiene un manifiesto
`CForge.pkg`, módulos fuente y un archivo de bloqueo reproducible.

```cfv
modulo tienda.pagos

usar std.red::{Cliente, Solicitud}
usar tienda.modelo::Orden

publico funcion cobrar(orden: &Orden) -> async Resultado<Recibo, ErrorPago>
    efectos { red, reloj }
{
    // ...
}
```

Los símbolos son privados salvo `publico`. Las dependencias forman un grafo
acíclico entre módulos durante inicialización. No existe ejecución implícita al
importar: la inicialización se declara en `funcion iniciar_modulo`.

Los efectos sensibles (`archivos`, `red`, `procesos`, `entropia`, `reloj`,
`hardware`, `ffi`) aparecen en firmas públicas y en el manifiesto. El ejecutor
puede denegar capacidades antes de iniciar el programa.

Los paquetes se identifican por nombre, versión semántica, hash de contenido y
firma. El archivo de bloqueo fija el grafo completo. Una versión revocada no se
selecciona en instalaciones nuevas y genera un diagnóstico verificable.

## 8. Errores

Los fallos recuperables usan `Resultado<T,E>` y el operador `?`. `T?` modela
ausencia, no error. Las excepciones tipadas se reservan para límites donde la
propagación estructurada es más clara y se declaran como efecto `lanza<E>`.

```cfv
funcion cargar(ruta: &Ruta) -> Resultado<Config, ErrorCarga>
    efectos { archivos }
{
    sea texto = Archivo.leer_texto(ruta)?
    retornar Json.decodificar<Config>(texto)
}
```

`intentar/capturar/finalmente` solo captura errores declarados. Un `panic`
representa una violación de contrato o corrupción interna; no debe usarse para
flujo normal. Los diagnósticos contienen código estable, archivo, rango,
explicación, contexto y corrección sugerida. Ningún backend debe filtrar un
traceback de su implementación anfitriona.

## 9. Concurrencia y asincronía

`async funcion` devuelve `Tarea<T>`. `esperar` suspende la tarea, no el hilo.
`grupo` crea concurrencia estructurada: al salir, todas sus tareas terminaron o
fueron canceladas y esperadas.

```cfv
async funcion descargar_todo(urls: &Lista<Url>)
    -> Resultado<Lista<Bytes>, ErrorRed>
    efectos { red }
{
    grupo trabajos {
        sea tareas = [lanzar_tarea descargar(url) para url en urls]
        retornar esperar todas(tareas)
    }
}
```

- `Canal<T>` transfiere mensajes con backpressure.
- `Actor<S,M>` serializa el acceso a su estado.
- `Mutex<T>` y `RwLock<T>` protegen memoria compartida.
- `Atomico<T>` especifica orden de memoria.
- `TokenCancelacion` propaga cancelación cooperativa.
- `seleccionar` espera canales, tareas o tiempo límite.

Solo valores `Transferible` cruzan ejecutores o hilos; solo valores
`Compartible` pueden referenciarse concurrentemente. Estas interfaces son
especiales del compilador y no pueden implementarse de forma insegura fuera de
`unsafe`. El código seguro no contiene carreras de datos. El orden de tareas no
se promete salvo sincronización explícita.

## 10. Modelo de compilación

La cadena autónoma objetivo es:

```text
fuente .cfv
  → lexer y parser
  → AST tipado
  → C-FIR (IR de alto nivel)
  → C-MIR (ownership, efectos y control de flujo)
  → backend nativo / WebAssembly / bytecode
```

C-FIR conserva tipos, genéricos y efectos. C-MIR materializa movimientos,
préstamos, destrucción y representación de datos. El formato de objeto y la ABI
de C-Forge se versionan independientemente de la sintaxis.

El compilador Stage 0 en C++ solo sirve para arrancar. La condición de
autonomía es que el compilador Stage 1 escrito en C-Forge compile Stage 2 y que
Stage 1 y Stage 2 produzcan resultados reproducibles para el conjunto
bootstrap. El sistema operativo y el enlazador son plataforma, no lenguajes
anfitriones.

## 11. Perfiles

- `nativo`: acceso controlado a sistema, SIMD, hardware y ABI de plataforma.
- `portable`: API estable común a sistemas soportados.
- `web`: WebAssembly, DOM y red mediante capacidades del host.
- `embebido`: sin asignador global ni sistema operativo, con biblioteca mínima.

Una API no disponible en un perfil falla en compilación. La biblioteca estándar
se divide por capacidades, no por detección tardía del sistema.

## 12. Conformidad

Una implementación solo puede declararse “C-Forge 2.0 conforme” si:

1. acepta la gramática publicada y rechaza construcciones inválidas;
2. supera las pruebas normativas de tipos, memoria, efectos y concurrencia;
3. conserva la semántica observable entre backends declarados;
4. documenta extensiones y no las confunde con el estándar;
5. publica arquitectura, ABI, versión de biblioteca y limitaciones;
6. pasa el bootstrap reproducible;
7. no anuncia como terminada una capacidad marcada experimental o planeada.

Hasta cumplir esas condiciones, el nombre correcto es **C-Forge 2.0 Draft**.
)CFV31DATA"},
        {R"CFV32DATA(docs/GRAMMAR-2.0-DRAFT.ebnf)CFV32DATA", R"CFV33DATA((* C-Forge 2.0 — gramática normativa candidata.
   Los saltos de línea separan sentencias cuando no hay ambigüedad.
   El punto y coma es opcional. Los bloques siempre usan llaves. *)

programa          = { item_modulo } ;
item_modulo       = anotaciones ,
                    [ visibilidad ] ,
                    [ modificadores ] ,
                    ( modulo | usar | tipo_alias | declaracion_tipo |
                      declaracion_funcion | declaracion_delegado |
                      declaracion_evento | declaracion_variable |
                      impl | prueba ) ;

visibilidad       = "publico" | "interno" | "privado" | "protegido" ;
modificadores     = modificador , { modificador } ;
modificador       = "async" | "unsafe" | "comptime" | "inline" |
                    "sellado" | "abstracto" | "estatico" | "parcial" ;
anotaciones       = { anotacion } ;
anotacion         = "@" , nombre_calificado ,
                    [ "(" , [ lista_argumentos ] , ")" ] ;

modulo            = "modulo" , ruta_modulo , bloque_modulo ;
bloque_modulo     = "{" , { item_modulo } , "}" ;
usar              = "usar" , ruta_modulo ,
                    [ "::" , ( "*" | "{" , lista_importados , "}" ) ] ,
                    [ "como" , identificador ] , fin ;
lista_importados  = importado , { "," , importado } , [ "," ] ;
importado         = identificador , [ "como" , identificador ] ;
ruta_modulo       = identificador , { "." , identificador } ;

tipo_alias        = "tipo" , identificador , [ parametros_genericos ] ,
                    "=" , tipo , fin ;
declaracion_tipo  = estructura | registro | clase | interfaz | enumeracion ;

estructura        = "estructura" , identificador , [ parametros_genericos ] ,
                    [ clausula_donde ] , cuerpo_tipo ;
registro          = "registro" , identificador , [ parametros_genericos ] ,
                    [ parametros_registro ] , [ clausula_donde ] , cuerpo_tipo ;
parametros_registro = "(" , [ parametro_registro ,
                      { "," , parametro_registro } , [ "," ] ] , ")" ;
parametro_registro = identificador , ":" , tipo , [ "=" , expresion ] ;

clase             = "clase" , identificador , [ parametros_genericos ] ,
                    [ ":" , tipo_nominal , { "," , tipo_nominal } ] ,
                    [ clausula_donde ] , cuerpo_tipo ;
interfaz          = "interfaz" , identificador , [ parametros_genericos ] ,
                    [ ":" , tipo_nominal , { "," , tipo_nominal } ] ,
                    [ clausula_donde ] , cuerpo_interfaz ;
enumeracion       = "enum" , identificador , [ parametros_genericos ] ,
                    [ clausula_donde ] , "{" ,
                    [ variante_enum , { "," , variante_enum } , [ "," ] ] ,
                    "}" ;
variante_enum     = identificador ,
                    [ "(" , lista_tipos , ")" |
                      "{" , lista_campos_tipo , "}" ] ;

cuerpo_tipo       = "{" , { miembro_tipo } , "}" ;
cuerpo_interfaz   = "{" , { miembro_interfaz } , "}" ;
miembro_tipo      = anotaciones , [ visibilidad ] , [ modificadores ] ,
                    ( campo | propiedad | constructor | destructor |
                      declaracion_funcion | declaracion_evento |
                      declaracion_delegado | tipo_alias ) ;
miembro_interfaz  = anotaciones , [ modificadores ] ,
                    ( firma_funcion | firma_propiedad |
                      declaracion_evento | tipo_asociado ) ;
campo             = ( "sea" | "var" | "const" ) , patron_nombre ,
                    [ ":" , tipo ] , [ "=" , expresion ] , fin ;
propiedad         = "propiedad" , identificador , ":" , tipo ,
                    ( fin | "{" , accesor_obtener ,
                      [ accesor_establecer ] , "}" ) ;
firma_propiedad   = "propiedad" , identificador , ":" , tipo ,
                    "{" , "obtener" , fin ,
                    [ "establecer" , fin ] , "}" ;
accesor_obtener   = "obtener" , ( fin | bloque ) ;
accesor_establecer = "establecer" , [ "(" , identificador , ")" ],
                     ( fin | bloque ) ;
constructor       = "iniciar" , "(" , [ parametros ] , ")" ,
                    [ efectos ] , bloque ;
destructor        = "destruir" , "(" , ")" , bloque ;
tipo_asociado     = "tipo" , identificador , [ ":" , limites_tipo ] , fin ;

declaracion_delegado = "delegado" , identificador ,
                       [ parametros_genericos ] ,
                       "(" , [ parametros_tipo ] , ")" ,
                       [ "->" , tipo ] , [ efectos ] , fin ;
declaracion_evento = "evento" , identificador , ":" , tipo , fin ;

declaracion_funcion = "funcion" , identificador ,
                      [ parametros_genericos ] ,
                      "(" , [ parametros ] , ")" ,
                      [ "->" , tipo ] , [ efectos ] ,
                      [ clausula_donde ] , ( bloque | fin ) ;
firma_funcion     = "funcion" , identificador ,
                    [ parametros_genericos ] ,
                    "(" , [ parametros_tipo ] , ")" ,
                    [ "->" , tipo ] , [ efectos ] ,
                    [ clausula_donde ] , fin ;
parametros        = parametro , { "," , parametro } , [ "," ] ;
parametro         = [ modo_parametro ] , patron_nombre , [ ":" , tipo ] ,
                    [ "=" , expresion ] ;
modo_parametro    = "mover" | "prestado" | "prestado_mut" ;
parametros_tipo   = parametro_tipo , { "," , parametro_tipo } , [ "," ] ;
parametro_tipo    = [ modo_parametro ] , identificador , ":" , tipo ;
efectos           = "efectos" , "{" , identificador ,
                    { "," , identificador } , [ "," ] , "}" ;

parametros_genericos = "<" , parametro_generico ,
                       { "," , parametro_generico } , [ "," ] , ">" ;
parametro_generico = identificador ,
                    [ ":" , limites_tipo ] , [ "=" , tipo ] ;
limites_tipo      = tipo , { "+" , tipo } ;
clausula_donde    = "donde" , restriccion ,
                    { "," , restriccion } , [ "," ] ;
restriccion       = tipo , ":" , limites_tipo ;

impl              = [ "unsafe" ] , "impl" , [ parametros_genericos ] ,
                    [ tipo_nominal , "para" ] , tipo_nominal ,
                    [ clausula_donde ] , "{" , { miembro_tipo } , "}" ;

declaracion_variable = ( "sea" | "var" | "const" ) , patron ,
                       [ ":" , tipo ] , "=" , expresion , fin ;

prueba            = "prueba" , texto , bloque ;

bloque            = "{" , { sentencia } , [ expresion_final ] , "}" ;
sentencia         = declaracion_variable | declaracion_funcion |
                    sentencia_expresion | sentencia_si |
                    sentencia_segun | sentencia_mientras |
                    sentencia_para | sentencia_bucle |
                    sentencia_retorno | sentencia_romper |
                    sentencia_continuar | sentencia_lanzar |
                    sentencia_deferir | sentencia_usar_recurso |
                    sentencia_grupo | sentencia_seleccionar |
                    sentencia_unsafe ;
sentencia_expresion = expresion , fin ;
expresion_final   = expresion , [ ";" ] ;

sentencia_si      = "si" , expresion , bloque ,
                    { "sino" , "si" , expresion , bloque } ,
                    [ "sino" , bloque ] ;
sentencia_segun   = "segun" , expresion , "{" ,
                    { brazo_patron } , "}" ;
brazo_patron      = patron , [ "si" , expresion ] , "=>" ,
                    ( expresion | bloque ) , [ "," ] ;
sentencia_mientras = "mientras" , expresion , bloque ;
sentencia_para    = "para" , patron , "en" , expresion , bloque ;
sentencia_bucle   = "bucle" , bloque ;
sentencia_retorno = "retornar" , [ expresion ] , fin ;
sentencia_romper  = "romper" , [ expresion ] , fin ;
sentencia_continuar = "continuar" , fin ;
sentencia_lanzar  = "lanzar" , expresion , fin ;
sentencia_deferir = "deferir" , ( expresion , fin | bloque ) ;
sentencia_usar_recurso = "recurso" , patron , "=" , expresion , bloque ;
sentencia_unsafe  = "unsafe" , bloque ;

sentencia_grupo   = "grupo" , [ identificador ] , bloque ;
sentencia_seleccionar = "seleccionar" , "{" ,
                        brazo_seleccion , { brazo_seleccion } , "}" ;
brazo_seleccion   = ( "caso" , patron , "=" , "recibir" ,
                      expresion | "timeout" , expresion |
                      "sino" ) , "=>" , ( expresion | bloque ) , [ "," ] ;

expresion         = asignacion ;
asignacion        = condicional ,
                    [ operador_asignacion , asignacion ] ;
operador_asignacion = "=" | "+=" | "-=" | "*=" | "/=" | "%=" |
                      "??=" | "&=" | "|=" | "^=" ;
condicional       = coalescencia ,
                    [ "si" , coalescencia , "sino" , condicional ] ;
coalescencia      = disyuncion , { "??" , disyuncion } ;
disyuncion        = conjuncion , { "o" , conjuncion } ;
conjuncion        = comparacion , { "y" , comparacion } ;
comparacion       = rango , { ( "==" | "!=" | "<" | "<=" | ">" | ">=" |
                    "es" | "no" , "es" | "en" | "no" , "en" ) , rango } ;
rango             = tuberia , [ ( ".." | "..=" ) , tuberia ] ;
tuberia           = bit_o , { "|>" , bit_o } ;
bit_o             = bit_xor , { "|" , bit_xor } ;
bit_xor           = bit_y , { "^" , bit_y } ;
bit_y             = desplazamiento , { "&" , desplazamiento } ;
desplazamiento    = suma , { ( "<<" | ">>" ) , suma } ;
suma              = producto , { ( "+" | "-" ) , producto } ;
producto          = potencia , { ( "*" | "/" | "%" ) , potencia } ;
potencia          = unaria , [ "**" , potencia ] ;
unaria            = ( "no" | "-" | "+" | "~" | "mover" | "prestar" |
                    "prestar_mut" | "*" | "&" ) , unaria | postfix ;

postfix           = primaria , { sufijo } ;
sufijo            = llamada | indice | miembro | miembro_seguro |
                    argumento_generico | operador_postfijo ;
llamada           = "(" , [ lista_argumentos ] , ")" ;
indice            = "[" , expresion , "]" ;
miembro           = "." , identificador ;
miembro_seguro    = "?." , identificador ;
argumento_generico = "::<" , lista_tipos , ">" ;
operador_postfijo = "?" | "!" ;

primaria          = literal | identificador | "esto" | "super" |
                    agrupada | lista | mapa | conjunto | tupla |
                    lambda | comprension | construir | expresion_si |
                    expresion_segun | expresion_intentar |
                    expresion_async | expresion_await |
                    expresion_spawn | expresion_tipo |
                    expresion_comptime ;
agrupada          = "(" , expresion , ")" ;
lista             = "[" , [ lista_argumentos ] , "]" ;
tupla             = "(" , expresion , "," ,
                    [ lista_argumentos ] , ")" ;
mapa              = "{" , [ entrada_mapa ,
                    { "," , entrada_mapa } , [ "," ] ] , "}" ;
entrada_mapa      = expresion , ":" , expresion | "..." , expresion ;
conjunto          = "#{" , [ lista_argumentos ] , "}" ;
construir         = tipo_nominal , "{" ,
                    [ inicializador_campo ,
                    { "," , inicializador_campo } , [ "," ] ] , "}" ;
inicializador_campo = identificador , [ ":" , expresion ] |
                      "..." , expresion ;

lambda            = [ "async" ] , "|" , [ parametros_lambda ] , "|" ,
                    ( expresion | bloque ) ;
parametros_lambda = parametro_lambda ,
                    { "," , parametro_lambda } , [ "," ] ;
parametro_lambda  = patron_nombre , [ ":" , tipo ] ;

comprension       = "[" , expresion , "para" , patron , "en" , expresion ,
                    { "para" , patron , "en" , expresion } ,
                    [ "si" , expresion ] , "]" ;
expresion_si      = "si" , expresion , bloque , "sino" , bloque ;
expresion_segun   = "segun" , expresion , "{" , { brazo_patron } , "}" ;
expresion_intentar = "intentar" , bloque ,
                     { "capturar" , patron , [ "si" , expresion ] , bloque } ,
                     [ "finalmente" , bloque ] ;
expresion_async   = "async" , bloque ;
expresion_await   = "esperar" , expresion ;
expresion_spawn   = "lanzar_tarea" , expresion ;
expresion_tipo    = ( "tipo_de" | "tamano_de" | "alineacion_de" ) ,
                    "(" , ( expresion | tipo ) , ")" ;
expresion_comptime = "comptime" , bloque ;

lista_argumentos  = argumento , { "," , argumento } , [ "," ] ;
argumento         = [ identificador , ":" ] , expresion |
                    "..." , expresion ;

patron            = patron_or ;
patron_or         = patron_atomico , { "|" , patron_atomico } ;
patron_atomico    = "_" | patron_nombre | literal |
                    patron_tupla | patron_lista | patron_objeto |
                    patron_variante | patron_tipo | patron_rango ;
patron_nombre     = [ "var" ] , identificador ;
patron_tupla      = "(" , patron , "," , [ lista_patrones ] , ")" ;
patron_lista      = "[" , [ lista_patrones ] , "]" ;
patron_objeto     = tipo_nominal , "{" , [ campo_patron ,
                    { "," , campo_patron } , [ "," ] ] , "}" ;
campo_patron      = identificador , [ ":" , patron ] | ".." ;
patron_variante   = tipo_nominal , "::" , identificador ,
                    [ "(" , [ lista_patrones ] , ")" ] ;
patron_tipo       = patron_nombre , ":" , tipo ;
patron_rango      = literal , ( ".." | "..=" ) , literal ;
lista_patrones    = patron , { "," , patron } , [ "," ] ;

tipo              = tipo_union ;
tipo_union        = tipo_interseccion , { "|" , tipo_interseccion } ;
tipo_interseccion = tipo_opcional , { "&" , tipo_opcional } ;
tipo_opcional     = tipo_primario , [ "?" ] ;
tipo_primario     = tipo_primitivo | tipo_nominal | tipo_tupla |
                    tipo_funcion | tipo_referencia | tipo_puntero |
                    tipo_array | tipo_condicional | tipo_mapeado |
                    "dinamico" | "nunca" | "cualquiera" ;
tipo_primitivo    = "bool" | "caracter" | "texto" |
                    "i8" | "i16" | "i32" | "i64" | "isize" |
                    "u8" | "u16" | "u32" | "u64" | "usize" |
                    "f32" | "f64" | "decimal" | "unidad" ;
tipo_nominal      = ruta_modulo , [ "<" , lista_tipos , ">" ] ;
tipo_tupla        = "(" , tipo , "," , lista_tipos , ")" ;
tipo_funcion      = [ "async" ] , "funcion" ,
                    "(" , [ lista_tipos ] , ")" , "->" , tipo ,
                    [ efectos ] ;
tipo_referencia   = "&" , [ "mut" ] , tipo ;
tipo_puntero      = "*" , ( "const" | "mut" ) , tipo ;
tipo_array        = "[" , tipo , [ ";" , expresion_constante ] , "]" ;
tipo_condicional  = "si_tipo" , "<" , tipo , "es" , tipo ,
                    "," , tipo , "," , tipo , ">" ;
tipo_mapeado      = "mapear_tipo" , "<" , identificador , "en" , tipo ,
                    "," , tipo , ">" ;
lista_tipos       = tipo , { "," , tipo } , [ "," ] ;
lista_campos_tipo = parametro_registro ,
                    { "," , parametro_registro } , [ "," ] ;
expresion_constante = expresion ;

literal           = numero | texto | caracter | booleano | ausencia ;
booleano          = "verdadero" | "falso" ;
ausencia          = "ninguno" ;

numero            = entero | real ;
entero            = decimal_entero | hexadecimal | binario | octal ;
decimal_entero    = digito , { digito | "_" } , [ sufijo_entero ] ;
hexadecimal       = "0x" , hex , { hex | "_" } , [ sufijo_entero ] ;
binario           = "0b" , bit , { bit | "_" } , [ sufijo_entero ] ;
octal             = "0o" , oct , { oct | "_" } , [ sufijo_entero ] ;
real              = digito , { digito | "_" } , "." ,
                    digito , { digito | "_" } ,
                    [ exponente ] , [ sufijo_real ] ;
exponente         = ( "e" | "E" ) , [ "+" | "-" ] ,
                    digito , { digito | "_" } ;
sufijo_entero     = "i8" | "i16" | "i32" | "i64" | "isize" |
                    "u8" | "u16" | "u32" | "u64" | "usize" ;
sufijo_real       = "f32" | "f64" | "dec" ;

texto             = '"' , { caracter_texto | escape } , '"' |
                    '"""' , { caracter_texto | salto | escape } , '"""' ;
caracter          = "'" , ( caracter_texto | escape ) , "'" ;
escape            = "\" , ( "\" | '"' | "'" | "n" | "r" | "t" | "0" |
                    "u{" , hex , { hex } , "}" ) ;

identificador     = inicio_id , { inicio_id | digito } ;
inicio_id         = letra | "_" ;
fin               = ";" | salto ;

comentario_linea  = "//" , { caracter_texto } , salto |
                    "#" , { caracter_texto } , salto ;
comentario_bloque = "/*" , { cualquier_caracter } , "*/" ;

letra             = ? letra Unicode XID_Start ? ;
digito            = "0" | "1" | "2" | "3" | "4" |
                    "5" | "6" | "7" | "8" | "9" ;
hex               = digito | "a" | "b" | "c" | "d" | "e" | "f" |
                    "A" | "B" | "C" | "D" | "E" | "F" ;
bit               = "0" | "1" ;
oct               = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" ;
salto             = ? LF o CRLF ? ;
caracter_texto    = ? carácter Unicode válido no reservado ? ;
cualquier_caracter = ? cualquier valor escalar Unicode ? ;
)CFV33DATA"},
        {R"CFV34DATA(ejemplos/diseno_cforge_20.cfv)CFV34DATA", R"CFV35DATA(// Contrato ilustrativo de C-Forge 2.0 Draft.
// Este archivo documenta la sintaxis objetivo; no se anuncia como ejecutable
// por el motor 1.6 hasta que la matriz de capacidades lo demuestre.

modulo demostracion

publico interfaz Describible {
    funcion describir() -> texto
}

publico registro Persona(nombre: texto, edad: u8)

impl Describible para Persona {
    funcion describir() -> texto {
        retornar nombre + " (" + edad.como_texto() + ")"
    }
}

publico enum ErrorDemo {
    EntradaVacia,
    EdadInvalida(valor: i64)
}

funcion crear_persona(nombre: texto, edad: i64)
    -> Resultado<Persona, ErrorDemo>
{
    si nombre.longitud == 0 {
        retornar error(ErrorDemo::EntradaVacia)
    }
    si edad < 0 o edad > 255 {
        retornar error(ErrorDemo::EdadInvalida(edad))
    }
    retornar correcto(Persona(nombre: nombre, edad: edad.convertir<u8>()?))
}

async funcion describir_en_paralelo(personas: &Lista<Persona>)
    -> Lista<texto>
{
    grupo descripciones {
        sea tareas = [
            lanzar_tarea async { persona.describir() }
            para persona en personas
        ]
        retornar esperar todas(tareas)
    }
}

funcion principal() -> async Resultado<unidad, ErrorDemo> {
    var personas: Lista<Persona> = []
    personas.agregar(crear_persona("Javier", 20)?)
    personas.agregar(crear_persona("C-Forge", 2)?)

    sea textos = esperar describir_en_paralelo(&personas)
    para texto en textos {
        mostrar(texto)
    }
    retornar correcto(())
}
)CFV35DATA"},
        {R"CFV36DATA(bootstrap/core_lexer.cfv)CFV36DATA", R"CFV37DATA(// Primer componente del compilador de C-Forge escrito en C-Forge.
// Alcance congelado: lexer de C-Forge Core Bootstrap 0.1.

estructura TokenCore {
    tipo: texto
    lexema: texto
    linea: numero
}

funcion es_espacio(caracter: texto): booleano {
    retornar caracter == " " o caracter == "\t" o caracter == "\r"
}

funcion es_digito(caracter: texto): booleano {
    retornar caracter >= "0" y caracter <= "9"
}

funcion es_letra(caracter: texto): booleano {
    retornar (caracter >= "a" y caracter <= "z") o
        (caracter >= "A" y caracter <= "Z") o caracter == "_"
}

funcion es_identificador(caracter: texto): booleano {
    retornar es_letra(caracter) o es_digito(caracter)
}

funcion tokenizar_core(fuente: texto): cualquiera {
    sea tokens: lista = []
    sea posicion: numero = 0
    sea linea: numero = 1

    mientras (posicion < longitud(fuente)) {
        sea actual: texto = fuente[posicion]

        si (actual == "\n") {
            linea = linea + 1
            posicion = posicion + 1
        } sino {
            si (es_espacio(actual)) {
                posicion = posicion + 1
            } sino {
                si (actual == "/" y posicion + 1 < longitud(fuente) y fuente[posicion + 1] == "/") {
                    mientras (posicion < longitud(fuente) y fuente[posicion] != "\n") {
                        posicion = posicion + 1
                    }
                } sino {
                    si (es_letra(actual)) {
                        sea inicio: numero = posicion
                        mientras (posicion < longitud(fuente) y es_identificador(fuente[posicion])) {
                            posicion = posicion + 1
                        }
                        sea lexema: texto = ""
                        sea cursor: numero = inicio
                        mientras (cursor < posicion) {
                            lexema = lexema + fuente[cursor]
                            cursor = cursor + 1
                        }
                        agregar(tokens, TokenCore("IDENT", lexema, linea))
                    } sino {
                        si (es_digito(actual)) {
                            sea inicio_numero: numero = posicion
                            mientras (posicion < longitud(fuente) y es_digito(fuente[posicion])) {
                                posicion = posicion + 1
                            }
                            sea numero_texto: texto = ""
                            sea cursor_numero: numero = inicio_numero
                            mientras (cursor_numero < posicion) {
                                numero_texto = numero_texto + fuente[cursor_numero]
                                cursor_numero = cursor_numero + 1
                            }
                            agregar(tokens, TokenCore("NUMBER", numero_texto, linea))
                        } sino {
                            si (actual == "\"") {
                                sea linea_texto: numero = linea
                                sea literal: texto = "\""
                                posicion = posicion + 1
                                sea cerrado: booleano = falso
                                mientras (posicion < longitud(fuente) y no cerrado) {
                                    sea parte: texto = fuente[posicion]
                                    literal = literal + parte
                                    posicion = posicion + 1
                                    si (parte == "\\" y posicion < longitud(fuente)) {
                                        literal = literal + fuente[posicion]
                                        posicion = posicion + 1
                                    } sino {
                                        si (parte == "\"") {
                                            cerrado = verdadero
                                        } sino {
                                            si (parte == "\n") {
                                                linea = linea + 1
                                            }
                                        }
                                    }
                                }
                                afirmar(cerrado, "texto sin cerrar en línea " + a_texto(linea_texto))
                                agregar(tokens, TokenCore("STRING", literal, linea_texto))
                            } sino {
                                sea simbolo: texto = actual
                                posicion = posicion + 1
                                si (posicion < longitud(fuente)) {
                                    sea par: texto = simbolo + fuente[posicion]
                                    si (par == "==" o par == "!=" o par == ">=" o par == "<=") {
                                        simbolo = par
                                        posicion = posicion + 1
                                    }
                                }
                                agregar(tokens, TokenCore("SYMBOL", simbolo, linea))
                            }
                        }
                    }
                }
            }
        }
    }

    agregar(tokens, TokenCore("EOF", "", linea))
    retornar tokens
}

sea muestra: texto = "sea respuesta: numero = 40 + 2\nmostrar(respuesta)\n"
sea resultado: lista = tokenizar_core(muestra)

test "lexer core reconoce el programa mínimo" {
    afirmar(longitud(resultado) == 13, "cantidad inesperada de tokens")
    afirmar(resultado[0].tipo == "IDENT", "sea debe ser identificador")
    afirmar(resultado[0].lexema == "sea", "primer lexema incorrecto")
    afirmar(resultado[4].tipo == "SYMBOL", "asignación no reconocida")
    afirmar(resultado[12].tipo == "EOF", "falta token EOF")
}
)CFV37DATA"},
        {R"CFV38DATA(bootstrap/core_ast.cfv)CFV38DATA", R"CFV39DATA(// AST canónico de C-Forge Core Bootstrap 0.4.
// No contiene objetos del runtime anfitrión: solo textos, números y listas.

estructura NodoASTCore {
    tipo: texto
    valor: texto
    linea: numero
    hijos: lista
}

funcion nodo_ast_core(tipo: texto, valor: texto, linea: numero): cualquiera {
    retornar NodoASTCore(tipo, valor, linea, [])
}

funcion nodo_ast_core_con_hijos(
    tipo: texto,
    valor: texto,
    linea: numero,
    hijos: lista
): cualquiera {
    retornar NodoASTCore(tipo, valor, linea, hijos)
}

funcion escapar_ast_core(valor: texto): texto {
    sea salida: texto = ""
    sea posicion: numero = 0
    mientras (posicion < longitud(valor)) {
        sea caracter: texto = valor[posicion]
        si (caracter == "\\") {
            salida = salida + "\\\\"
        } sino {
            si (caracter == "\n") {
                salida = salida + "\\n"
            } sino {
                si (caracter == "\r") {
                    salida = salida + "\\r"
                } sino {
                    si (caracter == "\t") {
                        salida = salida + "\\t"
                    } sino {
                        si (caracter == "[" o caracter == "]" o caracter == ":" o caracter == "@") {
                            salida = salida + "\\" + caracter
                        } sino {
                            salida = salida + caracter
                        }
                    }
                }
            }
        }
        posicion = posicion + 1
    }
    retornar salida
}

// Formato estable Core AST 1:
// tipo:largo:valor@linea[hijo,hijo]
// El largo corresponde al valor original y evita interpretaciones ambiguas.
funcion ast_core_canonico(nodo: cualquiera): texto {
    sea salida: texto = nodo.tipo + ":" + a_texto(longitud(nodo.valor)) + ":" +
        escapar_ast_core(nodo.valor) + "@" + a_texto(nodo.linea) + "["
    sea hijos: lista = nodo.hijos
    sea indice: numero = 0
    mientras (indice < longitud(hijos)) {
        si (indice > 0) {
            salida = salida + ","
        }
        salida = salida + ast_core_canonico(hijos[indice])
        indice = indice + 1
    }
    retornar salida + "]"
}
)CFV39DATA"},
        {R"CFV40DATA(bootstrap/core_parser.cfv)CFV40DATA", R"CFV41DATA(// Parser recursivo descendente de C-Forge Core Bootstrap 0.4.
// Requiere TokenCore de core_lexer.cfv y NodoASTCore de core_ast.cfv.

clase EstadoParserCore {
    campo tokens: lista
    campo posicion: numero

    metodo actual(): cualquiera {
        sea tokens_actuales: lista = este.tokens
        sea posicion_actual: numero = este.posicion
        retornar tokens_actuales[posicion_actual]
    }

    metodo anterior(): cualquiera {
        sea tokens_actuales: lista = este.tokens
        sea posicion_actual: numero = este.posicion
        retornar tokens_actuales[posicion_actual - 1]
    }

    metodo finalizado(): booleano {
        sea token: cualquiera = este.actual()
        retornar token.tipo == "EOF"
    }

    metodo avanzar(): cualquiera {
        si (no este.finalizado()) {
            este.posicion = este.posicion + 1
        }
        retornar este.anterior()
    }
}

funcion token_actual_core(estado: cualquiera): cualquiera {
    retornar estado.actual()
}

funcion token_anterior_core(estado: cualquiera): cualquiera {
    retornar estado.anterior()
}

funcion esta_al_final_core(estado: cualquiera): booleano {
    sea token: cualquiera = token_actual_core(estado)
    retornar token.tipo == "EOF"
}

funcion avanzar_parser_core(estado: cualquiera): cualquiera {
    retornar estado.avanzar()
}

funcion comprobar_lexema_core(estado: cualquiera, lexema: texto): booleano {
    sea token: cualquiera = token_actual_core(estado)
    retornar token.lexema == lexema
}

funcion tomar_lexema_core(estado: cualquiera, lexema: texto): booleano {
    si (comprobar_lexema_core(estado, lexema)) {
        avanzar_parser_core(estado)
        retornar verdadero
    }
    retornar falso
}

funcion requerir_lexema_core(
    estado: cualquiera,
    lexema: texto,
    mensaje: texto
): cualquiera {
    sea token: cualquiera = token_actual_core(estado)
    afirmar(token.lexema == lexema, mensaje + " en línea " + a_texto(token.linea))
    retornar avanzar_parser_core(estado)
}

funcion requerir_tipo_core(
    estado: cualquiera,
    tipo: texto,
    mensaje: texto
): cualquiera {
    sea token: cualquiera = token_actual_core(estado)
    afirmar(token.tipo == tipo, mensaje + " en línea " + a_texto(token.linea))
    retornar avanzar_parser_core(estado)
}

funcion primaria_parser_core(estado: cualquiera): cualquiera {
    sea token: cualquiera = token_actual_core(estado)
    si (token.tipo == "NUMBER") {
        avanzar_parser_core(estado)
        retornar nodo_ast_core("Numero", token.lexema, token.linea)
    }
    si (token.tipo == "STRING") {
        avanzar_parser_core(estado)
        retornar nodo_ast_core("Texto", token.lexema, token.linea)
    }
    si (token.tipo == "IDENT") {
        avanzar_parser_core(estado)
        si (token.lexema == "mover" y tomar_lexema_core(estado, "(")) {
            sea movido: cualquiera = expresion_parser_core(estado)
            requerir_lexema_core(
                estado, ")", "se esperaba ')' después de mover"
            )
            retornar nodo_ast_core_con_hijos(
                "Mover", "mover", token.linea, [movido]
            )
        }
        retornar nodo_ast_core("Identificador", token.lexema, token.linea)
    }
    si (tomar_lexema_core(estado, "(")) {
        sea expresion: cualquiera = expresion_parser_core(estado)
        requerir_lexema_core(estado, ")", "se esperaba ')' después de la expresión")
        retornar expresion
    }
    afirmar(falso, "expresión inválida en línea " + a_texto(token.linea))
    retornar nodo_ast_core("Inalcanzable", "", token.linea)
}

funcion producto_parser_core(estado: cualquiera): cualquiera {
    sea expresion: cualquiera = primaria_parser_core(estado)
    mientras (comprobar_lexema_core(estado, "*") o comprobar_lexema_core(estado, "/")) {
        sea operador: cualquiera = avanzar_parser_core(estado)
        sea derecho: cualquiera = primaria_parser_core(estado)
        expresion = nodo_ast_core_con_hijos(
            "Binario", operador.lexema, operador.linea, [expresion, derecho]
        )
    }
    retornar expresion
}

funcion expresion_parser_core(estado: cualquiera): cualquiera {
    sea expresion: cualquiera = producto_parser_core(estado)
    mientras (comprobar_lexema_core(estado, "+") o comprobar_lexema_core(estado, "-")) {
        sea operador: cualquiera = avanzar_parser_core(estado)
        sea derecho: cualquiera = producto_parser_core(estado)
        expresion = nodo_ast_core_con_hijos(
            "Binario", operador.lexema, operador.linea, [expresion, derecho]
        )
    }
    retornar expresion
}

funcion declaracion_parser_core(estado: cualquiera): cualquiera {
    sea palabra: cualquiera = requerir_lexema_core(
        estado, "sea", "se esperaba la declaración 'sea'"
    )
    sea nombre: cualquiera = requerir_tipo_core(
        estado, "IDENT", "se esperaba el nombre de la variable"
    )
    sea tipo_declarado: texto = ""
    si (tomar_lexema_core(estado, ":")) {
        sea token_tipo: cualquiera = requerir_tipo_core(
            estado, "IDENT", "se esperaba el tipo de la variable"
        )
        tipo_declarado = token_tipo.lexema
    }
    requerir_lexema_core(estado, "=", "se esperaba '=' en la declaración")
    sea valor: cualquiera = expresion_parser_core(estado)
    tomar_lexema_core(estado, ";")
    retornar nodo_ast_core_con_hijos(
        "Declaracion", nombre.lexema + ":" + tipo_declarado,
        palabra.linea, [valor]
    )
}

funcion impresion_parser_core(estado: cualquiera): cualquiera {
    sea palabra: cualquiera = avanzar_parser_core(estado)
    requerir_lexema_core(estado, "(", "se esperaba '(' después de mostrar")
    sea valor: cualquiera = expresion_parser_core(estado)
    requerir_lexema_core(estado, ")", "se esperaba ')' después del valor")
    tomar_lexema_core(estado, ";")
    retornar nodo_ast_core_con_hijos(
        "Mostrar", palabra.lexema, palabra.linea, [valor]
    )
}

funcion sentencia_parser_core(estado: cualquiera): cualquiera {
    si (comprobar_lexema_core(estado, "sea")) {
        retornar declaracion_parser_core(estado)
    }
    si (comprobar_lexema_core(estado, "mostrar") o comprobar_lexema_core(estado, "print")) {
        retornar impresion_parser_core(estado)
    }
    sea token: cualquiera = token_actual_core(estado)
    afirmar(
        falso,
        "sentencia Core desconocida '" + token.lexema +
        "' en línea " + a_texto(token.linea)
    )
    retornar nodo_ast_core("Inalcanzable", "", token.linea)
}

funcion parsear_tokens_core(tokens: lista): cualquiera {
    sea estado: cualquiera = EstadoParserCore(tokens, 0)
    sea sentencias: lista = []
    mientras (no esta_al_final_core(estado)) {
        agregar(sentencias, sentencia_parser_core(estado))
    }
    sea eof: cualquiera = token_actual_core(estado)
    retornar nodo_ast_core_con_hijos("Programa", "Core-0.4", eof.linea, sentencias)
}

funcion parsear_fuente_core(fuente: texto): cualquiera {
    retornar parsear_tokens_core(tokenizar_core(fuente))
}
)CFV41DATA"},
        {R"CFV42DATA(bootstrap/core_semantics.cfv)CFV42DATA", R"CFV43DATA(// Analizador de tipos y ownership de C-Forge Core Bootstrap 0.4.
// Opera únicamente sobre NodoASTCore y no depende del runtime anfitrión.

clase SimboloCore {
    campo nombre: texto
    campo tipo: texto
    campo movido: booleano

    metodo marcar_movido() {
        este.movido = verdadero
    }
}

estructura ResultadoSemanticoCore {
    valido: booleano
    errores: lista
}

funcion diagnostico_core(
    codigo: texto,
    linea: numero,
    mensaje: texto
): texto {
    retornar codigo + " línea " + a_texto(linea) + ": " + mensaje
}

funcion indice_dos_puntos_core(valor: texto): numero {
    sea posicion: numero = 0
    mientras (posicion < longitud(valor)) {
        si (valor[posicion] == ":") {
            retornar posicion
        }
        posicion = posicion + 1
    }
    retornar -1
}

funcion segmento_core(
    valor: texto,
    inicio: numero,
    final: numero
): texto {
    sea salida: texto = ""
    sea posicion: numero = inicio
    mientras (posicion < final) {
        salida = salida + valor[posicion]
        posicion = posicion + 1
    }
    retornar salida
}

funcion nombre_declaracion_core(valor: texto): texto {
    sea separador: numero = indice_dos_puntos_core(valor)
    si (separador < 0) {
        retornar valor
    }
    retornar segmento_core(valor, 0, separador)
}

funcion tipo_declaracion_core(valor: texto): texto {
    sea separador: numero = indice_dos_puntos_core(valor)
    si (separador < 0) {
        retornar ""
    }
    retornar segmento_core(valor, separador + 1, longitud(valor))
}

funcion buscar_simbolo_core(simbolos: lista, nombre: texto): numero {
    sea indice: numero = 0
    mientras (indice < longitud(simbolos)) {
        sea simbolo: cualquiera = simbolos[indice]
        si (simbolo.nombre == nombre) {
            retornar indice
        }
        indice = indice + 1
    }
    retornar -1
}

funcion tipo_identificador_core(
    nodo: cualquiera,
    simbolos: lista,
    errores: lista
): texto {
    sea indice: numero = buscar_simbolo_core(simbolos, nodo.valor)
    si (indice < 0) {
        agregar(
            errores,
            diagnostico_core(
                "CFB2002", nodo.linea,
                "variable no declarada '" + nodo.valor + "'"
            )
        )
        retornar "error"
    }
    sea simbolo: cualquiera = simbolos[indice]
    si (simbolo.movido) {
        agregar(
            errores,
            diagnostico_core(
                "CFB2003", nodo.linea,
                "uso después de mover '" + nodo.valor + "'"
            )
        )
        retornar "error"
    }
    retornar simbolo.tipo
}

funcion tipo_expresion_core(
    nodo: cualquiera,
    simbolos: lista,
    errores: lista
): texto {
    si (nodo.tipo == "Numero") {
        retornar "numero"
    }
    si (nodo.tipo == "Texto") {
        retornar "texto"
    }
    si (nodo.tipo == "Identificador") {
        retornar tipo_identificador_core(nodo, simbolos, errores)
    }
    si (nodo.tipo == "Mover") {
        sea hijos_mover: lista = nodo.hijos
        sea objetivo: cualquiera = hijos_mover[0]
        si (objetivo.tipo != "Identificador") {
            agregar(
                errores,
                diagnostico_core(
                    "CFB2004", nodo.linea,
                    "mover requiere una variable identificable"
                )
            )
            retornar "error"
        }
        sea indice: numero = buscar_simbolo_core(simbolos, objetivo.valor)
        sea tipo_objetivo: texto =
            tipo_identificador_core(objetivo, simbolos, errores)
        si (indice >= 0 y tipo_objetivo == "texto") {
            sea simbolo: cualquiera = simbolos[indice]
            simbolo.marcar_movido()
        }
        retornar tipo_objetivo
    }
    si (nodo.tipo == "Binario") {
        sea hijos_binarios: lista = nodo.hijos
        sea tipo_izquierdo: texto =
            tipo_expresion_core(hijos_binarios[0], simbolos, errores)
        sea tipo_derecho: texto =
            tipo_expresion_core(hijos_binarios[1], simbolos, errores)
        si (tipo_izquierdo == "error" o tipo_derecho == "error") {
            retornar "error"
        }
        si (nodo.valor == "+") {
            si (tipo_izquierdo == tipo_derecho y
                (tipo_izquierdo == "numero" o tipo_izquierdo == "texto")) {
                retornar tipo_izquierdo
            }
        } sino {
            si (tipo_izquierdo == "numero" y tipo_derecho == "numero") {
                retornar "numero"
            }
        }
        agregar(
            errores,
            diagnostico_core(
                "CFB2001", nodo.linea,
                "operador '" + nodo.valor + "' incompatible con " +
                tipo_izquierdo + " y " + tipo_derecho
            )
        )
        retornar "error"
    }
    agregar(
        errores,
        diagnostico_core(
            "CFB2099", nodo.linea,
            "nodo de expresión desconocido '" + nodo.tipo + "'"
        )
    )
    retornar "error"
}

funcion analizar_sentencia_core(
    nodo: cualquiera,
    simbolos: lista,
    errores: lista
) {
    sea hijos: lista = nodo.hijos
    si (nodo.tipo == "Declaracion") {
        sea nombre: texto = nombre_declaracion_core(nodo.valor)
        sea declarado: texto = tipo_declaracion_core(nodo.valor)
        si (buscar_simbolo_core(simbolos, nombre) >= 0) {
            agregar(
                errores,
                diagnostico_core(
                    "CFB2005", nodo.linea,
                    "variable duplicada '" + nombre + "'"
                )
            )
            retornar errores
        }
        sea inferido: texto =
            tipo_expresion_core(hijos[0], simbolos, errores)
        sea tipo_final: texto = inferido
        si (declarado != "") {
            tipo_final = declarado
            si (inferido != "error" y declarado != inferido) {
                agregar(
                    errores,
                    diagnostico_core(
                        "CFB2001", nodo.linea,
                        "la variable '" + nombre + "' requiere " +
                        declarado + " pero recibió " + inferido
                    )
                )
            }
        }
        agregar(simbolos, SimboloCore(nombre, tipo_final, falso))
        retornar errores
    }
    si (nodo.tipo == "Mostrar") {
        tipo_expresion_core(hijos[0], simbolos, errores)
        retornar errores
    }
    agregar(
        errores,
        diagnostico_core(
            "CFB2098", nodo.linea,
            "sentencia desconocida '" + nodo.tipo + "'"
        )
    )
}

funcion analizar_semantica_core(programa: cualquiera): cualquiera {
    sea simbolos: lista = []
    sea errores: lista = []
    sea sentencias: lista = programa.hijos
    sea indice: numero = 0
    mientras (indice < longitud(sentencias)) {
        analizar_sentencia_core(sentencias[indice], simbolos, errores)
        indice = indice + 1
    }
    retornar ResultadoSemanticoCore(longitud(errores) == 0, errores)
}

funcion diagnosticos_semanticos_core(resultado: cualquiera): texto {
    sea errores: lista = resultado.errores
    sea salida: texto = ""
    sea indice: numero = 0
    mientras (indice < longitud(errores)) {
        si (indice > 0) {
            salida = salida + "\n"
        }
        salida = salida + errores[indice]
        indice = indice + 1
    }
    retornar salida
}
)CFV43DATA"},
        {R"CFV44DATA(bootstrap/core_emitter.cfv)CFV44DATA", R"CFV45DATA(// Emisor nativo de C-Forge Core Bootstrap 0.4.
// Recibe el AST canónico validado por B2 y genera una unidad C++17 completa.

estructura ResultadoEmisionCore {
    valido: booleano
    codigo: texto
    errores: lista
}

funcion nombre_cpp_core(nombre: texto): texto {
    retornar "cfv_" + nombre
}

funcion encabezado_cpp_core(): texto {
    retornar
        "#include <cmath>\n" +
        "#include <iomanip>\n" +
        "#include <iostream>\n" +
        "#include <sstream>\n" +
        "#include <stdexcept>\n" +
        "#include <string>\n" +
        "#include <variant>\n\n" +
        "struct Valor {\n" +
        "    std::variant<double, std::string> dato;\n" +
        "    explicit Valor(double valor) : dato(valor) {}\n" +
        "    explicit Valor(std::string valor) : dato(std::move(valor)) {}\n" +
        "};\n\n" +
        "static double numero(const Valor& valor) {\n" +
        "    if (const auto* n = std::get_if<double>(&valor.dato)) return *n;\n" +
        "    throw std::runtime_error(\"se esperaba numero\");\n" +
        "}\n" +
        "static Valor sumar(const Valor& a, const Valor& b) {\n" +
        "    if (const auto* x = std::get_if<double>(&a.dato)) {\n" +
        "        if (const auto* y = std::get_if<double>(&b.dato)) return Valor(*x + *y);\n" +
        "    }\n" +
        "    if (const auto* x = std::get_if<std::string>(&a.dato)) {\n" +
        "        if (const auto* y = std::get_if<std::string>(&b.dato)) return Valor(*x + *y);\n" +
        "    }\n" +
        "    throw std::runtime_error(\"tipos incompatibles para +\");\n" +
        "}\n" +
        "static Valor restar(const Valor& a, const Valor& b) {\n" +
        "    return Valor(numero(a) - numero(b));\n" +
        "}\n" +
        "static Valor multiplicar(const Valor& a, const Valor& b) {\n" +
        "    return Valor(numero(a) * numero(b));\n" +
        "}\n" +
        "static Valor dividir(const Valor& a, const Valor& b) {\n" +
        "    const double divisor = numero(b);\n" +
        "    if (divisor == 0) throw std::runtime_error(\"division por cero\");\n" +
        "    return Valor(numero(a) / divisor);\n" +
        "}\n" +
        "static Valor mover_core(const Valor& valor) { return valor; }\n" +
        "static void mostrar_core(const Valor& valor) {\n" +
        "    if (const auto* texto = std::get_if<std::string>(&valor.dato)) {\n" +
        "        std::cout << *texto << '\\n';\n" +
        "        return;\n" +
        "    }\n" +
        "    const double n = numero(valor);\n" +
        "    if (std::floor(n) == n) {\n" +
        "        std::cout << static_cast<long long>(n) << '\\n';\n" +
        "    } else {\n" +
        "        std::ostringstream salida;\n" +
        "        salida << std::setprecision(15) << n;\n" +
        "        std::cout << salida.str() << '\\n';\n" +
        "    }\n" +
        "}\n\n" +
        "int main() {\n" +
        "    try {\n"
}

funcion pie_cpp_core(): texto {
    retornar
        "        return 0;\n" +
        "    } catch (const std::exception& error) {\n" +
        "        std::cerr << \"[C-Forge Core Runtime Error] \" << error.what() << '\\n';\n" +
        "        return 1;\n" +
        "    }\n" +
        "}\n"
}

funcion emitir_expresion_core(nodo: cualquiera): texto {
    si (nodo.tipo == "Numero") {
        retornar "Valor(" + nodo.valor + ")"
    }
    si (nodo.tipo == "Texto") {
        retornar "Valor(std::string(" + nodo.valor + "))"
    }
    si (nodo.tipo == "Identificador") {
        retornar nombre_cpp_core(nodo.valor)
    }
    sea hijos: lista = nodo.hijos
    si (nodo.tipo == "Mover") {
        retornar "mover_core(" + emitir_expresion_core(hijos[0]) + ")"
    }
    si (nodo.tipo == "Binario") {
        sea izquierdo: texto = emitir_expresion_core(hijos[0])
        sea derecho: texto = emitir_expresion_core(hijos[1])
        si (nodo.valor == "+") {
            retornar "sumar(" + izquierdo + ", " + derecho + ")"
        }
        si (nodo.valor == "-") {
            retornar "restar(" + izquierdo + ", " + derecho + ")"
        }
        si (nodo.valor == "*") {
            retornar "multiplicar(" + izquierdo + ", " + derecho + ")"
        }
        si (nodo.valor == "/") {
            retornar "dividir(" + izquierdo + ", " + derecho + ")"
        }
    }
    afirmar(
        falso,
        "B3 no puede emitir el nodo de expresión '" + nodo.tipo + "'"
    )
    retornar ""
}

funcion emitir_sentencia_core(nodo: cualquiera): texto {
    sea hijos: lista = nodo.hijos
    si (nodo.tipo == "Declaracion") {
        sea nombre: texto = nombre_declaracion_core(nodo.valor)
        retornar
            "        Valor " + nombre_cpp_core(nombre) + " = " +
            emitir_expresion_core(hijos[0]) + ";\n"
    }
    si (nodo.tipo == "Mostrar") {
        retornar
            "        mostrar_core(" +
            emitir_expresion_core(hijos[0]) + ");\n"
    }
    afirmar(falso, "B3 no puede emitir la sentencia '" + nodo.tipo + "'")
    retornar ""
}

funcion emitir_programa_core(programa: cualquiera): cualquiera {
    sea semantica: cualquiera = analizar_semantica_core(programa)
    si (no semantica.valido) {
        retornar ResultadoEmisionCore(falso, "", semantica.errores)
    }
    sea codigo: texto = encabezado_cpp_core()
    sea sentencias: lista = programa.hijos
    sea indice: numero = 0
    mientras (indice < longitud(sentencias)) {
        codigo = codigo + emitir_sentencia_core(sentencias[indice])
        indice = indice + 1
    }
    codigo = codigo + pie_cpp_core()
    retornar ResultadoEmisionCore(verdadero, codigo, [])
}
)CFV45DATA"},
        {R"CFV46DATA(bootstrap/core_driver.cfv)CFV46DATA", R"CFV47DATA(// Controlador principal de C-Forge Core Stage 1 — Bootstrap B4.
// Conecta lexer -> parser -> semántica/ownership -> emisor -> clang++.

estructura ResultadoCompilacionCore {
    valido: booleano
    codigo: texto
    errores: lista
}

funcion compilar_fuente_stage1(fuente: texto): cualquiera {
    sea tokens: lista = tokenizar_core(fuente)
    sea programa: cualquiera = parsear_tokens_core(tokens)
    sea semantica: cualquiera = analizar_semantica_core(programa)
    si (no semantica.valido) {
        retornar ResultadoCompilacionCore(
            falso,
            "",
            semantica.errores
        )
    }
    sea emision: cualquiera = emitir_programa_core(programa)
    retornar ResultadoCompilacionCore(
        emision.valido,
        emision.codigo,
        emision.errores
    )
}

funcion diagnosticos_stage1(resultado: cualquiera): texto {
    sea errores: lista = resultado.errores
    sea salida: texto = ""
    sea indice: numero = 0
    mientras (indice < longitud(errores)) {
        si (indice > 0) {
            salida = salida + "\n"
        }
        salida = salida + errores[indice]
        indice = indice + 1
    }
    retornar salida
}

sea argumentos_stage1: lista = argumentos_programa()
afirmar(
    longitud(argumentos_stage1) == 4,
    "uso: cforge-stage1 archivo.cfv -o ejecutable"
)
afirmar(
    argumentos_stage1[2] == "-o",
    "uso: cforge-stage1 archivo.cfv -o ejecutable"
)

sea entrada_stage1: texto = argumentos_stage1[1]
sea salida_stage1: texto = argumentos_stage1[3]
sea fuente_stage1: texto = leer_archivo(entrada_stage1)
sea resultado_stage1: cualquiera = compilar_fuente_stage1(fuente_stage1)
afirmar(
    resultado_stage1.valido,
    diagnosticos_stage1(resultado_stage1)
)

sea temporal_stage1: texto = salida_stage1 + ".stage1.cpp"
escribir_archivo(temporal_stage1, resultado_stage1.codigo)
sea nativo_stage1: booleano =
    compilar_cpp_nativo(temporal_stage1, salida_stage1)
eliminar_archivo(temporal_stage1)
afirmar(
    nativo_stage1,
    "el backend C++ no pudo producir el ejecutable nativo"
)
mostrar("C-Forge Stage 1 creó: " + salida_stage1)
)CFV47DATA"},
        {R"CFV48DATA(bootstrap/stage1/cforge_stage1.cfv)CFV48DATA", R"CFV49DATA(// C-Forge Stage 1 Bootstrap B4.
// Archivo generado únicamente a partir de componentes escritos en .cfv.

// ===== bootstrap/core_lexer.cfv =====
// Primer componente del compilador de C-Forge escrito en C-Forge.
// Alcance congelado: lexer de C-Forge Core Bootstrap 0.1.

estructura TokenCore {
    tipo: texto
    lexema: texto
    linea: numero
}

funcion es_espacio(caracter: texto): booleano {
    retornar caracter == " " o caracter == "\t" o caracter == "\r"
}

funcion es_digito(caracter: texto): booleano {
    retornar caracter >= "0" y caracter <= "9"
}

funcion es_letra(caracter: texto): booleano {
    retornar (caracter >= "a" y caracter <= "z") o
        (caracter >= "A" y caracter <= "Z") o caracter == "_"
}

funcion es_identificador(caracter: texto): booleano {
    retornar es_letra(caracter) o es_digito(caracter)
}

funcion tokenizar_core(fuente: texto): cualquiera {
    sea tokens: lista = []
    sea posicion: numero = 0
    sea linea: numero = 1

    mientras (posicion < longitud(fuente)) {
        sea actual: texto = fuente[posicion]

        si (actual == "\n") {
            linea = linea + 1
            posicion = posicion + 1
        } sino {
            si (es_espacio(actual)) {
                posicion = posicion + 1
            } sino {
                si (actual == "/" y posicion + 1 < longitud(fuente) y fuente[posicion + 1] == "/") {
                    mientras (posicion < longitud(fuente) y fuente[posicion] != "\n") {
                        posicion = posicion + 1
                    }
                } sino {
                    si (es_letra(actual)) {
                        sea inicio: numero = posicion
                        mientras (posicion < longitud(fuente) y es_identificador(fuente[posicion])) {
                            posicion = posicion + 1
                        }
                        sea lexema: texto = ""
                        sea cursor: numero = inicio
                        mientras (cursor < posicion) {
                            lexema = lexema + fuente[cursor]
                            cursor = cursor + 1
                        }
                        agregar(tokens, TokenCore("IDENT", lexema, linea))
                    } sino {
                        si (es_digito(actual)) {
                            sea inicio_numero: numero = posicion
                            mientras (posicion < longitud(fuente) y es_digito(fuente[posicion])) {
                                posicion = posicion + 1
                            }
                            sea numero_texto: texto = ""
                            sea cursor_numero: numero = inicio_numero
                            mientras (cursor_numero < posicion) {
                                numero_texto = numero_texto + fuente[cursor_numero]
                                cursor_numero = cursor_numero + 1
                            }
                            agregar(tokens, TokenCore("NUMBER", numero_texto, linea))
                        } sino {
                            si (actual == "\"") {
                                sea linea_texto: numero = linea
                                sea literal: texto = "\""
                                posicion = posicion + 1
                                sea cerrado: booleano = falso
                                mientras (posicion < longitud(fuente) y no cerrado) {
                                    sea parte: texto = fuente[posicion]
                                    literal = literal + parte
                                    posicion = posicion + 1
                                    si (parte == "\\" y posicion < longitud(fuente)) {
                                        literal = literal + fuente[posicion]
                                        posicion = posicion + 1
                                    } sino {
                                        si (parte == "\"") {
                                            cerrado = verdadero
                                        } sino {
                                            si (parte == "\n") {
                                                linea = linea + 1
                                            }
                                        }
                                    }
                                }
                                afirmar(cerrado, "texto sin cerrar en línea " + a_texto(linea_texto))
                                agregar(tokens, TokenCore("STRING", literal, linea_texto))
                            } sino {
                                sea simbolo: texto = actual
                                posicion = posicion + 1
                                si (posicion < longitud(fuente)) {
                                    sea par: texto = simbolo + fuente[posicion]
                                    si (par == "==" o par == "!=" o par == ">=" o par == "<=") {
                                        simbolo = par
                                        posicion = posicion + 1
                                    }
                                }
                                agregar(tokens, TokenCore("SYMBOL", simbolo, linea))
                            }
                        }
                    }
                }
            }
        }
    }

    agregar(tokens, TokenCore("EOF", "", linea))
    retornar tokens
}

sea muestra: texto = "sea respuesta: numero = 40 + 2\nmostrar(respuesta)\n"
sea resultado: lista = tokenizar_core(muestra)

// ===== bootstrap/core_ast.cfv =====
// AST canónico de C-Forge Core Bootstrap 0.4.
// No contiene objetos del runtime anfitrión: solo textos, números y listas.

estructura NodoASTCore {
    tipo: texto
    valor: texto
    linea: numero
    hijos: lista
}

funcion nodo_ast_core(tipo: texto, valor: texto, linea: numero): cualquiera {
    retornar NodoASTCore(tipo, valor, linea, [])
}

funcion nodo_ast_core_con_hijos(
    tipo: texto,
    valor: texto,
    linea: numero,
    hijos: lista
): cualquiera {
    retornar NodoASTCore(tipo, valor, linea, hijos)
}

funcion escapar_ast_core(valor: texto): texto {
    sea salida: texto = ""
    sea posicion: numero = 0
    mientras (posicion < longitud(valor)) {
        sea caracter: texto = valor[posicion]
        si (caracter == "\\") {
            salida = salida + "\\\\"
        } sino {
            si (caracter == "\n") {
                salida = salida + "\\n"
            } sino {
                si (caracter == "\r") {
                    salida = salida + "\\r"
                } sino {
                    si (caracter == "\t") {
                        salida = salida + "\\t"
                    } sino {
                        si (caracter == "[" o caracter == "]" o caracter == ":" o caracter == "@") {
                            salida = salida + "\\" + caracter
                        } sino {
                            salida = salida + caracter
                        }
                    }
                }
            }
        }
        posicion = posicion + 1
    }
    retornar salida
}

// Formato estable Core AST 1:
// tipo:largo:valor@linea[hijo,hijo]
// El largo corresponde al valor original y evita interpretaciones ambiguas.
funcion ast_core_canonico(nodo: cualquiera): texto {
    sea salida: texto = nodo.tipo + ":" + a_texto(longitud(nodo.valor)) + ":" +
        escapar_ast_core(nodo.valor) + "@" + a_texto(nodo.linea) + "["
    sea hijos: lista = nodo.hijos
    sea indice: numero = 0
    mientras (indice < longitud(hijos)) {
        si (indice > 0) {
            salida = salida + ","
        }
        salida = salida + ast_core_canonico(hijos[indice])
        indice = indice + 1
    }
    retornar salida + "]"
}

// ===== bootstrap/core_parser.cfv =====
// Parser recursivo descendente de C-Forge Core Bootstrap 0.4.
// Requiere TokenCore de core_lexer.cfv y NodoASTCore de core_ast.cfv.

clase EstadoParserCore {
    campo tokens: lista
    campo posicion: numero

    metodo actual(): cualquiera {
        sea tokens_actuales: lista = este.tokens
        sea posicion_actual: numero = este.posicion
        retornar tokens_actuales[posicion_actual]
    }

    metodo anterior(): cualquiera {
        sea tokens_actuales: lista = este.tokens
        sea posicion_actual: numero = este.posicion
        retornar tokens_actuales[posicion_actual - 1]
    }

    metodo finalizado(): booleano {
        sea token: cualquiera = este.actual()
        retornar token.tipo == "EOF"
    }

    metodo avanzar(): cualquiera {
        si (no este.finalizado()) {
            este.posicion = este.posicion + 1
        }
        retornar este.anterior()
    }
}

funcion token_actual_core(estado: cualquiera): cualquiera {
    retornar estado.actual()
}

funcion token_anterior_core(estado: cualquiera): cualquiera {
    retornar estado.anterior()
}

funcion esta_al_final_core(estado: cualquiera): booleano {
    sea token: cualquiera = token_actual_core(estado)
    retornar token.tipo == "EOF"
}

funcion avanzar_parser_core(estado: cualquiera): cualquiera {
    retornar estado.avanzar()
}

funcion comprobar_lexema_core(estado: cualquiera, lexema: texto): booleano {
    sea token: cualquiera = token_actual_core(estado)
    retornar token.lexema == lexema
}

funcion tomar_lexema_core(estado: cualquiera, lexema: texto): booleano {
    si (comprobar_lexema_core(estado, lexema)) {
        avanzar_parser_core(estado)
        retornar verdadero
    }
    retornar falso
}

funcion requerir_lexema_core(
    estado: cualquiera,
    lexema: texto,
    mensaje: texto
): cualquiera {
    sea token: cualquiera = token_actual_core(estado)
    afirmar(token.lexema == lexema, mensaje + " en línea " + a_texto(token.linea))
    retornar avanzar_parser_core(estado)
}

funcion requerir_tipo_core(
    estado: cualquiera,
    tipo: texto,
    mensaje: texto
): cualquiera {
    sea token: cualquiera = token_actual_core(estado)
    afirmar(token.tipo == tipo, mensaje + " en línea " + a_texto(token.linea))
    retornar avanzar_parser_core(estado)
}

funcion primaria_parser_core(estado: cualquiera): cualquiera {
    sea token: cualquiera = token_actual_core(estado)
    si (token.tipo == "NUMBER") {
        avanzar_parser_core(estado)
        retornar nodo_ast_core("Numero", token.lexema, token.linea)
    }
    si (token.tipo == "STRING") {
        avanzar_parser_core(estado)
        retornar nodo_ast_core("Texto", token.lexema, token.linea)
    }
    si (token.tipo == "IDENT") {
        avanzar_parser_core(estado)
        si (token.lexema == "mover" y tomar_lexema_core(estado, "(")) {
            sea movido: cualquiera = expresion_parser_core(estado)
            requerir_lexema_core(
                estado, ")", "se esperaba ')' después de mover"
            )
            retornar nodo_ast_core_con_hijos(
                "Mover", "mover", token.linea, [movido]
            )
        }
        retornar nodo_ast_core("Identificador", token.lexema, token.linea)
    }
    si (tomar_lexema_core(estado, "(")) {
        sea expresion: cualquiera = expresion_parser_core(estado)
        requerir_lexema_core(estado, ")", "se esperaba ')' después de la expresión")
        retornar expresion
    }
    afirmar(falso, "expresión inválida en línea " + a_texto(token.linea))
    retornar nodo_ast_core("Inalcanzable", "", token.linea)
}

funcion producto_parser_core(estado: cualquiera): cualquiera {
    sea expresion: cualquiera = primaria_parser_core(estado)
    mientras (comprobar_lexema_core(estado, "*") o comprobar_lexema_core(estado, "/")) {
        sea operador: cualquiera = avanzar_parser_core(estado)
        sea derecho: cualquiera = primaria_parser_core(estado)
        expresion = nodo_ast_core_con_hijos(
            "Binario", operador.lexema, operador.linea, [expresion, derecho]
        )
    }
    retornar expresion
}

funcion expresion_parser_core(estado: cualquiera): cualquiera {
    sea expresion: cualquiera = producto_parser_core(estado)
    mientras (comprobar_lexema_core(estado, "+") o comprobar_lexema_core(estado, "-")) {
        sea operador: cualquiera = avanzar_parser_core(estado)
        sea derecho: cualquiera = producto_parser_core(estado)
        expresion = nodo_ast_core_con_hijos(
            "Binario", operador.lexema, operador.linea, [expresion, derecho]
        )
    }
    retornar expresion
}

funcion declaracion_parser_core(estado: cualquiera): cualquiera {
    sea palabra: cualquiera = requerir_lexema_core(
        estado, "sea", "se esperaba la declaración 'sea'"
    )
    sea nombre: cualquiera = requerir_tipo_core(
        estado, "IDENT", "se esperaba el nombre de la variable"
    )
    sea tipo_declarado: texto = ""
    si (tomar_lexema_core(estado, ":")) {
        sea token_tipo: cualquiera = requerir_tipo_core(
            estado, "IDENT", "se esperaba el tipo de la variable"
        )
        tipo_declarado = token_tipo.lexema
    }
    requerir_lexema_core(estado, "=", "se esperaba '=' en la declaración")
    sea valor: cualquiera = expresion_parser_core(estado)
    tomar_lexema_core(estado, ";")
    retornar nodo_ast_core_con_hijos(
        "Declaracion", nombre.lexema + ":" + tipo_declarado,
        palabra.linea, [valor]
    )
}

funcion impresion_parser_core(estado: cualquiera): cualquiera {
    sea palabra: cualquiera = avanzar_parser_core(estado)
    requerir_lexema_core(estado, "(", "se esperaba '(' después de mostrar")
    sea valor: cualquiera = expresion_parser_core(estado)
    requerir_lexema_core(estado, ")", "se esperaba ')' después del valor")
    tomar_lexema_core(estado, ";")
    retornar nodo_ast_core_con_hijos(
        "Mostrar", palabra.lexema, palabra.linea, [valor]
    )
}

funcion sentencia_parser_core(estado: cualquiera): cualquiera {
    si (comprobar_lexema_core(estado, "sea")) {
        retornar declaracion_parser_core(estado)
    }
    si (comprobar_lexema_core(estado, "mostrar") o comprobar_lexema_core(estado, "print")) {
        retornar impresion_parser_core(estado)
    }
    sea token: cualquiera = token_actual_core(estado)
    afirmar(
        falso,
        "sentencia Core desconocida '" + token.lexema +
        "' en línea " + a_texto(token.linea)
    )
    retornar nodo_ast_core("Inalcanzable", "", token.linea)
}

funcion parsear_tokens_core(tokens: lista): cualquiera {
    sea estado: cualquiera = EstadoParserCore(tokens, 0)
    sea sentencias: lista = []
    mientras (no esta_al_final_core(estado)) {
        agregar(sentencias, sentencia_parser_core(estado))
    }
    sea eof: cualquiera = token_actual_core(estado)
    retornar nodo_ast_core_con_hijos("Programa", "Core-0.4", eof.linea, sentencias)
}

funcion parsear_fuente_core(fuente: texto): cualquiera {
    retornar parsear_tokens_core(tokenizar_core(fuente))
}

// ===== bootstrap/core_semantics.cfv =====
// Analizador de tipos y ownership de C-Forge Core Bootstrap 0.4.
// Opera únicamente sobre NodoASTCore y no depende del runtime anfitrión.

clase SimboloCore {
    campo nombre: texto
    campo tipo: texto
    campo movido: booleano

    metodo marcar_movido() {
        este.movido = verdadero
    }
}

estructura ResultadoSemanticoCore {
    valido: booleano
    errores: lista
}

funcion diagnostico_core(
    codigo: texto,
    linea: numero,
    mensaje: texto
): texto {
    retornar codigo + " línea " + a_texto(linea) + ": " + mensaje
}

funcion indice_dos_puntos_core(valor: texto): numero {
    sea posicion: numero = 0
    mientras (posicion < longitud(valor)) {
        si (valor[posicion] == ":") {
            retornar posicion
        }
        posicion = posicion + 1
    }
    retornar -1
}

funcion segmento_core(
    valor: texto,
    inicio: numero,
    final: numero
): texto {
    sea salida: texto = ""
    sea posicion: numero = inicio
    mientras (posicion < final) {
        salida = salida + valor[posicion]
        posicion = posicion + 1
    }
    retornar salida
}

funcion nombre_declaracion_core(valor: texto): texto {
    sea separador: numero = indice_dos_puntos_core(valor)
    si (separador < 0) {
        retornar valor
    }
    retornar segmento_core(valor, 0, separador)
}

funcion tipo_declaracion_core(valor: texto): texto {
    sea separador: numero = indice_dos_puntos_core(valor)
    si (separador < 0) {
        retornar ""
    }
    retornar segmento_core(valor, separador + 1, longitud(valor))
}

funcion buscar_simbolo_core(simbolos: lista, nombre: texto): numero {
    sea indice: numero = 0
    mientras (indice < longitud(simbolos)) {
        sea simbolo: cualquiera = simbolos[indice]
        si (simbolo.nombre == nombre) {
            retornar indice
        }
        indice = indice + 1
    }
    retornar -1
}

funcion tipo_identificador_core(
    nodo: cualquiera,
    simbolos: lista,
    errores: lista
): texto {
    sea indice: numero = buscar_simbolo_core(simbolos, nodo.valor)
    si (indice < 0) {
        agregar(
            errores,
            diagnostico_core(
                "CFB2002", nodo.linea,
                "variable no declarada '" + nodo.valor + "'"
            )
        )
        retornar "error"
    }
    sea simbolo: cualquiera = simbolos[indice]
    si (simbolo.movido) {
        agregar(
            errores,
            diagnostico_core(
                "CFB2003", nodo.linea,
                "uso después de mover '" + nodo.valor + "'"
            )
        )
        retornar "error"
    }
    retornar simbolo.tipo
}

funcion tipo_expresion_core(
    nodo: cualquiera,
    simbolos: lista,
    errores: lista
): texto {
    si (nodo.tipo == "Numero") {
        retornar "numero"
    }
    si (nodo.tipo == "Texto") {
        retornar "texto"
    }
    si (nodo.tipo == "Identificador") {
        retornar tipo_identificador_core(nodo, simbolos, errores)
    }
    si (nodo.tipo == "Mover") {
        sea hijos_mover: lista = nodo.hijos
        sea objetivo: cualquiera = hijos_mover[0]
        si (objetivo.tipo != "Identificador") {
            agregar(
                errores,
                diagnostico_core(
                    "CFB2004", nodo.linea,
                    "mover requiere una variable identificable"
                )
            )
            retornar "error"
        }
        sea indice: numero = buscar_simbolo_core(simbolos, objetivo.valor)
        sea tipo_objetivo: texto =
            tipo_identificador_core(objetivo, simbolos, errores)
        si (indice >= 0 y tipo_objetivo == "texto") {
            sea simbolo: cualquiera = simbolos[indice]
            simbolo.marcar_movido()
        }
        retornar tipo_objetivo
    }
    si (nodo.tipo == "Binario") {
        sea hijos_binarios: lista = nodo.hijos
        sea tipo_izquierdo: texto =
            tipo_expresion_core(hijos_binarios[0], simbolos, errores)
        sea tipo_derecho: texto =
            tipo_expresion_core(hijos_binarios[1], simbolos, errores)
        si (tipo_izquierdo == "error" o tipo_derecho == "error") {
            retornar "error"
        }
        si (nodo.valor == "+") {
            si (tipo_izquierdo == tipo_derecho y
                (tipo_izquierdo == "numero" o tipo_izquierdo == "texto")) {
                retornar tipo_izquierdo
            }
        } sino {
            si (tipo_izquierdo == "numero" y tipo_derecho == "numero") {
                retornar "numero"
            }
        }
        agregar(
            errores,
            diagnostico_core(
                "CFB2001", nodo.linea,
                "operador '" + nodo.valor + "' incompatible con " +
                tipo_izquierdo + " y " + tipo_derecho
            )
        )
        retornar "error"
    }
    agregar(
        errores,
        diagnostico_core(
            "CFB2099", nodo.linea,
            "nodo de expresión desconocido '" + nodo.tipo + "'"
        )
    )
    retornar "error"
}

funcion analizar_sentencia_core(
    nodo: cualquiera,
    simbolos: lista,
    errores: lista
) {
    sea hijos: lista = nodo.hijos
    si (nodo.tipo == "Declaracion") {
        sea nombre: texto = nombre_declaracion_core(nodo.valor)
        sea declarado: texto = tipo_declaracion_core(nodo.valor)
        si (buscar_simbolo_core(simbolos, nombre) >= 0) {
            agregar(
                errores,
                diagnostico_core(
                    "CFB2005", nodo.linea,
                    "variable duplicada '" + nombre + "'"
                )
            )
            retornar errores
        }
        sea inferido: texto =
            tipo_expresion_core(hijos[0], simbolos, errores)
        sea tipo_final: texto = inferido
        si (declarado != "") {
            tipo_final = declarado
            si (inferido != "error" y declarado != inferido) {
                agregar(
                    errores,
                    diagnostico_core(
                        "CFB2001", nodo.linea,
                        "la variable '" + nombre + "' requiere " +
                        declarado + " pero recibió " + inferido
                    )
                )
            }
        }
        agregar(simbolos, SimboloCore(nombre, tipo_final, falso))
        retornar errores
    }
    si (nodo.tipo == "Mostrar") {
        tipo_expresion_core(hijos[0], simbolos, errores)
        retornar errores
    }
    agregar(
        errores,
        diagnostico_core(
            "CFB2098", nodo.linea,
            "sentencia desconocida '" + nodo.tipo + "'"
        )
    )
}

funcion analizar_semantica_core(programa: cualquiera): cualquiera {
    sea simbolos: lista = []
    sea errores: lista = []
    sea sentencias: lista = programa.hijos
    sea indice: numero = 0
    mientras (indice < longitud(sentencias)) {
        analizar_sentencia_core(sentencias[indice], simbolos, errores)
        indice = indice + 1
    }
    retornar ResultadoSemanticoCore(longitud(errores) == 0, errores)
}

funcion diagnosticos_semanticos_core(resultado: cualquiera): texto {
    sea errores: lista = resultado.errores
    sea salida: texto = ""
    sea indice: numero = 0
    mientras (indice < longitud(errores)) {
        si (indice > 0) {
            salida = salida + "\n"
        }
        salida = salida + errores[indice]
        indice = indice + 1
    }
    retornar salida
}

// ===== bootstrap/core_emitter.cfv =====
// Emisor nativo de C-Forge Core Bootstrap 0.4.
// Recibe el AST canónico validado por B2 y genera una unidad C++17 completa.

estructura ResultadoEmisionCore {
    valido: booleano
    codigo: texto
    errores: lista
}

funcion nombre_cpp_core(nombre: texto): texto {
    retornar "cfv_" + nombre
}

funcion encabezado_cpp_core(): texto {
    retornar
        "#include <cmath>\n" +
        "#include <iomanip>\n" +
        "#include <iostream>\n" +
        "#include <sstream>\n" +
        "#include <stdexcept>\n" +
        "#include <string>\n" +
        "#include <variant>\n\n" +
        "struct Valor {\n" +
        "    std::variant<double, std::string> dato;\n" +
        "    explicit Valor(double valor) : dato(valor) {}\n" +
        "    explicit Valor(std::string valor) : dato(std::move(valor)) {}\n" +
        "};\n\n" +
        "static double numero(const Valor& valor) {\n" +
        "    if (const auto* n = std::get_if<double>(&valor.dato)) return *n;\n" +
        "    throw std::runtime_error(\"se esperaba numero\");\n" +
        "}\n" +
        "static Valor sumar(const Valor& a, const Valor& b) {\n" +
        "    if (const auto* x = std::get_if<double>(&a.dato)) {\n" +
        "        if (const auto* y = std::get_if<double>(&b.dato)) return Valor(*x + *y);\n" +
        "    }\n" +
        "    if (const auto* x = std::get_if<std::string>(&a.dato)) {\n" +
        "        if (const auto* y = std::get_if<std::string>(&b.dato)) return Valor(*x + *y);\n" +
        "    }\n" +
        "    throw std::runtime_error(\"tipos incompatibles para +\");\n" +
        "}\n" +
        "static Valor restar(const Valor& a, const Valor& b) {\n" +
        "    return Valor(numero(a) - numero(b));\n" +
        "}\n" +
        "static Valor multiplicar(const Valor& a, const Valor& b) {\n" +
        "    return Valor(numero(a) * numero(b));\n" +
        "}\n" +
        "static Valor dividir(const Valor& a, const Valor& b) {\n" +
        "    const double divisor = numero(b);\n" +
        "    if (divisor == 0) throw std::runtime_error(\"division por cero\");\n" +
        "    return Valor(numero(a) / divisor);\n" +
        "}\n" +
        "static Valor mover_core(const Valor& valor) { return valor; }\n" +
        "static void mostrar_core(const Valor& valor) {\n" +
        "    if (const auto* texto = std::get_if<std::string>(&valor.dato)) {\n" +
        "        std::cout << *texto << '\\n';\n" +
        "        return;\n" +
        "    }\n" +
        "    const double n = numero(valor);\n" +
        "    if (std::floor(n) == n) {\n" +
        "        std::cout << static_cast<long long>(n) << '\\n';\n" +
        "    } else {\n" +
        "        std::ostringstream salida;\n" +
        "        salida << std::setprecision(15) << n;\n" +
        "        std::cout << salida.str() << '\\n';\n" +
        "    }\n" +
        "}\n\n" +
        "int main() {\n" +
        "    try {\n"
}

funcion pie_cpp_core(): texto {
    retornar
        "        return 0;\n" +
        "    } catch (const std::exception& error) {\n" +
        "        std::cerr << \"[C-Forge Core Runtime Error] \" << error.what() << '\\n';\n" +
        "        return 1;\n" +
        "    }\n" +
        "}\n"
}

funcion emitir_expresion_core(nodo: cualquiera): texto {
    si (nodo.tipo == "Numero") {
        retornar "Valor(" + nodo.valor + ")"
    }
    si (nodo.tipo == "Texto") {
        retornar "Valor(std::string(" + nodo.valor + "))"
    }
    si (nodo.tipo == "Identificador") {
        retornar nombre_cpp_core(nodo.valor)
    }
    sea hijos: lista = nodo.hijos
    si (nodo.tipo == "Mover") {
        retornar "mover_core(" + emitir_expresion_core(hijos[0]) + ")"
    }
    si (nodo.tipo == "Binario") {
        sea izquierdo: texto = emitir_expresion_core(hijos[0])
        sea derecho: texto = emitir_expresion_core(hijos[1])
        si (nodo.valor == "+") {
            retornar "sumar(" + izquierdo + ", " + derecho + ")"
        }
        si (nodo.valor == "-") {
            retornar "restar(" + izquierdo + ", " + derecho + ")"
        }
        si (nodo.valor == "*") {
            retornar "multiplicar(" + izquierdo + ", " + derecho + ")"
        }
        si (nodo.valor == "/") {
            retornar "dividir(" + izquierdo + ", " + derecho + ")"
        }
    }
    afirmar(
        falso,
        "B3 no puede emitir el nodo de expresión '" + nodo.tipo + "'"
    )
    retornar ""
}

funcion emitir_sentencia_core(nodo: cualquiera): texto {
    sea hijos: lista = nodo.hijos
    si (nodo.tipo == "Declaracion") {
        sea nombre: texto = nombre_declaracion_core(nodo.valor)
        retornar
            "        Valor " + nombre_cpp_core(nombre) + " = " +
            emitir_expresion_core(hijos[0]) + ";\n"
    }
    si (nodo.tipo == "Mostrar") {
        retornar
            "        mostrar_core(" +
            emitir_expresion_core(hijos[0]) + ");\n"
    }
    afirmar(falso, "B3 no puede emitir la sentencia '" + nodo.tipo + "'")
    retornar ""
}

funcion emitir_programa_core(programa: cualquiera): cualquiera {
    sea semantica: cualquiera = analizar_semantica_core(programa)
    si (no semantica.valido) {
        retornar ResultadoEmisionCore(falso, "", semantica.errores)
    }
    sea codigo: texto = encabezado_cpp_core()
    sea sentencias: lista = programa.hijos
    sea indice: numero = 0
    mientras (indice < longitud(sentencias)) {
        codigo = codigo + emitir_sentencia_core(sentencias[indice])
        indice = indice + 1
    }
    codigo = codigo + pie_cpp_core()
    retornar ResultadoEmisionCore(verdadero, codigo, [])
}

// ===== bootstrap/core_driver.cfv =====
// Controlador principal de C-Forge Core Stage 1 — Bootstrap B4.
// Conecta lexer -> parser -> semántica/ownership -> emisor -> clang++.

estructura ResultadoCompilacionCore {
    valido: booleano
    codigo: texto
    errores: lista
}

funcion compilar_fuente_stage1(fuente: texto): cualquiera {
    sea tokens: lista = tokenizar_core(fuente)
    sea programa: cualquiera = parsear_tokens_core(tokens)
    sea semantica: cualquiera = analizar_semantica_core(programa)
    si (no semantica.valido) {
        retornar ResultadoCompilacionCore(
            falso,
            "",
            semantica.errores
        )
    }
    sea emision: cualquiera = emitir_programa_core(programa)
    retornar ResultadoCompilacionCore(
        emision.valido,
        emision.codigo,
        emision.errores
    )
}

funcion diagnosticos_stage1(resultado: cualquiera): texto {
    sea errores: lista = resultado.errores
    sea salida: texto = ""
    sea indice: numero = 0
    mientras (indice < longitud(errores)) {
        si (indice > 0) {
            salida = salida + "\n"
        }
        salida = salida + errores[indice]
        indice = indice + 1
    }
    retornar salida
}

sea argumentos_stage1: lista = argumentos_programa()
afirmar(
    longitud(argumentos_stage1) == 4,
    "uso: cforge-stage1 archivo.cfv -o ejecutable"
)
afirmar(
    argumentos_stage1[2] == "-o",
    "uso: cforge-stage1 archivo.cfv -o ejecutable"
)

sea entrada_stage1: texto = argumentos_stage1[1]
sea salida_stage1: texto = argumentos_stage1[3]
sea fuente_stage1: texto = leer_archivo(entrada_stage1)
sea resultado_stage1: cualquiera = compilar_fuente_stage1(fuente_stage1)
afirmar(
    resultado_stage1.valido,
    diagnosticos_stage1(resultado_stage1)
)

sea temporal_stage1: texto = salida_stage1 + ".stage1.cpp"
escribir_archivo(temporal_stage1, resultado_stage1.codigo)
sea nativo_stage1: booleano =
    compilar_cpp_nativo(temporal_stage1, salida_stage1)
eliminar_archivo(temporal_stage1)
afirmar(
    nativo_stage1,
    "el backend C++ no pudo producir el ejecutable nativo"
)
mostrar("C-Forge Stage 1 creó: " + salida_stage1)
)CFV49DATA"},
        {R"CFV50DATA(bootstrap/stage0/cforge_bootstrap.cpp)CFV50DATA", R"CFV51DATA(// C-Forge Stage 0 Bootstrap
// Compilador mínimo alojado exclusivamente en C++17. No carga Python, JVM,
// .NET, Node ni ningún runtime extranjero.

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

struct CompileError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

enum class TokenKind { identifier, number, string, symbol, end };

struct Token {
    TokenKind kind;
    std::string text;
    std::size_t line;
    std::size_t column;
};

class Lexer {
public:
    explicit Lexer(std::string source) : source_(std::move(source)) {}

    std::vector<Token> scan() {
        std::vector<Token> tokens;
        while (!at_end()) {
            const char current = peek();
            if (current == ' ' || current == '\t' || current == '\r') {
                advance();
            } else if (current == '\n') {
                advance();
                ++line_;
                column_ = 1;
            } else if (current == '/' && peek_next() == '/') {
                while (!at_end() && peek() != '\n') advance();
            } else if (is_identifier_start(current)) {
                tokens.push_back(identifier());
            } else if (std::isdigit(static_cast<unsigned char>(current))) {
                tokens.push_back(number());
            } else if (current == '"') {
                tokens.push_back(string());
            } else {
                const std::size_t token_line = line_;
                const std::size_t token_column = column_;
                std::string symbol(1, advance());
                if (!at_end()) {
                    const std::string pair = symbol + peek();
                    if (pair == "==" || pair == "!=" || pair == "<=" || pair == ">=") {
                        symbol.push_back(advance());
                    }
                }
                tokens.push_back({TokenKind::symbol, symbol, token_line, token_column});
            }
        }
        tokens.push_back({TokenKind::end, "", line_, column_});
        return tokens;
    }

private:
    std::string source_;
    std::size_t position_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;

    bool at_end() const { return position_ >= source_.size(); }
    char peek() const { return at_end() ? '\0' : source_[position_]; }
    char peek_next() const {
        return position_ + 1 >= source_.size() ? '\0' : source_[position_ + 1];
    }
    char advance() {
        const char value = source_[position_++];
        ++column_;
        return value;
    }
    static bool is_identifier_start(char value) {
        const auto byte = static_cast<unsigned char>(value);
        return std::isalpha(byte) || value == '_';
    }
    static bool is_identifier_continue(char value) {
        const auto byte = static_cast<unsigned char>(value);
        return std::isalnum(byte) || value == '_';
    }

    Token identifier() {
        const std::size_t start = position_;
        const std::size_t token_line = line_;
        const std::size_t token_column = column_;
        while (!at_end() && is_identifier_continue(peek())) advance();
        return {TokenKind::identifier, source_.substr(start, position_ - start),
                token_line, token_column};
    }

    Token number() {
        const std::size_t start = position_;
        const std::size_t token_line = line_;
        const std::size_t token_column = column_;
        while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        if (!at_end() && peek() == '.' &&
            std::isdigit(static_cast<unsigned char>(peek_next()))) {
            advance();
            while (!at_end() && std::isdigit(static_cast<unsigned char>(peek()))) advance();
        }
        return {TokenKind::number, source_.substr(start, position_ - start),
                token_line, token_column};
    }

    Token string() {
        const std::size_t token_line = line_;
        const std::size_t token_column = column_;
        advance();
        std::string value;
        while (!at_end() && peek() != '"') {
            char current = advance();
            if (current == '\\') {
                if (at_end()) fail(token_line, token_column, "escape incompleto");
                const char escaped = advance();
                if (escaped == 'n') value.push_back('\n');
                else if (escaped == 't') value.push_back('\t');
                else if (escaped == 'r') value.push_back('\r');
                else if (escaped == '"' || escaped == '\\') value.push_back(escaped);
                else fail(token_line, token_column, "escape desconocido");
            } else {
                if (current == '\n') {
                    ++line_;
                    column_ = 1;
                }
                value.push_back(current);
            }
        }
        if (at_end()) fail(token_line, token_column, "texto sin cerrar");
        advance();
        return {TokenKind::string, value, token_line, token_column};
    }

    [[noreturn]] static void fail(std::size_t line, std::size_t column,
                                  const std::string& message) {
        throw CompileError("Línea " + std::to_string(line) + ", columna " +
                           std::to_string(column) + ": " + message);
    }
};

enum class ValueType { number, text };

struct Expression {
    ValueType type;
    std::string generated;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::string compile_to_cpp() {
        std::ostringstream body;
        while (!check(TokenKind::end)) statement(body);
        return prologue() + body.str() + "    return 0;\n}\n";
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::map<std::string, ValueType> variables_;

    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    bool check(TokenKind kind) const { return peek().kind == kind; }
    bool check_text(const std::string& text) const {
        return peek().kind != TokenKind::string && peek().text == text;
    }
    const Token& advance() {
        if (!check(TokenKind::end)) ++current_;
        return previous();
    }
    bool take(const std::string& text) {
        if (!check_text(text)) return false;
        advance();
        return true;
    }
    const Token& require(TokenKind kind, const std::string& message) {
        if (!check(kind)) fail(peek(), message);
        return advance();
    }
    void require_text(const std::string& text, const std::string& message) {
        if (!take(text)) fail(peek(), message);
    }
    static std::string safe_name(const std::string& name) { return "cfv_" + name; }

    void statement(std::ostringstream& body) {
        if (take(";")) return;
        if (take("sea")) {
            const Token name = require(TokenKind::identifier,
                                       "se esperaba el nombre de la variable");
            if (take(":")) {
                const Token declared = require(TokenKind::identifier,
                                               "se esperaba el tipo declarado");
                if (declared.text != "numero" && declared.text != "texto") {
                    fail(declared, "Stage 0 solo admite numero y texto");
                }
            }
            require_text("=", "se esperaba '=' en la declaración");
            const Expression value = expression();
            if (variables_.count(name.text)) fail(name, "variable duplicada");
            variables_[name.text] = value.type;
            body << "    Value " << safe_name(name.text) << " = " << value.generated << ";\n";
            take(";");
            return;
        }
        if (take("mostrar") || take("print")) {
            require_text("(", "se esperaba '(' después de mostrar");
            const Expression value = expression();
            require_text(")", "se esperaba ')' después del valor");
            body << "    cfv_print(" << value.generated << ");\n";
            take(";");
            return;
        }
        fail(peek(), "Stage 0 esperaba 'sea' o 'mostrar'");
    }

    Expression expression() {
        Expression left = term();
        while (check_text("+") || check_text("-")) {
            const Token operation = advance();
            Expression right = term();
            if (operation.text == "+") {
                if (left.type != right.type) fail(operation, "tipos incompatibles para '+'");
                left.generated = "cfv_add(" + left.generated + ", " + right.generated + ")";
            } else {
                require_numbers(operation, left, right);
                left.generated = "cfv_sub(" + left.generated + ", " + right.generated + ")";
            }
        }
        return left;
    }

    Expression term() {
        Expression left = primary();
        while (check_text("*") || check_text("/")) {
            const Token operation = advance();
            Expression right = primary();
            require_numbers(operation, left, right);
            left.generated = (operation.text == "*" ? "cfv_mul(" : "cfv_div(") +
                             left.generated + ", " + right.generated + ")";
        }
        return left;
    }

    Expression primary() {
        if (check(TokenKind::number)) {
            const Token value = advance();
            return {ValueType::number, "Value(" + value.text + ")"};
        }
        if (check(TokenKind::string)) {
            const Token value = advance();
            return {ValueType::text, "Value(std::string(" + cpp_string(value.text) + "))"};
        }
        if (check(TokenKind::identifier)) {
            const Token name = advance();
            const auto found = variables_.find(name.text);
            if (found == variables_.end()) fail(name, "variable desconocida '" + name.text + "'");
            return {found->second, safe_name(name.text)};
        }
        if (take("(")) {
            Expression value = expression();
            require_text(")", "se esperaba ')'");
            return value;
        }
        fail(peek(), "expresión inválida");
    }

    static void require_numbers(const Token& operation, const Expression& left,
                                const Expression& right) {
        if (left.type != ValueType::number || right.type != ValueType::number) {
            fail(operation, "el operador '" + operation.text + "' requiere números");
        }
    }

    static std::string cpp_string(const std::string& value) {
        std::ostringstream escaped;
        escaped << '"';
        for (const unsigned char byte : value) {
            if (byte == '\\') escaped << "\\\\";
            else if (byte == '"') escaped << "\\\"";
            else if (byte == '\n') escaped << "\\n";
            else if (byte == '\r') escaped << "\\r";
            else if (byte == '\t') escaped << "\\t";
            else escaped << static_cast<char>(byte);
        }
        escaped << '"';
        return escaped.str();
    }

    [[noreturn]] static void fail(const Token& token, const std::string& message) {
        throw CompileError("Línea " + std::to_string(token.line) + ", columna " +
                           std::to_string(token.column) + ": " + message);
    }

    static std::string prologue() {
        return R"CPP(#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>

struct Value {
    std::variant<double, std::string> data;
    explicit Value(double value) : data(value) {}
    explicit Value(std::string value) : data(std::move(value)) {}
};
static double number(const Value& value) {
    if (const auto* found = std::get_if<double>(&value.data)) return *found;
    throw std::runtime_error("se esperaba numero");
}
static Value cfv_add(const Value& left, const Value& right) {
    if (const auto* a = std::get_if<double>(&left.data)) {
        if (const auto* b = std::get_if<double>(&right.data)) return Value(*a + *b);
    }
    if (const auto* a = std::get_if<std::string>(&left.data)) {
        if (const auto* b = std::get_if<std::string>(&right.data)) return Value(*a + *b);
    }
    throw std::runtime_error("tipos incompatibles para '+'");
}
static Value cfv_sub(const Value& a, const Value& b) { return Value(number(a) - number(b)); }
static Value cfv_mul(const Value& a, const Value& b) { return Value(number(a) * number(b)); }
static Value cfv_div(const Value& a, const Value& b) {
    const double divisor = number(b);
    if (divisor == 0) throw std::runtime_error("no se puede dividir por cero");
    return Value(number(a) / divisor);
}
static void cfv_print(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value.data)) {
        std::cout << *text << '\n';
        return;
    }
    const double number_value = number(value);
    if (std::floor(number_value) == number_value) {
        std::cout << static_cast<long long>(number_value) << '\n';
    } else {
        std::ostringstream output;
        output << std::setprecision(15) << number_value;
        std::cout << output.str() << '\n';
    }
}
int main() {
)CPP";
    }
};

// Compilador del subconjunto Core 0.4 requerido por B1/B2/B3. Conserva Stage 0 como
// una herramienta pequeña, pero añade funciones, control de flujo, listas y
// objetos suficientes para compilar el lexer, el AST y el parser escritos en
// C-Forge. La semántica continúa alojada únicamente en este binario C++17.
class CoreB1Compiler {
public:
    explicit CoreB1Compiler(std::vector<Token> tokens) : tokens_(std::move(tokens)) {
        discover_symbols();
    }

    std::string compile_to_cpp() {
        std::ostringstream declarations;
        std::ostringstream body;
        current_ = 0;
        scopes_.push_back({});
        while (!check(TokenKind::end)) {
            if (take(";")) continue;
            if (check_text("estructura")) {
                parse_structure();
            } else if (check_text("clase")) {
                parse_class(declarations);
            } else if (check_text("funcion")) {
                parse_function(declarations, false, "");
            } else {
                statement(body);
            }
        }

        std::ostringstream prototypes;
        for (const auto& name : functions_) {
            prototypes << "static Value cfv_fn_" << name
                       << "(const std::vector<Value>&);\n";
        }
        for (const auto& name : methods_) {
            prototypes << "static Value cfv_method_" << name
                       << "(Value, const std::vector<Value>&);\n";
        }
        return runtime() + prototypes.str() + declarations.str() +
               "int main(int argc, char** argv) {\n"
               "    try {\n"
               "        cfv_process_args.clear();\n"
               "        for (int index = 0; index < argc; ++index) {\n"
               "            cfv_process_args.emplace_back(std::string(argv[index]));\n"
               "        }\n" + body.str() +
               "        return 0;\n"
               "    } catch (const std::exception& error) {\n"
               "        std::cerr << \"[C-Forge Core Runtime Error] \""
               " << error.what() << '\\n';\n"
               "        return 1;\n"
               "    }\n}\n";
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::map<std::string, std::vector<std::string>> records_;
    std::vector<std::string> functions_;
    std::vector<std::string> methods_;
    std::vector<std::map<std::string, bool>> scopes_;

    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    const Token& look(std::size_t offset) const {
        const std::size_t index = current_ + offset;
        return tokens_[index < tokens_.size() ? index : tokens_.size() - 1];
    }
    bool check(TokenKind kind) const { return peek().kind == kind; }
    bool check_text(const std::string& text) const {
        return peek().kind != TokenKind::string && peek().text == text;
    }
    const Token& advance() {
        if (!check(TokenKind::end)) ++current_;
        return previous();
    }
    bool take(const std::string& text) {
        if (!check_text(text)) return false;
        advance();
        return true;
    }
    const Token& require(TokenKind kind, const std::string& message) {
        if (!check(kind)) fail(peek(), message);
        return advance();
    }
    Token require_name(const std::string& message) {
        return require(TokenKind::identifier, message);
    }
    void require_text(const std::string& text, const std::string& message) {
        if (!take(text)) fail(peek(), message);
    }
    static std::string safe(const std::string& name) { return "cfv_" + name; }
    static bool contains(const std::vector<std::string>& values,
                         const std::string& value) {
        for (const auto& item : values) if (item == value) return true;
        return false;
    }

    void discover_symbols() {
        for (std::size_t index = 0; index + 1 < tokens_.size(); ++index) {
            if (tokens_[index].text == "funcion" &&
                tokens_[index + 1].kind == TokenKind::identifier) {
                const std::string name = tokens_[index + 1].text;
                if (!contains(functions_, name)) functions_.push_back(name);
            }
            if (tokens_[index].text == "metodo" &&
                tokens_[index + 1].kind == TokenKind::identifier) {
                const std::string name = tokens_[index + 1].text;
                if (!contains(methods_, name)) methods_.push_back(name);
            }
        }
    }

    void skip_type() {
        if (!check(TokenKind::identifier)) fail(peek(), "se esperaba un tipo");
        advance();
        if (take("<")) {
            int depth = 1;
            while (depth > 0 && !check(TokenKind::end)) {
                if (take("<")) ++depth;
                else if (take(">")) --depth;
                else advance();
            }
        }
    }

    void parse_structure() {
        require_text("estructura", "se esperaba 'estructura'");
        const Token name = require_name("se esperaba el nombre de la estructura");
        require_text("{", "se esperaba '{'");
        std::vector<std::string> fields;
        while (!take("}")) {
            const Token field = require_name("se esperaba un campo");
            require_text(":", "se esperaba ':'");
            skip_type();
            take(";");
            fields.push_back(field.text);
        }
        take(";");
        records_[name.text] = fields;
    }

    std::vector<std::string> parameters() {
        std::vector<std::string> result;
        require_text("(", "se esperaba '('");
        if (!check_text(")")) {
            do {
                const Token parameter = require_name("se esperaba un parámetro");
                result.push_back(parameter.text);
                if (take(":")) skip_type();
            } while (take(","));
        }
        require_text(")", "se esperaba ')'");
        if (take(":")) skip_type();
        return result;
    }

    void parse_class(std::ostringstream& output) {
        require_text("clase", "se esperaba 'clase'");
        const Token class_name = require_name("se esperaba el nombre de la clase");
        require_text("{", "se esperaba '{'");
        std::vector<std::string> fields;
        while (!check_text("}")) {
            if (take("campo")) {
                const Token field = require_name("se esperaba el campo");
                require_text(":", "se esperaba ':'");
                skip_type();
                take(";");
                fields.push_back(field.text);
            } else if (check_text("metodo")) {
                advance();
                const Token method = require_name("se esperaba el método");
                parse_function_after_name(output, true, class_name.text, method);
            } else {
                fail(peek(), "miembro de clase no admitido por Core 0.4");
            }
        }
        advance();
        take(";");
        records_[class_name.text] = fields;
    }

    void parse_function(std::ostringstream& output, bool method,
                        const std::string& owner) {
        require_text("funcion", "se esperaba 'funcion'");
        const Token name = require_name("se esperaba el nombre de la función");
        parse_function_after_name(output, method, owner, name);
    }

    void parse_function_after_name(std::ostringstream& output, bool method,
                                   const std::string&, const Token& name) {
        const auto params = parameters();
        output << "static Value "
               << (method ? "cfv_method_" : "cfv_fn_") << name.text << "(";
        if (method) output << "Value cfv_este, ";
        output << "const std::vector<Value>& cfv_args) {\n";
        scopes_.push_back({});
        if (method) scopes_.back()["este"] = true;
        for (std::size_t index = 0; index < params.size(); ++index) {
            scopes_.back()[params[index]] = true;
            output << "    Value " << safe(params[index]) << " = cfv_arg(cfv_args, "
                   << index << ", " << cpp_string(name.text) << ");\n";
        }
        require_text("{", "se esperaba el cuerpo de la función");
        while (!check_text("}")) statement(output);
        advance();
        output << "    return Value();\n}\n";
        scopes_.pop_back();
    }

    void statement(std::ostringstream& output) {
        if (take(";")) return;
        if (take("sea")) {
            const Token name = require_name("se esperaba el nombre de la variable");
            if (take(":")) skip_type();
            require_text("=", "se esperaba '='");
            const std::string value = expression();
            scopes_.back()[name.text] = true;
            output << "    Value " << safe(name.text) << " = " << value << ";\n";
            take(";");
            return;
        }
        if (take("si")) {
            const bool grouped = take("(");
            const std::string condition = expression();
            if (grouped) require_text(")", "se esperaba ')'");
            output << "    if (cfv_truth(" << condition << ")) ";
            block(output);
            if (take("sino")) {
                output << "    else ";
                block(output);
            }
            return;
        }
        if (take("mientras")) {
            const bool grouped = take("(");
            const std::string condition = expression();
            if (grouped) require_text(")", "se esperaba ')'");
            output << "    while (cfv_truth(" << condition << ")) ";
            block(output);
            return;
        }
        if (take("retornar")) {
            output << "    return " << expression() << ";\n";
            take(";");
            return;
        }
        if (take("mostrar") || take("print")) {
            require_text("(", "se esperaba '('");
            const std::string value = expression();
            require_text(")", "se esperaba ')'");
            output << "    cfv_print(" << value << ");\n";
            take(";");
            return;
        }
        if (check(TokenKind::identifier) && look(1).text == "=") {
            const Token name = advance();
            advance();
            output << "    " << variable(name) << " = " << expression() << ";\n";
            take(";");
            return;
        }
        if (check(TokenKind::identifier) && look(1).text == "." &&
            look(2).kind == TokenKind::identifier && look(3).text == "=") {
            const Token owner = advance();
            advance();
            const Token field = advance();
            advance();
            output << "    cfv_member_ref(" << variable(owner) << ", "
                   << cpp_string(field.text) << ") = " << expression() << ";\n";
            take(";");
            return;
        }
        output << "    (void)(" << expression() << ");\n";
        take(";");
    }

    void block(std::ostringstream& output) {
        require_text("{", "se esperaba '{'");
        output << "{\n";
        scopes_.push_back(scopes_.back());
        while (!check_text("}")) statement(output);
        advance();
        scopes_.pop_back();
        output << "    }\n";
    }

    std::string expression() { return logic_or(); }
    std::string logic_or() {
        std::string left = logic_and();
        while (take("o")) left = "cfv_bool(cfv_truth(" + left + ") || cfv_truth(" +
                                  logic_and() + "))";
        return left;
    }
    std::string logic_and() {
        std::string left = equality();
        while (take("y")) left = "cfv_bool(cfv_truth(" + left + ") && cfv_truth(" +
                                  equality() + "))";
        return left;
    }
    std::string equality() {
        std::string left = comparison();
        while (check_text("==") || check_text("!=")) {
            const std::string op = advance().text;
            const std::string right = comparison();
            left = "cfv_bool(cfv_equal(" + left + ", " + right + ")" +
                   (op == "!=" ? " == false" : "") + ")";
        }
        return left;
    }
    std::string comparison() {
        std::string left = sum();
        while (check_text("<") || check_text("<=") || check_text(">") ||
               check_text(">=")) {
            const std::string op = advance().text;
            left = "cfv_compare(" + left + ", " + sum() + ", " + cpp_string(op) + ")";
        }
        return left;
    }
    std::string sum() {
        std::string left = product();
        while (check_text("+") || check_text("-")) {
            const std::string op = advance().text;
            left = (op == "+" ? "cfv_add(" : "cfv_sub(") + left + ", " +
                   product() + ")";
        }
        return left;
    }
    std::string product() {
        std::string left = unary();
        while (check_text("*") || check_text("/") || check_text("%")) {
            const std::string op = advance().text;
            const std::string right = unary();
            if (op == "*") left = "cfv_mul(" + left + ", " + right + ")";
            else if (op == "/") left = "cfv_div(" + left + ", " + right + ")";
            else left = "cfv_mod(" + left + ", " + right + ")";
        }
        return left;
    }
    std::string unary() {
        if (take("no")) return "cfv_bool(!cfv_truth(" + unary() + "))";
        if (take("-")) return "cfv_neg(" + unary() + ")";
        return postfix();
    }

    std::string postfix() {
        std::string value = primary();
        while (true) {
            if (take("[")) {
                const std::string index = expression();
                require_text("]", "se esperaba ']'");
                value = "cfv_index(" + value + ", " + index + ")";
            } else if (take(".")) {
                const Token member = require_name("se esperaba el miembro");
                if (take("(")) {
                    const auto args = arguments_after_open();
                    value = "cfv_method_" + member.text + "(" + value + ", " +
                            vector_expression(args) + ")";
                } else {
                    value = "cfv_member(" + value + ", " +
                            cpp_string(member.text) + ")";
                }
            } else {
                break;
            }
        }
        return value;
    }

    std::string primary() {
        if (check(TokenKind::number)) return "cfv_number(" + advance().text + ")";
        if (check(TokenKind::string)) return "cfv_text(" + cpp_string(advance().text) + ")";
        if (take("verdadero")) return "cfv_bool(true)";
        if (take("falso")) return "cfv_bool(false)";
        if (take("[")) return "cfv_list(" + vector_expression(arguments_after("[", "]")) + ")";
        if (take("(")) {
            const std::string value = expression();
            require_text(")", "se esperaba ')'");
            return value;
        }
        if (check(TokenKind::identifier)) {
            const Token name = advance();
            if (take("(")) {
                const auto args = arguments_after_open();
                const auto record = records_.find(name.text);
                if (record != records_.end()) {
                    return "cfv_object(" + cpp_string(name.text) + ", " +
                           string_vector(record->second) + ", " +
                           vector_expression(args) + ")";
                }
                return call(name, args);
            }
            return variable(name);
        }
        fail(peek(), "expresión Core 0.4 inválida");
    }

    std::vector<std::string> arguments_after_open() {
        std::vector<std::string> args;
        if (!check_text(")")) {
            do args.push_back(expression()); while (take(","));
        }
        require_text(")", "se esperaba ')' después de los argumentos");
        return args;
    }
    std::vector<std::string> arguments_after(const std::string&,
                                             const std::string& close) {
        std::vector<std::string> args;
        if (!check_text(close)) {
            do args.push_back(expression()); while (take(","));
        }
        require_text(close, "se esperaba el cierre de la lista");
        return args;
    }

    std::string call(const Token& name, const std::vector<std::string>& args) {
        if (name.text == "longitud") return "cfv_length(" + one(name, args) + ")";
        if (name.text == "a_texto") return "cfv_text(cfv_format(" + one(name, args) + "))";
        if (name.text == "agregar") {
            if (args.size() != 2) fail(name, "agregar requiere dos argumentos");
            return "cfv_append(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "afirmar") {
            if (args.size() != 2) fail(name, "afirmar requiere dos argumentos");
            return "cfv_assert(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "argumentos_programa") {
            if (!args.empty()) fail(name, "argumentos_programa no recibe argumentos");
            return "cfv_arguments()";
        }
        if (name.text == "leer_archivo") {
            return "cfv_read_file(" + one(name, args) + ")";
        }
        if (name.text == "escribir_archivo") {
            if (args.size() != 2) fail(name, "escribir_archivo requiere dos argumentos");
            return "cfv_write_file(" + args[0] + ", " + args[1] + ")";
        }
        if (name.text == "eliminar_archivo") {
            return "cfv_remove_file(" + one(name, args) + ")";
        }
        if (name.text == "compilar_cpp_nativo") {
            if (args.size() != 2) fail(name, "compilar_cpp_nativo requiere dos argumentos");
            return "cfv_compile_cpp(" + args[0] + ", " + args[1] + ")";
        }
        if (!contains(functions_, name.text)) {
            fail(name, "función desconocida '" + name.text + "'");
        }
        return "cfv_fn_" + name.text + "(" + vector_expression(args) + ")";
    }
    std::string one(const Token& name, const std::vector<std::string>& args) {
        if (args.size() != 1) fail(name, name.text + " requiere un argumento");
        return args[0];
    }
    std::string variable(const Token& name) const {
        if (name.text == "este") return "cfv_este";
        return safe(name.text);
    }
    static std::string vector_expression(const std::vector<std::string>& values) {
        std::ostringstream output;
        output << "std::vector<Value>{";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index) output << ", ";
            output << values[index];
        }
        return output.str() + "}";
    }
    static std::string string_vector(const std::vector<std::string>& values) {
        std::ostringstream output;
        output << "std::vector<std::string>{";
        for (std::size_t index = 0; index < values.size(); ++index) {
            if (index) output << ", ";
            output << cpp_string(values[index]);
        }
        return output.str() + "}";
    }
    static std::string cpp_string(const std::string& value) {
        std::ostringstream escaped;
        escaped << '"';
        for (const unsigned char byte : value) {
            if (byte == '\\') escaped << "\\\\";
            else if (byte == '"') escaped << "\\\"";
            else if (byte == '\n') escaped << "\\n";
            else if (byte == '\r') escaped << "\\r";
            else if (byte == '\t') escaped << "\\t";
            else escaped << static_cast<char>(byte);
        }
        escaped << '"';
        return escaped.str();
    }
    [[noreturn]] static void fail(const Token& token, const std::string& message) {
        throw CompileError("Línea " + std::to_string(token.line) + ", columna " +
                           std::to_string(token.column) + ": " + message);
    }

    static std::string runtime() {
        return R"CPP(#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

struct Value;
using List = std::vector<Value>;
using Object = std::map<std::string, Value>;
struct Value {
    using Data = std::variant<std::monostate, double, bool, std::string,
                              std::shared_ptr<List>, std::shared_ptr<Object>>;
    Data data;
    Value() = default;
    explicit Value(double value) : data(value) {}
    explicit Value(bool value) : data(value) {}
    explicit Value(std::string value) : data(std::move(value)) {}
    explicit Value(std::shared_ptr<List> value) : data(std::move(value)) {}
    explicit Value(std::shared_ptr<Object> value) : data(std::move(value)) {}
};
static std::vector<Value> cfv_process_args;
static Value cfv_number(double value) { return Value(value); }
static Value cfv_text(std::string value) { return Value(std::move(value)); }
static Value cfv_bool(bool value) { return Value(value); }
static double cfv_num(const Value& value) {
    if (const auto* found = std::get_if<double>(&value.data)) return *found;
    throw std::runtime_error("se esperaba numero");
}
static bool cfv_truth(const Value& value) {
    if (const auto* found = std::get_if<bool>(&value.data)) return *found;
    if (const auto* found = std::get_if<double>(&value.data)) return *found != 0;
    if (const auto* found = std::get_if<std::string>(&value.data)) return !found->empty();
    if (const auto* found = std::get_if<std::shared_ptr<List>>(&value.data))
        return !(*found)->empty();
    return !std::holds_alternative<std::monostate>(value.data);
}
static std::string cfv_format(const Value& value) {
    if (std::holds_alternative<std::monostate>(value.data)) return "nulo";
    if (const auto* found = std::get_if<bool>(&value.data))
        return *found ? "verdadero" : "falso";
    if (const auto* found = std::get_if<std::string>(&value.data)) return *found;
    if (const auto* found = std::get_if<double>(&value.data)) {
        if (std::floor(*found) == *found) return std::to_string(static_cast<long long>(*found));
        std::ostringstream output; output << std::setprecision(15) << *found;
        return output.str();
    }
    return "<objeto>";
}
static bool cfv_equal(const Value& left, const Value& right) {
    if (left.data.index() != right.data.index()) return false;
    if (const auto* a = std::get_if<double>(&left.data))
        return *a == std::get<double>(right.data);
    if (const auto* a = std::get_if<bool>(&left.data))
        return *a == std::get<bool>(right.data);
    if (const auto* a = std::get_if<std::string>(&left.data))
        return *a == std::get<std::string>(right.data);
    return left.data == right.data;
}
static Value cfv_add(const Value& left, const Value& right) {
    if (const auto* a = std::get_if<double>(&left.data)) {
        if (const auto* b = std::get_if<double>(&right.data)) return Value(*a + *b);
    }
    if (const auto* a = std::get_if<std::string>(&left.data)) {
        if (const auto* b = std::get_if<std::string>(&right.data)) return Value(*a + *b);
    }
    throw std::runtime_error("tipos incompatibles para '+'");
}
static Value cfv_sub(const Value& a, const Value& b) { return Value(cfv_num(a) - cfv_num(b)); }
static Value cfv_mul(const Value& a, const Value& b) { return Value(cfv_num(a) * cfv_num(b)); }
static Value cfv_div(const Value& a, const Value& b) {
    const double divisor = cfv_num(b);
    if (divisor == 0) throw std::runtime_error("división por cero");
    return Value(cfv_num(a) / divisor);
}
static Value cfv_mod(const Value& a, const Value& b) {
    return Value(std::fmod(cfv_num(a), cfv_num(b)));
}
static Value cfv_neg(const Value& value) { return Value(-cfv_num(value)); }
static Value cfv_compare(const Value& a, const Value& b, const std::string& op) {
    if (const auto* left = std::get_if<double>(&a.data)) {
        const double right = cfv_num(b);
        return Value(op == "<" ? *left < right : op == "<=" ? *left <= right :
                     op == ">" ? *left > right : *left >= right);
    }
    const auto* left = std::get_if<std::string>(&a.data);
    const auto* right = std::get_if<std::string>(&b.data);
    if (!left || !right) throw std::runtime_error("comparación incompatible");
    return Value(op == "<" ? *left < *right : op == "<=" ? *left <= *right :
                 op == ">" ? *left > *right : *left >= *right);
}
static Value cfv_list(const std::vector<Value>& values) {
    return Value(std::make_shared<List>(values));
}
static Value cfv_object(const std::string& type,
                        const std::vector<std::string>& fields,
                        const std::vector<Value>& values) {
    if (fields.size() != values.size())
        throw std::runtime_error(type + " recibió una cantidad de campos inválida");
    auto object = std::make_shared<Object>();
    (*object)["__tipo"] = Value(type);
    for (std::size_t i = 0; i < fields.size(); ++i) (*object)[fields[i]] = values[i];
    return Value(object);
}
static Value cfv_index(const Value& value, const Value& index) {
    const auto position = static_cast<std::size_t>(cfv_num(index));
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value.data)) {
        if (position >= (*list)->size()) throw std::runtime_error("índice fuera de rango");
        return (**list)[position];
    }
    if (const auto* text = std::get_if<std::string>(&value.data)) {
        if (position >= text->size()) throw std::runtime_error("índice fuera de rango");
        return Value(std::string(1, (*text)[position]));
    }
    throw std::runtime_error("el valor no admite índices");
}
static Value cfv_member(const Value& value, const std::string& field) {
    const auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);
    if (!object) throw std::runtime_error("se esperaba un objeto");
    const auto found = (*object)->find(field);
    if (found == (*object)->end()) throw std::runtime_error("campo desconocido " + field);
    return found->second;
}
static Value& cfv_member_ref(Value& value, const std::string& field) {
    auto* object = std::get_if<std::shared_ptr<Object>>(&value.data);
    if (!object) throw std::runtime_error("se esperaba un objeto mutable");
    const auto found = (*object)->find(field);
    if (found == (*object)->end()) throw std::runtime_error("campo desconocido " + field);
    return found->second;
}
static Value cfv_length(const Value& value) {
    if (const auto* text = std::get_if<std::string>(&value.data))
        return Value(static_cast<double>(text->size()));
    if (const auto* list = std::get_if<std::shared_ptr<List>>(&value.data))
        return Value(static_cast<double>((*list)->size()));
    throw std::runtime_error("longitud requiere texto o lista");
}
static Value cfv_append(Value value, const Value& item) {
    auto* list = std::get_if<std::shared_ptr<List>>(&value.data);
    if (!list) throw std::runtime_error("agregar requiere una lista");
    (*list)->push_back(item);
    return Value();
}
static Value cfv_assert(const Value& condition, const Value& message) {
    if (!cfv_truth(condition)) throw std::runtime_error(cfv_format(message));
    return Value();
}
static std::string cfv_required_text(const Value& value,
                                     const std::string& function) {
    if (const auto* text = std::get_if<std::string>(&value.data)) return *text;
    throw std::runtime_error(function + " requiere texto");
}
static Value cfv_arguments() {
    return cfv_list(cfv_process_args);
}
static Value cfv_read_file(const Value& path_value) {
    const std::string path = cfv_required_text(path_value, "leer_archivo");
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("no se pudo abrir " + path);
    return Value(std::string(std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()));
}
static Value cfv_write_file(const Value& path_value, const Value& content_value) {
    const std::string path = cfv_required_text(path_value, "escribir_archivo");
    const std::string content =
        cfv_required_text(content_value, "escribir_archivo");
    std::ofstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("no se pudo escribir " + path);
    stream << content;
    if (!stream) throw std::runtime_error("escritura incompleta en " + path);
    return Value(true);
}
static Value cfv_remove_file(const Value& path_value) {
    const std::string path = cfv_required_text(path_value, "eliminar_archivo");
    return Value(std::remove(path.c_str()) == 0);
}
static std::string cfv_shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (const char byte : value) {
        if (byte == '\'') quoted += "'\\''";
        else quoted.push_back(byte);
    }
    return quoted + "'";
}
static Value cfv_compile_cpp(const Value& source_value, const Value& output_value) {
    const std::string source =
        cfv_required_text(source_value, "compilar_cpp_nativo");
    const std::string output =
        cfv_required_text(output_value, "compilar_cpp_nativo");
    const std::string command =
        "clang++ -std=c++17 -O2 " + cfv_shell_quote(source) +
        " -o " + cfv_shell_quote(output);
    return Value(std::system(command.c_str()) == 0);
}
static Value cfv_arg(const std::vector<Value>& args, std::size_t index,
                     const std::string& function) {
    if (index >= args.size()) throw std::runtime_error(function + ": faltan argumentos");
    return args[index];
}
static void cfv_print(const Value& value) { std::cout << cfv_format(value) << '\n'; }
)CPP";
    }
};

static std::string read_file(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw CompileError("no se pudo abrir " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

static std::string shell_quote(const fs::path& path) {
    std::string quoted = "'";
    for (const char byte : path.string()) {
        if (byte == '\'') quoted += "'\\''";
        else quoted.push_back(byte);
    }
    return quoted + "'";
}

int main(int argc, char** argv) {
    try {
        if (argc != 4 || std::string(argv[2]) != "-o") {
            std::cerr << "Uso: cforge-bootstrap archivo.cfv -o ejecutable\n";
            return 2;
        }
        const fs::path input = fs::absolute(argv[1]);
        const fs::path output = fs::absolute(argv[3]);
        std::vector<Token> tokens = Lexer(read_file(input)).scan();
        bool requires_b1 = false;
        for (const auto& token : tokens) {
            if (token.text == "funcion" || token.text == "estructura" ||
                token.text == "clase" || token.text == "mientras" ||
                token.text == "si") {
                requires_b1 = true;
                break;
            }
        }
        const std::string generated = requires_b1
            ? CoreB1Compiler(std::move(tokens)).compile_to_cpp()
            : Parser(std::move(tokens)).compile_to_cpp();
        const fs::path temporary = output.string() + ".stage0.cpp";
        {
            std::ofstream stream(temporary, std::ios::binary);
            if (!stream) throw CompileError("no se pudo crear fuente temporal");
            stream << generated;
        }
        const std::string command =
            "clang++ -std=c++17 -O2 " + shell_quote(temporary) + " -o " + shell_quote(output);
        const int status = std::system(command.c_str());
        std::error_code ignored;
        fs::remove(temporary, ignored);
        if (status != 0) throw CompileError("clang++ no pudo producir el ejecutable Stage 0");
        std::cout << "C-Forge Stage 0 creó: " << output << "\n";
        return 0;
    } catch (const CompileError& error) {
        std::cerr << "[C-Forge Bootstrap Error] " << error.what() << "\n";
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "[C-Forge Bootstrap Internal Error] " << error.what() << "\n";
        return 1;
    }
}
)CFV51DATA"},
        {R"CFV52DATA(bootstrap/fixtures/minimal.cfv)CFV52DATA", R"CFV53DATA(// Programa aceptado por C-Forge Stage 0.
sea proyecto: texto = "C-Forge"
sea base: numero = 40
sea resultado: numero = base + 2
mostrar(proyecto + " Core Bootstrap")
mostrar(resultado)
)CFV53DATA"},
        {R"CFV54DATA(bootstrap/fixtures/parser_b1_driver.cfv)CFV54DATA", R"CFV55DATA(// Driver B1. Se concatena después de core_lexer, core_ast y core_parser.
sea fuente_b1: texto =
    "sea respuesta: numero = 40 + 2 * 3\n" +
    "mostrar(respuesta)\n"
sea programa_b1: cualquiera = parsear_fuente_core(fuente_b1)
mostrar(ast_core_canonico(programa_b1))

sea fuente_texto_b1: texto =
    "sea nombre: texto = \"C-\" + \"Forge\";\n" +
    "print((nombre + \" B1\"));\n"
sea programa_texto_b1: cualquiera = parsear_fuente_core(fuente_texto_b1)
mostrar(ast_core_canonico(programa_texto_b1))

sea fuente_asociatividad_b1: texto =
    "sea calculo = 20 - 5 - 3\n" +
    "mostrar(calculo)\n"
sea programa_asociatividad_b1: cualquiera =
    parsear_fuente_core(fuente_asociatividad_b1)
mostrar(ast_core_canonico(programa_asociatividad_b1))
)CFV55DATA"},
        {R"CFV56DATA(bootstrap/fixtures/semantics_b2_driver.cfv)CFV56DATA", R"CFV57DATA(// Corpus normativo B2. Se concatena tras lexer, AST, parser y semántica.

sea fuente_valida_b2: texto =
    "sea titulo: texto = \"C-Forge\"\n" +
    "sea copia: texto = mover(titulo)\n" +
    "sea base: numero = 40\n" +
    "sea copia_base: numero = mover(base)\n" +
    "mostrar(base + 2)\n"
sea resultado_valido_b2: cualquiera =
    analizar_semantica_core(parsear_fuente_core(fuente_valida_b2))
mostrar(resultado_valido_b2.valido)

sea fuente_tipo_b2: texto = "sea edad: numero = \"veinte\"\n"
sea resultado_tipo_b2: cualquiera =
    analizar_semantica_core(parsear_fuente_core(fuente_tipo_b2))
mostrar(diagnosticos_semanticos_core(resultado_tipo_b2))

sea fuente_desconocida_b2: texto = "mostrar(fantasma)\n"
sea resultado_desconocida_b2: cualquiera =
    analizar_semantica_core(parsear_fuente_core(fuente_desconocida_b2))
mostrar(diagnosticos_semanticos_core(resultado_desconocida_b2))

sea fuente_movida_b2: texto =
    "sea nombre: texto = \"Javier\"\n" +
    "sea destino: texto = mover(nombre)\n" +
    "mostrar(nombre)\n"
sea resultado_movida_b2: cualquiera =
    analizar_semantica_core(parsear_fuente_core(fuente_movida_b2))
mostrar(diagnosticos_semanticos_core(resultado_movida_b2))
)CFV57DATA"},
        {R"CFV58DATA(bootstrap/fixtures/emitter_b3_driver.cfv)CFV58DATA", R"CFV59DATA(// Driver del emisor B3. Su única salida es una unidad C++17 compilable.
sea fuente_b3: texto =
    "sea nombre: texto = \"C-Forge\"\n" +
    "sea nombre_movido: texto = mover(nombre)\n" +
    "sea base: numero = 40\n" +
    "sea resultado: numero = base + 2\n" +
    "mostrar(nombre_movido + \" B3\")\n" +
    "mostrar(resultado)\n"
sea programa_b3: cualquiera = parsear_fuente_core(fuente_b3)
sea emision_b3: cualquiera = emitir_programa_core(programa_b3)
afirmar(emision_b3.valido, diagnosticos_semanticos_core(emision_b3))
mostrar(emision_b3.codigo)
)CFV59DATA"},
        {R"CFV60DATA(registry/index.json)CFV60DATA", R"CFV61DATA({
  "format": 2,
  "registry": "C-Forge Community Registry",
  "publishers": {},
  "revocations": [],
  "packages": {}
}
)CFV61DATA"},
        {R"CFV62DATA(registry/README.md)CFV62DATA", R"CFV63DATA(# Registro público de paquetes C-Forge

Este directorio define el índice público, auditable y versionado del gestor `cforge pkg`.
Cada versión del formato 2 debe publicar una URL HTTPS, el SHA-256 exacto del
archivo `.tar.gz`, la clave pública Ed25519, su `key_id` y la firma. El cliente
verifica todos esos campos antes de extraer el paquete y rechaza versiones o claves
incluidas en `revocations`.

La publicación se realiza mediante pull request para conservar revisión, historial y
protecciones de rama. `cforge pkg build` crea el archivo y su digest; ningún paquete se
ejecuta durante instalación. El cliente rechaza HTTP, rutas ascendentes, enlaces,
archivos mayores a 32 MiB y hashes incorrectos.

El publicador crea su identidad mediante `cforge pkg keygen` y firma el archivo con
`cforge pkg sign archivo.tar.gz clave.pem nombre versión`. La clave privada nunca
se publica. La cuenta del publicador sigue representada por su identidad GitHub y
la revisión del pull request; un servicio de cuentas independiente aún no está
operativo.

El registro está vacío hasta que el primer paquete sea revisado y aceptado. Esto evita
presentar paquetes de ejemplo como dependencias oficiales.

`publishers` contiene identidades aprobadas, claves y estado. El instalador exige
que cada paquete señale un publicador `active` y que su `key_id` esté autorizada
por esa cuenta. Esta fase usa
identidades de GitHub y revisión por pull request; todavía no existe un servicio
central de inicio de sesión, recuperación de cuenta ni publicación automática.
)CFV63DATA"},
        {R"CFV64DATA(include/cforgev_ffi.h)CFV64DATA", R"CFV65DATA(#ifndef CFORGEV_FFI_H
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
    CFV_TEXT = 3,
    CFV_BOOLEAN = 4,
    CFV_LIST = 5,
    CFV_MAP = 6,
    CFV_RECORD = 7
} CfvType;

typedef void (*CfvReleaseFunction)(void* owner);

/* Vista prestada, contigua y de solo lectura. El puntero solo es válido durante
   la llamada extranjera y nunca debe liberarse ni conservarse. */
typedef struct CfvNumberSlice {
    const double* data;
    uint64_t length;
} CfvNumberSlice;

/* Resultado propietario para extern_c segura. C-Forge copia el contenido a su
   runtime y luego invoca release(owner) una vez. */
typedef struct CfvOwnedNumberList {
    const double* data;
    uint64_t length;
    void* owner;
    CfvReleaseFunction release;
} CfvOwnedNumberList;

/* Texto UTF-8 propietario. data no contiene NUL dentro de length. */
typedef struct CfvOwnedText {
    const char* data;
    uint64_t length;
    void* owner;
    CfvReleaseFunction release;
} CfvOwnedText;

/* Vista prestada de mapa texto->número. keys y values contienen length entradas. */
typedef struct CfvNumberMapView {
    const char* const* keys;
    const double* values;
    uint64_t length;
} CfvNumberMapView;

typedef struct CfvValue {
    int32_t type;
    int64_t integer;
    double decimal;
    const char* text;
    void* owner;
    CfvReleaseFunction release;
} CfvValue;

typedef struct CfvRecordField {
    const char* name;
    CfvValue value;
} CfvRecordField;

/* Vista prestada de un objeto nominal con campos escalares. */
typedef struct CfvRecordView {
    const char* type_name;
    const CfvRecordField* fields;
    uint64_t field_count;
} CfvRecordView;

typedef int (*CfvForeignFunction)(
    const CfvValue* arguments,
    size_t argument_count,
    CfvValue* result,
    char* error_buffer,
    size_t error_buffer_size
);

#define CFV_ABI_V2 0x00020000u
#define CFV_V2_BORROWED 0x00000001ull
#define CFV_V2_OWNED 0x00000002ull
#define CFV_V2_MAX_DEPTH 64u

typedef struct CfvValueV2 {
    uint32_t struct_size;
    uint32_t type;
    uint64_t flags;
    uint64_t length;
    int64_t integer;
    double decimal;
    const void* data;
    void* owner;
    CfvReleaseFunction release;
} CfvValueV2;

/* Una lista V2 usa data=CfvValueV2[length]. Sus elementos son vistas
   recursivas y no pueden conservarse después de la llamada si BORROWED. */
typedef struct CfvMapEntryV2 {
    CfvValueV2 key;
    CfvValueV2 value;
} CfvMapEntryV2;

/* Un mapa V2 usa data=CfvMapEntryV2[length]. Las claves actuales deben ser
   CFV_TEXT; la forma permite ampliar el ABI sin cambiar CfvValueV2. */
typedef struct CfvRecordFieldV2 {
    const char* name;
    uint64_t name_length;
    CfvValueV2 value;
} CfvRecordFieldV2;

typedef struct CfvRecordV2 {
    const char* type_name;
    uint64_t type_name_length;
    const CfvRecordFieldV2* fields;
    uint64_t field_count;
} CfvRecordV2;

typedef int (*CfvForeignFunctionV2)(
    uint32_t abi_version,
    const CfvValueV2* arguments,
    size_t argument_count,
    CfvValueV2* result,
    char* error_buffer,
    size_t error_buffer_size
);

CFV_EXPORT int cfv_register_function(const char* name, CfvForeignFunction function);
CFV_EXPORT int cfv_register_function_v2(const char* name, CfvForeignFunctionV2 function);

#ifdef __cplusplus
}
#endif

#endif
)CFV65DATA"},
        {R"CFV66DATA(include/cforge_shared_arena.h)CFV66DATA", R"CFV67DATA(#ifndef CFORGE_SHARED_ARENA_H
#define CFORGE_SHARED_ARENA_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cforge::arena {

using Offset = std::uint64_t;

inline constexpr std::uint64_t kMagic = 0x43464F5247454131ULL;  // "CFORGEA1"
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint64_t kAlignment = 64;
inline constexpr std::uint32_t kRecordLive = 1;
inline constexpr std::uint32_t kRecordReleased = 2;

enum class ValueType : std::uint32_t {
    Null = 0,
    Boolean = 1,
    Integer = 2,
    Decimal = 3,
    Utf8 = 4,
    Bytes = 5,
    Json = 6,
    Float64Array = 7,
};

struct alignas(64) RecordHeader final {
    std::uint64_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t type = 0;
    std::uint64_t payload_size = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t generation = 0;
    std::atomic<std::uint64_t> references{1};
    std::atomic<std::uint32_t> state{kRecordLive};
    std::uint32_t checksum = 0;
    std::uint8_t reserved[8]{};
};

struct alignas(64) ArenaHeader final {
    std::uint64_t magic = kMagic;
    std::uint32_t version = kVersion;
    std::uint32_t header_size = 0;
    std::uint64_t capacity = 0;
    std::atomic<std::uint64_t> used{0};
    std::atomic<std::uint64_t> generation{1};
    std::atomic<std::uint64_t> live_records{0};
#ifndef _WIN32
    pthread_mutex_t mutex{};
#else
    std::uint8_t mutex_placeholder[64]{};
#endif
};

struct ByteView final {
    const std::byte* data = nullptr;
    std::uint64_t size = 0;
    ValueType type = ValueType::Null;
    Offset record = 0;
};

inline std::uint64_t align_up(std::uint64_t value) {
    if (value > std::numeric_limits<std::uint64_t>::max() - (kAlignment - 1))
        throw std::overflow_error("ForgeSharedArena: overflow de alineación");
    return (value + kAlignment - 1) & ~(kAlignment - 1);
}

inline std::uint32_t checksum32(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t hash = 2166136261u;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

class ForgeSharedArena final {
public:
    static ForgeSharedArena create(const std::filesystem::path& path, std::uint64_t capacity) {
        if (capacity < align_up(sizeof(ArenaHeader)) + align_up(sizeof(RecordHeader)) + 1)
            throw std::invalid_argument("ForgeSharedArena: capacidad demasiado pequeña");
        ForgeSharedArena arena;
        arena.open_mapping(path, capacity, true);
        std::memset(arena.base_, 0, static_cast<std::size_t>(capacity));
        auto* header = new (arena.base_) ArenaHeader{};
        header->header_size = static_cast<std::uint32_t>(sizeof(ArenaHeader));
        header->capacity = capacity;
        header->used.store(align_up(sizeof(ArenaHeader)), std::memory_order_release);
#ifndef _WIN32
        pthread_mutexattr_t attributes;
        if (pthread_mutexattr_init(&attributes) != 0)
            throw std::runtime_error("ForgeSharedArena: mutexattr_init falló");
        pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
#ifdef PTHREAD_MUTEX_ROBUST
        pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST);
#endif
        const int status = pthread_mutex_init(&header->mutex, &attributes);
        pthread_mutexattr_destroy(&attributes);
        if (status != 0) throw std::runtime_error("ForgeSharedArena: mutex compartido falló");
#endif
        arena.header_ = header;
        return arena;
    }

    static ForgeSharedArena open(const std::filesystem::path& path) {
        ForgeSharedArena arena;
        arena.open_mapping(path, 0, false);
        arena.header_ = reinterpret_cast<ArenaHeader*>(arena.base_);
        arena.validate_arena();
        return arena;
    }

    ForgeSharedArena() = default;
    ~ForgeSharedArena() { close(); }
    ForgeSharedArena(const ForgeSharedArena&) = delete;
    ForgeSharedArena& operator=(const ForgeSharedArena&) = delete;
    ForgeSharedArena(ForgeSharedArena&& other) noexcept { move_from(other); }
    ForgeSharedArena& operator=(ForgeSharedArena&& other) noexcept {
        if (this != &other) { close(); move_from(other); }
        return *this;
    }

    Offset store(ValueType type, const void* payload, std::uint64_t size) {
        if (size && !payload) throw std::invalid_argument("ForgeSharedArena: payload nulo");
        Lock guard(*this);
        const auto record_offset = align_up(header_->used.load(std::memory_order_acquire));
        const auto payload_offset = align_up(record_offset + sizeof(RecordHeader));
        if (size > header_->capacity || payload_offset > header_->capacity - size)
            throw std::runtime_error("ForgeSharedArena: espacio agotado");
        auto* record = new (address(record_offset, sizeof(RecordHeader))) RecordHeader{};
        record->type = static_cast<std::uint32_t>(type);
        record->payload_size = size;
        record->payload_offset = payload_offset;
        record->generation = header_->generation.fetch_add(1, std::memory_order_acq_rel);
        void* destination = address(payload_offset, size);
        if (size) std::memcpy(destination, payload, static_cast<std::size_t>(size));
        record->checksum = checksum32(destination, static_cast<std::size_t>(size));
        header_->used.store(align_up(payload_offset + size), std::memory_order_release);
        header_->live_records.fetch_add(1, std::memory_order_acq_rel);
        return record_offset;
    }

    Offset store_text(ValueType type, std::string_view value) {
        if (type != ValueType::Utf8 && type != ValueType::Json)
            throw std::invalid_argument("ForgeSharedArena: store_text requiere Utf8 o Json");
        return store(type, value.data(), value.size());
    }

    ByteView view(Offset offset) const {
        validate_arena();
        const auto* record = record_at(offset);
        if (record->state.load(std::memory_order_acquire) != kRecordLive)
            throw std::runtime_error("ForgeSharedArena: registro liberado");
        const auto* payload = static_cast<const std::byte*>(address(record->payload_offset, record->payload_size));
        if (checksum32(payload, static_cast<std::size_t>(record->payload_size)) != record->checksum)
            throw std::runtime_error("ForgeSharedArena: checksum inválido");
        return {payload, record->payload_size, static_cast<ValueType>(record->type), offset};
    }

    void retain(Offset offset) {
        auto* record = record_at(offset);
        if (record->state.load(std::memory_order_acquire) != kRecordLive)
            throw std::runtime_error("ForgeSharedArena: retain sobre registro liberado");
        record->references.fetch_add(1, std::memory_order_acq_rel);
    }

    void release(Offset offset) {
        auto* record = record_at(offset);
        const auto previous = record->references.fetch_sub(1, std::memory_order_acq_rel);
        if (previous == 0) {
            record->references.fetch_add(1, std::memory_order_relaxed);
            throw std::runtime_error("ForgeSharedArena: doble liberación");
        }
        if (previous == 1) {
            record->state.store(kRecordReleased, std::memory_order_release);
            header_->live_records.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    std::uint64_t capacity() const noexcept { return header_ ? header_->capacity : 0; }
    std::uint64_t used() const noexcept {
        return header_ ? header_->used.load(std::memory_order_acquire) : 0;
    }
    std::uint64_t live_records() const noexcept {
        return header_ ? header_->live_records.load(std::memory_order_acquire) : 0;
    }

private:
    class Lock final {
    public:
        explicit Lock(ForgeSharedArena& arena) : arena_(arena) { arena_.lock(); }
        ~Lock() { arena_.unlock(); }
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    private:
        ForgeSharedArena& arena_;
    };

    void validate_arena() const {
        if (!header_ || header_->magic != kMagic || header_->version != kVersion ||
            header_->header_size != sizeof(ArenaHeader))
            throw std::runtime_error("ForgeSharedArena: cabecera incompatible");
        const auto used = header_->used.load(std::memory_order_acquire);
        if (used < align_up(sizeof(ArenaHeader)) || used > header_->capacity)
            throw std::runtime_error("ForgeSharedArena: límites corruptos");
    }

    RecordHeader* record_at(Offset offset) const {
        auto* record = static_cast<RecordHeader*>(address(offset, sizeof(RecordHeader)));
        if (record->magic != kMagic || record->version != kVersion)
            throw std::runtime_error("ForgeSharedArena: registro incompatible");
        if (record->payload_offset < offset + sizeof(RecordHeader))
            throw std::runtime_error("ForgeSharedArena: offset de payload corrupto");
        address(record->payload_offset, record->payload_size);
        return record;
    }

    void* address(Offset offset, std::uint64_t size) const {
        if (!base_ || offset > mapped_size_ || size > mapped_size_ - offset)
            throw std::out_of_range("ForgeSharedArena: acceso fuera de límites");
        return static_cast<std::byte*>(base_) + offset;
    }

    void lock() {
#ifdef _WIN32
        const DWORD result = WaitForSingleObject(mutex_, INFINITE);
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED)
            throw std::runtime_error("ForgeSharedArena: WaitForSingleObject falló");
#else
        const int result = pthread_mutex_lock(&header_->mutex);
#if defined(EOWNERDEAD) && !defined(__APPLE__)
        if (result == EOWNERDEAD) { pthread_mutex_consistent(&header_->mutex); return; }
#endif
        if (result != 0) throw std::runtime_error("ForgeSharedArena: lock falló");
#endif
    }

    void unlock() noexcept {
#ifdef _WIN32
        if (mutex_) ReleaseMutex(mutex_);
#else
        if (header_) pthread_mutex_unlock(&header_->mutex);
#endif
    }

    void open_mapping(const std::filesystem::path& path, std::uint64_t size, bool create) {
#ifdef _WIN32
        const auto wide = path.wstring();
        file_ = CreateFileW(wide.c_str(), GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            create ? CREATE_ALWAYS : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE) throw std::runtime_error("ForgeSharedArena: CreateFile falló");
        if (!create) {
            LARGE_INTEGER value{};
            if (!GetFileSizeEx(file_, &value) || value.QuadPart <= 0) throw std::runtime_error("ForgeSharedArena: tamaño inválido");
            size = static_cast<std::uint64_t>(value.QuadPart);
        } else {
            LARGE_INTEGER value{}; value.QuadPart = static_cast<LONGLONG>(size);
            if (!SetFilePointerEx(file_, value, nullptr, FILE_BEGIN) || !SetEndOfFile(file_))
                throw std::runtime_error("ForgeSharedArena: no se pudo dimensionar");
        }
        mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE,
            static_cast<DWORD>(size >> 32), static_cast<DWORD>(size), nullptr);
        if (!mapping_) throw std::runtime_error("ForgeSharedArena: CreateFileMapping falló");
        base_ = MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, static_cast<SIZE_T>(size));
        if (!base_) throw std::runtime_error("ForgeSharedArena: MapViewOfFile falló");
        const auto mutex_name = L"Local\\CForgeArena-" + std::to_wstring(checksum32(wide.data(), wide.size() * sizeof(wchar_t)));
        mutex_ = CreateMutexW(nullptr, FALSE, mutex_name.c_str());
        if (!mutex_) throw std::runtime_error("ForgeSharedArena: CreateMutex falló");
#else
        descriptor_ = ::open(path.c_str(), create ? (O_RDWR | O_CREAT | O_TRUNC) : O_RDWR, 0600);
        if (descriptor_ < 0) throw std::runtime_error("ForgeSharedArena: open falló");
        if (create) {
            if (ftruncate(descriptor_, static_cast<off_t>(size)) != 0)
                throw std::runtime_error("ForgeSharedArena: ftruncate falló");
        } else {
            struct stat info{};
            if (fstat(descriptor_, &info) != 0 || info.st_size <= 0)
                throw std::runtime_error("ForgeSharedArena: fstat falló");
            size = static_cast<std::uint64_t>(info.st_size);
        }
        base_ = mmap(nullptr, static_cast<std::size_t>(size), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor_, 0);
        if (base_ == MAP_FAILED) { base_ = nullptr; throw std::runtime_error("ForgeSharedArena: mmap falló"); }
#endif
        mapped_size_ = size;
    }

    void close() noexcept {
#ifdef _WIN32
        if (base_) UnmapViewOfFile(base_);
        if (mapping_) CloseHandle(mapping_);
        if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_);
        if (mutex_) CloseHandle(mutex_);
        mapping_ = nullptr; file_ = INVALID_HANDLE_VALUE; mutex_ = nullptr;
#else
        if (base_) munmap(base_, static_cast<std::size_t>(mapped_size_));
        if (descriptor_ >= 0) ::close(descriptor_);
        descriptor_ = -1;
#endif
        base_ = nullptr; header_ = nullptr; mapped_size_ = 0;
    }

    void move_from(ForgeSharedArena& other) noexcept {
        base_ = other.base_; header_ = other.header_; mapped_size_ = other.mapped_size_;
#ifdef _WIN32
        file_ = other.file_; mapping_ = other.mapping_; mutex_ = other.mutex_;
        other.file_ = INVALID_HANDLE_VALUE; other.mapping_ = nullptr; other.mutex_ = nullptr;
#else
        descriptor_ = other.descriptor_; other.descriptor_ = -1;
#endif
        other.base_ = nullptr; other.header_ = nullptr; other.mapped_size_ = 0;
    }

    void* base_ = nullptr;
    ArenaHeader* header_ = nullptr;
    std::uint64_t mapped_size_ = 0;
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HANDLE mapping_ = nullptr;
    HANDLE mutex_ = nullptr;
#else
    int descriptor_ = -1;
#endif
};

}  // namespace cforge::arena

#endif
)CFV67DATA"},
        {R"CFV68DATA(herramientas/cforgev_ffi_runner.cpp)CFV68DATA", R"CFV69DATA(#include "cforgev_ffi.h"
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
        else if (value == "b:true" || value == "b:false") values.push_back({CFV_BOOLEAN, value == "b:true" ? 1 : 0, 0, nullptr});
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
    else if (result.type == CFV_BOOLEAN) std::cout << (result.integer ? "verdadero" : "falso");
    std::cout.flush();
    if (result.release) result.release(result.owner);
    return 0;
}
)CFV69DATA"},
        {R"CFV70DATA(herramientas/cforge_cli.cpp)CFV70DATA", R"CFV71DATA(#include <cerrno>
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
        << "C-Forge Toolchain 1.6.0 Developer Preview\n"
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
)CFV71DATA"},
        {R"CFV72DATA(herramientas/vscode-cforgev/package.json)CFV72DATA", R"CFV73DATA({
  "name": "cforgev-language",
  "displayName": "C-Forge Language Support",
  "description": "Resaltado de sintaxis y configuración oficial para el lenguaje C-Forge (.cfv)",
  "version": "1.6.0",
  "publisher": "vemoris-group",
  "author": {
    "name": "Vemoris Group"
  },
  "license": "SEE LICENSE IN LICENSE",
  "icon": "images/icon.png",
  "preview": true,
  "main": "./extension.js",
  "activationEvents": ["onLanguage:cforgev", "onDebug:cforge", "onCommand:cforge.runFile", "onCommand:cforge.checkFile", "onCommand:cforge.debugBreakpoint"],
  "pricing": "Free",
  "engines": {
    "vscode": "^1.80.0"
  },
  "categories": [
    "Programming Languages"
  ],
  "keywords": [
    "c-forge",
    "cforge",
    "cforgev",
    "cfv",
    "programming language",
    "syntax highlighting",
    "vemoris",
    "compiler"
  ],
  "galleryBanner": {
    "color": "#FF6A32",
    "theme": "light"
  },
  "repository": {
    "type": "git",
    "url": "https://github.com/VemorisGroup/C-Forge.git"
  },
  "homepage": "https://github.com/VemorisGroup/C-Forge#readme",
  "bugs": {
    "url": "https://github.com/VemorisGroup/C-Forge/issues"
  },
  "scripts": {
    "package": "vsce package"
  },
  "devDependencies": {
    "@vscode/vsce": "^3.6.2"
  },
  "contributes": {
    "commands": [
      {"command": "cforge.runFile", "title": "C-Forge: Ejecutar archivo"},
      {"command": "cforge.checkFile", "title": "C-Forge: Comprobar archivo"},
      {"command": "cforge.debugBreakpoint", "title": "C-Forge: Iniciar depuración visual"}
    ],
    "languages": [
      {
        "id": "cforgev",
        "aliases": [
          "C-Forge",
          "cforgev"
        ],
        "extensions": [
          ".cfv"
        ],
        "configuration": "./language-configuration.json",
        "icon": {
          "light": "./images/icon.png",
          "dark": "./images/icon.png"
        }
      }
    ],
    "grammars": [
      {
        "language": "cforgev",
        "scopeName": "source.cforgev",
        "path": "./syntaxes/cforgev.tmLanguage.json"
      }
    ],
    "debuggers": [
      {
        "type": "cforge",
        "label": "C-Forge VM",
        "languages": ["cforgev"],
        "configurationAttributes": {
          "launch": {
            "required": ["program"],
            "properties": {
              "program": {"type": "string", "description": "Archivo .cfv", "default": "${file}"},
              "cwd": {"type": "string", "default": "${workspaceFolder}"}
            }
          }
        },
        "initialConfigurations": [
          {"type": "cforge", "request": "launch", "name": "Depurar C-Forge", "program": "${file}"}
        ]
      }
    ]
  }
}
)CFV73DATA"},
        {R"CFV74DATA(herramientas/vscode-cforgev/extension.js)CFV74DATA", R"CFV75DATA("use strict";

const vscode = require("vscode");
const { execFile, spawn } = require("child_process");

let languageServer;

class CForgeLanguageServer {
  constructor(diagnostics) {
    this.diagnostics = diagnostics;
    this.sequence = 1;
    this.available = true;
    this.stopped = false;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.process = spawn("cforge", ["lsp"], { stdio: ["pipe", "pipe", "pipe"] });
    this.process.stdout.on("data", chunk => this.consume(chunk));
    this.process.stderr.on("data", chunk => console.error(`[C-Forge LSP] ${chunk}`));
    this.process.on("error", error => {
      this.available = false;
      for (const value of this.pending.values()) value.reject(error);
      this.pending.clear();
      console.error(`[C-Forge LSP] ${error.message}`);
    });
    this.process.on("exit", () => {
      for (const value of this.pending.values()) value.reject(new Error("C-Forge LSP finalizó"));
      this.pending.clear();
    });
    this.ready = this.request("initialize", {
      processId: process.pid, rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
      capabilities: {}
    }).then(() => this.notify("initialized", {})).catch(() => { this.available = false; });
  }
  send(message) {
    if (!this.available) return;
    const body = Buffer.from(JSON.stringify({ jsonrpc: "2.0", ...message }), "utf8");
    this.process.stdin.write(Buffer.from(`Content-Length: ${body.length}\r\n\r\n`, "ascii"));
    this.process.stdin.write(body);
  }
  request(method, params) {
    if (!this.available) return Promise.reject(new Error("C-Forge LSP no está disponible"));
    const id = this.sequence++;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.send({ id, method, params });
    });
  }
  async initializedRequest(method, params) {
    await this.ready;
    if (!this.available || this.stopped) throw new Error("C-Forge LSP no está disponible");
    return this.request(method, params);
  }
  notify(method, params) { this.send({ method, params }); }
  consume(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const end = this.buffer.indexOf("\r\n\r\n");
      if (end < 0) return;
      const header = this.buffer.subarray(0, end).toString("ascii");
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) { this.buffer = this.buffer.subarray(end + 4); continue; }
      const length = Number(match[1]);
      if (this.buffer.length < end + 4 + length) return;
      const message = JSON.parse(this.buffer.subarray(end + 4, end + 4 + length).toString("utf8"));
      this.buffer = this.buffer.subarray(end + 4 + length);
      if (message.id !== undefined && this.pending.has(message.id)) {
        const pending = this.pending.get(message.id); this.pending.delete(message.id);
        if (message.error) pending.reject(new Error(message.error.message)); else pending.resolve(message.result);
      } else if (message.method === "textDocument/publishDiagnostics") {
        const uri = vscode.Uri.parse(message.params.uri);
        this.diagnostics.set(uri, (message.params.diagnostics || []).map(item => {
          const diagnostic = new vscode.Diagnostic(asRange(item.range), item.message,
            item.severity === 1 ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning);
          diagnostic.source = item.source || "C-Forge"; diagnostic.code = item.code; return diagnostic;
        }));
      }
    }
  }
  async open(document) {
    await this.ready;
    if (!this.available) return;
    this.notify("textDocument/didOpen", { textDocument: {
      uri: document.uri.toString(), languageId: "cforgev", version: document.version, text: document.getText()
    }});
  }
  async change(document) {
    await this.ready;
    if (!this.available) return;
    this.notify("textDocument/didChange", { textDocument: {
      uri: document.uri.toString(), version: document.version
    }, contentChanges: [{ text: document.getText() }] });
  }
  async stop() {
    if (this.stopped) return;
    this.stopped = true;
    try { await this.request("shutdown", {}); this.notify("exit", {}); } catch (_) {}
    this.available = false;
    this.process.kill();
  }
}

function asPosition(value) { return new vscode.Position(value.line, value.character); }
function asRange(value) { return new vscode.Range(asPosition(value.start), asPosition(value.end)); }
function textParams(document, position) {
  return { textDocument: { uri: document.uri.toString() }, position: { line: position.line, character: position.character } };
}
function asLocation(value) { return new vscode.Location(vscode.Uri.parse(value.uri), asRange(value.range)); }

const words = [
  "sea", "si", "sino", "mientras", "funcion", "retornar", "estructura",
  "clase", "interfaz", "implementa", "campo", "metodo", "intentar", "capturar", "gpu", "cluster",
  "test", "mostrar", "print", "verdadero", "falso", "nulo", "file_read",
  "file_write", "json_parse", "sys_fetch", "forge_hash", "forge_bench"
];

function runCForge(args, callback) {
  execFile("cforge", args, { timeout: 30000, maxBuffer: 4 * 1024 * 1024 }, callback);
}

function activate(context) {
  const diagnostics = vscode.languages.createDiagnosticCollection("cforge");
  context.subscriptions.push(diagnostics);
  try {
    languageServer = new CForgeLanguageServer(diagnostics);
    context.subscriptions.push({ dispose: () => languageServer?.stop() });
    for (const document of vscode.workspace.textDocuments) {
      if (document.languageId === "cforgev") languageServer.open(document);
    }
    context.subscriptions.push(vscode.workspace.onDidOpenTextDocument(document => {
      if (document.languageId === "cforgev") languageServer.open(document);
    }));
    context.subscriptions.push(vscode.workspace.onDidChangeTextDocument(event => {
      if (event.document.languageId === "cforgev") languageServer.change(event.document);
    }));
  } catch (error) {
    languageServer = undefined;
    console.error(`[C-Forge] No se pudo iniciar LSP: ${error}`);
  }

  function check(document) {
    if (document.languageId !== "cforgev" || document.isUntitled) return;
    runCForge(["check", document.uri.fsPath, "--json"], (error, stdout) => {
      let values = [];
      try { values = JSON.parse(stdout || "[]"); } catch (_) { return; }
      diagnostics.set(document.uri, values.map(item => {
        const line = Math.max(0, Number(item.line || 1) - 1);
        const column = Math.max(0, Number(item.column || 1) - 1);
        const diagnostic = new vscode.Diagnostic(
          new vscode.Range(line, column, line, column + 1),
          `${item.code}: ${item.message}`,
          item.severity === "error" ? vscode.DiagnosticSeverity.Error : vscode.DiagnosticSeverity.Warning
        );
        diagnostic.source = "C-Forge";
        diagnostic.code = item.code;
        return diagnostic;
      }));
    });
  }

  context.subscriptions.push(vscode.workspace.onDidSaveTextDocument(check));
  if (vscode.window.activeTextEditor) check(vscode.window.activeTextEditor.document);
  context.subscriptions.push(vscode.languages.registerCompletionItemProvider("cforgev", {
    async provideCompletionItems(document, position) {
      if (languageServer) {
        try { return await languageServer.initializedRequest("textDocument/completion", textParams(document, position)); }
        catch (_) {}
      }
      return words.map(word => new vscode.CompletionItem(word, vscode.CompletionItemKind.Keyword));
    }
  }));
  context.subscriptions.push(vscode.languages.registerHoverProvider("cforgev", {
    async provideHover(document, position) {
      if (!languageServer) return undefined;
      const result = await languageServer.initializedRequest("textDocument/hover", textParams(document, position));
      if (!result) return undefined;
      const value = typeof result.contents === "string" ? result.contents : result.contents.value;
      return new vscode.Hover(new vscode.MarkdownString(value));
    }
  }));
  context.subscriptions.push(vscode.languages.registerDefinitionProvider("cforgev", {
    async provideDefinition(document, position) {
      if (!languageServer) return [];
      const result = await languageServer.initializedRequest("textDocument/definition", textParams(document, position));
      return (result || []).map(asLocation);
    }
  }));
  context.subscriptions.push(vscode.languages.registerReferenceProvider("cforgev", {
    async provideReferences(document, position, contextValue) {
      if (!languageServer) return [];
      const result = await languageServer.initializedRequest("textDocument/references", {
        ...textParams(document, position), context: { includeDeclaration: contextValue.includeDeclaration }
      });
      return (result || []).map(asLocation);
    }
  }));
  context.subscriptions.push(vscode.languages.registerRenameProvider("cforgev", {
    async provideRenameEdits(document, position, newName) {
      if (!languageServer) return undefined;
      const result = await languageServer.initializedRequest("textDocument/rename", {
        ...textParams(document, position), newName
      });
      const edit = new vscode.WorkspaceEdit();
      for (const [uri, edits] of Object.entries(result?.changes || {})) {
        for (const item of edits) edit.replace(vscode.Uri.parse(uri), asRange(item.range), item.newText);
      }
      return edit;
    }
  }));
  context.subscriptions.push(vscode.languages.registerDocumentFormattingEditProvider("cforgev", {
    async provideDocumentFormattingEdits(document, options) {
      if (!languageServer) return [];
      const result = await languageServer.initializedRequest("textDocument/formatting", {
        textDocument: { uri: document.uri.toString() }, options
      });
      return (result || []).map(item => vscode.TextEdit.replace(asRange(item.range), item.newText));
    }
  }));
  context.subscriptions.push(vscode.debug.registerDebugAdapterDescriptorFactory("cforge", {
    createDebugAdapterDescriptor() {
      return new vscode.DebugAdapterExecutable("cforge", ["dap"]);
    }
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.checkFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (editor) { check(editor.document); vscode.window.showInformationMessage("C-Forge: comprobación finalizada"); }
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.runFile", () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    const terminal = vscode.window.createTerminal("C-Forge");
    terminal.show();
    terminal.sendText(`cforge ${JSON.stringify(editor.document.uri.fsPath)}`);
  }));
  context.subscriptions.push(vscode.commands.registerCommand("cforge.debugBreakpoint", async () => {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.isUntitled) return;
    await vscode.debug.startDebugging(undefined, {
      type: "cforge", request: "launch", name: "Depurar C-Forge",
      program: editor.document.uri.fsPath
    });
  }));
}

async function deactivate() { if (languageServer) await languageServer.stop(); }

module.exports = { activate, deactivate };
)CFV75DATA"},
        {R"CFV76DATA(herramientas/vscode-cforgev/language-configuration.json)CFV76DATA", R"CFV77DATA({
  "comments": { "lineComment": "//" },
  "brackets": [["{", "}"], ["[", "]"], ["(", ")"]],
  "autoClosingPairs": [
    { "open": "{", "close": "}" },
    { "open": "[", "close": "]" },
    { "open": "(", "close": ")" },
    { "open": "\"", "close": "\"" }
  ]
}
)CFV77DATA"},
        {R"CFV78DATA(herramientas/vscode-cforgev/syntaxes/cforgev.tmLanguage.json)CFV78DATA", R"CFV79DATA({
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
    { "include": "#compatibility" },
    { "include": "#functions" }
  ],
  "repository": {
    "comments": { "patterns": [{ "name": "comment.line.double-slash.cforgev", "match": "//.*$" }] },
    "strings": { "patterns": [{ "name": "string.quoted.double.cforgev", "begin": "\"", "end": "\"", "patterns": [{ "name": "constant.character.escape.cforgev", "match": "\\\\." }] }] },
    "numbers": { "patterns": [{ "name": "constant.numeric.cforgev", "match": "\\b\\d+(?:\\.\\d+)?\\b" }] },
    "types": { "patterns": [{ "name": "storage.type.cforgev", "match": "\\b(numero|texto|booleano|lista|mapa|tupla|conjunto|opcion|nulo|cualquiera)\\b" }] },
    "externPython": { "patterns": [{ "name": "meta.embedded.block.python.cforgev", "begin": "\\bextern\\s*\\(\\s*\"python\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.python" }] },
    "externCpp": { "patterns": [{ "name": "meta.embedded.block.cpp.cforgev", "begin": "\\bextern\\s*\\(\\s*\"cpp\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.cpp" }] },
    "externJavaScript": { "patterns": [{ "name": "meta.embedded.block.javascript.cforgev", "begin": "\\bextern\\s*\\(\\s*\"(?:javascript|typescript)\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.js" }] },
    "externJava": { "patterns": [{ "name": "meta.embedded.block.java.cforgev", "begin": "\\bextern\\s*\\(\\s*\"java\"\\s*\\)\\s*\\{", "beginCaptures": { "0": { "name": "keyword.control.import.cforgev" } }, "end": "\\}", "contentName": "source.java" }] },
    "keywords": { "patterns": [
      { "name": "keyword.control.test.cforgev", "match": "\\b(test|afirmar)\\b" },
      { "name": "keyword.control.gpu.cforgev", "match": "\\bgpu\\b" },
      { "name": "storage.modifier.cluster.cforgev", "match": "\\bcluster\\b" },
      { "name": "storage.modifier.unsafe.cforgev", "match": "\\b(unsafe|region)\\b" },
      { "name": "storage.modifier.async.cforgev", "match": "\\b(async|await)\\b" },
      { "name": "keyword.control.cforgev", "match": "\\b(sea|si|sino|mientras|funcion|retornar|estructura|clase|interfaz|implementa|campo|metodo|este|usar|import|pip|nuget|npm|maven|extern|extern_c|segura|intentar|capturar|verdadero|falso|y|o|no)\\b" }
    ] },
    "compatibility": { "patterns": [
      { "name": "meta.function-call.compatibility.javascript.cforgev", "match": "\\b(console)(\\.)(log)(?=\\s*\\()", "captures": { "1": { "name": "support.class.console.cforgev" }, "2": { "name": "punctuation.accessor.cforgev" }, "3": { "name": "support.function.log.cforgev" } } },
      { "name": "meta.function-call.compatibility.java.cforgev", "match": "\\b(System)(\\.)(out)(\\.)(println)(?=\\s*\\()", "captures": { "1": { "name": "support.class.system.cforgev" }, "2": { "name": "punctuation.accessor.cforgev" }, "3": { "name": "support.variable.out.cforgev" }, "4": { "name": "punctuation.accessor.cforgev" }, "5": { "name": "support.function.println.cforgev" } } },
      { "name": "meta.output.compatibility.cpp.cforgev", "match": "\\b(?:std::)?(?:cout|endl)\\b", "captures": { "0": { "name": "support.function.output.cpp.cforgev" } } },
      { "name": "meta.method.compatibility.collection.cforgev", "match": "(\\.)(append|push|length|len)\\b", "captures": { "1": { "name": "punctuation.accessor.cforgev" }, "2": { "name": "support.function.collection.cforgev" } } }
    ] },
    "functions": { "patterns": [
      { "name": "entity.name.function.connector.cforgev", "match": "\\b(?:ia_|ui_|web_)[A-Za-z_][A-Za-z0-9_]*\\b" },
      { "name": "support.function.cforgev", "match": "\\b(mostrar|print|leer|leer_archivo|escribir_archivo|existe_archivo|file_read|file_write|file_append|sys_run|sys_info|net_listen|net_send|matrix|array_fast|conjunto|longitud|agregar|a_numero|a_texto|raiz|potencia|absoluto|redondear|tiempo_actual|argumentos|forge_hash|forge_bench|forge_catalogo|forge_arena_estado|json_parse|sys_fetch|use_python|use_csharp|use_native|use_cpp|use_javascript|use_typescript|use_java|cluster_estado|jit_estado|jit_caliente|paralelo|mover|prestar|prestar_mut|soltar_prestamo|destruir|algunos|ninguno|es_algunos|desenvolver|tarea|esperar|cancelar|canal|enviar|recibir|cerrar_canal)\\b" }
    ] }
  }
}
)CFV79DATA"}
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
    std::cout << "C-Forge Setup 1.6.0 Developer Preview\n";
    const bool clang = command_available("clang++");
    const bool python = command_available("python3");
#ifdef __APPLE__
    const bool java = std::system("/usr/libexec/java_home >/dev/null 2>&1") == 0;
#else
    const bool java = command_available("java") && command_available("javac");
#endif
    const bool node = command_available("node");
    const bool signatures = std::system("python3 -c 'import cryptography' >/dev/null 2>&1") == 0;
    std::cout << (clang ? "[OK] C++: clang++ disponible\n" : "[FALTA] C++: instala las herramientas de desarrollo\n");
    std::cout << (python ? "[OK] Python 3 disponible\n" : "[FALTA] Python 3\n");
    std::cout << (signatures ? "[OK] Paquetes: firmas Ed25519 disponibles\n" :
                               "[FALTA] Paquetes firmados: instala el componente cryptography\n");
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
