"""Análisis estático de ownership, préstamos y efectos interprocedurales."""
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
