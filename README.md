# C-Forge

[![CI multiplataforma](https://github.com/VemorisGroup/C-Forge/actions/workflows/ci.yml/badge.svg)](https://github.com/VemorisGroup/C-Forge/actions/workflows/ci.yml)
[![Seguridad](https://github.com/VemorisGroup/C-Forge/actions/workflows/security.yml/badge.svg)](https://github.com/VemorisGroup/C-Forge/actions/workflows/security.yml)

<p align="center">
  <img src="assets/cforgev-logo.svg" width="128" height="128" alt="Logo de C-Forge">
</p>

> Lenguaje de programacion de Vemoris Group con sintaxis propia, tipado gradual,
> interprete nativo en C++ y biblioteca estandar completa.

**Version actual:** `2.0.0`
**Extension oficial:** `.cfv`
**Estado:** funcional — apto para proyectos reales, scripting y aprendizaje.

---

## Inicio rapido

### Compilar el interprete

```bash
git clone https://github.com/VemorisGroup/C-Forge.git
cd C-Forge
g++ -std=c++20 -O2 -o cforgev cforgev.cpp
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

// Operadores de membresia e instanceof
si ("manzana" en ["pera", "manzana"]) { mostrar("encontrado") }
si (p es Perro) { mostrar("es un perro") }

// Asignacion compuesta
sea x: numero = 10
x += 5   // 15
x *= 2   // 30

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

// Enums
enum Color { Rojo, Verde, Azul }
sea c = Color.Rojo

// Para con rango lazy (sin crear lista en memoria)
para i en rango(0, 1000000) {
    si (i > 2) { romper }
    mostrar(i)
}
```

---

## Caracteristicas del lenguaje

| Caracteristica | Estado |
|---|---|
| Variables y tipado gradual | OK |
| Funciones y closures | OK |
| Clases con `este` | OK |
| Herencia (`extiende` / `super`) | OK |
| Modificadores `privado` / `publico` / `estatico` | OK |
| Parametros con valor por defecto | OK |
| Variadicos (`...args`) | OK |
| Desestructuracion de lista y mapa | OK |
| Spread (`[...lista, x]`) | OK |
| Operador ternario (`cond ? a : b`) | OK |
| `es` (instanceof con cadena de herencia) | OK |
| `en` (membresia en lista, mapa, texto) | OK |
| Asignacion compuesta (`+=`, `-=`, `*=`, `/=`, `%=`) | OK |
| `intentar` / `capturar` / `finalmente` / `lanzar` | OK |
| `segun` / `caso` / `otro` (switch/match) | OK |
| `??` (null coalescing) | OK |
| `?.` (safe navigation) | OK |
| Interpolacion de strings `"{variable}"` | OK |
| `para` con rango lazy | OK |
| `enum` | OK |
| REPL interactivo | OK |
| Stack traces con contexto | OK |
| `importar` modulos | OK |

---

## Biblioteca estandar

### Colecciones (`stdlib/colecciones.cfv`)

```cfv
importar "stdlib/colecciones.cfv"

// Cola (FIFO)
sea q = cola_nueva()
cola_encolar(q, "primero")
cola_encolar(q, "segundo")
mostrar(cola_desencolar(q))   // primero

// Pila (LIFO)
sea p = pila_nueva()
pila_push(p, 10)
pila_push(p, 20)
mostrar(pila_pop(p))   // 20

// Conjunto
sea s = conjunto_nuevo()
conjunto_agregar(s, "a")
conjunto_agregar(s, "a")   // duplicado ignorado
mostrar(conjunto_tam(s))   // 1

// Heap (min-heap)
sea h = heap_nuevo()
heap_insertar(h, 5)
heap_insertar(h, 1)
heap_insertar(h, 3)
mostrar(heap_extraer_min(h))   // 1
```

### Aleatorio (`stdlib/aleatorio.cfv`)

```cfv
importar "stdlib/aleatorio.cfv"

mostrar(aleatorio())                    // [0.0, 1.0)
mostrar(aleatorio_entero(1, 100))       // entero entre 1 y 100
mostrar(aleatorio_de(["a", "b", "c"])) // elemento aleatorio
sea lista = [1, 2, 3, 4, 5]
mezclar(lista)                          // Fisher-Yates in-place
```

### Fecha y hora (`stdlib/fecha.cfv`)

```cfv
importar "stdlib/fecha.cfv"

sea f = fecha_ahora()
mostrar(f["anio"])                       // 2026
mostrar(fecha_formato(f, "DD/MM/YYYY"))  // 28/07/2026
mostrar(fecha_ahora_ms())                // timestamp Unix en ms
mostrar(es_bisiesto(2024))              // verdadero
```

### JSON (`stdlib/json.cfv`)

```cfv
importar "stdlib/json.cfv"

sea obj = {"nombre": "Ana", "edad": 25}
sea txt = json_serializar(obj)
mostrar(txt)                      // {"edad":25,"nombre":"Ana"}
mostrar(json_bonito(obj))         // indentado
sea obj2 = json_parsear(txt)
mostrar(json_valido("{mal json")) // falso
```

### Regex (`stdlib/regex.cfv`)

```cfv
importar "stdlib/regex.cfv"

mostrar(regex_coincidir("^[0-9]+$", "12345"))        // verdadero
mostrar(regex_buscar("[0-9]+", "precio: 42 pesos"))  // 42
mostrar(regex_reemplazar("[aeiou]", "hola", "*"))     // h*l*
mostrar(es_email("user@example.com"))                 // verdadero
```

### Base64 y hashing (`stdlib/base64.cfv`)

```cfv
importar "stdlib/base64.cfv"

sea enc = base64_codificar("Hola C-Forge!")
mostrar(enc)                        // SG9sYSBDLUZvcmdlIQ==
mostrar(base64_decodificar(enc))    // Hola C-Forge!
mostrar(sha256("texto"))            // hash sha256
mostrar(url_codificar("a b+c"))     // a%20b%2Bc
```

### I/O y archivos

```cfv
// Funciones nativas (disponibles sin importar)
escribir_archivo("datos.txt", "contenido")
mostrar(leer_archivo("datos.txt"))
mostrar(existe_archivo("datos.txt"))  // verdadero
mostrar(listar_directorio("."))       // lista de archivos
crear_directorio("nueva_carpeta")

// HTTP
sea html = http_get("https://example.com")

// Helpers de alto nivel (stdlib/io.cfv)
importar "stdlib/io.cfv"
sea lineas = io_leer_lineas("datos.txt")
io_escribir_csv("tabla.csv", [[1, "Ana"], [2, "Luis"]])
```

---

## Package Manager (`cfpkg`)

C-Forge incluye un gestor de paquetes para instalar librerias desde GitHub.

### Instalar un paquete

```bash
./cfpkg install usuario/repositorio
./cfpkg install usuario/repositorio@v1.2.0
```

### Otros comandos

```bash
./cfpkg list                              # listar paquetes instalados
./cfpkg info nombre-paquete               # ver detalles
./cfpkg remove nombre-paquete             # desinstalar
./cfpkg init mi-lib 1.0.0 "Mi libreria"  # inicializar nuevo paquete
```

### Usar un paquete instalado

Los paquetes se instalan en `~/.cforge/pkgs/nombre/`. Para usarlos:

```cfv
importar "/Users/tu_usuario/.cforge/pkgs/mi-lib/src/mi-lib.cfv"
```

### Publicar tu paquete

Crea un repositorio en GitHub con un `cforge.json` en la raiz:

```json
{
  "nombre": "mi-lib",
  "version": "1.0.0",
  "descripcion": "Mi libreria para C-Forge",
  "autor": "tuUsuario",
  "licencia": "MIT",
  "archivos": ["src/mi-lib.cfv"],
  "dependencias": {}
}
```

Cualquiera puede instalarlo con `cfpkg install tuUsuario/mi-lib`.

---

## CLI

| Comando | Descripcion |
|---|---|
| `./cforgev archivo.cfv` | Ejecuta un programa |
| `./cforgev` | REPL interactivo |
| `./cfpkg install usuario/repo` | Instala un paquete desde GitHub |
| `./cfpkg list` | Lista paquetes instalados |
| `./cfpkg remove nombre` | Desinstala un paquete |
| `./cfpkg init nombre version desc` | Crea un nuevo paquete |

---

## Arquitectura

```
cforgev.cpp           -- Interprete nativo (C++20, sin dependencias externas)
stdlib/               -- Biblioteca estandar en C-Forge puro
cfpkg                 -- Package manager (shell script + cforgev)
cforgev.cfv           -- Interprete auto-hospedado (C-Forge en C-Forge)
compilador_nativo.cfv -- Compilador C-Forge a C++
```

El interprete `cforgev.cpp` es un ejecutable C++ puro que no requiere Python,
Node.js ni ningun runtime externo para ejecutar programas `.cfv`.

---

## Compilar el interprete

```bash
# Linux / macOS (basico)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp

# Con soporte Python opcional (para interop)
g++ -std=c++20 -O2 -o cforgev cforgev.cpp \
    -I/usr/include/python3.10 \
    -L/usr/lib -lpython3.10
```

---

## Seguridad

- `sys_run` ejecuta comandos reales del sistema. No construyas comandos con entradas externas.
- `http_get` llama a `curl` del sistema y acepta solo URLs `http`/`https`.
- Los paquetes instalados con `cfpkg` son codigo que se ejecuta con tus permisos — instala solo de fuentes confiables.

---

## Proyecto

C-Forge es una iniciativa de **Vemoris Group**, creada por **Javier**.

El repositorio incluye una licencia propietaria con derechos reservados.
Consulta [`LICENSE`](LICENSE) antes de usar el codigo en proyectos externos.

Documentacion adicional:
- [`ESPECIFICACION.md`](ESPECIFICACION.md) — sintaxis completa
- [`INTEROPERABILIDAD.md`](INTEROPERABILIDAD.md) — interop con C++, Python, Java
- [`CHANGELOG.md`](CHANGELOG.md) — historial de versiones
