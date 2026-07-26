# C-Forge Core Bootstrap

## Objetivo

C-Forge será autoalojado cuando un compilador escrito íntegramente en `.cfv`
pueda compilar su propio código fuente y las generaciones consecutivas sean
reproducibles. Los puentes políglotas no forman parte de este núcleo.

## Definiciones verificables

- **Stage 0:** implementación histórica alojada. Solo se usa para construir la
  primera generación y no cuenta como autonomía.
- **Stage 1:** compilador escrito en C-Forge y construido por Stage 0.
- **Stage 2:** Stage 1 compila exactamente las mismas fuentes `.cfv`.
- **Stage 3:** Stage 2 vuelve a compilar esas fuentes.
- **Autoalojado:** Stage 2 y Stage 3 producen artefactos equivalentes mediante
  una comparación reproducible.
- **Autónomo:** el compilador autoalojado, runtime, biblioteca estándar,
  ensamblador/enlazador y gestor básico funcionan en una instalación limpia sin
  Python, C++, JVM, .NET, Node ni LLVM.

No se usará la palabra “autoalojado” antes de superar Stage 2/3. No se usará la
palabra “autónomo” mientras el funcionamiento normal requiera otro runtime.

## C-Forge Core congelado

El compilador se implementará inicialmente con este subconjunto:

1. textos UTF-8, booleanos, enteros, listas y estructuras;
2. variables locales y asignación;
3. funciones con retornos explícitos;
4. `si/sino` y `mientras`;
5. indexación, comparación y concatenación;
6. lectura y escritura de archivos;
7. errores comprobados mediante `afirmar`;
8. ownership determinista para los valores del compilador.

Quedan fuera del bootstrap inicial: FFI, `extern`, GPU, cluster, red, paquetes,
interfaces gráficas y adaptadores extranjeros.

## Orden de migración

| Hito | Implementación `.cfv` | Criterio de salida |
|---|---|---|
| B0 | Lexer | Tokens iguales en intérprete y VM |
| B1 | Parser | AST canónico igual para la suite Core |
| B2 | Tipos y ownership | Diagnósticos iguales para casos positivos y negativos |
| B3 | Emisor CFBC | Bytecode válido aceptado por la VM |
| B4 | Compilador Stage 1 | Compila todas sus fuentes `.cfv` |
| B5 | Stage 2/3 | Artefactos reproducibles equivalentes |
| B6 | Runtime autónomo | Funciona sin runtimes externos instalados |

## Estado actual

**B0 en progreso.** `bootstrap/core_lexer.cfv` es el primer componente real del
compilador escrito en C-Forge. Todavía se ejecuta con la implementación alojada,
por lo que C-Forge continúa clasificado como Developer Preview no autoalojado.
