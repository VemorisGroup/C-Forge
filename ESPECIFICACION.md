# Especificación de C-Forge 1.4.1 Definitive

## Estado

C-Forge 1.2 es una implementación experimental de un lenguaje compilado de
propósito general creado por Vemoris Group. Esta especificación describe solamente
las capacidades implementadas y no promete compatibilidad futura absoluta.

## Filosofía

- Escritura legible y productiva inspirada en Python.
- Compilación y rendimiento inspirados en C++.
- Seguridad, organización y herramientas inspiradas en C#.
- Errores comprensibles y comportamiento consistente.

## Archivos y ejecución

Los archivos fuente usan `.cfv`. Pueden interpretarse durante el desarrollo o
compilarse a un ejecutable nativo mediante el backend C++17.
Sin archivo, `./cforgev` abre un REPL persistente que admite bloques multilínea.

## Valores

La versión 0.2 implementa números, textos Unicode, booleanos (`verdadero` y
`falso`), listas, mapas y `nulo`. Una variable puede declararse con `sea` o mediante
una primera asignación como `contador = 10`. Su tipo se infiere y queda fijo. El
analizador estático rechaza contradicciones evidentes antes de invocar Clang.

```text
sea nombre = "Javier";
sea activo = verdadero;
sea edad: numero = 20;
nombre = "Vemoris";
```

## Operaciones

Están disponibles `+`, `-`, `*`, `/`, `==`, `!=`, `<`, `<=`, `>` y `>=`.
`+` suma números o concatena dos textos. Dividir por cero produce un error.

Los operadores lógicos son `y`, `o` y `no`.

## Colecciones

```text
sea lenguajes: lista = ["C++", "C#", "Python"];
agregar(lenguajes, "C-Forge");
mostrar(lenguajes[0]);

sea persona: mapa = {"nombre": "Javier", "edad": 20};
mostrar(persona["nombre"]);
```

`longitud` admite textos, listas y mapas. Las claves de mapa son textos.

## Control de flujo

```text
si (edad >= 18) {
    mostrar("Adulto");
} sino {
    mostrar("Menor");
}

mientras (contador < 10) {
    contador = contador + 1;
}
```

## Funciones

Las funciones aceptan parámetros, poseen variables locales y pueden retornar un
valor. Deben declararse antes de utilizarse en el modo intérprete.

```text
funcion sumar(a, b) {
    retornar a + b;
}
```

## Funciones integradas

`mostrar(valor)` escribe un valor. `leer(mensaje)` obtiene texto del usuario.
`a_numero`, `a_texto`, `longitud` y `agregar` ofrecen operaciones básicas.

## Módulos

`usar "ruta.cfv";` incorpora un módulo local. Las rutas son relativas al archivo
que realiza la importación. Un módulo repetido se carga una sola vez.

## Archivos

`leer_archivo(ruta)`, `escribir_archivo(ruta, contenido)` y
`existe_archivo(ruta)` trabajan con texto UTF-8. Las rutas relativas se resuelven
desde la carpeta del programa principal al interpretar y al compilar.

## Errores controlados

```text
intentar {
    mostrar(leer_archivo("datos.txt"));
} capturar(error) {
    mostrar(error);
}
```

## Estructuras tipadas

```text
estructura Persona {
    nombre: texto;
    edad: numero;
}

sea persona: Persona = Persona("Javier", 20);
mostrar(persona.nombre);
```

Los constructores validan la cantidad y los tipos de los campos. Las estructuras
son datos sin métodos; las clases de la sección siguiente incorporan comportamiento.

## Clases y métodos

```text
clase Cuenta {
    campo saldo: numero;
    metodo depositar(cantidad) {
        este.saldo = este.saldo + cantidad;
        retornar este.saldo;
    }
}

sea cuenta: Cuenta = Cuenta(100);
mostrar(cuenta.depositar(50));
```

`este` referencia la instancia receptora. Las asignaciones de campos conservan el
tipo declarado. La versión 0.5 usa despacho dinámico y memoria compartida segura;
todavía no incluye herencia, interfaces ni visibilidad pública/privada.

## Matemáticas, tiempo y argumentos

La biblioteca incluye `raiz`, `potencia`, `absoluto`, `redondear`,
`tiempo_actual` y `argumentos`. Esta última devuelve una lista de textos.

## Interoperabilidad

- `use_python(modulo, funcion, argumentos)` importa y llama Python real.
- `use_native(ruta, simbolo, argumentos)` carga una biblioteca Native AOT/C ABI.
- `use_cpp(nombre, argumentos)` llama una función C/C++ registrada al vincular.

Los argumentos extranjeros se entregan en una lista y pueden ser nulo, enteros,
decimales o textos. Los detalles normativos están en `INTEROPERABILIDAD.md`.

## Paralelismo y perfil adaptativo

`paralelo("funcion", trabajos)` aplica una función C-Forge de un argumento a cada
elemento. El backend C++ usa `std::async(std::launch::async)` y ejecuta tareas
C-Forge nativas fuera del GIL. El intérprete mantiene la misma semántica con
hilos, aunque el GIL anfitrión puede limitar trabajo intensivo no compilado.

Cada función registra invocaciones mediante contadores sincronizados.
`jit_estado(nombre)` y `jit_caliente(nombre)` exponen el perfil, con umbral 1000.
Esto prepara un futuro reemplazo de código; 0.8 todavía no emite código máquina JIT.

## Importación universal

`import pip:math;` permite `math.sqrt(81)`. `import nuget:CSharpNative;` resuelve
una biblioteca Native AOT local y permite `CSharpNative.csharp_add(20, 22)`.
Los paquetes deben estar instalados: 0.9 no descarga ni ejecuta instaladores.

## Bloques GPU

`gpu { ... }` delimita trabajo acelerable. En 0.9 el backend funcional usa una
tarea CPU nativa y puede combinarse con `paralelo`. La frontera queda preparada
para compiladores Metal/CUDA posteriores; no afirma usar físicamente la GPU aún.

## Autorreparación

Ante errores, el CLI sugiere correcciones seguras de palabras clave, comillas y
delimitadores. `--reparar` las aplica y conserva el original como `.cfv.bak`.
No se usa un modelo remoto ni se modifican automáticamente decisiones ambiguas.

## WebAssembly y recarga caliente

`--wasm` genera WAT válido para el subconjunto numérico inicial. El host web debe
proporcionar `env.cfv_print_f64`. `--vigilar` observa cambios de fecha del archivo,
re-tokeniza y ejecuta la nueva versión conservando variables, tipos y funciones.

## Código externo literal

`extern("python") { ... }` y `extern("cpp") { ... }` son extraídos por el lexer
como un token opaco. Python se entrega al runtime embebido. C++ se compila como
parte del ejecutable nativo; en el intérprete se construye en un proceso aislado.
También se admiten `javascript`, `typescript` y `java`. JavaScript/TypeScript usan
Node aislado. Java genera y ejecuta una clase temporal mediante el JDK disponible.

## Puentes npm y Maven

`import npm:paquete` crea un proxy JavaScript y `use_javascript` llama una función
exportada. `import maven:paquete` reserva `paquete.call(clase, método, argumentos)`;
`use_java` permite indicar el JAR directamente. La conversión universal admite
datos recursivos con Python/JavaScript y escalares con el puente Java 1.1.

## ForgeValue y árbol compartido

`ForgeValue` es la representación nativa única para nulo, número, texto, booleano,
lista y mapa, con procedencia del runtime. Las variables globales se registran en
un árbol compartido. Python las ve como `ForgeSymbols`, JavaScript como
`globalThis.ForgeSymbols` y C++ mediante `cfv_symbol`. Los mapas provenientes de
otro runtime admiten acceso con punto igual que los datos de C-Forge.

## Seguridad de memoria asistida

El analizador rechaza memoria manual, liberación explícita, casts inseguros y
acceso por puntero dentro de bloques C++ literales. Los valores normales usan RAII,
smart pointers y callbacks ABI de propiedad. Esta política no constituye una
prueba formal ni puede sanear una DLL externa defectuosa o maliciosa.

## Símbolos distribuidos

`cluster variable = valor;` y `cluster funcion nombre(...) { ... }` registran
metadatos de distribución. `cluster_estado()` devuelve entradas serializables como
`variable:version`. La versión 1.0 prepara la tabla de símbolos; todavía no abre
sockets, descubre nodos ni transmite código por la red.

## Pruebas y formato

`test "nombre" { ... }` declara una prueba y `afirmar(condición, mensaje)` falla
si la condición es falsa. `cforge test archivo.cfv` ejecuta la suite y resume sus
resultados. `cforge fmt archivo.cfv` aplica el formato oficial básico.

`print` es alias de `mostrar`; `use_csharp` es alias de `use_native` para una
biblioteca C# Native AOT compatible con el ABI.

## Limitaciones conocidas

Todavía no existen herencia, interfaces, transporte distribuido ni gestor de paquetes.
El ABI extranjero 1.2 continúa siendo experimental y puede evolucionar.
El sistema de tipos aún no incluye genéricos y los módulos no poseen espacios de
nombres ni control de visibilidad. El backend
nativo actual genera C++17 y requiere Clang. No debe utilizarse aún para software
financiero, crítico o de producción.
