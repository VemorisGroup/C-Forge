# Changelog — C-Forge Language Support (VS Code)

## 3.3.0

- Versión sincronizada con C-Forge 3.3.0.
- Resaltado de sintaxis actualizado: `funcion`, `retornar`, `importar`, `exportar`,
  `clase`, `nuevo`, `intentar`, `capturar`, `lanzar`, `para`, `mientras`, `si`, `sino`,
  `sea`, `nulo`, `verdadero`, `falso`, `y`, `o`, `no`.
- Palabras clave de tipos anotadas: `numero`, `texto`, `booleano`, `lista`, `mapa`, `nulo`.
- Funciones de la stdlib resaltadas: `mostrar`, `longitud`, `agregar`, `eliminar`,
  `tipo`, `texto`, `numero`, `entero`, `raiz`, `abs`, `redondear` y más.
- Constantes SDL2 incluidas: `SDL_TIPO_TECLA_DOWN`, `SDL_TECLA_ESC`, etc.
- Soporte de snippets para patrones comunes (`funcion`, `clase`, `para`, `mientras`,
  `intentar/capturar`, `si/sino`).
- Icono de lenguaje actualizado para archivos `.cfv`.
- Eliminadas referencias a puentes Java, Python y JS que nunca existieron en esta versión.
- Eliminadas referencias a `console.log`, `System.out.println`, `gpu` y `cluster`.

## 2.6.0

- Primer lanzamiento público estable alineado con el motor nativo C-Forge 2.6.
- Resaltado básico de `.cfv`, configuración de bloques, pares de brackets.
- Comandos: `C-Forge: Ejecutar archivo`, `C-Forge: Comprobar archivo`.
- Icono oficial registrado en el Marketplace de VS Code.
