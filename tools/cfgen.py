#!/usr/bin/env python3
"""cfgen — Scaffolding y generador de código para C-Forge"""

import sys, os, re, json
from pathlib import Path
from datetime import datetime

VERSION = "3.0.0"

TEMPLATES = {
    "proyecto": {
        "description": "Proyecto C-Forge completo",
        "files": {
            "main.cfv": """// {nombre} — Programa principal
// Creado: {fecha}

importar "io"
importar "errores"

funcion main() {{
    mostrar("Hola desde {nombre}!")
}}

main()
""",
            "cforge.json": """{
  "nombre": "{nombre}",
  "version": "1.0.0",
  "descripcion": "",
  "autor": "",
  "licencia": "MIT",
  "main": "main.cfv",
  "stdlib": "./stdlib",
  "dependencias": {{}}
}
""",
            "README.md": """# {nombre}

Proyecto creado con C-Forge v3.0

## Uso

```bash
cforgev main.cfv
```

## Estructura

- `main.cfv` — Punto de entrada
- `lib/` — Módulos del proyecto
- `test/` — Tests
""",
            "lib/.gitkeep": "",
            "test/test_main.cfv": """// Tests para {nombre}
importar "pruebas"

prueba("Hola mundo", funcion() {{
    afirmar(verdadero)
}})
""",
        }
    },

    "clase": {
        "description": "Clase C-Forge con constructor, métodos y tests",
        "single_file": """\
// {nombre}.cfv — Clase {nombre}
// Creado: {fecha}

clase {nombre} {{
    constructor({params}) {{
{ctor_body}
    }}

    funcion a_texto() {{
        retornar "{nombre}()" 
    }}

    funcion clone() {{
        retornar {nombre}({clone_args})
    }}
}}
"""
    },

    "api": {
        "description": "API REST con C-Forge",
        "files": {
            "main.cfv": """// {nombre} API — Servidor REST
// Creado: {fecha}

importar "microservicio"
importar "db"
importar "auth"
importar "validar"
importar "log"

sea config = {{
    "nombre": "{nombre}",
    "puerto": 8080,
    "db_url": "sqlite:///{nombre}.db",
    "secret": "cambiar_en_produccion"
}}

// Rutas
registro_ruta("GET", "/health", funcion(req, res) {{
    retornar respuesta_ok({{"status": "ok", "servicio": "{nombre}"}})
}})

registro_ruta("GET", "/api/items", funcion(req, res) {{
    // TODO: Listar items
    retornar respuesta_ok({{"items": []}})
}})

registro_ruta("POST", "/api/items", funcion(req, res) {{
    sea body = req["body"]
    // TODO: Crear item
    retornar respuesta_creado({{"id": 1}})
}})

registro_ruta("GET", "/api/items/:id", funcion(req, res) {{
    sea id = req["params"]["id"]
    // TODO: Obtener por ID
    retornar respuesta_ok({{"id": id}})
}})

registro_ruta("PUT", "/api/items/:id", funcion(req, res) {{
    sea id = req["params"]["id"]
    // TODO: Actualizar
    retornar respuesta_ok({{"actualizado": verdadero}})
}})

registro_ruta("DELETE", "/api/items/:id", funcion(req, res) {{
    sea id = req["params"]["id"]
    // TODO: Eliminar
    retornar respuesta_ok({{"eliminado": verdadero}})
}})

servicio_iniciar(config)
""",
            "cforge.json": """{
  "nombre": "{nombre}",
  "version": "1.0.0",
  "tipo": "api",
  "main": "main.cfv",
  "dependencias": {}
}
"""
        }
    },

    "cli": {
        "description": "Herramienta CLI de línea de comandos",
        "single_file": """\
// {nombre} — Herramienta CLI
// Creado: {fecha}

importar "io"
importar "os"
importar "errores"

// Parsear argumentos
funcion parsear_args(args) {{
    sea resultado = {{"flags": {{}}, "positional": [], "help": falso, "version": falso}}
    sea i = 0
    mientras (i < longitud(args)) {{
        sea arg = args[i]
        si (arg == "--help" o arg == "-h") {{
            resultado["help"] = verdadero
        }} sino si (arg == "--version" o arg == "-v") {{
            resultado["version"] = verdadero
        }} sino si (texto_empieza_con(arg, "--")) {{
            sea clave = subcadena(arg, 2, longitud(arg))
            si (i + 1 < longitud(args)) {{
                resultado["flags"][clave] = args[i + 1]
                i = i + 1
            }} sino {{
                resultado["flags"][clave] = verdadero
            }}
        }} sino {{
            agregar(resultado["positional"], arg)
        }}
        i = i + 1
    }}
    retornar resultado
}}

funcion mostrar_ayuda() {{
    mostrar("{nombre} — Descripción de la herramienta")
    mostrar("")
    mostrar("USO:")
    mostrar("  {nombre} [opciones] <argumento>")
    mostrar("")
    mostrar("OPCIONES:")
    mostrar("  --help, -h         Mostrar esta ayuda")
    mostrar("  --version, -v      Mostrar versión")
    mostrar("  --output <ruta>    Archivo de salida")
    mostrar("")
    mostrar("EJEMPLOS:")
    mostrar("  {nombre} entrada.txt")
    mostrar("  {nombre} --output resultado.txt entrada.txt")
}}

sea VERSION = "1.0.0"

funcion main(args) {{
    sea opts = parsear_args(args)
    
    si (opts["version"]) {{
        mostrar("{nombre} v" + VERSION)
        retornar
    }}
    
    si (opts["help"] o longitud(opts["positional"]) == 0) {{
        mostrar_ayuda()
        retornar
    }}
    
    // Lógica principal
    sea input_file = opts["positional"][0]
    mostrar("Procesando: " + input_file)
    
    // TODO: Implementar lógica
}}

main(args_programa())
"""
    },

    "videojuego": {
        "description": "Estructura de videojuego 2D",
        "files": {
            "main.cfv": """// {nombre} — Videojuego 2D
// Creado: {fecha}

importar "ecs"
importar "escena"
importar "fisica2d"
importar "input"
importar "particulas"
importar "audio"

// ── Escena principal ───────────────────────────────────────────────

escena_registrar("juego",
    funcion(datos) {{
        // Inicialización
        datos["mundo_ecs"] = ecs_mundo_crear()
        datos["mundo_fisico"] = f2d_mundo_crear(0, -300)
        datos["jugador_id"] = nulo
        input_configurar_juego_plataformas()
        
        // Crear jugador
        sea jugador = ecs_crear_entidad(datos["mundo_ecs"])
        ecs_agregar_componente(datos["mundo_ecs"], jugador, "Transform", comp_transform(100, 200))
        ecs_agregar_componente(datos["mundo_ecs"], jugador, "RigidBody", comp_rigidbody(1, 0, 0, 0, verdadero))
        datos["jugador_id"] = jugador
        
        mostrar("{nombre}: Juego iniciado")
    }},
    funcion(datos, delta) {{
        // Actualización
        input_actualizar()
        
        // Movimiento del jugador
        sea px = input_eje_x()
        sea jugador = datos["jugador_id"]
        sea rb = ecs_obtener_componente(datos["mundo_ecs"], jugador, "RigidBody")
        si (rb != nulo) {{
            f2d_aplicar_fuerza(rb, px * 500, 0)
            si (input_accion_inicio("saltar") y absoluto(rb["vel_y"]) < 1) {{
                f2d_aplicar_impulso(rb, 0, 400)
            }}
        }}
        
        ecs_actualizar(datos["mundo_ecs"], delta)
        f2d_paso(datos["mundo_fisico"], delta)
        particulas_actualizar(delta)
    }},
    funcion(datos, ctx) {{
        // Renderizado (ctx = contexto de dibujo del sistema)
        // Aquí se dibujarían los sprites, UI, etc.
    }},
    nulo
)

escena_registrar("menu",
    funcion(datos) {{
        mostrar("{nombre}: Menu cargado")
    }},
    funcion(datos, delta) {{
        si (input_recien_presionada("Space") o input_recien_presionada("Enter")) {{
            escena_cambiar("juego")
        }}
    }},
    nulo, nulo
)

// Iniciar en el menú
escena_cambiar("menu")
escena_aplicar_cambio()
mostrar("{nombre} listo. Presiona ESPACIO para jugar.")
""",
        }
    },

    "ml": {
        "description": "Proyecto de Machine Learning",
        "single_file": """\
// {nombre} — Proyecto ML
// Creado: {fecha}

importar "tensor"
importar "red_neuronal"
importar "ml"
importar "datos"
importar "nlp"

// ── Cargar datos ───────────────────────────────────────────────────

funcion cargar_datos() {{
    // TODO: Cargar tu dataset real
    // Ejemplo con datos sintéticos:
    sea X_data = []
    sea Y_data = []
    sea i = 0
    mientras (i < 100) {{
        sea x1 = aleatorio()
        sea x2 = aleatorio()
        agregar(X_data, [x1, x2])
        agregar(Y_data, [si(x1 + x2 > 1, 1, 0)])
        i = i + 1
    }}
    retornar {{"X": X_data, "Y": Y_data}}
}}

// ── Definir modelo ─────────────────────────────────────────────────

funcion crear_modelo() {{
    retornar red_crear([
        capa_densa(2, 16, "relu"),
        capa_dropout(0.2),
        capa_densa(16, 8, "relu"),
        capa_densa(8, 1, "sigmoid")
    ])
}}

// ── Entrenamiento ──────────────────────────────────────────────────

funcion main() {{
    mostrar("=== {nombre} ===")
    
    // Datos
    sea raw = cargar_datos()
    sea split = train_test_split(raw["X"], raw["Y"], 0.2)
    
    sea X_train = tensor_desde_2d(split["X_train"])
    sea Y_train = tensor_desde_2d(split["Y_train"])
    sea X_test = tensor_desde_2d(split["X_test"])
    sea Y_test = tensor_desde_2d(split["Y_test"])
    
    mostrar("Train: " + a_texto(X_train["forma"][0]) + " muestras")
    mostrar("Test:  " + a_texto(X_test["forma"][0]) + " muestras")
    
    // Modelo
    sea modelo = crear_modelo()
    sea historial = red_entrenar(modelo, X_train, Y_train, {{
        "epocas": 50,
        "batch_size": 16,
        "lr": 0.001,
        "perdida": "cross_entropy",
        "verbose": verdadero
    }})
    
    // Evaluación
    sea preds = red_predecir(modelo, X_test)
    sea acc = precision(preds, Y_test)
    mostrar("\\n✓ Precisión en test: " + a_texto(numero_decimales(acc * 100, 2)) + "%")
    
    // Guardar modelo
    sea json_modelo = red_guardar(modelo)
    escribir_archivo("{nombre}_modelo.json", json_modelo)
    mostrar("Modelo guardado: {nombre}_modelo.json")
}}

main()
"""
    }
}

def render_template(template_str: str, context: dict) -> str:
    result = template_str
    for key, value in context.items():
        result = result.replace("{" + key + "}", str(value))
    return result

def scaffold(template_name: str, nombre: str, output_dir: Path, extra: dict = None):
    if template_name not in TEMPLATES:
        print(f"\033[31mTemplate desconocido: {template_name}\033[0m")
        print(f"Templates disponibles: {', '.join(TEMPLATES.keys())}")
        sys.exit(1)

    tpl = TEMPLATES[template_name]
    context = {
        "nombre": nombre,
        "fecha": datetime.now().strftime("%Y-%m-%d"),
        "params": extra.get("params", "x, y") if extra else "x, y",
        "ctor_body": extra.get("ctor_body", "        este.x = x\n        este.y = y") if extra else "        este.x = x\n        este.y = y",
        "clone_args": extra.get("clone_args", "este.x, este.y") if extra else "este.x, este.y",
    }

    print(f"\033[34mcfgen v{VERSION} — Generando {template_name}: {nombre}\033[0m")

    if "single_file" in tpl:
        output_dir.mkdir(parents=True, exist_ok=True)
        fname = f"{nombre}.cfv"
        path = output_dir / fname
        content = render_template(tpl["single_file"], context)
        path.write_text(content, encoding="utf-8")
        print(f"  \033[32m✓ {path}\033[0m")
    elif "files" in tpl:
        for rel_path, content_tpl in tpl["files"].items():
            full_path = output_dir / nombre / rel_path
            full_path.parent.mkdir(parents=True, exist_ok=True)
            content = render_template(content_tpl, context)
            full_path.write_text(content, encoding="utf-8")
            print(f"  \033[32m✓ {full_path}\033[0m")

    print(f"\n\033[32m✓ {template_name} '{nombre}' generado correctamente\033[0m")
    if template_name == "proyecto":
        print(f"\n  Para iniciar:")
        print(f"    cd {nombre}")
        print(f"    cforgev main.cfv")

def main():
    import argparse
    p = argparse.ArgumentParser(description=f"cfgen v{VERSION} — Scaffolding para C-Forge")
    p.add_argument("template", nargs="?", help=f"Template: {', '.join(TEMPLATES.keys())}")
    p.add_argument("nombre", nargs="?", help="Nombre del proyecto/clase/módulo")
    p.add_argument("--output", "-o", default=".", help="Directorio de salida")
    p.add_argument("--list", action="store_true", help="Listar templates disponibles")
    p.add_argument("--params", help="Parámetros del constructor (para template clase)")
    p.add_argument("--version", action="version", version=f"cfgen {VERSION}")
    args = p.parse_args()

    if args.list:
        print(f"cfgen v{VERSION} — Templates disponibles:\n")
        for name, tpl in TEMPLATES.items():
            print(f"  \033[34m{name:<15}\033[0m {tpl['description']}")
        print()
        return

    if not args.template:
        p.print_help()
        return

    if not args.nombre:
        print(f"\033[31mError: se requiere un nombre\033[0m")
        print(f"  Uso: cfgen {args.template} MiNombre")
        sys.exit(1)

    extra = {}
    if args.params:
        param_names = [p.strip() for p in args.params.split(",")]
        extra["params"] = args.params
        extra["ctor_body"] = "\n".join(f"        este.{p} = {p}" for p in param_names)
        extra["clone_args"] = ", ".join(f"este.{p}" for p in param_names)

    scaffold(args.template, args.nombre, Path(args.output), extra)

if __name__ == "__main__":
    main()
