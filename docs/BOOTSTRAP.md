# C-Forge Core Bootstrap

## Objetivo

El objetivo del bootstrap es que el compilador de C-Forge llegue a compilar sus
propias fuentes. C++20 se acepta únicamente como Stage 0: el punto de arranque
para construir el primer motor.

## Fuentes conservadas

El árbol activo conserva Stage 0 y tres emisores directos mínimos. Las fuentes
anteriores de lexer, parser, semántica, runtime, driver y Stage 1 en `.cfv` se
retiraron porque no eran aceptadas por el parser actual. Mantener archivos que
no compilan habría contradicho la política de completitud.

## Estado por etapa

| Etapa | Estado verificable |
|---|---|
| Stage 0 | Bootstrap C++20 disponible en `bootstrap/stage0/` |
| Stage 1 | Pendiente de reimplementación incremental en `.cfv` |
| Stage 2/3 | No forman parte del gate reproducible 2.6 |
| Runtime autónomo | El binario ejecuta `.cfv` sin Python/JVM/.NET/Node |
| Toolchain autónoma | Pendiente |
| Backends directos | Prototipos literales; consulta `RUNTIME-AUTONOMY.md` |

No se usará la palabra **autoalojado** para una versión publicada hasta que el
repositorio reconstruya Stage 2 y Stage 3 en CI, compare sus artefactos byte por
byte y repita la prueba en las plataformas soportadas.

## Gate actual

```sh
make clean
make build
make check
make test
make backend-check
make install-check
```

Este gate no utiliza Python. Comprueba el motor activo, los módulos `.cfv`, las
pruebas nativas, la estructura de los tres prototipos binarios y una instalación
aislada. Sus límites forman parte de la evidencia: pasar el gate no convierte
los prototipos directos en backends generales.

## Siguiente criterio de avance

Antes de reactivar Stage 2/3 se necesita una prueba escrita en C-Forge que:

1. construya Stage 1 usando Stage 0;
2. use Stage 1 para producir Stage 2;
3. use Stage 2 para producir Stage 3;
4. compare Stage 2 y Stage 3 de forma reproducible;
5. falle de forma limpia si aparece una dependencia externa no autorizada.

Hasta superar ese gate, C-Forge permanece en **Developer Preview**.
