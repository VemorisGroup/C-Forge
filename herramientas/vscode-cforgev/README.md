# C-Forge Language

Soporte oficial de lenguaje para archivos `.cfv` de **C-Forge**, un proyecto
experimental de Vemoris Group.

## Funciones

- Resaltado TextMate para palabras clave, tipos, funciones, números y textos.
- Reconocimiento de `gpu`, `cluster` y `test` como sintaxis experimental.
- Soporte para funciones nativas de sistema, archivos, TCP, matrices y arrays.
- Resaltado de la sintaxis propia `mostrar`, `agregar` y `longitud`.
- Comentarios de línea mediante `//`.
- Cierre automático de llaves, corchetes, paréntesis y comillas.
- Cliente LSP integrado sin dependencias externas: diagnósticos, hover tipado,
  autocompletado, definición/referencias entre archivos, renombrado y formato.
- Depuración DAP con breakpoints normales, condicionales, por cantidad de
  impactos y logpoints; inspección segura de objetos y colecciones.

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

Versión `1.6.0`, publicada como Preview. C-Forge y esta extensión continúan en
desarrollo experimental.

Copyright © 2026 Vemoris Group y Javier. Todos los derechos reservados.
