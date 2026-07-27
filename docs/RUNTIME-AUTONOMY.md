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

## B6.3: Mach-O ARM64 ejecutable en macOS

`bootstrap/direct/cforge_macho_arm64.cfv` implementa el objetivo nativo del Mac:

- cabecera Mach-O de 64 bits para ARM64;
- segmentos `__PAGEZERO`, `__TEXT` y `__LINKEDIT`;
- comandos de carga para `dyld`, `libSystem`, `LC_MAIN` y UUID determinista;
- instrucciones ARM64 que realizan `write` y `exit`;
- firma ad hoc embebida con CodeDirectory SHA-256;
- permisos de ejecución sin llamar a `chmod` externo.

La firma también se genera dentro del runtime C-Forge; no se ejecuta
`codesign`. La prueba bloquea compiladores, ensambladores, enlazadores,
`codesign` y Python durante la emisión, valida la firma con macOS y ejecuta el
resultado físicamente en Apple Silicon.

B6.3 sigue limitado al programa mínimo `mostrar("texto")`. El siguiente trabajo
es bajar el AST Core completo a instrucciones, añadir relocaciones internas y
crear el objetivo PE x64 para Windows.

## B6.4: control de flujo y aritmética Mach-O ARM64

`bootstrap/direct/cforge_macho_arm64_core.cfv` integra el lexer, parser y AST
Core con el emisor Mach-O. El ejecutable del backend, una vez construido por
Stage 0, reconoce y baja directamente a instrucciones ARM64:

- variables numéricas locales y asignaciones;
- suma, resta, multiplicación y división entera con signo;
- comparaciones `==`, `!=`, `<`, `<=`, `>` y `>=`;
- bloques `si`/`sino`;
- ciclos `mientras`;
- salida de literales mediante `mostrar`.

Las variables viven en un marco de pila de tamaño fijo cuya base se conserva en
`x19`; las expresiones usan la pila sin desplazar las direcciones de las
variables. Las ramas se resuelven en dos pasadas con etiquetas internas, y los
datos de texto se referencian mediante `ADR`.

La prueba física compila
`bootstrap/fixtures/machine_control_b6.cfv`, bloquea compiladores,
ensambladores, enlazadores, `codesign` y Python durante la emisión, valida la
firma Mach-O y ejecuta el resultado en macOS ARM64. B6.4 todavía no implementa
funciones de usuario, colecciones, texto dinámico ni llamadas al runtime.

## B6.5: PE32+ x86-64 ejecutable en Windows

`bootstrap/direct/cforge_pe_x64.cfv` implementa un emisor PE independiente:

- cabecera DOS y firma `PE\0\0`;
- cabecera opcional PE32+ para la arquitectura AMD64;
- secciones `.text`, `.rdata` y `.data`;
- tabla de importaciones e IAT para `KERNEL32.dll`;
- llamadas nativas a `GetStdHandle`, `WriteFile` y `ExitProcess`;
- código máquina x86-64 con la convención de llamadas de Windows;
- imagen determinista de 2 KiB sin marcas de tiempo variables.

La prueba local bloquea C/C++, ensambladores, enlazadores y Python durante la
emisión, compara dos archivos byte por byte y valida todas las cabeceras,
secciones, importaciones e instrucciones esenciales. El CI genera el mismo
artefacto en macOS y lo ejecuta físicamente en un runner Windows x64.

B6.5 es todavía un corte vertical mínimo: acepta `mostrar("texto")`. Variables,
control de flujo y el frontend Core completo deben incorporarse al objetivo PE
en una fase posterior.
