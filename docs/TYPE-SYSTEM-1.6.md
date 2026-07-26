# Sistema de tipos normativo de C-Forge 1.6

Estado: Developer Preview. Este documento define el contrato semántico. Cuando
una implementación contradiga este documento, la contradicción es un defecto.

## Juicios y tipos

`Γ ⊢ e : T` significa que, bajo el entorno `Γ`, la expresión `e` tiene tipo `T`.
`T ≈ U` significa que ambos tipos son compatibles para una asignación o llamada.

Tipos fundamentales:

- `numero`, `texto`, `booleano`, `nulo` y `cualquiera`.
- `lista<T>`, `mapa<T>`, `tupla<T...>`, `conjunto<T>` y `opcion<T>`.
- Tipos nominales declarados por `estructura`, `clase` e `interfaz`.
- Parámetros de tipo de funciones genéricas.
- `tarea<T>` para el resultado de una función `async`.

`cualquiera` es la salida gradual explícita: acepta cualquier valor y pospone la
comprobación hasta la operación que lo consume. No convierte silenciosamente
un valor en otro tipo.

## ForgeValue

El valor común del intérprete y de las fronteras del runtime es `ForgeValue`.
Tiene una etiqueta de tipo, una carga útil y, para valores extranjeros, el
runtime de procedencia y su propietario. Sus formas observables son:

| Tipo C-Forge | Carga de ForgeValue |
|---|---|
| `numero` | entero de 64 bits o flotante de 64 bits |
| `texto` | texto UTF-8 |
| `booleano` | verdadero o falso |
| `nulo` | ausencia de carga |
| `lista<T>` / `tupla<T...>` / `conjunto<T>` | secuencia de ForgeValue |
| `mapa<T>` | claves UTF-8 y valores ForgeValue |
| tipo nominal | identidad nominal y campos ForgeValue |
| valor extranjero | manejador RAII, procedencia y adaptador |

Los procesos separados no comparten punteros del heap. La Arena compartida usa
offsets de 64 bits y cabeceras verificables; todo adaptador debe validar versión,
tipo, tamaño y límites antes de leer. “Zero-copy” solo se declara para una ruta
ABI que demuestre compatibilidad de memoria.

## Inferencia y compatibilidad

- `42 : numero`, `3.14 : numero`, `"x" : texto`, `verdadero : booleano`,
  `nulo : nulo`.
- Una lista o conjunto no vacío infiere el tipo común de sus elementos.
- Una tupla conserva el tipo de cada posición.
- Las claves de un mapa literal deben ser textos.
- `nulo` es compatible con `opcion<T>`. Un `T` es compatible con `opcion<T>`.
- Dos tipos construidos son compatibles cuando tienen el mismo constructor y
  argumentos compatibles.
- `cualquiera ≈ T` y `T ≈ cualquiera`; cualquier operación posterior todavía
  debe comprobar que el valor tenga la forma requerida.
- No hay conversión implícita general entre `numero`, `texto` y `booleano`.

Una declaración sin anotación toma el tipo de su inicializador. Una declaración
anotada comprueba el inicializador. Después de declararse, una variable conserva
su tipo estático, salvo que sea `cualquiera`.

## Operadores

- `+`, `-`, `*`, `/` y `%` numéricos requieren operandos `numero`.
- `+` concatena dos `texto`.
- Las comparaciones ordenadas requieren operandos comparables del mismo tipo.
- `==` y `!=` admiten valores compatibles.
- `y`, `o` y `no` requieren `booleano`.
- La división o módulo por cero produce una excepción C-Forge comprobable.

## Funciones, retornos y genéricos

Los argumentos se comprueban contra sus parámetros. Todo `retornar e` debe ser
compatible con el tipo de retorno declarado. Una función sin retorno explícito
retorna `nulo`.

En una llamada genérica, los parámetros de tipo se unifican con los argumentos.
Una sustitución contradictoria es un error. La implementación puede
monomorfizar una instancia, pero no puede cambiar su resultado observable.

Una función `async` produce `tarea<T>`. `await` solo es válido dentro de una
función `async` y extrae `T` de una tarea.

## Tipos nominales e interfaces

Las estructuras y clases son nominales: dos declaraciones con los mismos campos
no son el mismo tipo. Una clase que declara `implementa I` debe proporcionar
todas las firmas de `I` con parámetros y retorno compatibles. Esta versión no
define herencia de implementación ni visibilidad pública/privada.

## Ownership y memoria

El análisis vigente aplica estas reglas antes de VM o LLVM:

- Un valor poseído no puede utilizarse después de moverse o destruirse.
- Puede existir cualquier cantidad de préstamos compartidos, o un préstamo
  mutable exclusivo, pero no ambos simultáneamente.
- Un préstamo local no puede escapar de la vida de su propietario.
- Los ciclos fuertes de ownership se rechazan.
- Los destructores nominales se ejecutan de forma determinista al terminar su
  región.
- `unsafe { ... }` marca una frontera explícita donde el programador asume las
  obligaciones que el analizador no puede demostrar.

Este análisis es una implementación Developer Preview y no constituye todavía
una prueba formal de seguridad de memoria. Toda operación FFI se considera
`unsafe` salvo que el adaptador esté declarado seguro y valide su contrato ABI.

## Errores obligatorios

Los errores estáticos deben incluir archivo, línea y columna cuando estén
disponibles. Un backend no puede aceptar silenciosamente un programa rechazado
por el analizador normativo. Si una característica no está implementada en un
backend, este debe emitir “capacidad no soportada”, no simularla.
