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

## B6.2: primer backend de código máquina directo

`bootstrap/direct/cforge_elf_x64.cfv` implementa en C-Forge un emisor ELF64 para
x86-64. Construye directamente:

- la cabecera ELF;
- una tabla de un segmento ejecutable;
- opcodes x86-64 para `write` y `exit`;
- los datos del literal de `mostrar`;
- los permisos ejecutables del archivo final.

Durante la emisión, la prueba bloquea `clang++`, `clang`, GCC, `cc`, `ld`, `as`
y Python. El resultado se valida byte por byte y se ejecuta cuando la prueba
corre sobre Linux x86-64.

Este es el primer corte vertical de compilación directa. Todavía no sustituye
el backend general: solo acepta el programa mínimo `mostrar("texto")` y el
formato inicial es ELF64/x86-64. Mach-O ARM64, PE x64, expresiones, variables,
control de flujo y el runtime completo permanecen pendientes.
