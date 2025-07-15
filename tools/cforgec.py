#!/usr/bin/env python3
"""
cforgec — Transpilador AOT de C-Forge → C99
Compila archivos .cfv a C nativo, que luego puede compilarse con gcc/clang.

Uso:
  cforgec archivo.cfv                  # genera archivo.c
  cforgec archivo.cfv -o salida.c      # nombre explícito
  cforgec archivo.cfv --compile        # genera y compila con gcc
  cforgec archivo.cfv --run            # genera, compila y ejecuta
  cforgec --ir archivo.cfv             # solo mostrar IR intermedio
"""

import sys
import re
import os
import subprocess
import argparse
from pathlib import Path
from dataclasses import dataclass, field
from typing import List, Optional, Dict, Tuple

# ── AST nodes ─────────────────────────────────────────────────────────────────
@dataclass
class Node:
    tipo: str
    hijos: List = field(default_factory=list)
    valor: object = None
    nombre: str = ""
    linea: int = 0


# ── Tokenizador ────────────────────────────────────────────────────────────────
KEYWORDS = {
    "sea", "funcion", "retornar", "si", "sino", "esi", "para", "en",
    "mientras", "segun", "caso", "romper", "continuar", "clase", "nuevo",
    "importar", "exportar", "lanzar", "intentar", "capturar", "finalmente",
    "verdadero", "falso", "nulo", "rango", "y", "o", "no"
}

TOKEN_RE = re.compile(
    r'(?P<COMENTARIO>//[^\n]*)'
    r'|(?P<MLCOMENTARIO>/\*[\s\S]*?\*/)'
    r'|(?P<NUMERO>-?\d+\.?\d*(?:e[+-]?\d+)?)'
    r'|(?P<TEXTO>"(?:[^"\\]|\\.)*"|\'(?:[^\'\\]|\\.)*\')'
    r'|(?P<IDENT>[a-zA-Z_]\w*)'
    r'|(?P<OP_COMP>==|!=|<=|>=|&&|\|\||=>|->|\.\.\.)'
    r'|(?P<OP>[+\-*/%<>!&|^~=.,;:?\[\](){}])'
    r'|(?P<NL>\n)'
    r'|(?P<WS>[ \t]+)'
)

def tokenizar(codigo: str) -> List[Tuple[str, str, int]]:
    tokens = []
    linea = 1
    for m in TOKEN_RE.finditer(codigo):
        tipo = m.lastgroup
        val = m.group()
        if tipo in ("WS", "COMENTARIO", "MLCOMENTARIO"):
            if "\n" in val:
                linea += val.count("\n")
            continue
        if tipo == "NL":
            linea += 1
            continue
        if tipo == "IDENT" and val in KEYWORDS:
            tipo = "KW"
        tokens.append((tipo, val, linea))
    return tokens


# ── Parser simple → AST ────────────────────────────────────────────────────────
class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.pos = 0

    def peek(self, offset=0):
        i = self.pos + offset
        if i < len(self.tokens):
            return self.tokens[i]
        return ("EOF", "", 0)

    def consume(self, tipo=None, val=None):
        tok = self.tokens[self.pos] if self.pos < len(self.tokens) else ("EOF","",0)
        if tipo and tok[0] != tipo:
            raise SyntaxError(f"Línea {tok[2]}: se esperaba {tipo}, se obtuvo '{tok[1]}' ({tok[0]})")
        if val and tok[1] != val:
            raise SyntaxError(f"Línea {tok[2]}: se esperaba '{val}', se obtuvo '{tok[1]}'")
        self.pos += 1
        return tok

    def parse_programa(self) -> Node:
        stmts = []
        while self.peek()[0] != "EOF":
            stmts.append(self.parse_stmt())
        return Node("programa", stmts)

    def parse_stmt(self) -> Node:
        tok = self.peek()
        if tok[0] == "KW":
            if tok[1] == "sea":      return self.parse_declaracion()
            if tok[1] == "funcion":  return self.parse_funcion()
            if tok[1] == "retornar": return self.parse_retornar()
            if tok[1] == "si":       return self.parse_si()
            if tok[1] == "para":     return self.parse_para()
            if tok[1] == "mientras": return self.parse_mientras()
            if tok[1] == "lanzar":   return self.parse_lanzar()
            if tok[1] == "intentar": return self.parse_intentar()
            if tok[1] == "romper":   self.consume(); return Node("romper", linea=tok[2])
            if tok[1] == "continuar":self.consume(); return Node("continuar", linea=tok[2])
            if tok[1] == "importar": return self.parse_importar()
        return self.parse_expr_stmt()

    def parse_declaracion(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "sea")
        nombre = self.consume("IDENT")[1]
        tipo_var = None
        if self.peek()[1] == ":":
            self.consume("OP", ":")
            tipo_var = self.consume("IDENT")[1]
        self.consume("OP", "=")
        expr = self.parse_expr()
        self._consume_semi()
        n = Node("declaracion", [expr], nombre=nombre, linea=linea)
        n.valor = tipo_var
        return n

    def parse_funcion(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "funcion")
        nombre = self.consume("IDENT")[1]
        self.consume("OP", "(")
        params = []
        while self.peek()[1] != ")":
            pnombre = self.consume("IDENT")[1]
            ptipo = None
            if self.peek()[1] == ":":
                self.consume("OP", ":")
                ptipo = self.consume("IDENT")[1]
            params.append((pnombre, ptipo))
            if self.peek()[1] == ",":
                self.consume("OP", ",")
        self.consume("OP", ")")
        tipo_ret = None
        if self.peek()[1] == ":":
            self.consume("OP", ":")
            tipo_ret = self.consume("IDENT")[1]
        cuerpo = self.parse_bloque()
        n = Node("funcion", [cuerpo], nombre=nombre, linea=linea)
        n.valor = (params, tipo_ret)
        return n

    def parse_retornar(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "retornar")
        expr = None
        if self.peek()[1] not in ("}", ";") and self.peek()[0] != "EOF":
            expr = self.parse_expr()
        self._consume_semi()
        return Node("retornar", [expr] if expr else [], linea=linea)

    def parse_si(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "si")
        self.consume("OP", "(")
        cond = self.parse_expr()
        self.consume("OP", ")")
        cuerpo = self.parse_bloque()
        ramas = [Node("rama_si", [cond, cuerpo])]
        while self.peek()[1] == "sino" and self.peek(1)[1] == "si":
            self.consume("KW", "sino")
            self.consume("KW", "si")
            self.consume("OP", "(")
            c2 = self.parse_expr()
            self.consume("OP", ")")
            b2 = self.parse_bloque()
            ramas.append(Node("rama_esi", [c2, b2]))
        sino = None
        if self.peek()[1] == "sino":
            self.consume("KW", "sino")
            sino = self.parse_bloque()
        return Node("si", ramas + ([sino] if sino else []), linea=linea)

    def parse_para(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "para")
        var = self.consume("IDENT")[1]
        self.consume("KW", "en")
        iter_expr = self.parse_expr()
        cuerpo = self.parse_bloque()
        n = Node("para", [iter_expr, cuerpo], nombre=var, linea=linea)
        return n

    def parse_mientras(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "mientras")
        self.consume("OP", "(")
        cond = self.parse_expr()
        self.consume("OP", ")")
        cuerpo = self.parse_bloque()
        return Node("mientras", [cond, cuerpo], linea=linea)

    def parse_lanzar(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "lanzar")
        expr = self.parse_expr()
        return Node("lanzar", [expr], linea=linea)

    def parse_intentar(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "intentar")
        cuerpo = self.parse_bloque()
        var_ex = None
        cap = None
        if self.peek()[1] == "capturar":
            self.consume("KW", "capturar")
            self.consume("OP", "(")
            var_ex = self.consume("IDENT")[1]
            self.consume("OP", ")")
            cap = self.parse_bloque()
        fin = None
        if self.peek()[1] == "finalmente":
            self.consume("KW", "finalmente")
            fin = self.parse_bloque()
        n = Node("intentar", [cuerpo] + ([cap] if cap else []) + ([fin] if fin else []), linea=linea)
        n.nombre = var_ex or ""
        return n

    def parse_importar(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "importar")
        ruta = self.consume("TEXTO")[1].strip('"\'')
        return Node("importar", [], valor=ruta, linea=linea)

    def parse_bloque(self) -> Node:
        linea = self.peek()[2]
        self.consume("OP", "{")
        stmts = []
        while self.peek()[1] != "}" and self.peek()[0] != "EOF":
            stmts.append(self.parse_stmt())
        self.consume("OP", "}")
        return Node("bloque", stmts, linea=linea)

    def parse_expr_stmt(self) -> Node:
        expr = self.parse_expr()
        self._consume_semi()
        return Node("expr_stmt", [expr], linea=expr.linea)

    def parse_expr(self) -> Node:
        return self.parse_asignacion()

    def parse_asignacion(self) -> Node:
        left = self.parse_or()
        if self.peek()[1] == "=":
            linea = self.peek()[2]
            self.consume("OP", "=")
            right = self.parse_asignacion()
            return Node("asignar", [left, right], linea=linea)
        return left

    def parse_or(self) -> Node:
        left = self.parse_and()
        while self.peek()[1] in ("o", "||"):
            op = self.consume()[1]
            right = self.parse_and()
            left = Node("binop", [left, right], valor="||", linea=left.linea)
        return left

    def parse_and(self) -> Node:
        left = self.parse_igualdad()
        while self.peek()[1] in ("y", "&&"):
            op = self.consume()[1]
            right = self.parse_igualdad()
            left = Node("binop", [left, right], valor="&&", linea=left.linea)
        return left

    def parse_igualdad(self) -> Node:
        left = self.parse_comparacion()
        while self.peek()[1] in ("==", "!="):
            op = self.consume()[1]
            right = self.parse_comparacion()
            left = Node("binop", [left, right], valor=op, linea=left.linea)
        return left

    def parse_comparacion(self) -> Node:
        left = self.parse_suma()
        while self.peek()[1] in ("<", ">", "<=", ">="):
            op = self.consume()[1]
            right = self.parse_suma()
            left = Node("binop", [left, right], valor=op, linea=left.linea)
        return left

    def parse_suma(self) -> Node:
        left = self.parse_producto()
        while self.peek()[1] in ("+", "-"):
            op = self.consume()[1]
            right = self.parse_producto()
            left = Node("binop", [left, right], valor=op, linea=left.linea)
        return left

    def parse_producto(self) -> Node:
        left = self.parse_unario()
        while self.peek()[1] in ("*", "/", "%"):
            op = self.consume()[1]
            right = self.parse_unario()
            left = Node("binop", [left, right], valor=op, linea=left.linea)
        return left

    def parse_unario(self) -> Node:
        tok = self.peek()
        if tok[1] == "no" or tok[1] == "!":
            self.consume()
            expr = self.parse_unario()
            return Node("unario", [expr], valor="!", linea=tok[2])
        if tok[1] == "-" and self.peek(1)[0] in ("NUMERO", "IDENT", "OP"):
            self.consume()
            expr = self.parse_unario()
            return Node("unario", [expr], valor="-", linea=tok[2])
        return self.parse_postfijo()

    def parse_postfijo(self) -> Node:
        expr = self.parse_primario()
        while True:
            tok = self.peek()
            if tok[1] == "(":
                self.consume("OP", "(")
                args = []
                while self.peek()[1] != ")":
                    args.append(self.parse_expr())
                    if self.peek()[1] == ",":
                        self.consume("OP", ",")
                self.consume("OP", ")")
                expr = Node("llamada", [expr] + args, linea=tok[2])
            elif tok[1] == "[":
                self.consume("OP", "[")
                idx = self.parse_expr()
                self.consume("OP", "]")
                expr = Node("indice", [expr, idx], linea=tok[2])
            elif tok[1] == ".":
                self.consume("OP", ".")
                campo = self.consume("IDENT")[1]
                expr = Node("campo", [expr], nombre=campo, linea=tok[2])
            else:
                break
        return expr

    def parse_primario(self) -> Node:
        tok = self.peek()
        if tok[0] == "NUMERO":
            self.consume()
            return Node("numero", valor=float(tok[1]), linea=tok[2])
        if tok[0] == "TEXTO":
            self.consume()
            val = tok[1][1:-1]  # quitar comillas
            return Node("texto", valor=val, linea=tok[2])
        if tok[0] == "KW":
            if tok[1] == "verdadero":
                self.consume()
                return Node("booleano", valor=True, linea=tok[2])
            if tok[1] == "falso":
                self.consume()
                return Node("booleano", valor=False, linea=tok[2])
            if tok[1] == "nulo":
                self.consume()
                return Node("nulo", linea=tok[2])
            if tok[1] == "funcion":
                return self.parse_lambda()
            if tok[1] == "nuevo":
                return self.parse_nuevo()
        if tok[0] == "IDENT":
            self.consume()
            return Node("ident", nombre=tok[1], linea=tok[2])
        if tok[1] == "(":
            self.consume("OP", "(")
            expr = self.parse_expr()
            self.consume("OP", ")")
            return expr
        if tok[1] == "[":
            return self.parse_lista_literal()
        if tok[1] == "{":
            return self.parse_mapa_literal()
        raise SyntaxError(f"Línea {tok[2]}: token inesperado '{tok[1]}' ({tok[0]})")

    def parse_lambda(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "funcion")
        self.consume("OP", "(")
        params = []
        while self.peek()[1] != ")":
            pnombre = self.consume("IDENT")[1]
            ptipo = None
            if self.peek()[1] == ":":
                self.consume("OP", ":")
                ptipo = self.consume("IDENT")[1]
            params.append((pnombre, ptipo))
            if self.peek()[1] == ",":
                self.consume("OP", ",")
        self.consume("OP", ")")
        cuerpo = self.parse_bloque()
        n = Node("lambda", [cuerpo], linea=linea)
        n.valor = params
        return n

    def parse_nuevo(self) -> Node:
        linea = self.peek()[2]
        self.consume("KW", "nuevo")
        nombre = self.consume("IDENT")[1]
        self.consume("OP", "(")
        args = []
        while self.peek()[1] != ")":
            args.append(self.parse_expr())
            if self.peek()[1] == ",":
                self.consume("OP", ",")
        self.consume("OP", ")")
        return Node("nuevo", args, nombre=nombre, linea=linea)

    def parse_lista_literal(self) -> Node:
        linea = self.peek()[2]
        self.consume("OP", "[")
        items = []
        while self.peek()[1] != "]":
            items.append(self.parse_expr())
            if self.peek()[1] == ",":
                self.consume("OP", ",")
        self.consume("OP", "]")
        return Node("lista_lit", items, linea=linea)

    def parse_mapa_literal(self) -> Node:
        linea = self.peek()[2]
        self.consume("OP", "{")
        pares = []
        while self.peek()[1] != "}":
            clave = self.parse_expr()
            self.consume("OP", ":")
            val = self.parse_expr()
            pares.append((clave, val))
            if self.peek()[1] == ",":
                self.consume("OP", ",")
        self.consume("OP", "}")
        n = Node("mapa_lit", linea=linea)
        n.valor = pares
        return n

    def _consume_semi(self):
        if self.peek()[1] == ";":
            self.consume("OP", ";")


# ── Generador de código C ──────────────────────────────────────────────────────
C_HEADER = r"""
/* Generado por cforgec 2.4.0 — NO EDITAR MANUALMENTE */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

/* ── Runtime mínimo de C-Forge ─────────────────────────────────────────── */
typedef enum { CFV_NULL, CFV_NUM, CFV_BOOL, CFV_STR, CFV_LIST, CFV_MAP } CfvType;
typedef struct CfvValue CfvValue;
typedef struct CfvList { CfvValue** items; int len; int cap; } CfvList;

struct CfvValue {
    CfvType type;
    union {
        double num;
        bool bval;
        char* str;
        CfvList* list;
        void* map;   /* simplificado */
    };
};

static CfvValue CFV_NULL_VAL = {.type = CFV_NULL};
static CfvValue* cfv_num(double v) {
    CfvValue* r = calloc(1, sizeof(CfvValue));
    r->type = CFV_NUM; r->num = v; return r;
}
static CfvValue* cfv_bool(bool v) {
    CfvValue* r = calloc(1, sizeof(CfvValue));
    r->type = CFV_BOOL; r->bval = v; return r;
}
static CfvValue* cfv_str(const char* s) {
    CfvValue* r = calloc(1, sizeof(CfvValue));
    r->type = CFV_STR; r->str = strdup(s); return r;
}
static void cfv_mostrar(CfvValue* v) {
    if (!v || v->type == CFV_NULL) { puts("nulo"); return; }
    switch(v->type) {
        case CFV_NUM:  printf("%g\n", v->num); break;
        case CFV_BOOL: puts(v->bval ? "verdadero" : "falso"); break;
        case CFV_STR:  puts(v->str); break;
        default:       puts("[coleccion]"); break;
    }
}
static bool cfv_truthy(CfvValue* v) {
    if (!v || v->type == CFV_NULL) return false;
    if (v->type == CFV_BOOL) return v->bval;
    if (v->type == CFV_NUM) return v->num != 0.0;
    if (v->type == CFV_STR) return v->str && v->str[0] != '\0';
    return true;
}

"""


class Generador:
    def __init__(self):
        self.out = []
        self.indent = 0
        self.vars_locales: List[set] = [set()]
        self.func_names: set = set()
        self.tmp_counter = 0

    def emit(self, linea: str):
        self.out.append("    " * self.indent + linea)

    def tmp_var(self) -> str:
        self.tmp_counter += 1
        return f"_cfv_tmp{self.tmp_counter}"

    def generar(self, nodo: Node) -> str:
        self.out = [C_HEADER]
        self.emit("int main(void) {")
        self.indent += 1
        self.gen_bloque_body(nodo)
        self.emit("return 0;")
        self.indent -= 1
        self.emit("}")
        return "\n".join(self.out)

    def gen_bloque_body(self, nodo: Node):
        for hijo in nodo.hijos:
            self.gen_stmt(hijo)

    def gen_stmt(self, nodo: Node):
        t = nodo.tipo
        if t == "declaracion":
            self.gen_declaracion(nodo)
        elif t == "funcion":
            self.gen_funcion(nodo)
        elif t == "retornar":
            self.gen_retornar(nodo)
        elif t == "si":
            self.gen_si(nodo)
        elif t == "para":
            self.gen_para(nodo)
        elif t == "mientras":
            self.gen_mientras(nodo)
        elif t == "lanzar":
            e = self.gen_expr(nodo.hijos[0]) if nodo.hijos else "NULL"
            self.emit(f"fprintf(stderr, \"Error: %s\\n\", ({e})->str); exit(1);")
        elif t == "intentar":
            # C no tiene excepciones — usamos setjmp/longjmp básico
            self.emit("/* intento (simplificado en C) */")
            self.gen_bloque_stmts(nodo.hijos[0])
        elif t == "romper":
            self.emit("break;")
        elif t == "continuar":
            self.emit("continue;")
        elif t == "expr_stmt":
            expr = self.gen_expr(nodo.hijos[0])
            self.emit(f"(void)({expr});")
        elif t == "importar":
            self.emit(f'/* importar "{nodo.valor}" — enlazar stdlib .c equivalente */')
        elif t == "bloque":
            self.emit("{")
            self.indent += 1
            self.gen_bloque_stmts(nodo)
            self.indent -= 1
            self.emit("}")
        else:
            self.emit(f"/* nodo {t} no implementado */")

    def gen_bloque_stmts(self, nodo: Node):
        for hijo in nodo.hijos:
            self.gen_stmt(hijo)

    def gen_declaracion(self, nodo: Node):
        nombre = nodo.nombre
        expr = self.gen_expr(nodo.hijos[0])
        self.emit(f"CfvValue* {nombre} = {expr};")

    def gen_funcion(self, nodo: Node):
        nombre = nodo.nombre
        params, tipo_ret = nodo.valor
        params_c = ", ".join(f"CfvValue* {p[0]}" for p in params) if params else "void"
        self.func_names.add(nombre)
        self.emit(f"CfvValue* {nombre}({params_c}) {{")
        self.indent += 1
        self.gen_bloque_stmts(nodo.hijos[0])
        self.emit("return &CFV_NULL_VAL;")
        self.indent -= 1
        self.emit("}")

    def gen_retornar(self, nodo: Node):
        if nodo.hijos:
            expr = self.gen_expr(nodo.hijos[0])
            self.emit(f"return {expr};")
        else:
            self.emit("return &CFV_NULL_VAL;")

    def gen_si(self, nodo: Node):
        primera = True
        for rama in nodo.hijos:
            if rama.tipo == "rama_si":
                cond = self.gen_expr(rama.hijos[0])
                self.emit(f"if (cfv_truthy({cond})) {{")
                self.indent += 1
                self.gen_bloque_stmts(rama.hijos[1])
                self.indent -= 1
                self.emit("}")
                primera = False
            elif rama.tipo == "rama_esi":
                cond = self.gen_expr(rama.hijos[0])
                self.emit(f"else if (cfv_truthy({cond})) {{")
                self.indent += 1
                self.gen_bloque_stmts(rama.hijos[1])
                self.indent -= 1
                self.emit("}")
            elif rama.tipo == "bloque":  # sino
                self.emit("else {")
                self.indent += 1
                self.gen_bloque_stmts(rama)
                self.indent -= 1
                self.emit("}")

    def gen_para(self, nodo: Node):
        var = nodo.nombre
        iter_expr = self.gen_expr(nodo.hijos[0])
        tmp = self.tmp_var()
        # rango(n) → for loop simple
        self.emit(f"/* para {var} en ... */")
        self.emit(f"CfvValue* {tmp} = {iter_expr};")
        self.emit(f"for (int _i_{var} = 0; _i_{var} < (int)({tmp}->num); _i_{var}++) {{")
        self.indent += 1
        self.emit(f"CfvValue* {var} = cfv_num(_i_{var});")
        self.gen_bloque_stmts(nodo.hijos[1])
        self.indent -= 1
        self.emit("}")

    def gen_mientras(self, nodo: Node):
        cond = self.gen_expr(nodo.hijos[0])
        self.emit(f"while (cfv_truthy({cond})) {{")
        self.indent += 1
        self.gen_bloque_stmts(nodo.hijos[1])
        self.indent -= 1
        self.emit("}")

    def gen_expr(self, nodo: Node) -> str:
        t = nodo.tipo
        if t == "numero":
            return f"cfv_num({nodo.valor})"
        if t == "texto":
            escaped = nodo.valor.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
            return f'cfv_str("{escaped}")'
        if t == "booleano":
            return f"cfv_bool({'true' if nodo.valor else 'false'})"
        if t == "nulo":
            return "&CFV_NULL_VAL"
        if t == "ident":
            return nodo.nombre
        if t == "binop":
            l = self.gen_expr(nodo.hijos[0])
            r = self.gen_expr(nodo.hijos[1])
            op = nodo.valor
            if op == "+":
                return f"cfv_num(({l})->num + ({r})->num)"
            if op == "-":
                return f"cfv_num(({l})->num - ({r})->num)"
            if op == "*":
                return f"cfv_num(({l})->num * ({r})->num)"
            if op == "/":
                return f"cfv_num(({l})->num / ({r})->num)"
            if op == "%":
                return f"cfv_num(fmod(({l})->num, ({r})->num))"
            if op == "==":
                return f"cfv_bool(({l})->num == ({r})->num)"
            if op == "!=":
                return f"cfv_bool(({l})->num != ({r})->num)"
            if op == "<":
                return f"cfv_bool(({l})->num < ({r})->num)"
            if op == ">":
                return f"cfv_bool(({l})->num > ({r})->num)"
            if op == "<=":
                return f"cfv_bool(({l})->num <= ({r})->num)"
            if op == ">=":
                return f"cfv_bool(({l})->num >= ({r})->num)"
            if op == "&&":
                return f"cfv_bool(cfv_truthy({l}) && cfv_truthy({r}))"
            if op == "||":
                return f"cfv_bool(cfv_truthy({l}) || cfv_truthy({r}))"
            return f"cfv_num(0) /* op {op} */"
        if t == "unario":
            e = self.gen_expr(nodo.hijos[0])
            if nodo.valor == "!":
                return f"cfv_bool(!cfv_truthy({e}))"
            if nodo.valor == "-":
                return f"cfv_num(-({e})->num)"
        if t == "llamada":
            fn = self.gen_expr_fn(nodo.hijos[0])
            args = ", ".join(self.gen_expr(a) for a in nodo.hijos[1:])
            # Mapear builtins de C-Forge
            fn_mapped = self._map_builtin(fn, nodo.hijos[1:])
            if fn_mapped:
                return fn_mapped
            return f"{fn}({args})"
        if t == "indice":
            base = self.gen_expr(nodo.hijos[0])
            idx = self.gen_expr(nodo.hijos[1])
            return f"/* indice[{idx}] de {base} */"
        if t == "lista_lit":
            return f"cfv_str(\"[lista]\") /* lista literal */"
        if t == "mapa_lit":
            return f"cfv_str(\"{{mapa}}\") /* mapa literal */"
        if t == "asignar":
            nombre = nodo.hijos[0].nombre if nodo.hijos[0].tipo == "ident" else "_cfv_tmp"
            val = self.gen_expr(nodo.hijos[1])
            return f"({nombre} = {val})"
        return f"cfv_num(0) /* expr {t} */"

    def gen_expr_fn(self, nodo: Node) -> str:
        if nodo.tipo == "ident":
            return nodo.nombre
        return self.gen_expr(nodo)

    def _map_builtin(self, fn: str, args) -> Optional[str]:
        if fn == "mostrar":
            if args:
                e = self.gen_expr(args[0])
                return f"(cfv_mostrar({e}), &CFV_NULL_VAL)"
            return 'puts("")'
        if fn == "rango":
            if args:
                e = self.gen_expr(args[0])
                return e  # el for loop lo maneja gen_para
        return None


# ── CLI ────────────────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        description="cforgec — Transpilador AOT C-Forge → C nativo",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Ejemplos:
  cforgec hola.cfv                   # genera hola.c
  cforgec hola.cfv --compile         # genera hola.c y compila con gcc
  cforgec hola.cfv --run             # compila y ejecuta
  cforgec hola.cfv --ir              # muestra AST
        """
    )
    parser.add_argument("archivo", help="Archivo .cfv a compilar")
    parser.add_argument("-o", "--output", help="Archivo de salida .c")
    parser.add_argument("--compile", action="store_true", help="Compilar el .c generado con gcc")
    parser.add_argument("--run", action="store_true", help="Compilar y ejecutar")
    parser.add_argument("--ir", action="store_true", help="Mostrar AST (representación intermedia)")
    parser.add_argument("--opt", choices=["0","1","2","3"], default="2", help="Nivel de optimización gcc")
    parser.add_argument("--target", choices=["c", "wasm", "wasm-js"], default="c",
                        help="Target de compilación: c (default), wasm (WebAssembly via emcc), wasm-js (WASM + wrapper JS)")
    parser.add_argument("--version", action="version", version="cforgec 2.5.0")
    args = parser.parse_args()

    ruta = Path(args.archivo)
    if not ruta.exists():
        print(f"cforgec: archivo no encontrado: {ruta}", file=sys.stderr)
        sys.exit(1)

    codigo = ruta.read_text(encoding="utf-8")

    # Tokenizar
    try:
        tokens = tokenizar(codigo)
    except Exception as e:
        print(f"cforgec: error en tokenización: {e}", file=sys.stderr)
        sys.exit(1)

    # Parsear
    try:
        ast = Parser(tokens).parse_programa()
    except SyntaxError as e:
        print(f"cforgec: error de sintaxis: {e}", file=sys.stderr)
        sys.exit(1)

    if args.ir:
        def imprimir_ast(n, prof=0):
            tipo = n.tipo
            extra = ""
            if n.nombre: extra += f" nombre={n.nombre}"
            if n.valor is not None and not isinstance(n.valor, (list, tuple)):
                extra += f" val={repr(n.valor)}"
            print("  " * prof + f"[{tipo}]{extra}")
            for h in n.hijos:
                if isinstance(h, Node):
                    imprimir_ast(h, prof+1)
        imprimir_ast(ast)
        return

    # Generar C
    gen = Generador()
    c_code = gen.generar(ast)

    # Determinar archivo de salida
    salida = Path(args.output) if args.output else ruta.with_suffix(".c")
    salida.write_text(c_code, encoding="utf-8")
    print(f"cforgec: generado {salida}")

    if args.target in ("wasm", "wasm-js"):
        compilar_wasm(salida, ruta, args)
    elif args.compile or args.run:
        ejecutable = salida.with_suffix("")
        cmd = ["gcc", f"-O{args.opt}", "-o", str(ejecutable), str(salida), "-lm"]
        print(f"cforgec: compilando con: {' '.join(cmd)}")
        result = subprocess.run(cmd)
        if result.returncode != 0:
            print("cforgec: error de compilación", file=sys.stderr)
            sys.exit(1)
        print(f"cforgec: ejecutable: {ejecutable}")

        if args.run:
            print(f"cforgec: ejecutando {ejecutable}...")
            print("─" * 40)
            subprocess.run([str(ejecutable)])


# ── Target WASM ────────────────────────────────────────────────────────────────
WASM_JS_WRAPPER = """\
// ── C-Forge WASM wrapper ──────────────────────────────────────────────────────
// Auto-generado por cforgec --target wasm-js
// Uso: <script src="{module_name}.js"></script>
//      CForge.run()   → ejecuta el programa
//      CForge.call(fn, ...args) → llama una función exportada

const CForge = (() => {
    let _instance = null;
    let _memory   = null;

    const _imports = {
        env: {
            memory: new WebAssembly.Memory({{ initial: 16 }}),
            cfv_print_num: (n)  => console.log(n),
            cfv_print_str: (ptr, len) => {
                const buf = new Uint8Array(_memory.buffer, ptr, len);
                console.log(new TextDecoder().decode(buf));
            },
            cfv_print_bool: (b) => console.log(b ? "verdadero" : "falso"),
        }
    };

    async function init(wasmPath) {{
        const resp = await fetch(wasmPath || "{module_name}.wasm");
        const buf  = await resp.arrayBuffer();
        const res  = await WebAssembly.instantiate(buf, _imports);
        _instance  = res.instance;
        _memory    = _imports.env.memory;
    }}

    function run() {{
        if (!_instance) throw new Error("CForge WASM no inicializado. Llama CForge.init() primero.");
        return _instance.exports.main();
    }}

    function call(fn, ...args) {{
        if (!_instance) throw new Error("CForge WASM no inicializado.");
        const exported = _instance.exports[fn];
        if (!exported) throw new Error(`Función '${{fn}}' no exportada.`);
        return exported(...args);
    }}

    return {{ init, run, call }};
})();

// Auto-init si hay un atributo data-auto en el script tag
document.currentScript?.dataset?.auto !== undefined && CForge.init().then(() => CForge.run());
"""

WASM_HTML_TEMPLATE = """\
<!DOCTYPE html>
<html lang="es">
<head>
    <meta charset="UTF-8">
    <title>C-Forge WASM — {module_name}</title>
    <style>
        body {{ font-family: monospace; background: #1e1e2e; color: #cdd6f4; padding: 2rem; }}
        #output {{ background: #181825; padding: 1rem; border-radius: 8px; min-height: 200px; }}
        button {{ background: #89b4fa; color: #1e1e2e; border: none; padding: .5rem 1rem;
                  border-radius: 4px; cursor: pointer; font-family: monospace; font-size: 1rem; }}
    </style>
</head>
<body>
    <h1>C-Forge WASM — {module_name}</h1>
    <button onclick="runProgram()">▶ Ejecutar</button>
    <pre id="output">(esperando ejecución...)</pre>
    <script src="{module_name}.js"></script>
    <script>
        const out = document.getElementById("output");
        const origLog = console.log;
        console.log = (...a) => {{ out.textContent += a.join(" ") + "\\n"; origLog(...a); }};
        async function runProgram() {{
            out.textContent = "";
            await CForge.init("{module_name}.wasm");
            CForge.run();
        }}
    </script>
</body>
</html>
"""


def compilar_wasm(c_file: Path, ruta_original: Path, args):
    """Compila el C generado a WebAssembly usando emcc (Emscripten)."""
    module_name = ruta_original.stem
    wasm_out    = c_file.with_suffix(".wasm")
    js_out      = c_file.with_suffix(".js")

    # Verificar que emcc está disponible
    emcc_check = subprocess.run(["which", "emcc"], capture_output=True)
    if emcc_check.returncode != 0:
        print("cforgec: emcc (Emscripten) no encontrado.")
        print("         Instala Emscripten: https://emscripten.org/docs/getting_started/")
        print()
        print("cforgec: generando stub WASM para desarrollo sin emcc...")
        _generar_wasm_stub(c_file, module_name, args)
        return

    # Flags de emcc para WASM standalone
    exported_funcs = ["_main", "_malloc", "_free"]
    exported_str   = ",".join(exported_funcs)

    cmd = [
        "emcc",
        str(c_file),
        f"-O{args.opt}",
        "-o", str(wasm_out),
        "-s", "WASM=1",
        "-s", "STANDALONE_WASM=1",
        "-s", f"EXPORTED_FUNCTIONS={exported_str}",
        "-s", "ERROR_ON_UNDEFINED_SYMBOLS=0",
        "--no-entry",
    ]
    print(f"cforgec: compilando a WASM con: {' '.join(cmd)}")
    result = subprocess.run(cmd)
    if result.returncode != 0:
        print("cforgec: error compilando a WASM", file=sys.stderr)
        sys.exit(1)

    print(f"cforgec: WASM generado: {wasm_out}")

    if args.target == "wasm-js":
        wrapper = WASM_JS_WRAPPER.format(module_name=module_name)
        js_out.write_text(wrapper, encoding="utf-8")
        print(f"cforgec: wrapper JS generado: {js_out}")

        html_out = c_file.with_suffix(".html")
        html_out.write_text(
            WASM_HTML_TEMPLATE.format(module_name=module_name), encoding="utf-8")
        print(f"cforgec: página HTML generada: {html_out}")
        print(f"\n  Abre {html_out.name} en un servidor local:")
        print(f"  python3 -m http.server 8080")


def _generar_wasm_stub(c_file: Path, module_name: str, args):
    """
    Genera archivos WAT (WebAssembly Text) y JS de demostración
    cuando emcc no está disponible. El WAT puede compilarse con `wat2wasm`.
    """
    wat_out = c_file.with_suffix(".wat")
    wat_content = f"""\
;; C-Forge WASM stub — {module_name}
;; Generado por cforgec --target wasm (sin emcc)
;; Compila con: wat2wasm {module_name}.wat -o {module_name}.wasm

(module
  (import "env" "cfv_print_num" (func $cfv_print_num (param f64)))
  (import "env" "cfv_print_bool" (func $cfv_print_bool (param i32)))
  (memory (export "memory") 1)

  (func $main (export "main") (result i32)
    ;; Stub generado — recompila con emcc para código real
    f64.const 42
    call $cfv_print_num
    i32.const 0  ;; return 0
  )
)
"""
    wat_out.write_text(wat_content, encoding="utf-8")
    print(f"cforgec: WAT stub generado: {wat_out}")
    print(f"         Instala emcc o compila con: wat2wasm {wat_out.name}")

    if args.target == "wasm-js":
        js_out = c_file.with_suffix(".js")
        wrapper = WASM_JS_WRAPPER.format(module_name=module_name)
        js_out.write_text(wrapper, encoding="utf-8")
        print(f"cforgec: wrapper JS generado: {js_out}")


if __name__ == "__main__":
    main()
