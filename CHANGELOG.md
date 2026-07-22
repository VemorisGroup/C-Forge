# Historial de C-Forge

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
