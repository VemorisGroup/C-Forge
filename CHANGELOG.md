# Changelog — C-Forge

## [3.0.0] — 2026-07-29

### 🚀 Enterprise-Grade — Better than Java, C++, C#

### Nuevas funcionalidades del intérprete
- **Anotaciones de tipo**: `sea x: Numero = 5`, `sea items: Lista<Texto> = []`
- **Clases abstractas**: `abstracto clase Animal { ... }`
- **Interfaces**: `interfaz IServicio { metodo(args) }` + `implementa`
- **Modificadores de acceso**: `publico`, `privado`, `protegido`

### Stdlib — Videojuegos (15 módulos nuevos)
- `ecs` — Entity Component System completo con sistemas built-in
- `fisica2d` — Motor de física 2D (AABB, círculos, raycasting, springs)
- `fisica3d` — Motor de física 3D (quaterniones, matrices 4x4, esferas)
- `escena` — Scene manager, cámara 2D/3D, UI, transiciones
- `particulas` — Sistema de partículas (fuego, explosión, lluvia, magia, nieve)
- `input` — Teclado, ratón, gamepad, gestos táctiles, mapeo de acciones
- `audio` — Sistema de audio 3D, efectos, música, crossfade, pool
- `animacion` — Keyframes, easing, tweening, sprite sheets, animador state machine
- `assets` — Atlas de texturas, lazy loading, cache LRU, paquetes
- `colision` — AABB, círculos, polígonos, raycasting, Quadtree, capas

### Stdlib — Machine Learning / IA (5 módulos nuevos)
- `tensor` — Tensores N-dimensionales, activaciones, operaciones matriciales
- `red_neuronal` — Deep learning: Dense, Dropout, BatchNorm, Adam, SGD, RMSProp
- `ml` — Regresión lineal/logística, KNN, K-Means, Árbol de decisión, Random Forest
- `nlp` — Tokenización, TF-IDF, sentimiento, Levenshtein, resumen automático
- `datos` — DataFrame: filtrar, groupby, join, describe, correlación, CSV I/O

### Stdlib — Enterprise (8 módulos previos)
- `auth`, `redis`, `metricas`, `microservicio`, `graphql`, `cola_mensajes`, `auditoria`, `transacciones`

### Herramientas nuevas
- `cftype` — Type checker estático para C-Forge
- `cfcover` — Code coverage con reporte HTML
- `cfprofile` — Profiler con detección de hotspots
- `cfmigrate` — Gestor de migraciones de base de datos
- `cfgen` — Scaffolding: proyecto, clase, api, cli, videojuego, ml

### Ejemplos completos
- `ejemplos/banco/sistema_bancario.cfv` — Sistema bancario con SAGA
- `ejemplos/ml/clasificador.cfv` — Pipeline ML: NN + Árbol + Random Forest
- `ejemplos/cli/cforgecli.cfv` — Herramienta CLI con tabla, colores, barra de progreso
- `ejemplos/microservicio/arquitectura.cfv` — Arquitectura distribuida completa

### Cifras
- **73** módulos de stdlib
- **17** herramientas Python
- **66** ejemplos
- **~15,000** líneas de código nuevo en esta versión


## 2.5.1 — 2026-07-28

### Correcciones y mejoras del intérprete

**Resolución de rutas (`cforgev.cpp`):**
- Corregido bug crítico: `argv[1]` ahora se canonicaliza con `std::filesystem::weakly_canonical` antes de construir `cfv_base_archivos`, evitando doble prefijo en rutas relativas.
- Scripts pueden ejecutarse desde cualquier directorio sin error de "archivo no encontrado".

**Constructores de clase:**
- El método `constructor` de una clase ahora se invoca automáticamente al instanciar con `NombreClase(args...)`.
- `esto` correctamente enlazado dentro del constructor para acceder y modificar campos.

**Closures en funciones nombradas:**
- Funciones declaradas con `funcion nombre(...)` ahora capturan su entorno de definición igual que las lambdas.
- Permite que módulos stdlib accedan a constantes top-level (`PI`, `E`, `PHI`) desde funciones internas como `seno()`, `coseno()`.

**Builtins de orden superior (HOF):**
- `mapear(lista, fn)` — transforma cada elemento con fn.
- `filtrar(lista, pred)` — retorna elementos donde pred es verdadero.
- `reducir(lista, fn, inicial)` — fold/reduce con acumulador.
- `para_cada(lista, fn)` — forEach sin retorno.
- `ordenar(lista)` / `ordenar(lista, comp)` — ordena con comparador opcional.
- `mapa_claves(mapa)` — alias de `claves()`.
- `mapa_valores(mapa)` — retorna lista de valores.
- `aleatorio()` — número decimal en [0, 1).

**Stdlib:**
- `stdlib/numero.cfv`: corregido `NAN = 0/0` → `NAN = 0` (división por cero en importación).
- `stdlib/algoritmos.cfv`: corregido `mergesort` con loop de índices, `busqueda_binaria` con comparador opcional por defecto, `agrupar_por` usa `a_texto()`, `grafo_bfs` con cola basada en offset, `grafo_dfs` usa `tiene_clave()`.

### Nuevos archivos

**Empaquetado:**
- `pyproject.toml` — `pip install .` instala cfmt, cflint, cftest, cfdoc, cfwatch, cforgec, cforge en PATH.
- `Dockerfile` + `.dockerignore` — imagen multi-stage Ubuntu 24.04 con cforgev + tools + stdlib.

**Ejemplos verificados (10 archivos):**
- `ejemplos/01_hola.cfv` — Hola mundo y variables.
- `ejemplos/02_tipos.cfv` — Tipos de datos, listas, mapas.
- `ejemplos/03_funciones.cfv` — Funciones, closures, HOF.
- `ejemplos/04_clases.cfv` — OOP, constructores, herencia.
- `ejemplos/05_manejo_errores.cfv` — try/catch/lanzar.
- `ejemplos/06_stdlib_texto.cfv` — Módulo texto de stdlib.
- `ejemplos/07_stdlib_lista.cfv` — Módulo lista + builtins HOF.
- `ejemplos/08_concurrencia.cfv` — Canales y concurrencia.
- `ejemplos/09_stdlib_algoritmos.cfv` — quicksort, mergesort, grafos.
- `ejemplos/10_stdlib_numero.cfv` — Estadísticas, trigonometría, formateo.

Todos los ejemplos verificados con `CFORGE_STDLIB=./stdlib ./cforgev ejemplos/XX_*.cfv`.

---

## 2.4.0 — 2026-07-28

### Herramientas de desarrollo completas

**Formatter — `tools/cfmt.py`:**
- Formateador canónico de código C-Forge
- `cfmt archivo.cfv` — formatea en lugar
- `cfmt --check` — verifica sin modificar (exit 1 si hay cambios)
- `cfmt --stdout` — imprime a stdout
- Soporta directorios y globs (`cfmt stdlib/`)

**Linter — `tools/cflint.py`:**
- Análisis estático con 60+ reglas organizadas por categoría
- CF001-CF009: Variables no usadas, redeclaraciones
- CF010-CF019: Funciones no llamadas, parámetros excesivos, funciones largas
- CF020-CF029: `var`/`let`/`const`, `console.log`, `print()`, `return` en lugar de `retornar`
- CF030-CF039: Líneas largas, mezcla tabs/espacios, TODO/FIXME
- CF040-CF049: Anidamiento excesivo (complejidad ciclomática)
- CF050-CF059: Builtins deprecados
- CF060-CF069: Credenciales hardcodeadas, SQL injection hints
- `--strict` convierte warnings en errores, `--json` para integración CI/CD

**Transpilador AOT — `tools/cforgec.py`:**
- Compila C-Forge → C99 nativo vía transpilación
- Tokenizador, parser y generador de código C completos
- `cforgec archivo.cfv --compile` — genera y compila con gcc
- `cforgec archivo.cfv --run` — compila y ejecuta directamente
- `cforgec --ir archivo.cfv` — imprime AST
- Runtime C mínimo incluido (CfvValue, tipos, mostrar, cfv_truthy)

**Package Registry — `tools/pkg_registry.py` + `cfpkg`:**
- Servidor HTTP del registro de paquetes (estilo npm)
- `cfpkg serve --port 7373` — iniciar el servidor
- `cfpkg publish ./mi-paquete/` — publicar leyendo cforge.toml
- `cfpkg install nombre@1.0.0` — instalar en `cforge_modules/`
- `cfpkg search texto` — buscar paquetes
- `cfpkg info nombre` — ver metadata completa
- Almacenamiento en archivos `.cfpkg` (tarballs gz)

**Debugger DAP — `tools/dap_server.py`:**
- Servidor Debug Adapter Protocol (Microsoft DAP spec)
- Compatible con VS Code, Neovim DAP, cualquier cliente DAP
- `python3 dap_server.py --stdio` — para VS Code launch config
- `python3 dap_server.py --port 4711` — modo TCP para debug remoto
- Soporta: breakpoints, step in/over/out, stack frames, variables, evaluate
- Modo simulación cuando el intérprete no está disponible

### Nuevos módulos stdlib

- `stdlib/hilos.cfv` — Concurrencia de alto nivel
  - `hilo_lanzar`, `pool_crear/enviar/esperar_todo`
  - Semáforos, promesas, `promesa_todo` (Promise.all)
  - Workers con canal entrada/salida
- `stdlib/fecha.cfv` — Fechas y tiempos completos
  - Parsear ISO 8601, DD/MM/YYYY, formatear con patrones
  - Aritmética: agregar días/meses/años/horas
  - `tiempo_relativo()` — "hace 3 días", "en 2 horas"
  - Rangos de fechas, semana del año, año bisiesto

### Mejoras de base de datos

**PostgreSQL nativo (`#ifdef CFV_WITH_PGSQL`):**
- `pg_conectar(conn_str)`, `pg_cerrar(id)`, `pg_query(id, sql)`, `pg_exec(id, sql)`, `pg_escapar(id, str)`
- Pool de conexiones interno

**MySQL nativo (`#ifdef CFV_WITH_MYSQL`):**
- `mysql_conectar(conf)`, `mysql_cerrar(id)`, `mysql_query(id, sql)`, `mysql_escapar(id, str)`

**WebSocket RFC 6455 nativo (sin deps):**
- SHA-1 implementado desde cero (FIPS 180-4)
- `ws_escuchar`, `ws_aceptar`, `ws_recibir`, `ws_enviar`, `ws_broadcast`, `ws_cerrar`

---

## 2.3.1 — 2026-07-28

### Nuevas funciones nativas (builtins v2.3b)

**Lista avanzada:**
- `lista_slice(lista, desde, hasta)` — sublista por índices
- `lista_buscar(lista, valor)` — índice de la primera ocurrencia (o -1)
- `lista_contiene(lista, valor)` — booleano
- `lista_contar_elem(lista, valor)` — contar ocurrencias de un valor
- `lista_invertir(lista)` — nueva lista invertida
- `lista_rellenar(n, valor)` — crear lista de N copias del valor
- `lista_cada_n(lista, n)` — tomar cada N-ésimo elemento

**Texto:**
- `texto_es_numero(texto)` — verdadero si el texto es un número válido
- `texto_unir(lista, sep)` — unir lista con separador (alias de join)

**Número:**
- `numero_es_entero(n)` — verdadero si n no tiene parte decimal
- `numero_es_nan(n)` — verdadero si n es NaN

**Rango:**
- `rango_paso(desde, hasta, paso)` — rango con paso personalizado, soporta negativo

**Mapa:**
- `mapa_filtrar_claves(mapa, lista_claves)` — conservar solo las claves indicadas
- `mapa_omitir_claves(mapa, lista_claves)` — excluir las claves indicadas

### Nueva stdlib (31 módulos)

- `stdlib/config.cfv` — parser INI/JSON, variables de entorno, validación
- `stdlib/cache.cfv` — LRU cache, TTL cache, memoización, cache de disco
- `stdlib/router.cfv` — router HTTP con parámetros, middleware, grupos, CORS

### Nuevos ejemplos

- `ejemplos/servidor_archivos.cfv` — servidor de archivos estáticos con listado HTML
- `ejemplos/scraper.cfv` — web scraper: extracción de texto, links, emails, regex

### Herramientas

- `vscode-extension/` — extensión VSCode completa: resaltado, snippets, hover docs, completions, formateador, diagnósticos, botón de ejecución

---

## 2.3.0 — 2026-07-28

### Nuevas funciones nativas (builtins)

**Archivo y sistema de archivos:**
- `archivo_copiar(src, dst)` — copiar archivo
- `archivo_mover(src, dst)` — mover/renombrar archivo
- `archivo_eliminar(ruta)` — eliminar archivo
- `archivo_tam(ruta)` — tamaño en bytes
- `directorio_crear(ruta)` — crear directorio (recursivo)
- `directorio_eliminar(ruta)` — eliminar directorio y contenido
- `directorio_listar(dir)` — listar entradas del directorio
- `directorio_listar_rec(dir)` — listar recursivamente
- `es_directorio(ruta)` — booleano
- `es_archivo_regular(ruta)` — booleano

**Rutas:**
- `ruta_unir(lista)` o `ruta_unir(a,b,c)` — unir segmentos de ruta
- `ruta_directorio(ruta)` — directorio padre
- `ruta_nombre(ruta)` — nombre del archivo
- `ruta_extension(ruta)` — extensión (.cfv)
- `ruta_sin_extension(ruta)` — nombre sin extensión
- `ruta_absoluta(ruta)` — ruta absoluta

**Texto avanzado:**
- `texto_repetir(texto, n)` — repetir texto N veces
- `texto_invertir(texto)` — invertir string
- `texto_contar(texto, sub)` — contar ocurrencias
- `texto_posiciones(texto, sub)` — posiciones de todas las ocurrencias

**Números:**
- `numero_formato(n, decimales)` — formato con N decimales fijos
- `numero_abs(n)` — valor absoluto

**Colecciones:**
- `lista_max(lista)` — máximo de una lista numérica
- `lista_min(lista)` — mínimo de una lista numérica
- `lista_suma(lista)` — suma de una lista numérica
- `lista_promedio(lista)` — promedio de una lista numérica

### Nueva stdlib (28 módulos)

- `stdlib/archivo.cfv` — operaciones avanzadas de archivo/directorio
- `stdlib/cli.cfv` — herramientas CLI (colores, tabla, progreso, menu)
- `stdlib/matematica_avanzada.cfv` — estadísticas, regresión lineal, KNN, vectores
- `stdlib/plantilla.cfv` — motor de plantillas HTML

### Nuevos ejemplos

- `ejemplos/ml_basico.cfv` — Machine Learning: estadística, regresión lineal, KNN
- `ejemplos/cli_herramienta.cfv` — herramienta CLI completa con colores y tabla
- `ejemplos/chat_tcp.cfv` — servidor de chat HTTP multi-sala

---

## 2.2.0 — 2026-07-28

### Nuevas funciones nativas

- `json_texto`, `json_bonito`, `json_parsear` — JSON nativo sin dependencias
- `db_abrir`, `db_cerrar`, `db_ejecutar`, `db_consulta`, `db_consulta_p`, `db_ultimo_id`, `db_transaccion`, `db_confirmar`, `db_revertir` — SQLite integrado
- `http_post`, `http_put`, `http_delete`, `http_solicitud` — cliente HTTP completo
- `regex_coincidir`, `regex_buscar`, `regex_reemplazar`, `regex_grupos` — regex nativo
- `canal_nuevo`, `canal_enviar`, `canal_recibir`, `canal_cerrar`, `canal_tam` — canales concurrentes
- `fecha_ahora`, `fecha_formatear`, `tiempo_ms`, `tiempo_segundos` — fecha/hora nativa
- `lista_unica`, `lista_aplanar`, `lista_zip` — colecciones avanzadas
- `mapa_claves`, `mapa_valores`, `mapa_entradas`, `mapa_fusionar` — operaciones de mapa
- `texto_relleno`, `texto_relleno_der`, `texto_formato` — texto avanzado
- `env_obtener`, `env_establecer`, `proceso_ejecutar`, `salir`, `pausa` — sistema

### Nueva stdlib (v2.2)

- `stdlib/db.cfv` — SQLite ORM completo
- `stdlib/pruebas.cfv` — framework de testing
- `stdlib/log.cfv` — logging con colores y niveles
- `stdlib/validar.cfv` — validación de datos y esquemas
- `stdlib/http_cliente.cfv` — cliente HTTP de alto nivel
- `stdlib/concurrencia.cfv` — canales, mutex, reintentos

### Nuevos ejemplos (v2.2)

- `ejemplos/app_banco.cfv` — app bancaria completa con SQLite, JWT, transacciones
- `ejemplos/api_rest.cfv` — API REST con CRUD completo
- `ejemplos/juego_2d.cfv` — Snake con SDL2
- `ejemplos/juego_3d.cfv` — cubo 3D con OpenGL
- `ejemplos/test_suite.cfv` — suite de pruebas unitarias completa

---

## 2.1.0 — 2026-07-28

- Templates Android (JNI + NDK) en `herramientas/android-cforgev/`
- Templates iOS (Swift + ObjC++) en `herramientas/ios-cforgev/`
- OpenGL 3D: `gl_iniciar`, `gl_programa_basico`, `gl_malla_cubo`, `gl_dibujar_malla`, matrices MVP
- SDL2 bindings: `juego_iniciar`, `juego_eventos`, `sdl_dibujar_rect`, `sdl_delay`
- Plugin Unreal Engine 5: `CForgeSubsystem.h`, `CForgeRuntime.cpp`
- Plugin Unity: `CForge.cs`, `CForgePlugin.cs`
- `stdlib/sdl.cfv` y `stdlib/gl.cfv`

---

## 2.0.0 — 2026-07-26

- Servidor HTTP nativo (POSIX sockets): `web_escuchar`, `web_solicitud`, `web_responder`
- Criptografía real OpenSSL: SHA-256, HMAC-SHA256, AES-256-CBC, PBKDF2, JWT HS256
- `stdlib/web.cfv`, `stdlib/crypto.cfv`
- Package manager `cfpkg`
- Install script `install.sh`

---

## 1.6.0-developer-preview — en desarrollo

- Comienza C-Forge Core Bootstrap B0: contrato Stage 0/1/2/3, subconjunto
  congelado y primer lexer del compilador escrito íntegramente en `.cfv`.
- Stage 0 incorpora un compilador C++17 independiente de Python que lee el
  subconjunto Core mínimo y produce un ejecutable nativo mediante `clang++`.
- La dirección del Core exige memoria, objetos, scripting, concurrencia y web
  implementados nativamente; los puentes históricos quedan fuera del núcleo.
- La indexación segura de textos, necesaria para el lexer autoalojado, mantiene
  paridad comprobada entre intérprete, VM y backend nativo.
- Las funciones nuevas quedan congeladas hasta completar el camino de bootstrap;
  el estado sigue siendo planeado y no se afirma autoalojamiento prematuro.
- `extern("cpp")` detecta `clang++` ausente y emite un diagnóstico C-Forge
  limpio; la demostración maestra captura esa condición y puede continuar.
- CI, README y el manifiesto de capacidades fijan una única orden reproducible
  de fuzzing: `--cases 10000`, equivalente a 20.000 casos en dos pasadas.
- `verified-preview` incorpora umbrales cuantitativos comprobados por el gate.
- Los bloques `extern` quedan bloqueados por defecto en automatización, solicitan
  consentimiento en terminal y requieren `--allow-extern` para una autorización
  explícita; la compilación nativa aplica la misma barrera.
- El ownership trata la asignación de estructuras y colecciones no copiables
  como un movimiento implícito y rechaza doble movimiento/use-after-move.
- El ownership cubre argumentos por valor, duplicados dentro de colecciones y
  restaura correctamente una variable después de reasignarla.
- La detección de `extern` en compilación nativa recorre el AST y ya no depende
  de su representación textual.
- LLVM diagnostica firmas sin tipos explícitos mediante un error C-Forge limpio,
  sin filtrar tracebacks internos del implementador.
- `capabilities.json` establece el estado y evidencia de cada capacidad pública;
  CI rechaza características del README sin etiqueta oficial o enlaces rotos.
- `cforge capabilities [--json]` expone la matriz honesta desde instalaciones
  fuente, portables, monolíticas y Homebrew.
- La EBNF documenta `este` como autorreferencia exclusiva de métodos.
- El lenguaje queda definido independientemente de su implementación mediante
  una gramática EBNF normativa, un contrato de tipos/`ForgeValue`/ownership y
  una especificación de semántica observable y paridad de backends. Una prueba
  ejecuta el ejemplo contractual y exige que la documentación permanezca
  presente.
- Tuplas inmutables y conjuntos sin duplicados incorporados al intérprete,
  análisis estático, bytecode 1.1, VM, backend C++ y LLVM, con representación
  determinista y conservación de tipos en el puente Python.
- Mapas LLVM nativos homogéneos con claves de texto, valores numéricos, de
  texto o booleanos, acceso verificado, longitud y destrucción determinista.
- Ownership interprocedural: los movimientos y préstamos devueltos atraviesan
  llamadas, se impide devolver préstamos de variables locales y se detectan
  ciclos indirectos. Los constructores transfieren campos no copiables.
- Destructores nominales LLVM transitivos para objetos que poseen listas,
  mapas, tuplas, conjuntos u otros objetos.
- Primera bajada de excepciones comprobadas a LLVM: la división por cero salta
  a `intentar/capturar`; fuera de un manejador produce diagnóstico C-Forge y
  finalización no exitosa, en lugar de continuar con infinito silencioso.
- FFI LLVM C ABI tipado mediante `extern_c funcion`, inicialmente para números,
  booleanos y textos; `--compilar-llvm` enlaza fuentes u objetos con
  `--vincular` y rechaza layouts todavía no estabilizados.
- Los parámetros `lista<numero>` cruzan el FFI LLVM mediante una vista prestada
  `CfvNumberSlice { data, length }`, sin copia y sin transferir propiedad.
- Una función `extern_c segura` puede devolver `lista<numero>` mediante
  `CfvOwnedNumberList`; C-Forge valida, copia y ejecuta su liberador una sola vez.
- Los retornos `texto` seguros usan `CfvOwnedText` con longitud UTF-8 explícita;
  se rechazan NUL internos y el propietario se libera incluso al capturar el error.
- Los parámetros `mapa<numero>` se exponen sin copia como claves UTF-8 y valores
  numéricos paralelos mediante `CfvNumberMapView`.
- Estructuras y clases con campos escalares cruzan el FFI como `CfvRecordView`
  etiquetado; los campos propietarios anidados se rechazan de manera explícita.
- Los campos nominales ahora aceptan anotaciones genéricas completas como
  `lista<numero>` y `opcion<texto>` en el parser profesional.
- El ABI dinámico C/C++/Native AOT transporta booleanos en ambas direcciones.
- El adaptador Python embebido usa longitudes UTF-8 explícitas para textos y
  claves de mapas, preservando NUL internos sin truncamiento.
- ABI dinámico V2 paralelo y versionado (`CfvValueV2`): tamaño de estructura,
  longitud, flags y ownership explícitos. V1 continúa compatible y V2 tiene
  prioridad mediante `cfv_register_function_v2`.
- ABI V2 recursivo para listas, mapas y registros nominales en el adaptador
  C/C++: argumentos prestados, retornos propietarios de raíz, profundidad
  limitada y rechazo de liberadores anidados.
- Nueva puerta `cforge parity` que ejecuta un mismo `.cfv` en intérprete, VM y
  LLVM y exige igualdad exacta de salida, errores y código de terminación.
- Los paquetes del registro y archivos portables normalizan orden, tiempos,
  propietarios, permisos y cabeceras gzip/ZIP; dos builds de las mismas fuentes
  producen exactamente el mismo SHA-256.
- El cargador CFBC valida límites de carga, profundidad, versión interna,
  operandos, conteos y destinos de salto antes de entregar código a la VM;
  rechaza bytecode futuro o estructuralmente inválido sin ejecutarlo.
- LLVM IR 1.4 incorpora `opcion<T>` etiquetada para números, textos y booleanos;
  `desenvolver` comprueba valores ausentes, participa en `intentar/capturar` y
  la representación posee destrucción determinista.
- `extern_c segura funcion` define una frontera de error estable basada en
  `status + out + error`; los fallos nativos se capturan en C-Forge y nunca
  requieren propagar una excepción C++ a través del C ABI.
- El LSP indexa declaraciones y tipos inferidos básicos, muestra firmas FFI y
  `opcion<T>` en hover/autocompletado, y resuelve definición, referencias y
  renombrado entre todos los documentos abiertos del proyecto.
- La extensión oficial inicia `cforge lsp` directamente y expone sus funciones
  en VS Code sin una dependencia Node adicional. DAP incorpora breakpoints
  condicionales, hit conditions y logpoints con evaluación restringida.

- Primera fase comprobable de memoria segura: `mover`, `prestar`,
  `prestar_mut` y `soltar_prestamo` con diagnóstico estático `CF3001`.
- Tipo `opcion` con `algunos`, `ninguno`, `es_algunos` y `desenvolver` en el
  intérprete y la VM propia.
- Firmas de funciones con parámetros y retornos tipados, verificadas estática y
  dinámicamente sin romper funciones graduales existentes.
- Regiones léxicas, destrucción explícita y frontera `unsafe` auditable.
- Formato `.cfb` 1.0 con firma `CFBC`, versión, longitud y SHA-256 verificable.
- Fuzzing determinista del parser y cargador de bytecode en la matriz de
  seguridad para macOS, Ubuntu y Windows.
- Clases, estructuras, métodos y mutación de campos conservados y ejecutados
  directamente por la VM y el formato `.cfb`.
- Resolución recursiva de módulos en la compilación de bytecode.
- Archivos, procesos y sockets nativos en la VM con confinamiento de rutas y
  permisos `fs-read`, `fs-write`, `process` y `network`.
- Tareas estructuradas, espera con timeout, cancelación consultiva y canales
  sincronizados en intérprete, VM y backend C++ nativo.
- Sintaxis real `async funcion` y `await`, conservada en bytecode y ejecutada
  de forma asíncrona en intérprete, VM y backend C++.
- LLVM IR 1.1 incorpora textos UTF-8, variables y parámetros de texto,
  concatenación, igualdad, retornos tipados e impresión booleana legible.
- Listas numéricas LLVM con longitud/capacidad, búfer dinámico, acceso con
  límites, `append`, paso por funciones, impresión y liberación determinista.
- `cforge check` resuelve módulos recursivamente y comprueba firmas tipadas
  entre archivos.
- Backend LLVM IR real para el núcleo numérico, verificado mediante Clang.
- Puertas técnicas para rendimiento, FFI, memoria, plataformas y producción crítica.
- Prueba de paridad que compila y ejecuta un programa C-Forge mediante LLVM IR.

## 1.5.0-developer-preview — 2026-07-22

- Bytecode propio y máquina virtual de pila con límites de ejecución.
- VM con funciones, ciclos, colecciones, compatibilidad de sintaxis y excepciones.
- Diagnósticos estructurados `CFxxxx` y análisis gradual reforzado.
- Servidor LSP 3.17 inicial con diagnósticos, autocompletado y hover.
- Depurador inicial de bytecode con traza de instrucciones y variables.
- Gestor local reproducible con `cforge.json`, `cforge.lock` y SHA-256.
- Empaquetado Homebrew, Debian, portable y Windows actualizado con los módulos nuevos.
- Matriz pública de preparación para producción y límites verificables.

## 1.4.1-definitive — 2026-07-20

- Sintaxis compatible `console.log`, `System.out.println`, `std::cout` y `cout`.
- Colecciones compatibles mediante `.append`, `.push`, `.length` y `.len`.
- Forge Shared Arena 1.0 y catálogo declarativo de conectores.
- Fórmula Homebrew pública renombrada a `cforge`.

## En desarrollo

- LLVM IR 1.2: layouts nominales para estructuras y clases, construcción con
  `malloc`, campos tipados, mutación, métodos estáticos por clase y liberación
  explícita mediante `destruir`/`free`.
- LLVM IR 1.2 resuelve módulos `.cfv` antes del codegen y monomorfiza funciones
  genéricas por combinación concreta de tipos, evitando despacho dinámico.

- Depuración: el bytecode conserva líneas de fuente verificadas y `cforge dap`
  implementa el protocolo DAP para breakpoints `.cfv`, continuar, step-in/over/out,
  stack trace, scopes e inspección de variables.
- DAP ampliado con pila real de llamadas, step-in/over/out según profundidad,
  objetos y colecciones expandibles y evaluación de campos/índices sin ejecutar
  llamadas ni permitir mutaciones.

- Gestor de paquetes formato 2: identidades Ed25519, firmas de artefactos,
  verificación antes de extracción y revocación tanto por versión como por clave.
  Se añadieron `cforge pkg keygen` y `cforge pkg sign`.

- Sistema de tipos: funciones genéricas inferidas por llamada (`funcion id<T>`),
  anotaciones genéricas anidadas y contratos nominales mediante `interfaz`/`implementa`.
  El analizador rechaza sustituciones genéricas contradictorias y clases que no
  satisfacen las firmas exigidas.
- Ownership: los alias creados por `prestar` y `prestar_mut` conservan ahora su
  propietario, se liberan determinísticamente al salir de una `region` o al
  destruir el alias, y el analizador rechaza ciclos directos e indirectos salvo
  dentro de una frontera `unsafe` explícita.

- CI ejecuta ahora bytecode estable, `async/await` y los backends LLVM numérico,
  textual y de listas en macOS, Linux y Windows; en Unix también compila y ejecuta
  el IR con Clang. La matriz es evidencia automatizada, no sustituye todavía las
  pruebas físicas de FFI en cada arquitectura.

- Conectores nativos `forge_hash`, `forge_bench`, `sys_fetch` y `json_parse`.
- Flujo transparente de mapas, listas, textos y números mediante `ForgeValue`.
- Programas de fusión compilables sin `extern`, prefijos de runtime ni punto y coma.
- Gestor seguro de dependencias del sistema con consentimiento explícito.
- Alias `.cfv-gui` para componentes gráficos utilizados desde C-Forge.
- Captura de `stdout`/`stderr` y salida limpia del C-Forge Package Manager.
- Calculadora gráfica de macOS distribuida como aplicación `.app`.

## 1.4.0-definitive — 2026-07-20

- La marca pública cambia de C-Forgev a **C-Forge**.
- Se conservan `cforge`, `.cfv` e identificadores internos como compatibilidad.
- Documentación, ejemplos, paquetes, extensión y enlaces oficiales migrados.
- La extensión de VS Code avanza a 1.3.1 con la nueva identidad pública.

## Distribución multiplataforma — 2026-07-20

- CI de ejecución portable para macOS, Linux y Windows.
- GitHub Releases automáticas mediante tags `v*`.
- Archivos portables para los tres sistemas y ejecutable autónomo de Windows.
- Paquete `.deb` para Debian/Ubuntu.
- Generadores de fórmula Homebrew y manifiesto WinGet con SHA-256.
- Diagnóstico `--setup` adaptado a cada sistema operativo.

## 1.3.0-definitive — 2026-07-20

- Núcleo de sistema: `sys_run` y `sys_info` en intérprete y backend C++.
- I/O nativo: `file_read`, `file_write` y `file_append`.
- TCP nativo: `net_listen` con timeout y `net_send` con envío completo.
- Datos matemáticos: `matrix` y `array_fast`, utilizables dentro de bloques `gpu`.
- Gramática de VS Code, ejemplo integral y pruebas de regresión ampliadas.

## 1.2.1-definitive — 2026-07-20

- Añadido `cforge --setup` para diagnosticar C++, Python, Node.js y el JDK.
- Añadido `cforge --install` para instalar el ejecutable monolítico en `/usr/local/bin`.
- Auto-Link C++ detecta símbolos literales de `use_cpp` y enlaza fuentes registrables cercanas.
- Distribución monolítica regenerada y prueba maestra completa: 7/7.

## 1.2.0-experimental — 2026-07-20

- `ForgeValue` nativo central basado en `std::variant` con procedencia.
- Registro compartido de símbolos globales accesible desde C++.
- `ForgeSymbols` automático para funciones Python y módulos JavaScript.
- Resolución homogénea por punto para módulos y mapas extranjeros.
- Pruebas cruzadas Python → C-Forge → JavaScript en intérprete y nativo.

## 1.1.0-experimental — 2026-07-20

- Puente Node.js real mediante `use_javascript`, `use_typescript` e `import npm:`.
- Bloques literales `extern("javascript")` y `extern("typescript")`.
- Infraestructura Java/JNI/JAR con `use_java`, `import maven:` y `extern("java")`.
- Conversión recursiva universal para nulo, booleanos, números, textos, listas y mapas.
- Gramática VS Code, prueba maestra y amalgama actualizadas.

## 1.0.0-experimental — 2026-07-20

- Lexer opaco para bloques `extern("python")` y `extern("cpp")`.
- Ejecución literal mediante Python embebido o compilación C++17.
- Barrera estática Memory Safety para operaciones nativas peligrosas.
- Modificador `cluster` para variables y funciones registradas.
- Tabla de símbolos distribuibles expuesta mediante `cluster_estado()`.
- Paridad entre intérprete y ejecutable nativo con pruebas automatizadas.
- CLI `cforge fmt` y `cforge test`, bloques `test` y función `afirmar`.
- Gramática TextMate 1.0 para GPU, cluster, extern y funciones multilenguaje.
- `main.cfv` como prueba maestra del ecosistema completo.
- Amalgama reproducible `cforge_master.cpp` con todos los módulos embebidos.
- Bootstrap C++ con `<Python.h>`, recursos RAII y CLI completo en un binario.

## 0.9.0-experimental — 2026-07-20

- Bloques `gpu` con ejecución CPU asíncrona y frontera para Metal/CUDA.
- Motor self-healing conservador con sugerencias, reparación explícita y respaldo.
- Backend WebAssembly WAT para variables y operaciones numéricas.
- Observador `--vigilar` con re-tokenización y estado global persistente.
- Nuevos ejemplos, resaltado y pruebas de paridad nativa.

## 0.8.0-experimental — 2026-07-20

- Declaraciones `nombre = valor` con tipo estable inferido automáticamente.
- Análisis estático que detecta contradicciones de tipos evidentes al compilar.
- `paralelo` con tareas `std::async` reales en el backend C++.
- Contadores sincronizados y umbral caliente para infraestructura JIT adaptativa.
- Importaciones universales `import pip:nombre` e `import nuget:nombre`.
- Invocación natural `paquete.funcion(...)` mediante los puentes existentes.

## 0.7.0-experimental — 2026-07-20

- Resultados ABI con propiedad explícita y callback de liberación RAII.
- `PyObject*` administrados por referencias automáticas y excepciones extraídas.
- Errores C++, C# y Python normalizados como `[C-Forge Runtime Exception]`.
- Excepciones extranjeras compatibles con `intentar/capturar`.
- REPL persistente y multilínea al ejecutar `./cforgev` sin archivo.
- Pruebas de liberación exacta, recuperación de errores e interoperabilidad real.

## 0.6.0-experimental — 2026-07-20

- Python embebido mediante `Python.h`, `PyImport_ImportModule` y llamadas reales.
- ABI común para nulo, enteros, decimales y textos.
- Carga dinámica con `dlopen/dlsym` o `LoadLibrary/GetProcAddress`.
- Contrato compatible con bibliotecas C# Native AOT exportadas.
- Registro de funciones C/C++ vinculadas mediante `cfv_register_function`.
- Comando `--vincular`, ejemplos y pruebas reales de interoperabilidad.

## 0.5.0-experimental — 2026-07-20

- Clases con campos tipados y métodos.
- Referencia `este` y mutación de campos con verificación de tipos.
- Despacho de métodos equivalente en intérprete y C++17.
- Biblioteca: `raiz`, `potencia`, `absoluto`, `redondear` y `tiempo_actual`.
- Argumentos del programa mediante `argumentos()`.
- Ejemplo orientado a objetos y soporte VS Code actualizado.

## 0.4.0-experimental — 2026-07-20

- Estructuras tipadas con constructores validados.
- Acceso a campos mediante punto.
- Paridad en intérprete y backend C++17.
- Lanzador local `./cforgev`.
- Extensión experimental para Visual Studio Code.

## 0.3.0-experimental — 2026-07-20

- Módulos locales con `usar` y resolución recursiva de dependencias.
- Lectura, escritura y comprobación de archivos UTF-8.
- Manejo estructurado mediante `intentar/capturar`.
- Rutas relativas estables para intérprete y ejecutables nativos.
- Pruebas de paridad para módulos, archivos y errores.

## 0.2.0-experimental — 2026-07-20

- Tipos fijos con inferencia y anotaciones explícitas.
- Operadores lógicos `y`, `o` y `no`.
- Entrada con `leer` y conversiones con `a_numero`/`a_texto`.
- Listas, mapas, acceso por índice o clave, `longitud` y `agregar`.
- Paridad de estas capacidades en el intérprete y backend C++17.
- Pruebas nativas de colecciones y tipos.

## 0.1.0-experimental — 2026-07-20

- Primera sintaxis `.cfv`.
- Variables, reasignación y valores básicos.
- Operaciones matemáticas y comparaciones.
- Condiciones `si/sino` y ciclos `mientras`.
- Funciones, parámetros y `retornar`.
- Intérprete de desarrollo.
- Backend C++17 y ejecutables nativos ARM64 para macOS.
- Textos Unicode, errores comprensibles y pruebas automatizadas.