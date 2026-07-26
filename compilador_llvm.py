"""Backend LLVM IR real de C-Forge para el núcleo numérico tipado.

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
