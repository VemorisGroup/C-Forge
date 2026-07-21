#!/usr/bin/env python3
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

VERSION = "1.4.1-definitive"

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
            if field.value == "length" and isinstance(value, (str, list, dict)):
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
            self.imported_modules, self.structures, self.program_arguments
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
    print("C-Forge Setup 1.4.1")
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
