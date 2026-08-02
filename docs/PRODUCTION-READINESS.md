# Preparación para producción

C-Forge 2.6.0-dev es un **Developer Preview**. No está certificado para bancos,
salud, infraestructura crítica ni sistemas donde un fallo pueda causar daños.

## Verificado en el repositorio

- El motor C++20 se construye sin avisos con el gate oficial en macOS ARM64.
- El ejecutable interpreta `.cfv` sin Python, JVM, .NET ni Node.
- 31 módulos `.cfv` pasan análisis sintáctico y carga.
- 10 archivos de prueba nativos pasan localmente.
- La instalación aislada instala motor y biblioteca estándar y ejecuta una
  prueba real.
- Los emisores directos generan cabeceras Mach-O ARM64, ELF x64 y PE x64 para
  el caso literal documentado.
- La CI contiene trabajos de compilación para macOS, Linux y Windows.

## No verificado o no terminado

- Cobertura funcional exhaustiva de los 31 módulos.
- Toolchain autoalojada reproducible Stage 2/3.
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
```

Un resultado verde solo respalda el alcance anterior. No debe interpretarse
como una certificación de seguridad o preparación bancaria.
