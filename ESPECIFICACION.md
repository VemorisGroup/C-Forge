# Especificación activa de C-Forge 2.6 (Developer Preview)

Este documento describe únicamente el subconjunto ejecutado por el motor
actual. Los diseños futuros pertenecen a documentos de hoja de ruta y no forman
parte del contrato del lenguaje.

## Archivos y codificación

- Extensión: `.cfv`.
- Texto: UTF-8.
- Comentarios de línea: `// comentario`.
- El punto y coma es opcional al final de una instrucción.
- Los bloques se delimitan con `{` y `}`.

## Valores

El runtime reconoce `nulo`, `numero`, `texto`, `booleano`, `lista`, `mapa`,
`tupla`, `conjunto`, arreglos numéricos rápidos y matrices. Las declaraciones
usan inferencia o una anotación gradual:

```text
sea nombre = "C-Forge"
sea intentos: numero = 3
sea activo: booleano = verdadero
sea valores: lista = [1, 2, 3]
```

El motor rechaza operaciones que requieren tipos incompatibles. `+` suma dos
números o concatena dos textos. La división y el módulo por cero producen una
excepción C-Forge.

## Control de flujo

```text
si (condicion) {
    mostrar("sí")
} sino {
    mostrar("no")
}

mientras (condicion) {
    // instrucciones
}

para elemento en coleccion {
    mostrar(elemento)
}
```

## Funciones

```text
funcion sumar(a: numero, b: numero): numero {
    retornar a + b
}

sea duplicar = funcion(valor) {
    retornar valor * 2
}
```

Las funciones pueden capturar su ámbito léxico. Los argumentos y retornos
anotados se comprueban según el sistema gradual disponible en el motor.

## Colecciones

```text
sea lista = [2, 4]
agregar(lista, 6)
mostrar(lista[0])
mostrar(longitud(lista))

sea mapa = {"nombre": "C-Forge"}
mostrar(mapa["nombre"])
```

## Clases

```text
clase Contador {
    constructor(inicial) {
        esto.valor = inicial
    }

    incrementar() {
        esto.valor = esto.valor + 1
    }
}
```

Las características verificadas de clases se ejercitan en
`tests/cfv/08_clases.cfv`. Las construcciones no cubiertas por pruebas deben
considerarse experimentales.

## Errores

```text
intentar {
    lanzar "fallo"
} capturar (error) {
    mostrar(error)
}
```

Los errores no capturados terminan con código distinto de cero y un diagnóstico
identificado como C-Forge.

## Módulos

```text
importar "matematica"
mostrar(potencia(2, 8))
```

La búsqueda utiliza `CFORGE_STDLIB` y las rutas configuradas por el motor. Solo
los módulos presentes en `stdlib/` forman parte de la distribución activa.

## Pruebas

`afirmar(condicion, mensaje)` falla si la condición es falsa. Los archivos de
prueba se ejecutan con `cforge test archivo.cfv`.

## Implementación y límites

El motor activo está arrancado desde C++20, pero los programas y la biblioteca
estándar se escriben en C-Forge. No existen en esta especificación puentes a
Python, Java, JavaScript, TypeScript o C#. Los backends directos Mach-O, ELF y
PE son prototipos literales y no implementan todavía toda esta especificación.

La lista exacta de capacidades y su estado está en `capabilities.json`.
