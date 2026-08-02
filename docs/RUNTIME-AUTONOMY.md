# Autonomía del runtime y backends directos

## Estado verificable en 2.6.0-dev

El ejecutable `cforge` puede interpretar programas `.cfv` sin Python, JVM,
.NET ni Node. El motor distribuido se construye desde `cforgev.cpp` con un
compilador C++20; por ello la *toolchain* completa todavía no es autónoma.

La biblioteca estándar activa contiene únicamente fuentes `.cfv`. El gate
local comprueba los 31 módulos y ejecuta ocho archivos de prueba nativos:

```sh
make clean
make build
make check
make test
make install-check
```

## Backends de código máquina directo

Hay tres emisores escritos en C-Forge:

- `bootstrap/direct/cforge_macho_arm64.cfv`
- `bootstrap/direct/cforge_elf_x64.cfv`
- `bootstrap/direct/cforge_pe_x64.cfv`

Los tres producen de forma determinista un ejecutable mínimo para un programa
cuya forma es `mostrar("texto")`. `make backend-check` genera los artefactos,
verifica sus cabeceras con `file` y ejecuta únicamente el formato soportado por
el anfitrión.

Esto demuestra que C-Forge puede escribir las estructuras iniciales de Mach-O,
ELF y PE sin invocar un enlazador para ese caso limitado. No demuestra todavía
un backend general: expresiones completas, funciones, objetos, memoria,
excepciones, módulos, depuración y biblioteca estándar siguen fuera de estos
tres emisores.

Las variantes históricas llamadas `*_core` se retiraron del árbol activo porque
no pasaban el parser actual. No volverán a anunciarse hasta ser reimplementadas
en `.cfv`, pasar el gate y ejecutarse en su plataforma real.

## Criterio para declarar autonomía completa

C-Forge solo se declarará autónomo cuando una distribución limpia pueda:

1. construir Stage 1 y las etapas siguientes sin Python ni herramientas de
   otros runtimes;
2. generar programas generales para cada plataforma objetivo;
3. compilar sus propias fuentes y obtener artefactos reproducibles;
4. instalar, probar y desinstalar el motor y la biblioteca estándar;
5. superar CI y validación física en macOS, Linux y Windows.

Hasta entonces el estado correcto es **Developer Preview**.
