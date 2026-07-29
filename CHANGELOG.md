# Historial de C-Forge

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
