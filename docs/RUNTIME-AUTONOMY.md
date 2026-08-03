# Autonomía del runtime y backends directos

## Estado verificable en 2.6.0 estable

El ejecutable `cforge` puede interpretar programas `.cfv` sin Python, JVM,
.NET ni Node. El motor distribuido se construye desde `cforgev.cpp` con un
compilador C++20; por ello la *toolchain* completa todavía no es autónoma.

La biblioteca estándar activa contiene únicamente fuentes `.cfv`. El gate
local carga realmente los 30 módulos y ejecuta veinte archivos de prueba nativos:

```sh
make clean
make build
make check
make test
make install-check
make release-check
```

## Backends de código máquina directo

Hay tres emisores escritos en C-Forge:

- `bootstrap/direct/cforge_macho_arm64.cfv`
- `bootstrap/direct/cforge_elf_x64.cfv`
- `bootstrap/direct/cforge_pe_x64.cfv`

Además, los tres formatos tienen variantes Core B6.8:

- `bootstrap/direct/cforge_macho_arm64_core.cfv`
- `bootstrap/direct/cforge_elf_x64_core.cfv`
- `bootstrap/direct/cforge_pe_x64_core.cfv`

Los tres emisores mínimos producen de forma determinista un ejecutable para un programa
cuya forma es `mostrar("texto")`. `make backend-check` genera los artefactos,
verifica sus cabeceras con `file` y ejecuta únicamente el formato soportado por
el anfitrión.

`make backend-core-check` construye los compiladores Core B6.8 una vez con
Stage 0 y después ejecuta la emisión con un `PATH` aislado. Las variantes Core
generan directamente Mach-O, ELF y PE deterministas para variables, aritmética,
condiciones, ciclos, funciones, listas y texto dinámico. En macOS ARM64 se
ejecuta Mach-O; en Linux x64 se ejecuta ELF. PE se verifica estructuralmente
hasta que el mismo gate se ejecute en Windows con el compilador Core habilitado.

Esto demuestra emisión sin enlazador para el alcance probado. Todavía no existe
un backend general común: mapas, objetos, clases, interfaces, excepciones,
ownership completo, módulos, depuración, async y biblioteca estándar permanecen
fuera de B6.8.

## Core IR 1 — base de B6.9

`bootstrap/core_ir.cfv` define un contrato propio y determinista para layouts
de estructuras y clases. Cada campo recibe índice, tipo, tamaño, alineación y
offset; los métodos conservan propietario, retorno y parámetros. El mismo IR
será consumido por Mach-O, ELF y PE, evitando que cada backend invente una ABI
distinta. `make ir-core-check` compila y ejecuta esa implementación escrita en
C-Forge. La presencia del IR no significa todavía que los tres emisores puedan
materializar objetos o despachar métodos; ese lowering permanece en progreso.

## Criterio para declarar autonomía completa

C-Forge solo se declarará autónomo cuando una distribución limpia pueda:

1. construir Stage 1 y las etapas siguientes sin Python ni herramientas de
   otros runtimes;
2. generar programas generales para cada plataforma objetivo;
3. compilar sus propias fuentes y obtener artefactos reproducibles;
4. instalar, probar y desinstalar el motor y la biblioteca estándar;
5. superar CI y validación física en macOS, Linux y Windows.

La falta de autoalojamiento completo no impide que el runtime distribuido tenga
un contrato estable: los usuarios finales ejecutan el binario `cforge` sin
Python, JVM, .NET ni Node. La autonomía completa de la *toolchain* permanece
como un objetivo separado y no se anuncia como terminada.
