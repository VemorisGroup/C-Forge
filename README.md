# C-Forge

![C-Forge](assets/cforgev-logo.svg)

C-Forge es un lenguaje de programación creado por Vemoris Group.
Su objetivo es ofrecer una sintaxis clara, ejecución nativa y una biblioteca
estándar escrita en el propio lenguaje.

> Estado actual: **C-Forge 2.6.0 estable**. El núcleo, CLI y biblioteca estable
> superan el gate reproducible `make release-check`. Las capacidades marcadas
> como experimentales no forman parte del contrato estable. C-Forge no posee
> todavía certificación regulatoria para sistemas bancarios, médicos,
> aeroespaciales ni otras cargas críticas.

## Principios

- Los programas usan la extensión oficial `.cfv`.
- La biblioteca estándar y las pruebas del lenguaje se escriben en C-Forge.
- El ejecutable no necesita otro runtime para ejecutar un programa `.cfv`.
- C++ se conserva únicamente como bootstrap para construir el motor nativo.
- Una capacidad solo se anuncia como terminada cuando tiene evidencia y pruebas.

El estado verificable de cada componente está en
[`capabilities.json`](capabilities.json).

## Ejemplo

```text
funcion factorial(n) {
    si (n <= 1) {
        retornar 1
    }
    retornar n * factorial(n - 1)
}

sea nombre = "C-Forge"
mostrar(nombre)
mostrar(factorial(6))
```

Guarda el programa como `hola.cfv` y ejecútalo:

```sh
cforge hola.cfv
```

## Construir desde el código fuente

Se necesita un compilador C++20 únicamente para generar el motor inicial:

```sh
make clean
make build
./cforge --version
make check
make test
```

Después de construirlo, el motor ejecuta archivos `.cfv` directamente.

## Instalar

```sh
./install.sh
cforge --version
```

También se puede instalar con Make:

```sh
sudo make install
```

## CLI

| Comando | Función |
|---|---|
| `cforge archivo.cfv` | Ejecutar un programa |
| `cforge run archivo.cfv` | Ejecutar un programa |
| `cforge repl` | Abrir la consola interactiva |
| `cforge check archivo.cfv` | Verificar la sintaxis |
| `cforge test archivo.cfv` | Ejecutar pruebas C-Forge |
| `cforge fmt archivo.cfv` | Validar sintaxis y formato sin modificar |
| `cforge --version` | Mostrar la versión |
| `cforge --help` | Mostrar ayuda |

## Biblioteca estándar

Los módulos oficiales están en [`stdlib/`](stdlib/) y conservan formato `.cfv`.
Actualmente hay **30 módulos que pasan el análisis sintáctico y la carga del
motor**. Incluyen utilidades para:

- texto, números y matemáticas;
- listas, mapas, colecciones y algoritmos;
- archivos y JSON;
- concurrencia y canales;
- validación, logging y manejo de errores;
- red, bases de datos, criptografía y gráficos en estado experimental.

Se retiraron del árbol activo los módulos falsos, incompletos o dependientes de
otros runtimes. Pasar el análisis y la carga no equivale a estabilidad de API:
los módulos experimentales no deben emplearse todavía como base de sistemas
críticos.

## Pruebas

La suite principal está escrita en C-Forge:

```sh
make check
make test
make bootstrap-check
make release-check
```

Los archivos están en [`tests/cfv/`](tests/cfv/). El proceso falla de inmediato
si una prueba devuelve un código distinto de cero. El gate estable verifica 30
módulos, ejecuta 20 archivos de prueba nativos, rechaza entradas dañadas,
comprueba el CLI y la instalación y ejecuta el núcleo con ASan/UBSan. Es una
validación reproducible del alcance estable, no una certificación regulatoria.

## Arquitectura

```text
programa .cfv
     │
     ▼
lexer → parser → AST → runtime
     │
     ▼
ejecutable nativo C-Forge
```

Stage 0 y los backends de emisión directa se encuentran en
[`bootstrap/`](bootstrap/). Las fuentes antiguas de Stage 1 se retiraron porque
no pasaban el parser actual. Consulta [`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md)
para conocer el estado verificable.

## Plataformas

- macOS ARM64: validación local activa.
- Linux: validación mediante CI.
- Windows: backend experimental; falta validación física completa.

Los emisores mínimos Mach-O ARM64, ELF x64 y PE x64 aceptan el programa
`mostrar("texto")`. Los tres incluyen también variantes Core B6.8 para
variables, aritmética, control, funciones, listas y texto dinámico. El gate comprueba sus cabeceras y ejecuta
únicamente el formato compatible con el sistema anfitrión. Todavía no son
backends completos del lenguaje.

El avance B6.9 comienza con `bootstrap/core_ir.cfv`: un IR común escrito en
C-Forge que fija layouts reproducibles para estructuras, clases, campos y
métodos antes de bajarlos a los tres formatos nativos.
La etapa `bootstrap/core_object_lowering.cfv` inicia B6.10 y convierte
instancias, lecturas y escrituras de campos a almacenamiento escalar común.
Esa representación ya alimenta los emisores Mach-O ARM64, ELF x64 y PE x64:
el gate construye el mismo programa de objetos en los tres formatos y ejecuta
el binario compatible con el anfitrión. B6.12 añade métodos de instancia y la
referencia `este`; B6.13 incorpora métodos mutables mediante expansión segura
de escrituras sobre la instancia. B6.14 incorpora el método reservado `crear`
como constructor y `destruir` como finalizador determinista; las instancias se
destruyen en orden inverso al terminar el ámbito superior. Interfaces y
destrucción de ámbitos anidados siguen en progreso.
B6.15 añade `interfaz` y `implementa`: el IR verifica la existencia del
contrato, sus métodos, retornos y parámetros antes de cualquier emisión.
El mismo programa válido se genera para Mach-O, ELF y PE; un contrato
incompleto es rechazado por el gate.

No se anunciará compatibilidad total con una plataforma hasta que instalación,
ejecución, pruebas y desinstalación estén verificadas en ella.

## Seguridad

Reporta vulnerabilidades siguiendo [`SECURITY.md`](SECURITY.md). C-Forge aún no
ha completado una auditoría de seguridad profesional independiente.

## Documentación

- [Especificación](ESPECIFICACION.md)
- [Dirección del núcleo](docs/CORE-DIRECTION.md)
- [Bootstrap](docs/BOOTSTRAP.md)
- [Preparación para producción](docs/PRODUCTION-READINESS.md)
- [Política de completitud](docs/COMPLETENESS-POLICY.md)
- [Registro de capacidades](capabilities.json)

## Licencia

Consulta [`LICENSE`](LICENSE).

Copyright © 2026 Vemoris Group.
