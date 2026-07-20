# C-Forge

<p align="center">
  <img src="assets/cforgev-logo.svg" width="128" height="128" alt="Logo de C-Forge">
</p>

> Lenguaje de programación experimental de Vemoris Group con sintaxis propia,
> ejecución interactiva, compilación a C++17 e interoperabilidad políglota.

**Versión actual:** `1.4.0-definitive`<br>
**Extensión oficial:** `.cfv`<br>
**Estado:** experimental; apto para aprendizaje, demostraciones y desarrollo del motor.

C-Forge combina una sintaxis legible, tipado gradual, compilación nativa y un
sistema de valores común llamado `ForgeValue`. El proyecto permite ejecutar un
programa durante el desarrollo o traducirlo a un ejecutable C++ nativo.

Cuando una función necesita un componente conocido del sistema, la VM lo
presenta mediante un alias propio, como `.cfv-gui`, solicita autorización y
captura la salida del instalador. C-Forge muestra el paquete real antes de
ejecutarlo y nunca instala dependencias sin consentimiento explícito.

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

## Características

- Variables inferidas y declaraciones con tipo explícito.
- Números, textos, booleanos, listas, mapas, estructuras y clases.
- Condiciones, ciclos, funciones, métodos y excepciones controladas.
- REPL persistente para ejecutar instrucciones en vivo.
- Compilación de `.cfv` a C++17 y después a un ejecutable nativo.
- Módulos locales y acceso homogéneo mediante `objeto.miembro`.
- Interoperabilidad con Python, C/C++, C# Native AOT, Java, JavaScript y TypeScript.
- Bloques literales `extern("lenguaje") { ... }`.
- Tabla compartida `ForgeValue` y símbolos `cluster`.
- Tareas paralelas, bloques `gpu` y contadores para perfil adaptativo/JIT.
- Archivos, procesos, información del hardware y sockets TCP.
- `array_fast` y matrices densas con almacenamiento numérico contiguo al compilar.
- Formateador, pruebas nativas, hot reload, reparación conservadora y salida WAT.
- Extensión de sintaxis para Visual Studio Code.
- Distribución monolítica C++ e instalador global para macOS.
- Paquetes portables y automatización de lanzamientos para macOS, Linux y Windows.

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

Los lanzamientos etiquetados generan automáticamente paquetes para los tres
sistemas. Mientras los catálogos públicos revisan los manifiestos, se instalan
desde [GitHub Releases](https://github.com/VemorisGroup/C-Forge/releases).

macOS, cuando se publique el tap de Vemoris Group:

```bash
brew install VemorisGroup/cforgev/cforgev
```

Windows, después de la aceptación del manifiesto en WinGet:

```powershell
winget install VemorisGroup.CForgev
```

Debian/Ubuntu usando el paquete descargado del lanzamiento:

```bash
sudo apt install ./cforgev_1.4.0_all.deb
```

La preparación y las condiciones necesarias para ofrecer los comandos cortos
están documentadas en [`DISTRIBUCION.md`](DISTRIBUCION.md).

## Prueba maestra

El archivo [`main.cfv`](main.cfv) demuestra tipado, clases, Python, JavaScript,
TypeScript, Java, C#, C++, ForgeValue, archivos, hardware, procesos, matrices,
paralelismo, GPU/CPU, JIT, `cluster`, networking y manejo de errores.

```bash
cforge test main.cfv
```

Resultado esperado:

```text
C-Forge Test: 10 aprobados, 0 fallidos
```

Para comprobar también el backend nativo:

```bash
cforge --compilar main.cfv -o build/main-final
./build/main-final
```

La suite interna contiene 55 pruebas:

```bash
PYTHONPYCACHEPREFIX=/tmp/cforgev-pycache \
python3 -m unittest discover -s tests -v
```

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
| `herramientas/generar_amalgama.py` | Generador del superarchivo C++. |
| `herramientas/vscode-cforgev/` | Gramática y configuración de VS Code. |
| `tests/test_cforgev.py` | Suite de regresión. |
| `ejemplos/` | Programas y demostraciones. |

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
