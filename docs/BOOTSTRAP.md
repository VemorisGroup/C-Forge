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
| B4 | Compilador Stage 1 | **Cumplido:** Stage 0 construye el compilador `.cfv`; este compila programas Core a ejecutables sin Python |
| B5 | Stage 2/3 | **Cumplido:** Stage 1 compila sus fuentes; Stage 2/3 generan C++ y binarios idénticos byte por byte |
| B6 | Runtime autónomo | **En progreso:** B6.1 verifica ejecución Core sin Python; toolchain, enlazador y biblioteca estándar autónomos siguen pendientes |

## Estado actual

**B5 completado de forma verificable; C-Forge Core 0.5 está autoalojado. B6 es el siguiente hito.**

- `bootstrap/stage0/cforge_bootstrap.cpp` es el compilador inicial C++.
- `bootstrap/fixtures/minimal.cfv` se convierte en un ejecutable de máquina real.
- `bootstrap/core_lexer.cfv` es el primer componente de Stage 1 escrito en
  C-Forge.
- `bootstrap/core_ast.cfv` define el árbol independiente del runtime anfitrión.
- `bootstrap/core_parser.cfv` implementa el parser recursivo descendente.
- `bootstrap/core_semantics.cfv` implementa tipos y ownership Core.
- `bootstrap/core_emitter.cfv` genera una unidad C++17 completa desde el AST
  aprobado por B2.
- `bootstrap/core_driver.cfv` conecta automáticamente lexer, parser, análisis
  semántico, ownership, emisión C++ y compilación nativa.
- `bootstrap/stage1/cforge_stage1.cfv` es la unidad Stage 1 autocontenida,
  escrita íntegramente en C-Forge y construible por Stage 0.
- `bootstrap/core_runtime.cfv` contiene el runtime nativo reproducible como
  fuente C-Forge.
- `docs/CORE-GRAMMAR-0.5.ebnf` congela exactamente el subconjunto autoalojado.
- `tests/test_bootstrap_b1.py` compila la unidad B1 con Stage 0 y exige que
  Stage 0, intérprete y VM emitan bytes idénticos para el AST canónico.

B5 cubre toda la gramática **Core 0.5**, no la gramática general de C-Forge
2.0. `numero` es copiable; `texto` tiene ownership y `mover(texto)` invalida el
origen. El analizador detecta tipos incompatibles, variables no declaradas y uso
después de mover. Préstamos, tiempos de vida, regiones, clases del usuario,
módulos y el resto del lenguaje se añadirán solo cuando los hitos posteriores
los necesiten y posean pruebas normativas.

La prueba B4 construye Stage 1 con Stage 0 y usa ese nuevo binario para compilar
un programa `.cfv` Core completo. La ejecución de Stage 1 no carga ni enlaza
Python. También verifica que los errores sintácticos y semánticos detengan la
compilación antes de producir un ejecutable.

Stage 1 acepta todas las construcciones usadas por sus propias fuentes. Stage 1
produce Stage 2; Stage 2 produce Stage 3 desde la misma unidad `.cfv`. Ambos
generan C++ idéntico y ejecutables idénticos byte por byte. En macOS, el backend
normaliza `LC_UUID` y aplica una firma ad hoc con identificador estable; en
Linux desactiva el build-id. Esto elimina metadatos del enlazador que no forman
parte de la semántica.

Por tanto, **el compilador C-Forge Core 0.5 está autoalojado** según la
definición verificable de este documento. Esto no significa que el lenguaje
completo 2.0 ni el runtime sean autónomos: ampliar la paridad y eliminar la
dependencia normal de `clang++` corresponden a B6 y hitos posteriores.

B6.1 añade una prueba normativa que bloquea deliberadamente los comandos
`python` y `python3`, ejecuta un programa Core con listas, argumentos y archivos,
y comprueba que su binario no enlace Python. Los criterios y límites se
documentan en [`RUNTIME-AUTONOMY.md`](RUNTIME-AUTONOMY.md). Esta fase demuestra
autonomía de ejecución del runtime Core; todavía no elimina `clang++` del flujo
de compilación.

B6.2 inicia esa eliminación con un backend escrito en C-Forge que emite
directamente ELF64 y opcodes x86-64 para el programa mínimo
`mostrar("texto")`. Su prueba bloquea compilador, ensamblador y enlazador
externos durante la emisión. Sigue siendo un corte inicial: la compilación
general conserva el backend C++ mientras se implementan los objetivos Mach-O,
PE y el resto de la semántica Core.

B6.3 añade el objetivo Mach-O ARM64. C-Forge construye directamente los
segmentos, comandos de carga, opcodes y la firma ad hoc SHA-256. En macOS ARM64,
la prueba bloquea `clang++`, ensamblador, enlazador, `codesign` y Python durante
la emisión, y después ejecuta el binario resultante. El backend directo todavía
cubre únicamente `mostrar("texto")`; no reemplaza aún al backend general.

B6.4 conecta el frontend Core con ese objetivo Mach-O y añade variables
numéricas, asignaciones, aritmética, comparaciones, `si`/`sino` y `mientras`.
Las etiquetas y referencias a datos se resuelven dentro del emisor C-Forge, sin
ensamblador ni enlazador externos. La cobertura continúa siendo deliberadamente
parcial: funciones, colecciones y el runtime general permanecen fuera de este
corte.

B6.5 añade el objetivo Windows PE32+/x86-64. El emisor C-Forge construye la
imagen PE, sus tres secciones, la tabla de importaciones de `KERNEL32.dll` y las
instrucciones que llaman a `GetStdHandle`, `WriteFile` y `ExitProcess`. La
emisión no ejecuta compilador, ensamblador, enlazador ni Python. Un trabajo de CI
transporta el artefacto determinista y lo ejecuta en Windows x64. Esta primera
fase PE cubre el programa mínimo `mostrar("texto")`.

Construcción manual:

```bash
clang++ -std=c++17 -O2 bootstrap/stage0/cforge_bootstrap.cpp \
  -o build/cforge-bootstrap
./build/cforge-bootstrap bootstrap/fixtures/minimal.cfv -o build/minimal
./build/minimal

python3 herramientas/generar_stage1.py
./build/cforge-bootstrap bootstrap/stage1/cforge_stage1.cfv \
  -o build/cforge-stage1
./build/cforge-stage1 bootstrap/fixtures/minimal.cfv -o build/minimal-stage1
./build/minimal-stage1

./build/cforge-stage1 bootstrap/stage1/cforge_stage1.cfv -o build/stage2
./build/stage2 bootstrap/stage1/cforge_stage1.cfv -o build/stage3
cmp build/stage2 build/stage3
```

La salida debe ser:

```text
C-Forge Core Bootstrap
42
```

Stage 0 todavía requiere una toolchain C++ para generar el ejecutable. Esa
dependencia existe solo para arrancar. C-Forge continúa clasificado como
Developer Preview no autoalojado hasta superar Stage 2/3.
