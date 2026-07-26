# Semántica observable y paridad de backends de C-Forge 1.6

## Contrato

Para un programa válido y para una capacidad declarada como común, intérprete,
VM y LLVM deben producir el mismo comportamiento observable:

1. la misma salida estándar, en el mismo orden;
2. la misma clase y mensaje normalizado de error;
3. el mismo código de salida;
4. los mismos cambios autorizados en archivos, red y procesos;
5. los mismos valores retornados por funciones públicas.

Una diferencia es un bug. `cforge parity archivo.cfv` ejecuta la comparación
automática entre intérprete, VM y LLVM. El conjunto de capacidades comparables
se publica en `docs/CAPABILITY-GATES.md`.

## Canal de traducción

- **Intérprete:** tokeniza, analiza, comprueba tipos/ownership y evalúa el AST.
- **VM:** traduce el AST a bytecode versionado, lo verifica y ejecuta sus
  instrucciones.
- **LLVM:** traduce el AST comprobado a LLVM IR, verifica el módulo y produce
  código nativo mediante Clang/LLVM.
- **C++17:** traduce el programa compatible a una unidad nativa C++17.
- **Wasm:** emite WAT para el subconjunto documentado.

Cada backend debe preservar evaluación de izquierda a derecha y orden de efectos.
No puede caer silenciosamente en otro backend. Una capacidad ausente termina con
un diagnóstico verificable.

## Bytecode

Un archivo de bytecode estable debe contener magia, versión de formato, versión
mínima del runtime, tabla de constantes, instrucciones, metadatos de depuración
y checksum. La VM rechaza versiones desconocidas, instrucciones inválidas,
saltos fuera de rango y constantes mal tipadas antes de ejecutar.

Mientras el formato no tenga garantía de compatibilidad publicada, se considera
Developer Preview y no debe almacenarse como artefacto de larga duración.

### Firmas del backend LLVM

Las funciones compiladas por LLVM deben declarar explícitamente el tipo de cada
parámetro. El intérprete y la VM conservan inferencia gradual, pero LLVM necesita
una firma inequívoca para fijar su ABI. Si falta una anotación, el compilador
emite un diagnóstico C-Forge con el nombre de la función y los parámetros
pendientes, sin exponer un traceback interno.

## Excepciones

Los errores internos se normalizan como una excepción C-Forge con origen,
archivo, línea y columna. Los adaptadores deben traducir la excepción extranjera
sin imprimir directamente su traceback. La cancelación y los errores de tareas
conservan su causa.

## FFI y ForgeValue

Todo valor que cruza una frontera se convierte según
`docs/TYPE-SYSTEM-1.6.md`. La ABI C compatible puede ser directa para escalares
y layouts publicados. Python, JVM, .NET y Node poseen heaps distintos: sus
adaptadores usan manejadores RAII, copias verificadas o la Arena por offsets.
No se promete “zero-cost” ni “zero-copy” fuera de rutas medidas y documentadas.

`extern("python")`, `extern("cpp")`, `extern("javascript")`,
`extern("typescript")` y `extern("java")` son fronteras explícitas `unsafe`.
El código literal puede acceder al host con los permisos del proceso. Por ello
no forma parte de una prueba de paridad pura salvo que todos los entornos,
permisos y adaptadores sean idénticos.

El intérprete pide confirmación en una terminal interactiva y bloquea `extern`
en ejecuciones no interactivas. La advertencia especifica que el código puede
acceder a archivos, red y procesos con los permisos del usuario.
`--allow-extern` concede autorización explícita para un archivo confiable. La
compilación nativa también rechaza esos bloques si el autor no entrega esa opción.

## Capacidades y seguridad

Archivos, red, procesos, GPU, cluster, dependencias y código extranjero requieren
una capacidad concedida. El modo seguro rechaza por defecto una capacidad no
concedida. Ocultar `stdout` o `stderr` de una herramienta nunca puede ocultar un
fallo: C-Forge debe conservarlo en un registro diagnóstico y mostrar un resumen.

## Regla de lanzamiento

Una característica solo puede anunciarse como estable cuando:

- tiene especificación y diagnóstico;
- pasa las mismas pruebas en los backends declarados;
- pasa CI en las plataformas publicadas;
- conserva compatibilidad o documenta una migración;
- no depende de una simulación presentada como implementación real.
