# C-Forge Core Bootstrap

## Objetivo

C-Forge será autoalojado cuando un compilador escrito íntegramente en `.cfv`
pueda compilar su propio código fuente y las generaciones consecutivas sean
reproducibles. Los puentes políglotas no forman parte de este núcleo.

El lenguaje que deberá implementar el compilador autoalojado está definido por
[`C-FORGE-2.0-DRAFT.md`](C-FORGE-2.0-DRAFT.md) y
[`GRAMMAR-2.0-DRAFT.ebnf`](GRAMMAR-2.0-DRAFT.ebnf). Ambos son contratos
candidatos; el subconjunto Core crece de forma controlada hasta cubrirlos.

## Definiciones verificables

- **Stage 0:** compilador mínimo escrito en C++17. Lee C-Forge Core, genera una
  unidad nativa temporal y usa `clang++` para producir código máquina. Solo
  construye la primera generación y no cuenta como autonomía.
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

Stage 0 comienza deliberadamente con este subconjunto:

1. números y textos;
2. declaraciones `sea`, con anotación opcional `numero` o `texto`;
3. expresiones aritméticas y concatenación de textos;
4. `mostrar`/`print`;
5. comentarios de línea y punto y coma opcional;
6. diagnósticos con línea y columna.

Quedan fuera del bootstrap inicial: FFI, `extern`, GPU, cluster, red, paquetes,
interfaces gráficas y adaptadores extranjeros.

El subconjunto crecerá únicamente cuando Stage 1 necesite una construcción para
implementar el propio compilador: funciones, control de flujo, estructuras,
colecciones, archivos y ownership. Cada ampliación debe incluir una prueba que
compile y ejecute el artefacto nativo sin invocar Python.

## Orden de migración

| Hito | Implementación `.cfv` | Criterio de salida |
|---|---|---|
| B0 | Stage 0 C++ + lexer Stage 1 | Stage 0 produce un ejecutable; lexer `.cfv` pasa intérprete y VM |
| B1 | Parser escrito en C-Forge | **Cumplido:** AST canónico igual en Stage 0, intérprete y VM para la suite Core 0.2 |
| B2 | Tipos y ownership escritos en C-Forge | **Cumplido:** tipos, variable no declarada y uso después de mover producen diagnósticos iguales en los tres motores |
| B3 | Emisor nativo escrito en C-Forge | **Cumplido:** Stage 0 compila el emisor `.cfv`, que produce C++17 y un ejecutable nativo verificado |
| B4 | Compilador Stage 1 | Compila todas sus propias fuentes `.cfv` |
| B5 | Stage 2/3 | Artefactos reproducibles equivalentes |
| B6 | Runtime autónomo | Funciona sin runtimes externos instalados |

## Estado actual

**B3 completado de forma verificable; B4 es el siguiente hito.**

- `bootstrap/stage0/cforge_bootstrap.cpp` es el compilador inicial C++.
- `bootstrap/fixtures/minimal.cfv` se convierte en un ejecutable de máquina real.
- `bootstrap/core_lexer.cfv` es el primer componente de Stage 1 escrito en
  C-Forge.
- `bootstrap/core_ast.cfv` define el árbol independiente del runtime anfitrión.
- `bootstrap/core_parser.cfv` implementa el parser recursivo descendente.
- `bootstrap/core_semantics.cfv` implementa tipos y ownership Core.
- `bootstrap/core_emitter.cfv` genera una unidad C++17 completa desde el AST
  aprobado por B2.
- `docs/CORE-GRAMMAR-0.4.ebnf` congela exactamente el subconjunto aceptado.
- `tests/test_bootstrap_b1.py` compila la unidad B1 con Stage 0 y exige que
  Stage 0, intérprete y VM emitan bytes idénticos para el AST canónico.

B3 cubre toda la gramática **Core 0.4**, no la gramática general de C-Forge
2.0. `numero` es copiable; `texto` tiene ownership y `mover(texto)` invalida el
origen. El analizador detecta tipos incompatibles, variables no declaradas y uso
después de mover. Préstamos, tiempos de vida, regiones, clases del usuario,
módulos y el resto del lenguaje se añadirán solo cuando los hitos posteriores
los necesiten y posean pruebas normativas.

La prueba B3 compila el emisor con Stage 0, compara el C++ generado por Stage 0,
intérprete y VM, compila ese C++ con el compilador del sistema y ejecuta el
binario resultante. Un AST rechazado por B2 nunca se convierte en C++.

Construcción manual:

```bash
clang++ -std=c++17 -O2 bootstrap/stage0/cforge_bootstrap.cpp \
  -o build/cforge-bootstrap
./build/cforge-bootstrap bootstrap/fixtures/minimal.cfv -o build/minimal
./build/minimal
```

La salida debe ser:

```text
C-Forge Core Bootstrap
42
```

Stage 0 todavía requiere una toolchain C++ para generar el ejecutable. Esa
dependencia existe solo para arrancar. C-Forge continúa clasificado como
Developer Preview no autoalojado hasta superar Stage 2/3.
