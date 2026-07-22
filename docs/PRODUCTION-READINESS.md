# Estado profesional de C-Forge

Este documento distingue funciones verificables de objetivos futuros. C-Forge no
se presenta como más seguro o rápido que otros lenguajes sin mediciones y auditorías
independientes reproducibles.

## Implementado y comprobable

- Lexer, parser, intérprete, backend C++ experimental y backend WebAssembly `.wat`.
- Compilador de bytecode y VM de pila propios (`cforge vm` y `cforge bytecode`).
- Inferencia y comprobación estática gradual, con diagnósticos `CFxxxx` (`cforge check`).
- Servidor LSP 3.17 por entrada/salida estándar con diagnósticos, autocompletado y hover
  (`cforge lsp`).
- Depurador de bytecode inicial con traza de instrucciones y variables (`cforge debug`).
- Gestor local reproducible con manifiesto y archivo de bloqueo SHA-256
  (`cforge pkg init/add/remove/list`).
- REPL, formateador, pruebas, empaquetado multiplataforma y CI.
- Contrato C ABI y Forge Shared Arena con offsets, cabeceras validadas y sincronización.

## Experimental o de alcance limitado

- La VM 1.0 ejecuta el núcleo seguro del lenguaje; los bloques extranjeros y algunas
  construcciones avanzadas continúan en el intérprete principal.
- El gestor de paquetes acepta rutas locales deliberadamente. Un registro remoto exige
  firma de artefactos, política de nombres, revocación y operación de infraestructura.
- El depurador muestra trazas verificables, pero todavía no implementa DAP, breakpoints
  remotos ni integración visual completa.
- GPU, JIT, cluster y bridges son infraestructura experimental y no garantías universales.

## Requiere evidencia externa antes de afirmarse “terminado”

- Auditoría de seguridad independiente y corrección de sus hallazgos.
- Benchmarks publicados con hardware, versiones, calentamiento y metodología fija.
- Adaptadores reales y pruebas de integración para cada versión soportada de Python,
  JVM, .NET, Node/V8 y ABI C++ en macOS, Linux y Windows.
- Fuzzing prolongado, pruebas de carga, política de vulnerabilidades y soporte LTS.
- Compatibilidad hacia atrás validada contra un corpus versionado de programas `.cfv`.

## Comandos de verificación

```sh
cforge check programa.cfv
cforge bytecode programa.cfv
cforge vm programa.cfv
cforge pkg init mi-proyecto
python3 -m pytest -q
```
