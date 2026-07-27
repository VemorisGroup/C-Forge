# C-Forge Runtime Autonomy

## Regla

Un programa nativo producido por C-Forge Core debe iniciar y ejecutar sus
operaciones Core sin Python, JVM, .NET, Node ni otro runtime de lenguaje.
Las únicas bibliotecas dinámicas permitidas en esta fase son las bibliotecas
del sistema operativo y la biblioteca C++ incorporada al artefacto de
bootstrap.

## B6.1: ejecución sin Python

La primera fase de B6 cubre:

- valores `numero`, `booleano`, `texto`, listas y objetos Core;
- aritmética, comparaciones y control de flujo;
- argumentos del proceso;
- lectura, escritura y eliminación de archivos;
- errores del runtime C-Forge;
- ejecución con los comandos `python` y `python3` deliberadamente bloqueados;
- inspección del ejecutable para rechazar enlaces a Python.

La prueba normativa es `tests/test_bootstrap_b6.py` y el programa utilizado es
`bootstrap/fixtures/runtime_b6.cfv`.

## Límites todavía abiertos

B6 no se considerará completo mientras el flujo normal de compilación necesite
`clang++`, `codesign` o un enlazador extranjero. También faltan una biblioteca
estándar autónoma más amplia, el enlazador/ensamblador propio y el gestor de
paquetes ejecutable sin runtimes externos.

Por tanto, superar B6.1 demuestra **autonomía de ejecución del runtime Core**,
pero no autonomía completa de la toolchain.
