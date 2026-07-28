# Matriz de validación de plataformas

C-Forge solo considera una plataforma soportada cuando existe un informe
reproducible con versión del sistema, arquitectura, compilador, runtimes,
instalación limpia, desinstalación, suite, fuzzing, paridad y FFI.

| Sistema | Arquitectura | CI | Prueba física | FFI seis runtimes | Estado |
|---|---:|---:|---:|---:|---|
| macOS | ARM64 | configurada | núcleo local comprobado | incompleta | experimental |
| macOS | x64 | configurada | pendiente | pendiente | no validada |
| Linux | ARM64 | pendiente | pendiente | pendiente | no validada |
| Linux | x64 | configurada | pendiente | pendiente | no validada |
| Windows | ARM64 | pendiente | pendiente | pendiente | no validada |
| Windows | x64 | configurada | pendiente | pendiente | no validada |

“CI configurada” no significa “CI verde” ni sustituye una prueba física.

## Evidencia mínima por plataforma

1. Instalación en una máquina o VM limpia.
2. `cforge --version`, `check`, `vm`, `bytecode` y `parity`.
3. Compilación LLVM y ejecución del binario.
4. Puentes C/C++, Python, JVM, .NET, Node y TypeScript.
5. Suite completa y al menos 20.000 casos de fuzzing.
6. Paquete reproducible, SHA-256 y firma verificada.
7. Desinstalación sin archivos huérfanos ni modificación inesperada del sistema.

Los resultados deben adjuntar logs, hashes y fecha. Una fila solo pasa a
“soportada” después de revisar esa evidencia.
