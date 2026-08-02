# Puertas de capacidad de C-Forge

Una capacidad solo pasa de **experimental** a **estable** cuando cumple evidencia
reproducible. Este archivo sustituye promesas absolutas por criterios técnicos.

## Rendimiento y FFI

- No se usa “zero-cost” si existe conversión, copia, proceso externo o cambio de runtime.
- Cada adaptador debe publicar latencia, throughput, asignaciones y tamaño de entrada.
- “Más rápido que C++” solo puede afirmarse para un benchmark concreto, compiladores,
  flags, hardware y margen estadístico publicados.

## Memoria

- Ninguna ruta segura puede permitir use-after-free, double-free, acceso fuera de límites
  o data races en el corpus y fuzzing acordados.
- El ownership, préstamos, regiones, `Option` y `unsafe` deben formar parte de una
  especificación formal antes de prometer seguridad sin GC.
- Los runtimes extranjeros no forman parte del núcleo activo de C-Forge.

## LLVM y plataformas

- Un backend es real cuando genera LLVM IR aceptado por LLVM/Clang y ejecuta el resultado.
- Cada función del lenguaje debe tener prueba de paridad intérprete/VM/LLVM.
- `cforge parity archivo.cfv` es la puerta ejecutable de paridad; una capacidad
  común no se documenta como tal si esta orden no termina correctamente.
- macOS, Linux y Windows requieren CI verde y pruebas físicas de instalación y FFI.

## Producción crítica

- Auditoría externa cerrada, modelo de amenazas y revisión de correcciones.
- Versiones LTS, política de vulnerabilidades, builds reproducibles y artefactos firmados.
- Recuperación, observabilidad, pruebas de carga y compatibilidad hacia atrás.

Las capacidades que no cumplen su puerta permanecen marcadas como
`experimental`, `partial`, `planned` o `not-certified`; esto no rebaja el
contrato del núcleo estable.
