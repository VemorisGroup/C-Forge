# C-Forge

[![CI multiplataforma](https://github.com/VemorisGroup/C-Forge/actions/workflows/ci.yml/badge.svg)](https://github.com/VemorisGroup/C-Forge/actions/workflows/ci.yml)
[![Seguridad](https://github.com/VemorisGroup/C-Forge/actions/workflows/security.yml/badge.svg)](https://github.com/VemorisGroup/C-Forge/actions/workflows/security.yml)

<p align="center">
  <img src="assets/cforgev-logo.svg" width="128" height="128" alt="Logo de C-Forge">
</p>

> Lenguaje de programacion de Vemoris Group con sintaxis propia, tipado gradual,
> interprete nativo en C++ y biblioteca estandar completa.
> Diseñado para todo — desde scripts personales hasta bancos, estudios de videojuegos y empresas gigantes.

**Version actual:** `2.2.0`
**Extension oficial:** `.cfv`
**Estado:** produccion — JSON nativo, SQLite, HTTP client/server, Regex, Canales, Android/iOS, SDL2/OpenGL, criptografia AES-256.

---

## Inicio rapido

### Instalacion automatica (macOS y Linux)

```bash
curl -fsSL https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/install.sh | bash
```

### Compilar manualmente

```bash
git clone https://github.com/VemorisGroup/C-Forge.git
cd C-Forge

# Basico (sin dependencias extra)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp

# Con OpenSSL + SQLite (recomendado para produccion)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_OPENSSL -DCFV_WITH_SQLITE \
    -lsqlite3 -lcrypto -lpthread
```

### Ejecutar un programa

```bash
./cforgev mi_programa.cfv
```

### REPL interactivo

```bash
./cforgev
```

---

## El lenguaje

```cfv
// Variables con tipado gradual
sea nombre: texto = "Javier"
sea edad: numero = 20
sea activo: booleano = verdadero

// Clases con herencia
clase Animal {
    campo nombre: texto
    funcion hablar(): texto {
        retornar "..."
    }
}

clase Perro extiende Animal {
    funcion hablar(): texto {
        retornar "Guau! Soy {este.nombre}"
    }
}

sea p = Perro()
p.nombre = "Rex"
mostrar(p.hablar())   // Guau! Soy Rex
mostrar(p es Animal)  // verdadero

// Funciones con defaults y variadicos
funcion saludar(nombre: texto, saludo: texto = "Hola"): texto {
    retornar "{saludo}, {nombre}!"
}

funcion sumar(...nums): numero {
    sea total: numero = 0
    para n en nums { total += n }
    retornar total
}

mostrar(saludar("Ana"))          // Hola, Ana!
mostrar(sumar(1, 2, 3, 4, 5))   // 15

// Operador ternario
sea msg: texto = edad >= 18 ? "adulto" : "menor"

// Desestructuracion
sea [a, b, c] = [10, 20, 30]
sea {nombre, edad} = {nombre: "Luis", edad: 25}

// Spread
sea lista1 = [1, 2, 3]
sea lista2 = [...lista1, 4, 5, 6]

// Closures / lambdas
sea doble = funcion(n: numero): numero { retornar n * 2 }
mostrar(doble(7))   // 14

// Operadores de seguridad
sea config: cualquiera = nulo
sea timeout: numero = config?.timeout ?? 5000

// Manejo de errores
intentar {
    lanzar("algo salio mal")
} capturar (e) {
    mostrar("Error: {e}")
} finalmente {
    mostrar("siempre se ejecuta")
}

// switch/match
sea dia: numero = 1
segun (dia) {
    caso 1: mostrar("Lunes")
    caso 2: mostrar("Martes")
    otro:   mostrar("Otro dia")
}
```

---

## Caracteristicas del lenguaje

| Caracteristica | Estado |
|---|---|
| Variables y tipado gradual | ✅ |
| Funciones y closures | ✅ |
| Clases con `este` | ✅ |
| Herencia (`extiende` / `super`) | ✅ |
| Modificadores `privado` / `publico` / `estatico` | ✅ |
| Parametros con valor por defecto | ✅ |
| Variadicos (`...args`) | ✅ |
| Desestructuracion de lista y mapa | ✅ |
| Spread (`[...lista, x]`) | ✅ |
| Operador ternario (`cond ? a : b`) | ✅ |
| `es` (instanceof con cadena de herencia) | ✅ |
| `en` (membresia en lista, mapa, texto) | ✅ |
| Asignacion compuesta (`+=`, `-=`, `*=`, `/=`, `%=`) | ✅ |
| `intentar` / `capturar` / `finalmente` / `lanzar` | ✅ |
| `segun` / `caso` / `otro` (switch/match) | ✅ |
| `??` (null coalescing) | ✅ |
| `?.` (safe navigation) | ✅ |
| Interpolacion de strings `"{variable}"` | ✅ |
| `para` con rango lazy | ✅ |
| `enum` | ✅ |
| REPL interactivo | ✅ |
| Stack traces con contexto | ✅ |
| `importar` modulos | ✅ |
| Servidor HTTP nativo | ✅ |
| Cliente HTTP (GET/POST/PUT/DELETE) | ✅ |
| JSON nativo (sin dependencias) | ✅ |
| SQLite integrado | ✅ |
| Regex nativo (`std::regex`) | ✅ |
| Canales (concurrencia tipo Go) | ✅ |
| SHA-256 + AES-256-CBC (OpenSSL) | ✅ |
| JWT HS256 | ✅ |
| Fecha y hora nativa | ✅ |
| Colecciones avanzadas | ✅ |
| Android (JNI + NDK) | ✅ |
| iOS (Swift + ObjC++) | ✅ |
| SDL2 (juegos 2D) | ✅ |
| OpenGL (juegos 3D) | ✅ |

---

## v2.2 — Nuevas funciones nativas

### JSON nativo (sin dependencias)

```cfv
// Serializar cualquier valor
sea j = json_texto({"nombre": "Ana", "edad": 25})   // {"nombre":"Ana","edad":25}
sea jb = json_bonito({"a": 1, "b": [1, 2, 3]})     // indentado 2 espacios

// Parsear desde archivo (recomendado — evita conflicto con { en strings)
sea config = json_parsear(leer_archivo("config.json"))
mostrar(config["puerto"])

// Tipos soportados
json_texto(nulo)     // "null"
json_texto(verdadero) // "true"
json_texto(42)       // "42"
json_texto([1,2,3])  // "[1,2,3]"
```

### SQLite nativo

```cfv
// Compilar con: -DCFV_WITH_SQLITE -lsqlite3
sea db = db_abrir("mi_app.db")   // ":memory:" para RAM

db_ejecutar(db, "CREATE TABLE IF NOT EXISTS usuarios (id INTEGER PRIMARY KEY, nombre TEXT, email TEXT)")

db_consulta_p(db, "INSERT INTO usuarios (nombre, email) VALUES (?, ?)", ["Ana", "ana@ej.com"])
sea id = db_ultimo_id(db)

sea usuarios = db_consulta(db, "SELECT * FROM usuarios")
mostrar(usuarios[0]["nombre"])   // Ana

// Transacciones
db_transaccion(db)
intentar {
    db_consulta_p(db, "UPDATE cuentas SET saldo = saldo - ? WHERE id = ?", [500, 1])
    db_consulta_p(db, "UPDATE cuentas SET saldo = saldo + ? WHERE id = ?", [500, 2])
    db_confirmar(db)
} capturar (e) {
    db_revertir(db)
}

db_cerrar(db)
```

### Cliente HTTP completo

```cfv
// GET
sea resp = http_get("https://api.github.com/users/octocat")
sea datos = json_parsear(resp)

// POST con JSON
sea respuesta = http_post("https://api.ejemplo.com/usuarios",
    json_texto({"nombre": "Ana"}),
    "application/json")

// PUT, DELETE
http_put("https://api.ej.com/item/1", json_texto({"nombre": "Ana v2"}))
http_delete("https://api.ej.com/item/1")

// Solicitud completa con cabeceras
sea res = http_solicitud("GET", "https://api.privada.com/datos", {
    "cabeceras": {"Authorization": "Bearer mi_token"},
    "timeout":   10000
})
```

### Regex nativo

```cfv
// Coincidir (verdadero/falso)
mostrar(regex_coincidir("usuario@email.com", "^[\\w.+]+@[\\w]+\\.[a-z]{2,}$"))  // verdadero

// Buscar todos los matches
sea nums = regex_buscar("precio: 42 pesos, 100 extras", "[0-9]+")  // ["42", "100"]

// Primer match
sea primero = regex_buscar_primero("texto 123", "[0-9]+")  // "123"

// Reemplazar
sea limpio = regex_reemplazar("Hola   Mundo", "\\s+", " ")  // "Hola Mundo"

// Grupos de captura
sea grupos = regex_grupos("2026-07-28", "(\\d{4})-(\\d{2})-(\\d{2})")
// ["2026", "07", "28"]
```

### Canales (concurrencia tipo Go)

```cfv
importar "stdlib/concurrencia.cfv"

// Canal con buffer
sea c = canal_nuevo(10)

// Productor
canal_enviar(c, "mensaje 1")
canal_enviar(c, "mensaje 2")

// Consumidor
sea msg = canal_recibir(c)  // "mensaje 1" (bloquea si vacio)
mostrar(canal_tam(c))       // 1

// Dormir (en hilos)
hilo_dormir(100)   // 100 ms

canal_cerrar(c)
```

### Sistema y Procesos

```cfv
// Variables de entorno
sea path = env_obtener("PATH")
env_establecer("MI_VAR", "valor")

// Ejecutar procesos externos
sea salida = proceso_ejecutar("ls -la")
mostrar(salida)

// Tiempo
mostrar(tiempo_ms())       // timestamp en milisegundos
mostrar(tiempo_segundos()) // timestamp en segundos

// Control
pausa(500)         // esperar 500ms
limpiar_pantalla() // limpiar terminal
salir(0)           // salir con codigo
```

### Fecha y hora

```cfv
sea f = fecha_ahora()
mostrar(f["anio"])   // 2026
mostrar(f["mes"])    // 7
mostrar(f["dia"])    // 28
mostrar(f["hora"])   // 14
mostrar(f["iso"])    // "2026-07-28 14:30:00"

// Formatear
mostrar(fecha_formatear(f, "%d/%m/%Y"))  // "28/07/2026"
mostrar(fecha_formatear(f, "%H:%M:%S"))  // "14:30:00"
```

### Colecciones avanzadas

```cfv
// Lista unica (eliminar duplicados)
sea u = lista_unica([1, 2, 2, 3, 3, 3])   // [1, 2, 3]

// Aplanar lista anidada
sea a = lista_aplanar([[1, 2], [3, 4], [5]])  // [1, 2, 3, 4, 5]

// Zip de listas
sea z = lista_zip([1, 2, 3], ["a", "b", "c"])
// [[1,"a"], [2,"b"], [3,"c"]]

// Operaciones de mapa
sea claves = mapa_claves({"a": 1, "b": 2})    // ["a", "b"]
sea vals = mapa_valores({"a": 1, "b": 2})      // [1, 2]
sea entradas = mapa_entradas({"a": 1})         // [["a", 1]]
sea fusion = mapa_fusionar({"a": 1}, {"b": 2}) // {"a":1, "b":2}
```

### Texto avanzado

```cfv
// Relleno izquierda
mostrar(texto_relleno("42", 6, "0"))    // "000042"
mostrar(texto_relleno("hi", 5))         // "   hi"  (espacio por defecto)

// Relleno derecha
mostrar(texto_relleno_der("hi", 5, "-")) // "hi---"

// Formato printf-like
mostrar(texto_formato("Hola {0}, tienes {1} años", "Ana", 25))
// "Hola Ana, tienes 25 años"
```

---

## Base de datos (SQLite)

```cfv
importar "stdlib/db.cfv"

// Conectar y migrar automaticamente
sea db = db_conectar("app.db")
db_migrar(db, "usuarios", {
    "nombre": "TEXT NOT NULL",
    "email":  "TEXT UNIQUE NOT NULL",
    "activo": "INTEGER DEFAULT 1"
})

// CRUD completo
sea id = db_insertar(db, "usuarios", {"nombre": "Ana", "email": "ana@ej.com"})
sea todos = db_todos(db, "usuarios")
sea ana = db_por_id(db, "usuarios", id)
sea activos = db_donde(db, "usuarios", "activo = 1")
db_actualizar(db, "usuarios", id, {"nombre": "Ana M."})
sea total = db_contar(db, "usuarios")

// Paginacion
sea pagina = db_paginar(db, "usuarios", 1, 10)  // pagina 1, 10 por pagina

// Transacciones
db_en_transaccion(db, funcion(): nulo {
    db_insertar(db, "cuentas", {"saldo": 1000})
    db_insertar(db, "movimientos", {"tipo": "apertura", "monto": 1000})
})
```

---

## HTTP Server y API REST

```cfv
importar "stdlib/web.cfv"
importar "stdlib/log.cfv"
importar "stdlib/validar.cfv"

sea srv = web_escuchar(8080)
log_info("API en http://localhost:8080")

mientras (verdadero) {
    sea req = web_solicitud(srv)
    sea ruta = req["ruta"]
    sea met = req["metodo"]

    si (met == "GET" y ruta == "/") {
        web_responder(srv, 200, json_texto({"api": "C-Forge", "v": "2.2"}), "application/json")
    } sino si (met == "POST" y ruta == "/usuarios") {
        sea datos = json_parsear(req["cuerpo"])
        sea v = validar_esquema(datos, {
            "nombre": {"requerido": verdadero, "tipo": "texto"},
            "email":  {"requerido": verdadero, "email": verdadero}
        })
        si (v["valido"]) {
            web_responder(srv, 201, json_texto({"ok": verdadero}), "application/json")
        } sino {
            web_responder(srv, 400, json_texto({"error": v["errores"][0]}), "application/json")
        }
    }
}
```

---

## Logging y validacion

```cfv
importar "stdlib/log.cfv"
importar "stdlib/validar.cfv"

// Configurar log
log_configurar({"nivel": "debug", "color": verdadero, "timestamp": verdadero})

log_debug("iniciando...")
log_info("servidor arriba")
log_advertencia("memoria baja")
log_error("conexion fallida")

// Medir tiempo de ejecucion
sea resultado = log_tiempo("operacion pesada", funcion(): nulo {
    pausa(100)
})

// Validar datos
mostrar(es_email("ana@ej.com"))      // verdadero
mostrar(es_url("https://ej.com"))    // verdadero
mostrar(es_telefono("+1-555-0100"))  // verdadero

sea v = validar_esquema({"nombre": "A"}, {
    "nombre": {"requerido": verdadero, "min": 3, "max": 50}
})
mostrar(v["valido"])     // falso
mostrar(v["errores"])    // ["nombre: minimo 3 caracteres"]
```

---

## Pruebas unitarias

```cfv
importar "stdlib/pruebas.cfv"

suite("Matematicas", [
    prueba("suma", funcion(): nulo {
        esperar_igual(2 + 2, 4)
    }),
    prueba("division exacta", funcion(): nulo {
        esperar_igual(10 / 4, 2.5)
    }),
    prueba("lanza error", funcion(): nulo {
        esperar_lanza(funcion(): nulo {
            lanzar "division por cero"
        })
    })
])

sea r = ejecutar_pruebas()
// [PASS] Matematicas: suma
// [PASS] Matematicas: division exacta
// [PASS] Matematicas: lanza error
// Resultado: 3/3 pasadas
```

---

## Juegos 2D con SDL2

```cfv
importar "stdlib/sdl.cfv"

// Compilar: g++ cforgev.cpp -DCFV_WITH_SDL2 -lSDL2 -o cforgev

sea ventana = juego_iniciar("Mi Juego", 800, 600)
sea corriendo = verdadero

mientras (juego_corriendo(ventana) y corriendo) {
    sea eventos = juego_eventos(ventana)
    para ev en eventos {
        si (ev["tipo"] == "quit") { corriendo = falso }
        si (ev["tipo"] == "keydown" y ev["tecla"] == "ESCAPE") {
            corriendo = falso
        }
    }

    juego_limpiar(ventana, 20, 20, 40)

    // Dibujar rectangulo verde
    sdl_dibujar_rect(ventana, 100, 100, 200, 150, 0, 200, 100, 255)

    juego_mostrar(ventana)
    sdl_delay(16)   // ~60 FPS
}

juego_terminar(ventana)
```

Ver ejemplo completo: [`ejemplos/juego_2d.cfv`](ejemplos/juego_2d.cfv) (Snake con colisiones y puntaje)

---

## Juegos 3D con OpenGL

```cfv
importar "stdlib/sdl.cfv"
importar "stdlib/gl.cfv"

// Compilar: g++ cforgev.cpp -DCFV_WITH_SDL2 -DCFV_WITH_OPENGL -lSDL2 -lGL -o cforgev

sea ventana = juego_iniciar("3D C-Forge", 800, 600)
sea gl = gl_iniciar(ventana)
sea prog = gl_programa_basico()
sea cubo = gl_malla_cubo(1.0, 0.3, 0.1)
sea angulo = 0.0

mientras (juego_corriendo(ventana)) {
    angulo = angulo + 0.02
    gl_limpiar(0.05, 0.05, 0.1)
    sea mvp = mat4_rotar_y(mat4_trasladar(mat4_identidad(), 0, 0, -4.0), angulo)
    gl_dibujar_malla(prog, cubo, mvp)
    gl_mostrar(ventana)
    sdl_delay(16)
}

gl_cerrar(gl)
juego_terminar(ventana)
```

Ver ejemplo completo: [`ejemplos/juego_3d.cfv`](ejemplos/juego_3d.cfv) (cubo 3D con MVP matrix)

---

## Android e iOS

### Android (Android Studio)

1. Abre `herramientas/android-cforgev/` en Android Studio
2. Copia `cforgev.cpp` a `app/src/main/cpp/`
3. Build — el NDK compila `libcforgev_jni.so` para arm64-v8a y x86_64

```java
// Java: CForgeRuntime.java
CForgeRuntime.runFile("scripts/main.cfv");
String resultado = CForgeRuntime.runCode("mostrar(1 + 2)");
```

Escribe tu logica en `app/src/main/assets/scripts/main.cfv` — es C-Forge puro.

### iOS (Xcode)

1. Abre `herramientas/ios-cforgev/CForgeApp/` en Xcode
2. Configura Bridging Header: `CForgeApp-Bridging-Header.h`
3. Compila — Swift llama a ObjC++ que llama a C-Forge

```swift
// Swift
let resultado = CForgeRuntime.runCode("mostrar(fecha_ahora())")
```

---

## Banco, API REST y apps completas

Ver ejemplos en [`ejemplos/`](ejemplos/):

| Ejemplo | Descripcion |
|---|---|
| [`app_banco.cfv`](ejemplos/app_banco.cfv) | App bancaria: clientes, cuentas, transferencias, prestamos |
| [`api_rest.cfv`](ejemplos/api_rest.cfv) | API REST con CRUD, validacion, SQLite |
| [`juego_2d.cfv`](ejemplos/juego_2d.cfv) | Snake con SDL2 |
| [`juego_3d.cfv`](ejemplos/juego_3d.cfv) | Cubo 3D rotando con OpenGL |
| [`test_suite.cfv`](ejemplos/test_suite.cfv) | Suite completa de pruebas unitarias |

---

## Compilar el interprete

```bash
# Basico (sin dependencias extra)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp

# Con OpenSSL (AES-256-CBC + SHA-256 — para criptografia)
# macOS (Homebrew):
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_OPENSSL \
    -I$(brew --prefix openssl)/include \
    -L$(brew --prefix openssl)/lib \
    -lcrypto

# Linux (Ubuntu/Debian):
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_OPENSSL \
    /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
    -lpthread

# Con SQLite (db_abrir, db_consulta, etc.)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_SQLITE \
    -lsqlite3 -lpthread

# Completo (OpenSSL + SQLite — recomendado para produccion)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_OPENSSL -DCFV_WITH_SQLITE \
    /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
    -lsqlite3 -lpthread

# Con SDL2 (juegos 2D)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_SDL2 -lSDL2 -lpthread

# Con SDL2 + OpenGL (juegos 3D)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -DCFV_WITH_SDL2 -DCFV_WITH_OPENGL \
    -lSDL2 -lGL -lpthread
```

---

## Biblioteca estandar

| Modulo | Descripcion |
|---|---|
| `stdlib/colecciones.cfv` | Cola, Pila, Conjunto, Heap |
| `stdlib/aleatorio.cfv` | Numeros aleatorios, mezcla |
| `stdlib/fecha.cfv` | Fecha, hora, formatos |
| `stdlib/json.cfv` | json_serializar, json_bonito |
| `stdlib/regex.cfv` | coincidir, buscar, reemplazar |
| `stdlib/base64.cfv` | base64, sha256, url_encode |
| `stdlib/io.cfv` | leer_lineas, escribir_csv |
| `stdlib/texto.cfv` | Texto avanzado |
| `stdlib/lista.cfv` | Operaciones de lista |
| `stdlib/mapa.cfv` | Operaciones de mapa |
| `stdlib/matematica.cfv` | Funciones matematicas |
| `stdlib/errores.cfv` | Sistema de errores |
| `stdlib/tipos.cfv` | Conversion de tipos |
| `stdlib/web.cfv` | Servidor HTTP nativo |
| `stdlib/crypto.cfv` | AES-256, SHA-256, JWT |
| `stdlib/sdl.cfv` | Juegos 2D con SDL2 |
| `stdlib/gl.cfv` | Graficos 3D con OpenGL |
| `stdlib/db.cfv` | SQLite ORM completo |
| `stdlib/pruebas.cfv` | Testing framework |
| `stdlib/log.cfv` | Logging con colores |
| `stdlib/validar.cfv` | Validacion de datos |
| `stdlib/http_cliente.cfv` | Cliente HTTP REST |
| `stdlib/concurrencia.cfv` | Canales, mutex, hilos |

---

## Package Manager (`cfpkg`)

```bash
./cfpkg install usuario/repositorio
./cfpkg install usuario/repositorio@v1.2.0
./cfpkg list
./cfpkg remove nombre-paquete
./cfpkg init mi-lib 1.0.0 "Mi libreria"
```

---

## CLI

| Comando | Descripcion |
|---|---|
| `./cforgev archivo.cfv` | Ejecutar un programa |
| `./cforgev` | REPL interactivo |
| `./cforgev --version` | Ver version |
| `./cfpkg install u/repo` | Instalar paquete |
| `./cfpkg list` | Listar paquetes |
| `./cfpkg remove nombre` | Desinstalar |

---

## Arquitectura

```
cforgev.cpp           -- Interprete nativo (C++20, ~5400 lineas)
stdlib/               -- Biblioteca estandar en C-Forge puro (24 modulos)
ejemplos/             -- Ejemplos: juegos, banco, API, 3D
herramientas/
  android-cforgev/    -- Template Android Studio (JNI + NDK)
  ios-cforgev/        -- Template iOS (Swift + ObjC++)
cfpkg                 -- Package manager (shell script)
```

---

## Seguridad

- `proceso_ejecutar` ejecuta comandos reales. No construyas comandos con entradas externas.
- `http_get` / `http_solicitud` aceptan solo URLs `http`/`https`.
- Los paquetes instalados con `cfpkg` se ejecutan con tus permisos — instala solo de fuentes confiables.
- Para produccion bancaria o critica, se requiere auditoria profesional.

---

## Proyecto

C-Forge es una iniciativa de **Vemoris Group**, creada por **Javier**.

El repositorio incluye una licencia propietaria con derechos reservados.
Consulta [`LICENSE`](LICENSE) antes de usar el codigo en proyectos externos.

Documentacion adicional:
- [`ESPECIFICACION.md`](ESPECIFICACION.md) — sintaxis completa
- [`INTEROPERABILIDAD.md`](INTEROPERABILIDAD.md) — interop con C++, Python, Java
- [`CHANGELOG.md`](CHANGELOG.md) — historial de versiones
