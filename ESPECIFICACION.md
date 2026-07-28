# Especificación de C-Forge 1.6.0 Developer Preview

## Estado

C-Forge 1.6 es una implementación Developer Preview de un lenguaje de
propósito general creado por Vemoris Group. Esta especificación describe solamente
las capacidades implementadas y no promete compatibilidad futura absoluta.

Los contratos normativos de esta versión son:

- [`docs/GRAMMAR-1.6.ebnf`](docs/GRAMMAR-1.6.ebnf): gramática léxica y sintáctica.
- [`docs/TYPE-SYSTEM-1.6.md`](docs/TYPE-SYSTEM-1.6.md): tipos, `ForgeValue` y ownership.
- [`docs/BACKEND-SEMANTICS-1.6.md`](docs/BACKEND-SEMANTICS-1.6.md): traducción y paridad.

Si un ejemplo descriptivo contradice esos contratos, prevalece el contrato
normativo y la implementación debe corregirse.

## Filosofía

- Escritura legible y productiva inspirada en Python.
- Compilación y rendimiento inspirados en C++.
- Seguridad, organización y herramientas inspiradas en C#.
- Errores comprensibles y comportamiento consistente.

## Genéricos e interfaces (Developer Preview 1.6)

```cfv
funcion identidad<T>(valor: T): T {
    retornar valor
}

interfaz Sumable {
    metodo sumar(x: numero): numero
}

clase Contador implementa Sumable {
    campo valor: numero
    metodo sumar(x: numero): numero {
        retornar este.valor + x
    }
}
```

Los parámetros de tipo se infieren en cada llamada y deben conservar una
sustitución coherente. `implementa` es nominal: la clase debe declarar todos los
métodos exigidos con tipos compatibles.

## Archivos y ejecución

Los archivos fuente usan `.cfv`. Pueden interpretarse durante el desarrollo o
compilarse a un ejecutable nativo mediante el backend C++17.
Sin archivo, `./cforgev` abre un REPL persistente que admite bloques multilínea.

## Valores

La versión de desarrollo implementa números, textos Unicode, booleanos (`verdadero` y
`falso`), listas, mapas, tuplas, conjuntos y `nulo`. Una variable puede declararse con `sea` o mediante
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

sea version: tupla = ("C-Forge", 2, verdadero);
mostrar(version[1]);

sea motores: conjunto = conjunto("LLVM", "VM", "LLVM");
mostrar(motores); // conjunto(LLVM, VM)
```

`longitud` admite textos y colecciones. Las tuplas son inmutables y admiten
elementos heterogéneos. Los conjuntos eliminan duplicados y exigen elementos
inmutables; su presentación es determinista. Las claves de mapa son textos.

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
tipo declarado. La versión 1.6 usa despacho dinámico e interfaces nominales
comprobadas; todavía no incluye herencia de implementación ni visibilidad
pública/privada.

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
Estos bloques son una frontera `unsafe`: ejecutan código extranjero con los
permisos del proceso anfitrión. No son un sandbox y no deben ejecutar contenido
no confiable. El modo seguro de la VM los rechaza salvo autorización explícita.

### Frontera C ABI tipada de LLVM

El backend LLVM admite declaraciones externas explícitas:

```cfv
extern_c funcion native_add(a: numero, b: numero): numero
mostrar(native_add(20, 22))
```

La ABI V1 acepta `numero`, `booleano` y `texto`. La ABI V2 experimental agrega
vistas prestadas de listas numéricas y representaciones recursivas verificadas
para listas, mapas y registros. Los layouts, límites y ownership se validan al
cruzar la frontera. La implementación se enlaza con
`--compilar-llvm --vincular puente.c`; las capacidades incompatibles se
rechazan en vez de simularse.

Las declaraciones `extern_c segura funcion` usan un contrato C de estado,
puntero de salida y mensaje UTF-8. Un estado no cero se convierte en excepción
C-Forge capturable; así ninguna excepción C++ atraviesa directamente el ABI.

LLVM IR 1.4 representa `opcion<numero>`, `opcion<texto>` y
`opcion<booleano>` mediante una etiqueta y carga útil comprobada. Un
`desenvolver(ninguno())` transfiere el mensaje al bloque `capturar` activo; si no
existe manejador, el programa termina con un diagnóstico C-Forge.

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

No existen todavía herencia, transporte distribuido real, espacios de nombres
ni control de visibilidad. Interfaces, genéricos, ownership, paquetes, async,
LLVM, Arena compartida y ABI V2 existen como Developer Preview y pueden
evolucionar. Los seis adaptadores extranjeros no tienen aún cobertura uniforme
en macOS, Linux y Windows. LLVM y Wasm admiten subconjuntos documentados.
El backend C++17 requiere Clang. C-Forge no debe utilizarse aún para software
financiero, crítico o de producción.
