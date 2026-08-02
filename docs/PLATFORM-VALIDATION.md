# Matriz de validación de plataformas

| Sistema | Arquitectura | Gate declarado | Prueba física | Estado |
|---|---:|---:|---:|---|
| macOS | ARM64 | sí | motor, instalación y Mach-O literal comprobados localmente | preview verificada |
| macOS | x64 | sí | pendiente | no validada |
| Linux | x64 | sí | pendiente de confirmar CI verde del commit | parcial |
| Linux | ARM64 | no | pendiente | no validada |
| Windows | x64 | sí | pendiente de confirmar CI verde del commit | parcial |
| Windows | ARM64 | no | pendiente | no validada |

“Gate declarado” significa que existe un trabajo en GitHub Actions; no asegura
por sí mismo que esté verde. Una plataforma solo se anunciará como soportada
después de conservar logs de instalación limpia, ejecución, pruebas,
desinstalación y hashes de sus artefactos.

El backend PE se inspecciona por formato en macOS, pero aún no se ha ejecutado
físicamente en Windows durante esta revisión. Lo mismo aplica al ELF fuera de
un anfitrión Linux.
