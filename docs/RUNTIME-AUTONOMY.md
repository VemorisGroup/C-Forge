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

Además, Mach-O ARM64 y PE x64 tienen variantes Core B6.7:

- `bootstrap/direct/cforge_macho_arm64_core.cfv`
- `bootstrap/direct/cforge_pe_x64_core.cfv`

Los tres emisores mínimos producen de forma determinista un ejecutable para un programa
cuya forma es `mostrar("texto")`. `make backend-check` genera los artefactos,
verifica sus cabeceras con `file` y ejecuta únicamente el formato soportado por
el anfitrión.

`make backend-core-check` construye los compiladores Core B6.7 una vez con
Stage 0 y después ejecuta la emisión con un `PATH` aislado. Las variantes Core
generan directamente Mach-O y PE deterministas para variables, aritmética,
condiciones, ciclos, funciones, listas y texto dinámico. En macOS ARM64 también
se ejecuta el binario producido y se compara su salida.

Esto demuestra emisión sin enlazador para el alcance probado. Todavía no existe
un backend general común: mapas, objetos, clases, interfaces, excepciones,
ownership completo, módulos, depuración, async y biblioteca estándar permanecen
fuera de B6.7. ELF Core también continúa pendiente.

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
