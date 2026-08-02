# C-Forge Language

Soporte oficial de lenguaje para archivos `.cfv` de **C-Forge 2.6 estable**.

## Funciones

- Resaltado TextMate para palabras clave, tipos, funciones, números y textos.
- Reconocimiento de `gpu`, `cluster` y `test` como sintaxis experimental.
- Soporte para funciones nativas de sistema, archivos, TCP, matrices y arrays.
- Resaltado de la sintaxis propia `mostrar`, `agregar` y `longitud`.
- Comentarios de línea mediante `//`.
- Cierre automático de llaves, corchetes, paréntesis y comillas.
- Comprobación mediante el comando estable `cforge check`.
- Ejecución del archivo activo mediante el comando estable `cforge`.
- Autocompletado básico de palabras reservadas.

## Ejemplo

```cfv
cluster proyecto = "C-Forge";

funcion cuadrado(numero) {
    retornar numero * numero;
}

gpu {
    resultados = paralelo("cuadrado", [2, 3, 4, 5]);
    mostrar(resultados);
}

agregar(resultados, 36)
mostrar(longitud(resultados))
```

## Requisitos

Esta extensión proporciona soporte visual y no instala el compilador. Para
ejecutar programas necesitas una instalación independiente de C-Forge.

```bash
cforge --version
cforge programa.cfv
```

## Estado

Versión `2.6.0`, alineada con el núcleo estable. LSP y DAP permanecen fuera de
esta versión hasta disponer de implementación y pruebas públicas.

Copyright © 2026 Vemoris Group y Javier. Todos los derechos reservados.
