# Características avanzadas de C-Forge 2.6

Este documento especifica las construcciones avanzadas implementadas en el
parser general de C-Forge.

## Genéricos restringidos

```cfv
clase Contenedor<T> donde T implementa Comparable {
    sea elementos = []
}

sea valores = Contenedor<NumeroComparable>()
```

El parser produce un nodo `Clase` con un miembro sintético `Genericos`:

```text
Clase("Contenedor")
  Genericos(valor={"T": "Comparable"}, hijos=["T"])
  CampoDef("elementos")
```

Una especialización se representa en la llamada como
`Llamada("Contenedor<NumeroComparable>")`. El registro del compilador conserva
la definición canónica de `Contenedor`, verifica que el argumento implemente la
interfaz requerida y registra el tipo concreto en la instancia. Esta identidad
es la entrada estable para la monomorfización de los emisores nativos.

## Memoria controlada

```cfv
inseguro {
    sea bloque = memoria_asignar(256)
    sea puntero = @bloque
    *(puntero + 4) = 255
    memoria_liberar(bloque)
}
```

El AST usa `Inseguro(hijos=[...])` y expresiones `Unario("@")` y
`Unario("*")`. El runtime permite estas operaciones únicamente mientras se
ejecuta el nodo `Inseguro`. Los punteros son capacidades verificadas con
identificador, desplazamiento y tamaño. Se rechazan accesos fuera de límites,
uso después de liberar, tamaños inválidos y bytes fuera de `0..255`.

## Decoradores de compilación

```cfv
#[Ruta("/api/v1/pago", "POST")]
funcion pago(peticion: Request) {
    retornar {"estado": 200}
}
```

El parser genera:

```text
Atributo("Ruta")
  Funcion("pago")
  [Texto("/api/v1/pago"), Texto("POST")]
```

Durante la fase de registro, `#[Ruta]` valida la declaración y genera una
entrada `{ruta, metodo, manejador}` en `__rutas__`. El servidor consulta esa
tabla mediante `rutas_compiladas()`; no necesita registrar manualmente el
endpoint en el bucle HTTP.
