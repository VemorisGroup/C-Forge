# C-Forge Core Bootstrap

## Objetivo

El objetivo del bootstrap es que el compilador de C-Forge llegue a compilar sus
propias fuentes. C++20 se acepta únicamente como Stage 0: el punto de arranque
para construir el primer motor.

## Fuentes verificadas

El árbol activo conserva Stage 0 y el frontend de C-Forge Core escrito en
`.cfv`: lexer, AST, parser, análisis semántico, verificación básica de
movimientos, emisor, runtime y controlador Stage 1. El gate `bootstrap-check`
construye y ejecuta esas fuentes; no basta con analizarlas como texto.

## Estado por etapa

| Etapa | Estado verificable |
|---|---|
| Stage 0 | Bootstrap C++20 disponible en `bootstrap/stage0/` |
| Stage 1 | Frontend Core escrito en `.cfv` y construido por Stage 0 |
| Stage 2/3 | Reconstrucción reproducible y comparación byte por byte |
| Runtime autónomo | El binario ejecuta `.cfv` sin Python/JVM/.NET/Node |
| Toolchain autónoma | Parcial: el frontend se autoaloja; el enlazado final todavía usa `clang++` |
| Backends directos | Mach-O ARM64, ELF x64 y PE x64 Core B6.8 parciales; consulta `RUNTIME-AUTONOMY.md` |
| Objetos nativos | Core IR 1 y Object Lowering 1 alimentan Mach-O, ELF y PE con constructores de valor, lectura y escritura de campos | verificado B6.11 |
| Métodos nativos | Métodos de instancia de solo lectura y `este` se reducen a funciones nativas explícitas en los tres formatos | verificado B6.12 |
| Métodos mutables | Las llamadas de efecto expanden parámetros y escrituras de `este.campo` sobre la instancia receptora | verificado B6.13 |
| Ciclo de vida | `crear` inicializa todos los campos y `destruir` se ejecuta automáticamente en orden inverso para instancias superiores | verificado B6.14 |
| Interfaces | `interfaz`/`implementa` validan métodos, retornos y parámetros antes de emitir los tres formatos | verificado B6.15 |

El término **frontend Core autoalojado** solo describe el alcance probado por
`make bootstrap-check`. No significa que toda la toolchain sea autónoma: el
emisor produce C++17 y el runtime Stage 1 llama a `clang++` para crear el
ejecutable final.

## Gate actual

```sh
make clean
make build
make check
make test
make stdlib-load-check
make malformed-check
make sanitize-check
make backend-check
make install-check
make bootstrap-check
```

Este gate no utiliza Python. Comprueba el motor activo, los módulos `.cfv`, las
pruebas nativas, los backends mínimos, los tres backends Core B6.8 y una instalación
aislada. Sus límites forman parte de la evidencia: pasar el gate no convierte
los backends parciales en backends generales.

## Siguiente criterio de avance

La siguiente etapa debe sustituir la llamada a `clang++` por los emisores
Mach-O, ELF y PE propios, y ampliar esos emisores hasta cubrir todo Core. Solo
entonces podrá declararse autónoma la toolchain completa. El runtime y el
lenguaje 2.6 mantienen un contrato estable independiente de este avance.
