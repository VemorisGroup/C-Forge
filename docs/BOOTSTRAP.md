# C-Forge Core Bootstrap

## Objetivo

C-Forge será autoalojado cuando un compilador escrito íntegramente en `.cfv`
pueda compilar su propio código fuente y las generaciones consecutivas sean
reproducibles. Los puentes políglotas no forman parte de este núcleo.

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
| B1 | Parser escrito en C-Forge | AST canónico igual para la suite Core |
| B2 | Tipos y ownership escritos en C-Forge | Diagnósticos iguales para casos positivos y negativos |
| B3 | Emisor nativo escrito en C-Forge | Produce el artefacto que ejecutará Stage 1 |
| B4 | Compilador Stage 1 | Compila todas sus propias fuentes `.cfv` |
| B5 | Stage 2/3 | Artefactos reproducibles equivalentes |
| B6 | Runtime autónomo | Funciona sin runtimes externos instalados |

## Estado actual

**B0 en progreso verificable.**

- `bootstrap/stage0/cforge_bootstrap.cpp` es el compilador inicial C++.
- `bootstrap/fixtures/minimal.cfv` se convierte en un ejecutable de máquina real.
- `bootstrap/core_lexer.cfv` es el primer componente de Stage 1 escrito en
  C-Forge.

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
