#!/usr/bin/env python3
"""
cforge — CLI unificado de C-Forge (como cargo para Rust, npm para Node)

Uso:
  cforge run archivo.cfv [args...]    Ejecutar un archivo .cfv
  cforge build                        Build del proyecto (cforge.toml)
  cforge test [patrón]                Ejecutar tests
  cforge fmt [archivos...]            Formatear código
  cforge lint [archivos...]           Analizar código estático
  cforge docs [--serve]               Generar documentación
  cforge watch CMD                    Hot reload
  cforge repl                         REPL interactivo
  cforge pkg install PKG              Instalar paquete
  cforge pkg publish                  Publicar paquete
  cforge pkg lock                     Mostrar lock file
  cforge pkg ci                       Instalar desde lock (CI/CD)
  cforge new NOMBRE [--template T]    Crear nuevo proyecto
  cforge check                        Verificar instalación
  cforge version                      Mostrar versión

Versión: 2.5.0
"""

import sys
import os
import subprocess
import argparse
import shutil
import json
from pathlib import Path

VERSION = "2.5.0"

# ── Colores ANSI ───────────────────────────────────────────────────────────────
class C:
    R  = "\033[0;31m"
    G  = "\033[0;32m"
    Y  = "\033[1;33m"
    B  = "\033[0;34m"
    M  = "\033[0;35m"
    CY = "\033[0;36m"
    W  = "\033[1;37m"
    N  = "\033[0m"

def ok(msg):   print(f"{C.G}✓{C.N} {msg}")
def err(msg):  print(f"{C.R}✗{C.N} {msg}", file=sys.stderr)
def info(msg): print(f"{C.CY}→{C.N} {msg}")
def warn(msg): print(f"{C.Y}⚠{C.N} {msg}")

# ── Localización de herramientas ───────────────────────────────────────────────
def find_tool(name: str) -> str | None:
    """Busca una herramienta en el PATH y junto al script actual."""
    # 1. PATH
    p = shutil.which(name)
    if p: return p
    # 2. Directorio del script
    here = Path(__file__).parent
    for ext in ("", ".py"):
        t = here / (name + ext)
        if t.exists(): return str(t)
    # 3. tools/ relativo al cwd
    cwd_tools = Path.cwd() / "tools" / (name + ".py")
    if cwd_tools.exists(): return str(cwd_tools)
    return None

def find_interpreter() -> str:
    """Busca cforgev en el sistema."""
    for name in ("cforgev", "cforgev.bin"):
        p = shutil.which(name)
        if p: return p
    # Junto al script
    here = Path(__file__).parent.parent
    for candidate in [here / "cforgev", here / "bin" / "cforgev"]:
        if candidate.exists(): return str(candidate)
    return "cforgev"

def run_python_tool(tool_py: str, args: list[str]) -> int:
    """Ejecuta una herramienta Python."""
    cmd = [sys.executable, tool_py] + args
    return subprocess.run(cmd).returncode

def find_stdlib_dir() -> Path | None:
    """Busca el directorio stdlib/."""
    candidates = [
        Path(__file__).parent.parent / "stdlib",
        Path("/usr/local/lib/cforge/stdlib"),
        Path.home() / ".cforge" / "stdlib",
        Path.cwd() / "stdlib",
    ]
    for c in candidates:
        if c.is_dir(): return c
    return None


# ── Comando: run ──────────────────────────────────────────────────────────────
def cmd_run(args):
    interp = find_interpreter()
    if not shutil.which(interp) and not Path(interp).exists():
        err(f"cforgev no encontrado. Ejecuta: cforge check")
        sys.exit(1)
    cmd = [interp] + args.archivo + args.rest
    sys.exit(subprocess.run(cmd).returncode)


# ── Comando: build ────────────────────────────────────────────────────────────
def cmd_build(args):
    tool = find_tool("cfbuild.py") or find_tool("cfbuild")
    if not tool:
        err("cfbuild no encontrado")
        sys.exit(1)
    extra = []
    if args.release: extra += ["--mode", "prod"]
    if args.output:  extra += ["-o", args.output]
    sys.exit(run_python_tool(tool, ["build"] + extra))


# ── Comando: test ─────────────────────────────────────────────────────────────
def cmd_test(args):
    tool = find_tool("cftest.py") or find_tool("cftest")
    if not tool:
        err("cftest no encontrado")
        sys.exit(1)
    extra = []
    if args.patron:    extra += args.patron
    if args.watch:     extra += ["--watch"]
    if args.json:      extra += ["--json"]
    if args.tap:       extra += ["--tap"]
    if args.verbose:   extra += ["-v"]
    sys.exit(run_python_tool(tool, extra))


# ── Comando: fmt ──────────────────────────────────────────────────────────────
def cmd_fmt(args):
    tool = find_tool("cfmt.py") or find_tool("cfmt")
    if not tool:
        err("cfmt no encontrado")
        sys.exit(1)
    targets = args.archivos or ["."]
    extra = []
    if args.check:  extra.append("--check")
    if args.stdout: extra.append("--stdout")
    sys.exit(run_python_tool(tool, targets + extra))


# ── Comando: lint ─────────────────────────────────────────────────────────────
def cmd_lint(args):
    tool = find_tool("cflint.py") or find_tool("cflint")
    if not tool:
        err("cflint no encontrado")
        sys.exit(1)
    targets = args.archivos or ["."]
    extra = []
    if args.strict: extra.append("--strict")
    if args.json:   extra.append("--json")
    if args.ignore: extra += ["--ignore", ",".join(args.ignore)]
    sys.exit(run_python_tool(tool, targets + extra))


# ── Comando: docs ─────────────────────────────────────────────────────────────
def cmd_docs(args):
    tool = find_tool("cfdoc.py") or find_tool("cfdoc")
    if not tool:
        err("cfdoc no encontrado")
        sys.exit(1)
    extra = []
    if args.serve: extra += ["--serve", str(args.port)]
    if args.format: extra += ["--format", args.format]
    if args.output: extra += ["-o", args.output]
    src = args.src or "."
    sys.exit(run_python_tool(tool, [src] + extra))


# ── Comando: watch ────────────────────────────────────────────────────────────
def cmd_watch(args):
    tool = find_tool("cfwatch.py") or find_tool("cfwatch")
    if not tool:
        err("cfwatch no encontrado")
        sys.exit(1)
    interp = find_interpreter()
    on_change = " ".join(args.cmd) if args.cmd else f"{interp} {args.archivo or 'main.cfv'}"
    extra = ["--on-change", on_change]
    if args.delay: extra += ["--delay", str(args.delay)]
    if args.clear: extra.append("--clear")
    src = args.dir or "."
    sys.exit(run_python_tool(tool, [src] + extra))


# ── Comando: repl ─────────────────────────────────────────────────────────────
def cmd_repl(args):
    tool = find_tool("repl.py") or find_tool("cfrepl")
    if tool:
        sys.exit(run_python_tool(tool, []))
    # Fallback: cforgev --repl
    interp = find_interpreter()
    sys.exit(subprocess.run([interp, "--repl"]).returncode)


# ── Comando: pkg ──────────────────────────────────────────────────────────────
def cmd_pkg(args):
    tool = find_tool("pkg_registry.py") or find_tool("cfpkg")
    if not tool:
        err("pkg_registry no encontrado")
        sys.exit(1)

    subcmd = args.pkg_cmd
    extra  = getattr(args, "pkg_args", [])

    if subcmd == "install":
        sys.exit(run_python_tool(tool, ["install", args.paquete]))
    elif subcmd == "publish":
        sys.exit(run_python_tool(tool, ["publish"] + ([args.dir] if args.dir else ["."]) ))
    elif subcmd == "remove":
        sys.exit(run_python_tool(tool, ["remove", args.paquete]))
    elif subcmd == "search":
        sys.exit(run_python_tool(tool, ["search", args.query]))
    elif subcmd == "info":
        sys.exit(run_python_tool(tool, ["info", args.nombre]))
    elif subcmd == "list":
        sys.exit(run_python_tool(tool, ["list"]))
    elif subcmd == "lock":
        sys.exit(run_python_tool(tool, ["lock"]))
    elif subcmd == "ci":
        sys.exit(run_python_tool(tool, ["ci"]))
    elif subcmd == "verify":
        sys.exit(run_python_tool(tool, ["verify"]))
    elif subcmd == "serve":
        port = getattr(args, "port", 7373)
        sys.exit(run_python_tool(tool, ["serve", "--port", str(port)]))
    else:
        err(f"Subcomando pkg desconocido: {subcmd}")
        sys.exit(1)


# ── Comando: new ──────────────────────────────────────────────────────────────
TEMPLATES = {
    "cli": """\
// {name} — CLI C-Forge
importar "stdlib/cli"
importar "stdlib/log"

sea app = cli_crear("{name}", "1.0.0")
cli_descripcion(app, "Mi aplicación CLI")

cli_opcion(app, "--verbose", "-v", "Modo verbose", falso)
cli_argumento(app, "archivo", "Archivo a procesar", "")

funcion main(args: mapa) {{
    si (args["verbose"]) {{
        log_nivel("DEBUG")
    }}
    sea archivo = args["archivo"]
    si (archivo == "") {{
        mostrar("Uso: {name} [opciones] <archivo>")
        retornar
    }}
    mostrar("Procesando: " + archivo)
}}

cli_ejecutar(app, main)
""",
    "web": """\
// {name} — Servidor web C-Forge
importar "stdlib/framework"

sea app = crear_app()

ruta(app, "GET", "/", funcion(req, res) {{
    json_respuesta(res, {{"mensaje": "Hola desde {name}!", "version": "1.0.0"}})
}})

ruta(app, "GET", "/salud", funcion(req, res) {{
    json_respuesta(res, {{"estado": "ok"}})
}})

iniciar(app, 3000)
mostrar("Servidor en http://localhost:3000")
""",
    "lib": """\
// {name} — Librería C-Forge
// Exporta funciones para uso con importar

/// @doc Función principal de {name}
/// @param valor El valor a procesar
/// @retorna El resultado procesado
funcion procesar(valor) {{
    retornar valor
}}

/// @doc Versión de la librería
sea VERSION = "1.0.0"
""",
    "api": """\
// {name} — API REST C-Forge
importar "stdlib/framework"
importar "stdlib/orm"
importar "stdlib/esquema"
importar "stdlib/errores"

sea app = crear_app()
sea db  = db_conectar("sqlite", {{"archivo": "{name}.db"}})

// Modelo
sea Modelo = orm_modelo(db, "{name}", {{
    "id":     {{"tipo": "entero", "clave_primaria": verdadero}},
    "nombre": {{"tipo": "texto",  "requerido": verdadero}},
    "creado": {{"tipo": "fecha",  "auto": verdadero}}
}})

// Rutas
ruta(app, "GET",    "/api/items",     funcion(req, res) {{
    json_respuesta(res, orm_todos(Modelo))
}})
ruta(app, "POST",   "/api/items",     funcion(req, res) {{
    sea item = orm_crear(Modelo, req["body"])
    json_respuesta(res, item, 201)
}})
ruta(app, "GET",    "/api/items/:id", funcion(req, res) {{
    json_respuesta(res, orm_por_id(Modelo, req["params"]["id"]))
}})
ruta(app, "DELETE", "/api/items/:id", funcion(req, res) {{
    orm_eliminar(Modelo, req["params"]["id"])
    json_respuesta(res, {{"ok": verdadero}})
}})

iniciar(app, 8080)
mostrar("{name} API en http://localhost:8080")
""",
}

CFORGE_TOML_TEMPLATE = """\
[paquete]
nombre      = "{name}"
version     = "0.1.0"
descripcion = ""
autor       = ""
licencia    = "MIT"
entrada     = "src/main.cfv"

[dependencias]
# nombre_paquete = "^1.0.0"

[build]
modo   = "dev"
salida = "build/"

[test]
directorio = "tests/"
"""

GITIGNORE_TEMPLATE = """\
# C-Forge
build/
cforge_modules/
*.cfpkg
cforge.lock

# OS
.DS_Store
Thumbs.db

# Editors
.vscode/settings.json
.idea/

# Python
__pycache__/
*.pyc
"""

def cmd_new(args):
    nombre = args.nombre
    template = args.template or "cli"
    directorio = Path(nombre)

    if directorio.exists():
        err(f"El directorio '{nombre}' ya existe")
        sys.exit(1)

    if template not in TEMPLATES:
        err(f"Template desconocido: {template}. Opciones: {', '.join(TEMPLATES)}")
        sys.exit(1)

    # Crear estructura
    (directorio / "src").mkdir(parents=True)
    (directorio / "tests").mkdir()
    (directorio / "stdlib").mkdir()

    # main.cfv
    main_code = TEMPLATES[template].format(name=nombre)
    (directorio / "src" / "main.cfv").write_text(main_code, encoding="utf-8")

    # Test básico
    test_code = f"""\
// Tests para {nombre}
importar "stdlib/pruebas"

prueba("ejemplo básico", funcion() {{
    afirmar_verdadero(verdadero)
}})
"""
    (directorio / "tests" / "test_basico.cfv").write_text(test_code, encoding="utf-8")

    # cforge.toml
    (directorio / "cforge.toml").write_text(
        CFORGE_TOML_TEMPLATE.format(name=nombre), encoding="utf-8")

    # .gitignore
    (directorio / ".gitignore").write_text(GITIGNORE_TEMPLATE, encoding="utf-8")

    print(f"\n{C.G}✓ Proyecto '{nombre}' creado{C.N}\n")
    print(f"  {C.W}Estructura:{C.N}")
    print(f"  {nombre}/")
    print(f"  ├── src/main.cfv      # Código fuente")
    print(f"  ├── tests/            # Tests")
    print(f"  ├── stdlib/           # Stdlib local")
    print(f"  └── cforge.toml       # Configuración")
    print(f"\n{C.CY}Próximos pasos:{C.N}")
    print(f"  cd {nombre}")
    print(f"  cforge run src/main.cfv")
    print(f"  cforge test")


# ── Comando: check ────────────────────────────────────────────────────────────
def cmd_check(args):
    print(f"\n{C.W}C-Forge {VERSION} — Diagnóstico{C.N}\n")
    total_ok = 0
    total_err = 0

    checks = [
        ("cforgev (intérprete)", find_interpreter(), True),
        ("cfmt (formatter)",     find_tool("cfmt.py"),         False),
        ("cflint (linter)",      find_tool("cflint.py"),        False),
        ("cftest (tests)",       find_tool("cftest.py"),        False),
        ("cfbuild (build)",      find_tool("cfbuild.py"),       False),
        ("cfdoc (docs)",         find_tool("cfdoc.py"),         False),
        ("cfwatch (hot reload)", find_tool("cfwatch.py"),       False),
        ("repl (REPL)",          find_tool("repl.py"),          False),
        ("cforgec (transpiler)", find_tool("cforgec.py"),       False),
        ("pkg_registry (pkg)",   find_tool("pkg_registry.py"),  False),
        ("lsp_server (LSP)",     find_tool("lsp_server.py"),    False),
        ("dap_server (DAP)",     find_tool("dap_server.py"),    False),
    ]

    for label, path, required in checks:
        if path and (Path(path).exists() or shutil.which(path)):
            print(f"  {C.G}✓{C.N} {label:<30} {C.CY}{path}{C.N}")
            total_ok += 1
        else:
            marker = C.R + "✗" if required else C.Y + "!"
            print(f"  {marker}{C.N} {label:<30} {C.R}no encontrado{C.N}")
            if required: total_err += 1

    # stdlib
    stdlib = find_stdlib_dir()
    if stdlib:
        mods = len(list(stdlib.glob("*.cfv")))
        print(f"  {C.G}✓{C.N} {'stdlib/ (' + str(mods) + ' módulos)':<30} {C.CY}{stdlib}{C.N}")
        total_ok += 1
    else:
        print(f"  {C.Y}!{C.N} {'stdlib/':<30} {C.Y}no encontrada{C.N}")

    print(f"\n  {C.G}{total_ok}{C.N} OK  {C.R}{total_err}{C.N} requeridos faltantes\n")
    if total_err > 0:
        sys.exit(1)


# ── Comando: version ──────────────────────────────────────────────────────────
def cmd_version(args):
    print(f"cforge {VERSION}")
    interp = find_interpreter()
    if shutil.which(interp) or Path(interp).exists():
        subprocess.run([interp, "--version"], capture_output=True)


# ── CLI principal ──────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(
        prog="cforge",
        description=f"C-Forge {VERSION} — El gestor de proyectos del lenguaje C-Forge",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Comandos:
  run    archivo.cfv        Ejecutar un archivo
  build                     Build del proyecto
  test   [patrón]           Ejecutar tests
  fmt    [archivos]         Formatear código
  lint   [archivos]         Analizar código
  docs   [--serve]          Generar documentación
  watch  CMD                Hot reload
  repl                      REPL interactivo
  pkg    install PKG        Gestión de paquetes
  new    NOMBRE             Crear nuevo proyecto
  check                     Verificar instalación
  version                   Mostrar versión

Plantillas (cforge new NOMBRE --template T):
  cli    Aplicación de línea de comandos (default)
  web    Servidor web
  api    API REST con ORM
  lib    Librería exportable
        """
    )
    parser.add_argument("--version", action="version", version=f"cforge {VERSION}")

    sub = parser.add_subparsers(dest="cmd", metavar="COMANDO")

    # run
    p_run = sub.add_parser("run", help="Ejecutar archivo .cfv")
    p_run.add_argument("archivo", nargs="+", help="Archivo(s) .cfv a ejecutar")
    p_run.add_argument("rest", nargs=argparse.REMAINDER)

    # build
    p_build = sub.add_parser("build", help="Build del proyecto")
    p_build.add_argument("--release", action="store_true", help="Build en modo producción")
    p_build.add_argument("-o", "--output", help="Directorio de salida")

    # test
    p_test = sub.add_parser("test", help="Ejecutar tests")
    p_test.add_argument("patron", nargs="*", help="Archivos/patrones de test")
    p_test.add_argument("--watch",   action="store_true", help="Modo watch")
    p_test.add_argument("--json",    action="store_true", help="Salida JSON")
    p_test.add_argument("--tap",     action="store_true", help="Salida TAP")
    p_test.add_argument("-v","--verbose", action="store_true")

    # fmt
    p_fmt = sub.add_parser("fmt", help="Formatear código")
    p_fmt.add_argument("archivos", nargs="*")
    p_fmt.add_argument("--check",  action="store_true", help="Solo verificar, no modificar")
    p_fmt.add_argument("--stdout", action="store_true", help="Imprimir a stdout")

    # lint
    p_lint = sub.add_parser("lint", help="Analizar código estático")
    p_lint.add_argument("archivos", nargs="*")
    p_lint.add_argument("--strict", action="store_true")
    p_lint.add_argument("--json",   action="store_true")
    p_lint.add_argument("--ignore", nargs="+", metavar="REGLA")

    # docs
    p_docs = sub.add_parser("docs", help="Generar documentación")
    p_docs.add_argument("src", nargs="?", default=".")
    p_docs.add_argument("--serve",  action="store_true", help="Servir localmente")
    p_docs.add_argument("--port",   type=int, default=8080)
    p_docs.add_argument("--format", choices=["html","markdown","json"], default="html")
    p_docs.add_argument("-o","--output")

    # watch
    p_watch = sub.add_parser("watch", help="Hot reload")
    p_watch.add_argument("archivo", nargs="?")
    p_watch.add_argument("--cmd",   nargs="+")
    p_watch.add_argument("--dir",   default=".")
    p_watch.add_argument("--delay", type=int, default=500)
    p_watch.add_argument("--clear", action="store_true")

    # repl
    sub.add_parser("repl", help="REPL interactivo")

    # pkg
    p_pkg = sub.add_parser("pkg", help="Gestión de paquetes")
    pkg_sub = p_pkg.add_subparsers(dest="pkg_cmd", metavar="SUBCMD")

    p_pi = pkg_sub.add_parser("install",  help="Instalar paquete")
    p_pi.add_argument("paquete")

    p_pp = pkg_sub.add_parser("publish", help="Publicar paquete")
    p_pp.add_argument("dir", nargs="?", default=".")

    p_pr = pkg_sub.add_parser("remove",  help="Desinstalar paquete")
    p_pr.add_argument("paquete")

    p_ps = pkg_sub.add_parser("search",  help="Buscar paquetes")
    p_ps.add_argument("query")

    p_pf = pkg_sub.add_parser("info",    help="Info de paquete")
    p_pf.add_argument("nombre")

    pkg_sub.add_parser("list",   help="Listar paquetes")
    pkg_sub.add_parser("lock",   help="Ver lock file")
    pkg_sub.add_parser("ci",     help="Instalar desde lock (CI)")
    pkg_sub.add_parser("verify", help="Verificar integridad")
    p_srv = pkg_sub.add_parser("serve",  help="Iniciar servidor registry")
    p_srv.add_argument("--port", type=int, default=7373)

    # new
    p_new = sub.add_parser("new", help="Crear nuevo proyecto")
    p_new.add_argument("nombre", help="Nombre del proyecto")
    p_new.add_argument("--template", "-t", default="cli",
                       choices=list(TEMPLATES.keys()),
                       help="Plantilla de proyecto")

    # check
    sub.add_parser("check", help="Verificar instalación")

    # version
    sub.add_parser("version", help="Mostrar versión")

    # Parse
    args = parser.parse_args()

    dispatch = {
        "run":     cmd_run,
        "build":   cmd_build,
        "test":    cmd_test,
        "fmt":     cmd_fmt,
        "lint":    cmd_lint,
        "docs":    cmd_docs,
        "watch":   cmd_watch,
        "repl":    cmd_repl,
        "pkg":     cmd_pkg,
        "new":     cmd_new,
        "check":   cmd_check,
        "version": cmd_version,
    }

    fn = dispatch.get(args.cmd)
    if fn:
        fn(args)
    else:
        # Si no hay subcomando, imprimir ayuda con banner
        print(f"""
{C.B}   ___       ___                  {C.N}
{C.B}  / __| ___ | __| ___  _ _  __ _ {C.N}  {C.W}C-Forge {VERSION}{C.N}
{C.B} | (__ |___|| _| / _ \\| '_|/ _` |{C.N}  El lenguaje de programación completo
{C.B}  \\___| {C.N}    {C.B}|_|  \\___/|_|  \\__, |{C.N}
{C.B}                               |___/ {C.N}

{C.W}Uso:{C.N}  cforge <COMANDO> [opciones]

{C.W}Comandos:{C.N}
  {C.G}run{C.N}      archivo.cfv     Ejecutar un archivo
  {C.G}build{C.N}                    Build del proyecto (cforge.toml)
  {C.G}test{C.N}     [patrón]        Ejecutar tests
  {C.G}fmt{C.N}      [archivos]      Formatear código
  {C.G}lint{C.N}     [archivos]      Analizar código
  {C.G}docs{C.N}     [--serve]       Generar documentación
  {C.G}watch{C.N}    CMD             Hot reload
  {C.G}repl{C.N}                     REPL interactivo
  {C.G}pkg{C.N}      <subcmd>        Gestión de paquetes
  {C.G}new{C.N}      NOMBRE          Crear nuevo proyecto
  {C.G}check{C.N}                    Verificar instalación

  cforge help <COMANDO> para más info
""")


if __name__ == "__main__":
    main()
