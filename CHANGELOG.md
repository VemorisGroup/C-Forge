# Changelog — C-Forge

Sitio oficial: [c-forge.org](https://c-forge.org)

## 3.2.0 — dominio oficial + concurrencia real + generics + compilación nativa

- Dominio oficial adquirido: **c-forge.org**
  - Sitio principal: `https://c-forge.org`
  - Documentación: `https://docs.c-forge.org`
  - Playground online: `https://play.c-forge.org`
  - Registro de paquetes: `https://pkg.c-forge.org`
- Concurrencia real con `std::thread`: `hilo_crear`, `hilo_esperar`, `hilo_id`, `hilo_activo`.
- Mutex reales: `mutex_crear`, `mutex_bloquear`, `mutex_desbloquear`, `mutex_intentar`.
- Canales thread-safe ya existentes integrados con el nuevo sistema de hilos.
- Soporte Unicode avanzado: bytes ≥ 0x80 aceptados en identificadores.
- Generics en el type checker (`cftype.py`): `Lista<T>`, `Mapa<K,V>`, inferencia de retorno.
- Pipeline de compilación nativa completo: `cf2c.py` + runtime C + `--compile` en el CLI.
- LSP 3.17 completo: references, rename, signature help, semantic tokens, code actions.
- Extensión VSCode 3.1.0 con LSP client y playground integrado.
- BDD testing stdlib (`describir/cuando/entonces`) con 15+ matchers y mocks.
- `cfbench` CLI para benchmarks: run/compare/report/watch.
- Playground online con 13 ejemplos y compartir por URL.
- Documentación completa en `docs.c-forge.org` con 30+ secciones.

## 2.6.0 — primer núcleo estable

- Eliminados de las dos fuentes C++ los puentes heredados de Python, Java/JNI y
  JavaScript.
- Motor y CLI unificados bajo el ejecutable `cforge`.
- Biblioteca activa reducida a 31 módulos `.cfv` que pasan sintaxis y carga.
- Suite nativa ampliada a 15 archivos `.cfv`.
- Parser JSON endurecido frente a entradas truncadas, tokens inválidos y datos
  sobrantes.
- Construcción oficial reducida de 292 avisos a cero en el entorno local.
- Gate reproducible para build, sintaxis, carga real de 31 módulos, pruebas,
  CLI, entradas dañadas, ASan/UBSan, emisores directos e instalación aislada.
- Empaquetado Homebrew y Debian migrado al motor nativo, sin Python.
- Flujos de CI y release renovados para compilar el motor nativo en macOS,
  Linux y Windows.
- Backends directos reclasificados correctamente como prototipos limitados a
  `mostrar("texto")`.
- Artefactos históricos, caches y módulos incompletos retirados del árbol
  activo de forma recuperable.
- Runtime, CLI, JSON y núcleo de biblioteca declarados estables; red, bases de
  datos, criptografía, gráficos, backends completos y autoalojamiento conservan
  explícitamente sus estados experimental, parcial o planeado.

Las versiones anteriores mezclaban prototipos y afirmaciones no respaldadas.
Su historial permanece disponible en Git, pero no define las capacidades del
producto actual. La fuente de verdad es `capabilities.json`.
