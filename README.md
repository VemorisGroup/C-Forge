# C-Forge

[![CI multiplataforma](https://github.com/VemorisGroup/C-Forge/actions/workflows/ci.yml/badge.svg)](https://github.com/VemorisGroup/C-Forge/actions/workflows/ci.yml)
[![Seguridad](https://github.com/VemorisGroup/C-Forge/actions/workflows/security.yml/badge.svg)](https://github.com/VemorisGroup/C-Forge/actions/workflows/security.yml)

<p align="center">
  <img src="assets/cforgev-logo.svg" width="128" height="128" alt="Logo de C-Forge">
</p>

> Lenguaje de programación experimental de Vemoris Group con sintaxis propia,
> ejecución interactiva, compilación a C++17 e interoperabilidad políglota.

**Versión actual:** `1.6.0-developer-preview`<br>
**Extensión oficial:** `.cfv`<br>
**Estado:** experimental; apto para aprendizaje, demostraciones y desarrollo del motor.

C-Forge combina una sintaxis legible, tipado gradual, compilación nativa y un
sistema de valores común llamado `ForgeValue`. El proyecto permite ejecutar un
programa durante el desarrollo o traducirlo a un ejecutable C++ nativo.

Cuando una función necesita un componente conocido del sistema, la VM lo
presenta mediante un alias propio, como `.cfv-gui`, solicita autorización y
captura la salida del instalador. C-Forge muestra el paquete real antes de
ejecutarlo y nunca instala dependencias sin consentimiento explícito.

El catálogo de fusión ofrece conectores con sintaxis nativa que intercambian
`ForgeValue` sin conversiones manuales:

```cfv
datos = json_parse("{\"valor\":21}")
huella = forge_hash(datos)
respuesta = sys_fetch("https://example.com/data.json")
medicion = forge_bench("procesar", 1000, [datos.valor])
```

`sys_fetch` acepta únicamente HTTP/HTTPS, limita cada respuesta a 16 MiB y usa
un tiempo de espera para evitar que una descarga bloquee indefinidamente la VM.

> **Migración de marca:** C-Forge fue publicado inicialmente como C-Forgev.
> El comando `cforge`, la extensión `.cfv` y los identificadores técnicos
> `cforgev` se conservan para no romper instalaciones ni proyectos existentes.

```cfv
cluster proyecto = "C-Forge";

funcion cuadrado(numero) {
    retornar numero * numero;
}

sea creador: texto = "Javier";
resultado = cuadrado(7);

mostrar("Hola, " + creador);
mostrar(resultado);
```

También puede usarse sintaxis de impresión y colecciones conocida de otros
lenguajes; todas las variantes se normalizan internamente a `ForgeValue`:

```cfv
print("Python")
console.log("JavaScript")
System.out.println("Java")
std::cout << "C++" << std::endl

datos = [1, 2]
datos.append(3)
datos.push(4)
print(datos.length)
print(datos.length())
print(datos.len())
```

## Características

- **[Verificado en Developer Preview]** Variables inferidas, tipos explícitos,
  valores escalares, colecciones, estructuras, clases y control de flujo.
- **[Verificado en Developer Preview]** REPL, intérprete, bytecode y VM para el
  subconjunto cubierto por la suite automatizada.
- **[Experimental]** Compilación de `.cfv` a C++17 y posteriormente a ejecutable.
- **[Parcial]** Backend LLVM nativo para las capacidades enumeradas en
  [`docs/PRODUCTION-READINESS.md`](docs/PRODUCTION-READINESS.md).
- **[Experimental]** Módulos locales y acceso mediante `objeto.miembro`.
- **[Parcial]** Adaptadores para Python, C/C++, C# Native AOT, Java,
  JavaScript y TypeScript; no existe todavía paridad completa en tres sistemas.
- **[Experimental]** Bloques literales `extern("lenguaje") { ... }`, siempre
  sujetos a autorización de seguridad.
- **[Parcial]** FFI LLVM tipado mediante `extern_c funcion` y `--vincular`, con escalares y
  vistas zero-copy prestadas para parámetros `lista<numero>`.
- **[Experimental]** `ForgeValue`, Forge Shared Arena y símbolos `cluster`.
- **[Parcial]** Tareas paralelas y `async/await`; `gpu` ejecuta actualmente en
  CPU y el JIT solamente perfila rutas calientes.
- **[Experimental]** Archivos, procesos, hardware, sockets TCP, `array_fast`
  y matrices en los backends documentados.
- **[Experimental]** Formateador, pruebas, hot reload, reparación conservadora,
  LSP, DAP y extensión de Visual Studio Code.
- **[Parcial]** WebAssembly emite WAT para un subconjunto, no para todo C-Forge.
- **[Parcial]** Empaquetado para macOS, Linux y Windows; la validación física y
  los catálogos públicos aún no están completos.
- **[Planeado]** Compilador autoalojado y núcleo autónomo. B4 ya posee un
  compilador Stage 1 escrito en C-Forge, construido por Stage 0 y capaz de
  compilar programas Core sin Python. Stage 2/3 reproducible y el runtime
  independiente siguen pendientes; consulta
  [`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md).
- **[No certificado]** Uso bancario o crítico; requiere auditoría profesional,
  LTS, cumplimiento y evidencia externa.

La fuente de verdad legible por máquinas es
[`capabilities.json`](capabilities.json). La
[`política de completitud`](docs/COMPLETENESS-POLICY.md) prohíbe presentar una
capacidad parcial o planeada como terminada.

## C-Forge Core Bootstrap

El desarrollo de funciones nuevas queda congelado mientras se construye el
compilador autoalojado. Stage 0 es un compilador mínimo C++17 que no carga
Python ni runtimes extranjeros:

```bash
clang++ -std=c++17 -O2 bootstrap/stage0/cforge_bootstrap.cpp \
  -o build/cforge-bootstrap
./build/cforge-bootstrap bootstrap/fixtures/minimal.cfv -o build/minimal
./build/minimal

cforge test bootstrap/core_lexer.cfv
cforge vm bootstrap/core_lexer.cfv
```

El primer bloque verifica que Stage 0 produzca código máquina; los dos últimos
comprueban el lexer que formará parte de Stage 1. B1 está verificado, pero esto
no significa que C-Forge sea autoalojado. La condición definitiva será que Stage 2
y Stage 3 recompilen las mismas fuentes `.cfv` y produzcan artefactos
reproducibles equivalentes. La dirección nativa completa está en
[`docs/CORE-DIRECTION.md`](docs/CORE-DIRECTION.md).

Los hitos B1/B2 añaden [`core_ast.cfv`](bootstrap/core_ast.cfv),
[`core_parser.cfv`](bootstrap/core_parser.cfv),
[`core_semantics.cfv`](bootstrap/core_semantics.cfv) y la
[`gramática Core 0.4`](docs/CORE-GRAMMAR-0.4.ebnf). B3 agrega
[`core_emitter.cfv`](bootstrap/core_emitter.cfv). B4 agrega
[`core_driver.cfv`](bootstrap/core_driver.cfv) y la unidad autocontenida
[`cforge_stage1.cfv`](bootstrap/stage1/cforge_stage1.cfv). Sus pruebas ensamblan esas
fuentes con el lexer, las compilan mediante Stage 0, comparan AST, diagnósticos
y C++ generado contra el intérprete y la VM, y ejecutan el binario final.

## Herramientas profesionales (base 1.0)

```bash
cforge check programa.cfv          # análisis estático con códigos CFxxxx
cforge bytecode programa.cfv       # inspeccionar bytecode propio
cforge parity programa.cfv         # comparar intérprete, VM y LLVM
cforge vm programa.cfv             # ejecutar en la VM de pila
cforge --llvm programa.cfv         # emitir LLVM IR real para el núcleo compatible
cforge --compilar-llvm programa.cfv -o programa # LLVM IR + Clang
cforge debug programa.cfv          # trazar instrucciones y variables
cforge debug programa.cfv --break 20 # detenerse en un offset de bytecode
cforge lsp                          # servidor LSP 3.17 por stdio
cforge pkg init mi-proyecto        # crear cforge.json y cforge.lock
cforge pkg build                   # crear paquete y SHA-256
cforge pkg keygen                  # crear identidad Ed25519 del publicador
cforge pkg sign archivo clave nombre versión
cforge pkg search consulta         # consultar el índice público
cforge pkg install paquete         # verificar HTTPS, SHA-256, firma, cuenta y revocación
cforge dap                         # servidor de depuración DAP para editores
cforge --compilar-llvm ejemplos/llvm_objetos_16.cfv -o contador
cforge programa_confiable.cfv --allow-extern # autorizar código extranjero explícitamente
```

La matriz honesta de capacidades y trabajo pendiente está en
[`docs/PRODUCTION-READINESS.md`](docs/PRODUCTION-READINESS.md), con
[`validación por plataforma`](docs/PLATFORM-VALIDATION.md) y
[`alcance de auditoría externa`](docs/EXTERNAL-AUDIT-SCOPE.md). Una auditoría de
seguridad independiente y resultados de rendimiento no se sustituyen con afirmaciones:
deben publicarse como evidencia reproducible antes de declarar el motor apto para producción.

La definición independiente de la implementación está en la
[`gramática EBNF`](docs/GRAMMAR-1.6.ebnf), el
[`sistema de tipos`](docs/TYPE-SYSTEM-1.6.md) y el
[`contrato de backends`](docs/BACKEND-SEMANTICS-1.6.md).

La dirección del lenguaje autónomo está definida en
[`C-Forge 2.0 Draft`](docs/C-FORGE-2.0-DRAFT.md) y su
[`gramática candidata completa`](docs/GRAMMAR-2.0-DRAFT.ebnf). Estos documentos
son una especificación de diseño: **no afirman que el motor 1.6 implemente aún
todas esas construcciones**. El avance real continúa gobernado por
`capabilities.json` y la política de completitud.

## Inicio rápido

### 1. Comprobar el entorno

Desde la raíz del proyecto:

```bash
./outputs/cforge-master --setup
```

En el Mac utilizado para desarrollar C-Forge están activos Apple Clang,
Python 3, Node.js y Eclipse Temurin JDK.

### 2. Ejecutar un programa

```bash
./outputs/cforge-master ejemplos/hola.cfv
```

También puedes usar el frontend del repositorio:

```bash
./cforge ejemplos/hola.cfv
```

### 3. Compilar un ejecutable nativo

```bash
./outputs/cforge-master --compilar ejemplos/nucleo_sistema_13.cfv -o build/nucleo
./build/nucleo
```

### 4. Instalar el comando global en macOS

```bash
sudo ./outputs/cforge-master --install
cforge --version
```

El instalador copia la distribución monolítica a `/usr/local/bin/cforge`.

## Instalación multiplataforma

El workflow de lanzamientos está preparado para generar paquetes en los tres
sistemas. Cada artefacto solo se considera publicado y soportado cuando aparece
como evidencia en [GitHub Releases](https://github.com/VemorisGroup/C-Forge/releases)
y en la matriz de validación.

macOS, cuando se publique el tap de Vemoris Group:

```bash
brew install VemorisGroup/cforgev/cforge
```

Windows, después de la aceptación del manifiesto en WinGet:

```powershell
winget install VemorisGroup.CForgev
```

Debian/Ubuntu usando el paquete descargado del lanzamiento:

```bash
sudo apt install ./cforgev_1.4.1_all.deb
```

La preparación y las condiciones necesarias para ofrecer los comandos cortos
están documentadas en [`DISTRIBUCION.md`](DISTRIBUCION.md).

## Prueba maestra

El archivo experimental [`main.cfv`](main.cfv) demuestra tipado, clases, Python, JavaScript,
TypeScript, Java, C#, C++, ForgeValue, archivos, hardware, procesos, matrices,
paralelismo, GPU/CPU, JIT, `cluster`, networking y manejo de errores.

```bash
cforge test main.cfv --allow-extern
```

Resultado esperado:

```text
C-Forge Test: 10 aprobados, 0 fallidos
```

`main.cfv` contiene bloques extranjeros deliberados; `--allow-extern` confirma
que se confía en el archivo. Las pruebas normales no deben usar esa opción.

Para comprobar también el backend nativo:

```bash
cforge --compilar main.cfv -o build/main-final
./build/main-final
```

La suite interna contiene más de 100 pruebas automatizadas:

```bash
PYTHONPYCACHEPREFIX=/tmp/cforgev-pycache \
python3 -m unittest discover -s tests -v
```

El gate oficial de fuzzing ejecuta exactamente 20.000 casos deterministas:

```bash
python3 tests/fuzz_smoke.py --cases 10000
```

`--cases 10000` ejecuta 10.000 entradas sobre cada una de las dos pasadas del
fuzzer (lexer/parser y formato/ejecución), para un total verificable de 20.000.
La misma orden se exige en CI y en [`capabilities.json`](capabilities.json).

## Sintaxis esencial

### Tipado gradual

```cfv
nombre = "Javier";
sea edad = 20;
sea saldo: numero = 1500;
sea activo: booleano = verdadero;
```

Una variable inferida conserva su tipo después de la primera asignación. El
analizador estático también rechaza contradicciones evidentes antes de compilar.

### Control de flujo

```cfv
si (edad >= 18) {
    mostrar("Mayor de edad");
} sino {
    mostrar("Menor de edad");
}

contador = 0;
mientras (contador < 3) {
    contador = contador + 1;
}
```

### Estructuras y clases

```cfv
estructura Persona {
    nombre: texto;
    edad: numero;
}

clase Cuenta {
    campo saldo: numero;

    metodo depositar(cantidad) {
        este.saldo = este.saldo + cantidad;
        retornar este.saldo;
    }
}

persona = Persona("Javier", 20);
cuenta = Cuenta(100);
mostrar(cuenta.depositar(50));
```

### Excepciones y pruebas

```cfv
intentar {
    mostrar(10 / 0);
} capturar(error) {
    mostrar(error);
}

test "suma" {
    afirmar(20 + 22 == 42, "resultado incorrecto");
}
```

La referencia ampliada está en
[`outputs/C-FORGE-CHEATSHEET.md`](outputs/C-FORGE-CHEATSHEET.md).

## Núcleo nativo

```cfv
info = sys_info();
mostrar(info.cpu);
mostrar(info.nucleos);
mostrar(info.ram_bytes);

proceso = sys_run("printf C-Forge");
mostrar(proceso.estado);
mostrar(proceso.salida);

file_write("datos.txt", "Forge");
file_append("datos.txt", "v");
mostrar(file_read("datos.txt"));

vector = array_fast([1, 2, 3, 4]);
matriz = matrix(2, 3, 7);
```

Networking TCP:

```cfv
// Servidor local de una conexión, con timeout de 5 segundos.
paquete = net_listen(8080, 5000);
mostrar(paquete.datos);

// Desde otro programa o proceso:
bytes = net_send("127.0.0.1", 8080, "Hola por TCP");
```

`net_listen` escucha actualmente en loopback y no implementa un servidor HTTP.

## Paralelismo, GPU, JIT y cluster

```cfv
cluster version = "1.3";

cluster funcion cuadrado(n) {
    retornar n * n;
}

gpu {
    resultados = paralelo("cuadrado", [2, 3, 4, 5]);
    mostrar(resultados);
}

mostrar(jit_estado("cuadrado"));
mostrar(jit_caliente("cuadrado"));
mostrar(cluster_estado());
```

El backend `gpu` de la versión 1.3 ejecuta tareas paralelas en CPU. La integración
física con Metal/CUDA continúa siendo un punto de extensión. El JIT actual perfila
rutas calientes, pero todavía no reemplaza bytecode por código máquina optimizado.

## Interoperabilidad

### Importación universal

```cfv
import pip:math;
import npm:path;
import nuget:CSharpNative;
import maven:paquete;

mostrar(math.sqrt(81));
mostrar(path.extname("programa.ts"));
```

### Puentes explícitos

```cfv
mostrar(use_python("math", "pow", [2, 10]));
mostrar(use_javascript("path", "basename", ["/tmp/app.js"]));
mostrar(use_csharp("biblioteca.dylib", "sumar", [20, 22]));
mostrar(use_cpp("funcion_registrada", [20, 22]));
mostrar(use_java("app.jar", "MiClase", "metodo", [42]));
```

El compilador detecta llamadas literales `use_cpp` y busca implementaciones
registradas en `interop/`, `native/`, `cpp/` y `ejemplos/interop/`. Para una
ubicación personalizada sigue disponible `--vincular archivo.cpp`.

### Bloques extranjeros

```cfv
extern("python") {
    print("Python real")
}

extern("javascript") {
    console.log("JavaScript real");
}

extern("typescript") {
    const respuesta: number = 42;
    console.log(respuesta);
}

extern("java") {
    System.out.println("Java real");
}

extern("cpp") {
    std::cout << "C++ real" << std::endl;
}
```

Los valores universales —nulo, booleanos, números, textos, listas y mapas— se
transportan mediante `ForgeValue`. Los objetos opacos permanecen dentro de su
runtime para evitar intercambiar punteros inválidos.

Consulta [`INTEROPERABILIDAD.md`](INTEROPERABILIDAD.md) para conocer el ABI y el
contrato de propiedad de memoria.

## Herramientas del CLI

| Comando | Descripción |
|---|---|
| `cforge archivo.cfv` | Ejecuta un programa. |
| `cforge` | Abre el REPL. |
| `cforge --compilar archivo.cfv -o salida` | Genera un ejecutable nativo. |
| `cforge fmt archivo.cfv` | Formatea el código. |
| `cforge test archivo.cfv` | Ejecuta bloques `test`. |
| `cforge --vigilar archivo.cfv` | Recarga al detectar cambios. |
| `cforge --reparar archivo.cfv` | Aplica reparaciones conservadoras y crea respaldo. |
| `cforge --wasm archivo.cfv -o salida.wat` | Exporta el subconjunto Wasm/WAT. |
| `cforge --setup` | Comprueba dependencias. |
| `cforge --install` | Instala globalmente en macOS/Linux con permisos adecuados. |
| `cforge --version` | Muestra la versión. |

## Arquitectura

```text
programa.cfv
    │
    ├── Lexer + Parser + análisis estático
    │       └── Intérprete / REPL / hot reload
    │
    └── Backend C++17
            ├── ForgeValue + runtime RAII
            ├── puentes políglotas
            └── clang++ → ejecutable nativo
```

La distribución `outputs/cforge-master` es un ejecutable C++ monolítico que
inicializa CPython embebido y despliega el frontend y los backends incluidos en
un directorio temporal administrado mediante RAII. El frontend aún no ha sido
reescrito completamente en C++ puro.

Archivos principales:

| Ruta | Función |
|---|---|
| `cforgev.py` | Lexer, intérprete, REPL y CLI principal. |
| `compilador_nativo.py` | Parser, análisis y generador C++17. |
| `compilador_wasm.py` | Backend experimental WAT. |
| `include/cforgev_ffi.h` | ABI para bibliotecas nativas. |
| `include/cforge_shared_arena.h` | ABI Forge Shared Arena 1.0 con offsets de 64 bits. |
| `herramientas/generar_amalgama.py` | Generador del superarchivo C++. |
| `herramientas/vscode-cforgev/` | Gramática y configuración de VS Code. |
| `tests/test_cforgev.py` | Suite de regresión. |
| `ejemplos/` | Programas y demostraciones. |

La especificación de memoria compartida y del catálogo `ia_`/`ui_`/`web_` está
en [docs/FORGE-SHARED-ARENA.md](docs/FORGE-SHARED-ARENA.md). La demostración
nativa está en `ejemplos/arena_catalogo_16.cfv`.

## Reconstruir la distribución monolítica

En macOS con las Command Line Tools de Apple:

```bash
python3 herramientas/generar_amalgama.py

clang++ -std=c++17 -O2 outputs/cforge_master.cpp \
  -I /Library/Developer/CommandLineTools/Library/Frameworks/Python3.framework/Headers \
  -F /Library/Developer/CommandLineTools/Library/Frameworks \
  -framework Python3 \
  -Wl,-rpath,/Library/Developer/CommandLineTools/Library/Frameworks \
  -o outputs/cforge-master
```

## Visual Studio Code

La extensión experimental está en `herramientas/vscode-cforgev`. Incluye:

- Resaltado para archivos `.cfv`.
- Comentarios `//`.
- Pares de llaves, corchetes, paréntesis y comillas.
- Palabras clave del lenguaje y funciones nativas.

Consulta [`herramientas/vscode-cforgev/README.md`](herramientas/vscode-cforgev/README.md)
para instalarla localmente.

## Seguridad y limitaciones

- `sys_run` ejecuta comandos reales del sistema. No construyas comandos con
  entradas externas que no sean confiables.
- `extern`, bibliotecas C/C++ y DLL Native AOT pueden ejecutar código nativo. La
  seguridad depende también de esas bibliotecas.
- El analizador de memoria rechaza construcciones peligrosas conocidas dentro de
  `extern("cpp")`, pero no puede garantizar seguridad absoluta de memoria.
- El exportador Wasm cubre actualmente un subconjunto numérico del lenguaje.
- `net_listen` es TCP local de una conexión; no sustituye un framework web.
- Los perfiles JIT y los bloques GPU son infraestructura funcional con backend
  CPU, no un compilador JIT de producción ni un backend Metal/CUDA terminado.
- No se afirma que C-Forge supere en rendimiento o estabilidad a lenguajes
  maduros sin benchmarks independientes y reproducibles.
- No se recomienda todavía para banca, infraestructura crítica o producción.

## Documentación

- [`ESPECIFICACION.md`](ESPECIFICACION.md): sintaxis y comportamiento implementado.
- [`INTEROPERABILIDAD.md`](INTEROPERABILIDAD.md): ABI y puentes externos.
- [`CHANGELOG.md`](CHANGELOG.md): historial de versiones.
- [`outputs/C-FORGE-CHEATSHEET.md`](outputs/C-FORGE-CHEATSHEET.md): referencia rápida.

## Proyecto

C-Forge es una iniciativa de **Vemoris Group**, creada por **Javier**. El motor
se publica como proyecto experimental para continuar investigando diseño de
lenguajes, compilación e interoperabilidad.

El repositorio incluye una licencia propietaria con derechos reservados. Antes
de aceptar contribuciones externas, Vemoris Group debe definir una guía
`CONTRIBUTING.md` y sus políticas de seguridad y conducta. Si el proyecto se
convierte en código abierto, reemplaza `LICENSE` por la licencia elegida antes
de aceptar aportes.
