# Estado profesional de C-Forge

Este documento distingue funciones verificables de objetivos futuros. C-Forge no
se presenta como más seguro o rápido que otros lenguajes sin mediciones y auditorías
independientes reproducibles.

## Paquetes firmados

El índice formato 2 y el cliente implementan verificación Ed25519, identidad por
huella SHA-256 y revocación por clave o versión. Las publicaciones siguen entrando
por pull request y revisión humana. No se declara todavía un servicio público de
cuentas, alta disponibilidad del registro ni recuperación de cuenta.

## Implementado y comprobable

- Análisis de memoria fase 2: movimientos, préstamos compartidos/mutables,
  conflictos de alias, regiones, frontera `unsafe` explícita y tipo `opcion`
  sin valores nulos implícitos. Los efectos de movimiento y préstamo se resumen
  entre funciones; también se rechazan préstamos locales escapados y ciclos
  indirectos de ownership.
- Firmas tipadas de funciones con comprobación de argumentos y retornos.
- Bytecode `.cfb` 1.1 determinista, versionado y protegido por SHA-256; el
  cargador limita tamaño y anidamiento, valida estructura, opcodes, operandos
  y saltos antes de ejecutar, y conserva líneas de fuente para depuración.
- Adaptador DAP inicial con breakpoints por línea `.cfv`, pasos, pila, scopes e
  inspección de variables, integrado con la extensión de VS Code.
- VM con estructuras, clases, métodos, mutación segura de campos y módulos
  resueltos antes de crear el bytecode.
- Sandbox inicial de VM: las rutas quedan confinadas al proyecto y procesos o
  red requieren permisos explícitos en `CFORGE_PERMISSIONS`.
- Concurrencia nativa mediante `tarea`, `esperar`, `cancelar`, `canal`,
  `enviar`, `recibir` y `cerrar_canal`, disponible en intérprete, VM y C++.
- Funciones `async` y operador `await` con comprobación estática y persistencia
  dentro del formato de bytecode.
- Tuplas inmutables heterogéneas y conjuntos homogéneos sin duplicados en el
  intérprete, analizador estático, bytecode 1.1 y VM.
- Backend LLVM con escalares numéricos, booleanos, textos, listas numéricas,
  mapas homogéneos tipados, tuplas escalares heterogéneas, conjuntos escalares
  homogéneos, estructuras y clases; incluye funciones tipadas,
  resolución de módulos, genéricos monomorfizados, concatenación, comparación,
  indexación, crecimiento dinámico, layouts nominales, campos, métodos y
  liberación explícita. Los destructores nominales LLVM recorren campos propios
  y liberan transitivamente listas, mapas, tuplas, conjuntos y objetos. Colecciones
  anidadas e interfaces dinámicas continúan pendientes. `intentar/capturar` ya
  baja errores aritméticos comprobados a ramas LLVM y los errores no capturados
  terminan con diagnóstico C-Forge; el unwinding general entre ABI sigue pendiente.
- Lexer, parser, intérprete, backend C++ experimental y backend WebAssembly `.wat`.
- Backend LLVM IR textual real para el núcleo numérico, con compilación mediante Clang.
- Frontera LLVM `extern_c funcion` con firmas verificadas para números,
  booleanos y textos, vistas prestadas zero-copy de `lista<numero>` y resultados
  numéricos propietarios seguros; enlace explícito mediante `--vincular`.
- Contrato `extern_c segura funcion` con estado, salida y error UTF-8; los
  errores nativos entran al manejador C-Forge o terminan con diagnóstico propio.
  Los textos propietarios incluyen longitud, validación de NUL y liberación RAII.
- ABI dinámico V2 coexistente con V1, con longitud explícita y negociación de
  versión. El adaptador C/C++ ya transporta listas, mapas y registros nominales
  recursivos con préstamo y ownership de raíz comprobados.
- Compilador de bytecode y VM de pila propios (`cforge vm` y `cforge bytecode`).
- Inferencia y comprobación estática gradual, con diagnósticos `CFxxxx` (`cforge check`).
- Servidor LSP 3.17 por entrada/salida estándar con diagnósticos, autocompletado, hover,
  símbolos, definiciones, referencias, renombrado y formateo
  (`cforge lsp`).
- El índice LSP comparte símbolos entre documentos abiertos y presenta firmas
  tipadas de funciones, variables, `opcion<T>` y declaraciones C ABI.
- La extensión activa el LSP real por stdio. El DAP anuncia y aplica condiciones,
  conteos de impacto y logpoints sin permitir llamadas ni mutaciones al evaluar.
- Depurador de bytecode inicial con traza, variables y breakpoints por offset, accesible
  también desde la extensión (`cforge debug`).
- Gestor reproducible con manifiesto, lock SHA-256, construcción, búsqueda e instalación
  segura desde un índice público (`cforge pkg`).
- Paquetes y archivos portables reproducibles byte por byte mediante
  normalización de tiempos, orden, propietarios, permisos y cabeceras.
- Puerta `cforge parity` para exigir salida y terminación idénticas en
  intérprete, VM y LLVM sobre cada característica declarada común.
- REPL, formateador, pruebas, empaquetado multiplataforma y CI.
- Contrato C ABI y Forge Shared Arena con offsets, cabeceras validadas y sincronización.

La fase de memoria todavía no es un verificador completo de tiempos de vida.
Para C-Forge 2.0 faltan el análisis de alias sensible a caminos, tiempos de vida
genéricos, destrucción determinista equivalente en todos los backends y una
semántica formal publicada para la frontera `unsafe`.

## Experimental o de alcance limitado

- La VM ejecuta el núcleo seguro del lenguaje; los bloques extranjeros y algunas
  construcciones avanzadas continúan en el intérprete principal.
- LLVM cubre escalares, textos, listas numéricas, mapas escalares homogéneos,
  tuplas y conjuntos escalares, objetos, módulos y genéricos básicos.
  Colecciones anidadas, excepciones provenientes de llamadas externas y FFI
  para ForgeValue/objetos todavía deben bajarse por completo. El subconjunto C
  ABI escalar ya funciona, pero no equivale a seis adaptadores completos.
- LLVM IR 1.4 incluye `opcion<T>` escalar etiquetada, impresión, consulta,
  desenvoltura comprobada, propagación hacia `capturar` y liberación explícita.
- El protocolo de registro remoto verifica HTTPS, límites, rutas y SHA-256. El índice
  público comienza vacío; todavía requiere operación, moderación, firmas y revocación.
- El adaptador DAP y la integración visual inicial ya existen; faltan depuración
  remota, condiciones avanzadas y evaluación completa de expresiones.
- GPU, JIT, cluster y bridges son infraestructura experimental y no garantías universales.

## Requiere evidencia externa antes de afirmarse “terminado”

- Auditoría de seguridad independiente y corrección de sus hallazgos.
- Benchmarks publicados con hardware, versiones, calentamiento y metodología fija.
- Adaptadores reales y pruebas de integración para cada versión soportada de Python,
  JVM, .NET, Node/V8 y ABI C++ en macOS, Linux y Windows.
- Fuzzing prolongado, pruebas de carga, política de vulnerabilidades y soporte LTS.
- Compatibilidad hacia atrás validada contra un corpus versionado de programas `.cfv`.

La matriz exigida por sistema se mantiene en
[`PLATFORM-VALIDATION.md`](PLATFORM-VALIDATION.md). El pliego para terceros está
en [`EXTERNAL-AUDIT-SCOPE.md`](EXTERNAL-AUDIT-SCOPE.md). Ninguno equivale a una
certificación realizada.

## Comandos de verificación

```sh
cforge check programa.cfv
cforge bytecode programa.cfv
cforge vm programa.cfv
cforge parity programa.cfv
cforge pkg init mi-proyecto
python3 -m pytest -q
```
