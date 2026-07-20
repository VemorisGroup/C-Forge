# C-Forgev Language

Soporte oficial de lenguaje para archivos `.cfv` de **C-Forgev**, un proyecto
experimental de Vemoris Group.

## Funciones

- Resaltado TextMate para palabras clave, tipos, funciones, números y textos.
- Reconocimiento de `gpu`, `cluster`, `test` y bloques `extern`.
- Coloreado de los puentes Python, C#, C++, Java, JavaScript y TypeScript.
- Soporte para funciones nativas de sistema, archivos, TCP, matrices y arrays.
- Comentarios de línea mediante `//`.
- Cierre automático de llaves, corchetes, paréntesis y comillas.

## Ejemplo

```cfv
cluster proyecto = "C-Forgev";

funcion cuadrado(numero) {
    retornar numero * numero;
}

gpu {
    resultados = paralelo("cuadrado", [2, 3, 4, 5]);
    mostrar(resultados);
}
```

## Requisitos

Esta extensión proporciona soporte visual y no instala el compilador. Para
ejecutar programas necesitas una instalación independiente de C-Forgev.

```bash
cforge --version
cforge programa.cfv
```

## Estado

Versión `1.3.0`, publicada como Preview. C-Forgev y esta extensión continúan en
desarrollo experimental.

Copyright © 2026 Vemoris Group y Javier. Todos los derechos reservados.
