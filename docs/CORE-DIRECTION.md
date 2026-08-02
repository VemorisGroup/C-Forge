# Dirección nativa de C-Forge

## Regla principal

C-Forge Core no se construirá como una capa encima de Python, C++, C#, Java,
JavaScript, TypeScript, JVM, .NET, Node ni otro runtime. Stage 0 usa C++ solo
para arrancar el proceso de autoalojamiento. Stage 1 y generaciones posteriores
se escribirán en C-Forge.

Los puentes históricos fueron retirados del motor activo. No forman parte del
núcleo, de la biblioteca estable ni de la arquitectura futura.

## Capacidades que deben ser propias

| Área | Implementación futura de C-Forge |
|---|---|
| Control y rendimiento | Código nativo, tipos de tamaño explícito, SIMD, ABI y frontera `unsafe` propias |
| Memoria | Ownership, préstamos, regiones, destructores y asignador de C-Forge |
| Objetos | Clases, estructuras, interfaces, genéricos, despacho y módulos propios |
| Scripting | Compilación rápida incremental, REPL y ejecución de módulos C-Forge |
| Concurrencia | Tareas, canales, cancelación y scheduler del runtime propio |
| Web | Biblioteca HTTP, TLS, sockets, JSON, servidor y target WebAssembly propios |
| Herramientas | Compilador, formatter, LSP, DAP y gestor de paquetes escritos en C-Forge |

“Propio” significa que la característica está implementada por el compilador,
runtime o biblioteca estándar de C-Forge. No significa renombrar una llamada a
otro lenguaje.

## Capas permitidas

1. **Core autónomo:** obligatorio y sin runtimes extranjeros.
2. **Biblioteca estándar:** escrita en C-Forge con primitivas del sistema
   documentadas.
3. **Paquetes C-Forge:** escritos y distribuidos como C-Forge.
4. **Compatibilidad extranjera opcional:** fuera del Core, nunca requerida para
   compilar o ejecutar un programa C-Forge puro.

## Criterio de aceptación

Una instalación limpia debe poder:

1. compilar el compilador desde sus fuentes `.cfv`;
2. recompilarse de Stage 2 a Stage 3 reproduciblemente;
3. compilar y ejecutar programas Core sin Python, C++, JVM, .NET, Node ni LLVM;
4. ejecutar la biblioteca estándar nativa;
5. informar claramente cualquier capacidad todavía no implementada.

Este criterio corresponde a la autonomía completa de la *toolchain*. C-Forge
2.6 es estable como runtime distribuido; no se anuncia todavía como compilador
autoalojado.
