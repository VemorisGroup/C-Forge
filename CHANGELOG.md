# Changelog — C-Forge

## 2.6.0-dev — limpieza del núcleo

- Eliminados de las dos fuentes C++ los puentes heredados de Python, Java/JNI y
  JavaScript.
- Motor y CLI unificados bajo el ejecutable `cforge`.
- Biblioteca activa reducida a 31 módulos `.cfv` que pasan sintaxis y carga.
- Suite nativa ampliada de 4 a 10 archivos `.cfv`.
- Parser JSON endurecido frente a entradas truncadas, tokens inválidos y datos
  sobrantes.
- Construcción oficial reducida de 292 avisos a cero en el entorno local.
- Gate reproducible para build, sintaxis, pruebas, emisores directos e
  instalación aislada.
- Empaquetado Homebrew y Debian migrado al motor nativo, sin Python.
- Flujos de CI y release renovados para compilar el motor nativo en macOS,
  Linux y Windows.
- Backends directos reclasificados correctamente como prototipos limitados a
  `mostrar("texto")`.
- Artefactos históricos, caches y módulos incompletos retirados del árbol
  activo de forma recuperable.

Las versiones anteriores mezclaban prototipos y afirmaciones no respaldadas.
Su historial permanece disponible en Git, pero no define las capacidades del
producto actual. La fuente de verdad es `capabilities.json`.
