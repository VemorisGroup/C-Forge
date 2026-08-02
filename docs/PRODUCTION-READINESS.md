# Preparación para producción

C-Forge 2.6.0 posee un **núcleo estable** para aplicaciones generales. No está
certificado para bancos, salud, infraestructura crítica ni sistemas donde un
fallo pueda causar daños. Estabilidad técnica y certificación regulatoria son
compromisos distintos.

## Verificado en el repositorio

- El motor C++20 se construye sin avisos con el gate oficial en macOS ARM64.
- El ejecutable interpreta `.cfv` sin Python, JVM, .NET ni Node.
- 30 módulos `.cfv` pasan análisis sintáctico y carga real.
- 20 archivos de prueba nativos pasan localmente y bajo ASan/UBSan.
- Cinco clases de entradas dañadas se rechazan sin abortar el proceso.
- El contrato del CLI se comprueba automáticamente.
- La instalación aislada instala motor y biblioteca estándar y ejecuta una
  prueba real.
- Los emisores directos mínimos generan Mach-O ARM64, ELF x64 y PE x64. Las
  variantes Mach-O/PE Core B6.7 cubren además variables, aritmética, control,
  funciones, listas y texto dinámico sin toolchain durante la emisión.
- La CI contiene trabajos de compilación para macOS, Linux y Windows.

## No verificado o no terminado

- Cobertura funcional exhaustiva de los módulos experimentales.
- Toolchain completamente autónoma: Stage 2/3 ya es reproducible para el
  frontend Core, pero la generación final todavía depende de `clang++`.
- Backends directos para el lenguaje completo.
- Sandboxing fuerte de archivos, procesos y red.
- Modelo formal y comprobado de ownership para toda la sintaxis.
- Instaladores firmados, procedencia de artefactos y política LTS.
- Pruebas físicas completas en macOS/Linux/Windows ARM64 y x64.
- Fuzzing prolongado, pruebas de carga y recuperación ante fallos.
- Auditoría externa profesional y cumplimiento regulatorio.

## Gate local

```sh
make clean
make build
make check
make test
make backend-check
make install-check
make release-check
```

Un resultado verde respalda el contrato estable descrito en
`capabilities.json`. No debe interpretarse como una certificación regulatoria o
preparación bancaria.
