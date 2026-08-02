# C-Forge

![C-Forge](assets/cforgev-logo.svg)

C-Forge es un lenguaje de programación en desarrollo creado por Vemoris Group.
Su objetivo es ofrecer una sintaxis clara, ejecución nativa y una biblioteca
estándar escrita en el propio lenguaje.

> Estado actual: **2.6.0 Developer Preview**. Es adecuado para evaluación,
> aprendizaje y prototipos. Todavía no está certificado para sistemas bancarios,
> médicos, aeroespaciales ni otras cargas críticas.

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
Actualmente hay **31 módulos que pasan el análisis sintáctico y la carga del
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
```

Los archivos están en [`tests/cfv/`](tests/cfv/). El proceso falla de inmediato
si una prueba devuelve un código distinto de cero. El gate actual verifica 31
módulos y ejecuta 10 archivos de prueba nativos. Esta es una base reproducible,
no una certificación profesional de todo el lenguaje.

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

Stage 0 y los prototipos de emisión directa se encuentran en
[`bootstrap/`](bootstrap/). Las fuentes antiguas de Stage 1 se retiraron porque
no pasaban el parser actual. Consulta [`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md)
para conocer el estado verificable.

## Plataformas

- macOS ARM64: validación local activa.
- Linux: validación mediante CI.
- Windows: backend experimental; falta validación física completa.

Los emisores directos Mach-O ARM64, ELF x64 y PE x64 están limitados al
programa prototipo `mostrar("texto")`. El gate comprueba sus cabeceras y ejecuta
únicamente el formato compatible con el sistema anfitrión. Todavía no son
backends completos del lenguaje.

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
