"""Backend nativo experimental de C-Forge: .cfv -> C++ -> ejecutable."""

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
#include "cforge_shared_arena.h"
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
static std::string cfv_json_escape(const std::string&input){std::string out="\"";for(unsigned char c:input){switch(c){case '\"':out+="\\\"";break;case '\\':out+="\\\\";break;case '\n':out+="\\n";break;case '\r':out+="\\r";break;case '\t':out+="\\t";break;default:if(c<32){char b[7];std::snprintf(b,sizeof(b),"\\u%04x",c);out+=b;}else out+=(char)c;}}return out+"\"";}
static std::string cfv_canonical_json(const Value&v){if(v.index()==0)return "null";if(auto p=std::get_if<double>(&v.data))return cfv_number_text(*p);if(auto p=std::get_if<std::string>(&v.data))return cfv_json_escape(*p);if(auto p=std::get_if<bool>(&v.data))return *p?"true":"false";if(auto p=std::get_if<Lista>(&v.data)){std::string s="[";for(size_t i=0;i<(*p)->size();++i){if(i)s+=",";s+=cfv_canonical_json((*p)->at(i));}return s+"]";}if(auto p=std::get_if<Mapa>(&v.data)){std::string s="{";bool first=true;for(const auto&[k,x]:**p){if(!first)s+=",";first=false;s+=cfv_json_escape(k)+":"+cfv_canonical_json(x);}return s+"}";}return cfv_json_escape(texto(v));}
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
