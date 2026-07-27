# C-Forge 2.0 — especificación de diseño candidata

Estado: **borrador normativo; no representa todavía funcionalidad implementada**.

C-Forge es un lenguaje autónomo, compilado, seguro por defecto y de propósito
general. Su identidad no consiste en ejecutar otros lenguajes. Toma ideas
probadas de distintos paradigmas, las somete a un único modelo semántico y las
expresa con una sintaxis propia.

La meta de 2.0 es que una misma implementación sirva para software de sistemas,
servicios, aplicaciones, herramientas, cálculo, juegos y WebAssembly. Ninguna
carga de trabajo tiene garantizado superar a todos los demás lenguajes: el
rendimiento se demostrará mediante mediciones reproducibles.

## 1. Principios obligatorios

1. **Seguro por defecto, poderoso de forma explícita.** Punteros crudos,
   alias mutable sin comprobar y llamadas ABI inseguras solo existen dentro de
   `unsafe`.
2. **Un programa, una semántica.** Intérprete de desarrollo, VM, compilador
   nativo y WebAssembly deben conservar el mismo resultado observable.
3. **Inferencia sin ambigüedad.** El compilador infiere tipos cuando puede
   probar uno único; nunca adivina una conversión con pérdida.
4. **Coste visible.** Copias, asignaciones, conteo de referencias, despacho
   dinámico y recolección opcional deben poder identificarse con herramientas.
5. **Determinismo local.** Recursos con ownership se destruyen al abandonar su
   alcance, incluso durante propagación de errores.
6. **Concurrencia estructurada.** Una tarea no sobrevive accidentalmente al
   alcance que la creó.
7. **Compatibilidad verificable.** Toda evolución de la especificación declara
   sus rupturas, migraciones y nivel de estabilidad.

## 2. Identidad sintáctica

- Los bloques usan `{}`. La indentación es visual, no semántica.
- El salto de línea termina una sentencia cuando la expresión está completa;
  `;` es válido, pero opcional.
- `sea` crea un enlace inmutable e inferido; `var` crea uno mutable; `const`
  exige evaluación en compilación.
- Las funciones se declaran con `funcion`; los valores faltantes se modelan
  con `T?`, no con referencias nulas.
- Las palabras de control son propias y consistentes: `si`, `sino`, `segun`,
  `para`, `mientras`, `intentar`, `capturar`, `retornar`.

La gramática léxica y sintáctica candidata completa se encuentra en
[`GRAMMAR-2.0-DRAFT.ebnf`](GRAMMAR-2.0-DRAFT.ebnf).

```cfv
modulo ejemplo

publico registro Persona(nombre: texto, edad: u8)

publico funcion saludo(persona: &Persona) -> texto {
    retornar "Hola, " + persona.nombre
}

funcion principal() -> Resultado<unidad, Error> {
    sea persona = Persona(nombre: "Javier", edad: 20)
    mostrar(saludo(&persona))
    retornar correcto(())
}
```

## 3. Modelo unificado de características

| Necesidad | Forma propia de C-Forge |
|---|---|
| Bajo nivel y RAII | ownership, préstamos, regiones, destructores y `unsafe` |
| Objetos empresariales | clases, registros, estructuras, interfaces y propiedades |
| Scripts productivos | inferencia, colecciones literales, comprensiones y `dinamico` explícito |
| Programación funcional | lambdas, cierres, iteradores, tuberías y pattern matching |
| Eventos | delegados tipados, eventos y flujos asíncronos |
| Metaprogramación | genéricos, `comptime`, reflexión opt-in y atributos |
| Web | compilación WebAssembly y biblioteca web nativa por capacidades |
| Portabilidad | perfiles `nativo`, `portable` y `web`, sin cambiar la semántica |

Esto no crea seis dialectos. Por ejemplo, solo existe una operación estándar
para emitir valores, una jerarquía de colecciones y una forma de declarar
funciones. Los alias de sintaxis de lenguajes ajenos quedan fuera del núcleo 2.0.

## 4. Sistema de tipos

### 4.1 Categorías

- Escalares: `bool`, `caracter`, enteros con signo y sin signo de 8 a 64 bits,
  `isize`, `usize`, `f32`, `f64`, `decimal`, `unidad` y `nunca`.
- Valores compuestos: tuplas, arreglos de tamaño fijo, `estructura`, `registro`
  y `enum`.
- Referencias: `&T` y `&mut T`, siempre válidas durante su vida.
- Punteros crudos: `*const T` y `*mut T`, utilizables solo en `unsafe`.
- Objetos nominales: `clase` e `interfaz`.
- Colecciones estándar: `Lista<T>`, `Mapa<K,V>`, `Conjunto<T>`,
  `Vector<T,N>` y vistas prestadas.
- Funciones: `funcion(A, B) -> R` y `async funcion(A) -> R`.
- Ausencia: `T?`, equivalente semánticamente a `Opcion<T>`.
- Resultado: `Resultado<T,E>`.
- Gradualidad explícita: `dinamico`. El valor conserva una etiqueta de tipo y
  toda operación no demostrable se comprueba en ejecución.

`cualquiera` es el supertipo estático para abstracciones; no permite operar
sobre un valor sin acotarlo mediante patrón, interfaz o conversión comprobada.
`dinamico` sí permite despacho en ejecución y, por eso, hace visible ese coste.

### 4.2 Inferencia y conversiones

La inferencia es local con restricciones propagadas desde parámetros, retornos
y genéricos. Las interfaces públicas deben declarar sus tipos. Los literales
enteros se ajustan al tipo contextual si el valor cabe; sin contexto son `i64`.
Los reales son `f64`.

Solo son implícitas:

- ampliaciones enteras sin pérdida;
- coerción de `&mut T` a `&T`;
- conversión de un tipo concreto a una interfaz implementada;
- elevación de `T` a `T?`.

Toda reducción, cambio de signo riesgoso, conversión texto-número o salida de
`dinamico` exige `convertir<T>(valor)`, que retorna `Resultado<T, ErrorTipo>`.

### 4.3 Genéricos y metaprogramación

Los genéricos estáticos se monomorfizan por defecto. Las restricciones se
expresan con interfaces y `donde`. El despacho por interfaz puede ser estático
o dinámico según el tipo utilizado.

`comptime` ejecuta código puro y limitado durante compilación. Puede producir
constantes, tipos o AST higiénico, pero no acceder a red, reloj o sistema de
archivos sin una capacidad declarada. Los tipos condicionales y mapeados se
resuelven en compilación y nunca crean tipos parcialmente válidos en ejecución.

### 4.4 Tipos nominales y estructurales

Clases, estructuras, registros y enumeraciones son nominales. Las interfaces
son nominales al implementarlas, pero pueden existir contratos estructurales
locales marcados `interfaz estructural`. Una clase admite una sola clase base y
múltiples interfaces. Los registros son inmutables por defecto y poseen
igualdad por valor.

Las clases no tienen una cadena de prototipos mutable. La flexibilidad de ese
modelo se ofrece mediante `ObjetoDinamico`, extensiones y delegación explícita,
sin alterar silenciosamente métodos de tipos estáticos.

## 5. Modelo de memoria

Cada valor propietario tiene un único dueño. Asignarlo mueve el valor salvo que
el tipo implemente `Copiable`; `clonar()` hace una duplicación explícita.

- `&T` permite múltiples préstamos de solo lectura.
- `&mut T` es exclusivo.
- Ningún préstamo puede sobrevivir al propietario.
- Un valor movido no puede reutilizarse.
- `destruir()` se ejecuta exactamente una vez y en orden inverso de
  construcción.
- `deferir` y `recurso` cooperan con el desenrollado de errores.

Los objetos compartidos usan `Compartido<T>` con conteo atómico de referencias.
`Debil<T>` rompe ciclos. La biblioteca puede incluir un heap trazado
`Recolectado<T>` como perfil **opcional y aislado**; no cambia el modelo por
defecto ni permite que una referencia prestada escape.

Las regiones (`region`) agrupan asignaciones cuya vida se demuestra de forma
conjunta. Un valor solo sale de la región si se mueve a un dueño externo válido.
El compilador rechaza uso después de mover, doble destrucción, referencias
colgantes y carreras de datos demostrables.

`unsafe` permite desreferenciar punteros, construir una referencia desde una
dirección, acceder a registros de hardware o cruzar una ABI. El bloque no
desactiva el sistema de tipos: únicamente transfiere al programador cinco
obligaciones verificables — validez, alineación, inicialización, alias y vida.

## 6. Semántica de ejecución

- El alcance es léxico.
- Los argumentos y operandos se evalúan de izquierda a derecha.
- Una asignación devuelve `unidad`.
- Los enteros comprueban overflow de forma predeterminada en todos los perfiles.
  `sumar_envuelto`, `sumar_saturado` y `sumar_comprobado` expresan alternativas.
- `==` es igualdad de valor; `identico` compara identidad donde exista.
- Los `enum` y opcionales deben cubrirse exhaustivamente en `segun`.
- El despacho virtual solo ocurre al usar una referencia de interfaz o clase
  virtual; el resto se resuelve estáticamente.
- Los cierres capturan por préstamo cuando es seguro y por movimiento cuando
  se escribe `mover |...|`.
- Los iteradores son perezosos. Una comprensión crea una colección; una tubería
  no lo hace hasta una operación terminal.

La reflexión es de compilación por defecto. La reflexión en ejecución requiere
`@reflejable` y conserva únicamente los metadatos solicitados.

## 7. Módulos, paquetes y capacidades

Cada archivo pertenece a un módulo. Un paquete contiene un manifiesto
`CForge.pkg`, módulos fuente y un archivo de bloqueo reproducible.

```cfv
modulo tienda.pagos

usar std.red::{Cliente, Solicitud}
usar tienda.modelo::Orden

publico funcion cobrar(orden: &Orden) -> async Resultado<Recibo, ErrorPago>
    efectos { red, reloj }
{
    // ...
}
```

Los símbolos son privados salvo `publico`. Las dependencias forman un grafo
acíclico entre módulos durante inicialización. No existe ejecución implícita al
importar: la inicialización se declara en `funcion iniciar_modulo`.

Los efectos sensibles (`archivos`, `red`, `procesos`, `entropia`, `reloj`,
`hardware`, `ffi`) aparecen en firmas públicas y en el manifiesto. El ejecutor
puede denegar capacidades antes de iniciar el programa.

Los paquetes se identifican por nombre, versión semántica, hash de contenido y
firma. El archivo de bloqueo fija el grafo completo. Una versión revocada no se
selecciona en instalaciones nuevas y genera un diagnóstico verificable.

## 8. Errores

Los fallos recuperables usan `Resultado<T,E>` y el operador `?`. `T?` modela
ausencia, no error. Las excepciones tipadas se reservan para límites donde la
propagación estructurada es más clara y se declaran como efecto `lanza<E>`.

```cfv
funcion cargar(ruta: &Ruta) -> Resultado<Config, ErrorCarga>
    efectos { archivos }
{
    sea texto = Archivo.leer_texto(ruta)?
    retornar Json.decodificar<Config>(texto)
}
```

`intentar/capturar/finalmente` solo captura errores declarados. Un `panic`
representa una violación de contrato o corrupción interna; no debe usarse para
flujo normal. Los diagnósticos contienen código estable, archivo, rango,
explicación, contexto y corrección sugerida. Ningún backend debe filtrar un
traceback de su implementación anfitriona.

## 9. Concurrencia y asincronía

`async funcion` devuelve `Tarea<T>`. `esperar` suspende la tarea, no el hilo.
`grupo` crea concurrencia estructurada: al salir, todas sus tareas terminaron o
fueron canceladas y esperadas.

```cfv
async funcion descargar_todo(urls: &Lista<Url>)
    -> Resultado<Lista<Bytes>, ErrorRed>
    efectos { red }
{
    grupo trabajos {
        sea tareas = [lanzar_tarea descargar(url) para url en urls]
        retornar esperar todas(tareas)
    }
}
```

- `Canal<T>` transfiere mensajes con backpressure.
- `Actor<S,M>` serializa el acceso a su estado.
- `Mutex<T>` y `RwLock<T>` protegen memoria compartida.
- `Atomico<T>` especifica orden de memoria.
- `TokenCancelacion` propaga cancelación cooperativa.
- `seleccionar` espera canales, tareas o tiempo límite.

Solo valores `Transferible` cruzan ejecutores o hilos; solo valores
`Compartible` pueden referenciarse concurrentemente. Estas interfaces son
especiales del compilador y no pueden implementarse de forma insegura fuera de
`unsafe`. El código seguro no contiene carreras de datos. El orden de tareas no
se promete salvo sincronización explícita.

## 10. Modelo de compilación

La cadena autónoma objetivo es:

```text
fuente .cfv
  → lexer y parser
  → AST tipado
  → C-FIR (IR de alto nivel)
  → C-MIR (ownership, efectos y control de flujo)
  → backend nativo / WebAssembly / bytecode
```

C-FIR conserva tipos, genéricos y efectos. C-MIR materializa movimientos,
préstamos, destrucción y representación de datos. El formato de objeto y la ABI
de C-Forge se versionan independientemente de la sintaxis.

El compilador Stage 0 en C++ solo sirve para arrancar. La condición de
autonomía es que el compilador Stage 1 escrito en C-Forge compile Stage 2 y que
Stage 1 y Stage 2 produzcan resultados reproducibles para el conjunto
bootstrap. El sistema operativo y el enlazador son plataforma, no lenguajes
anfitriones.

## 11. Perfiles

- `nativo`: acceso controlado a sistema, SIMD, hardware y ABI de plataforma.
- `portable`: API estable común a sistemas soportados.
- `web`: WebAssembly, DOM y red mediante capacidades del host.
- `embebido`: sin asignador global ni sistema operativo, con biblioteca mínima.

Una API no disponible en un perfil falla en compilación. La biblioteca estándar
se divide por capacidades, no por detección tardía del sistema.

## 12. Conformidad

Una implementación solo puede declararse “C-Forge 2.0 conforme” si:

1. acepta la gramática publicada y rechaza construcciones inválidas;
2. supera las pruebas normativas de tipos, memoria, efectos y concurrencia;
3. conserva la semántica observable entre backends declarados;
4. documenta extensiones y no las confunde con el estándar;
5. publica arquitectura, ABI, versión de biblioteca y limitaciones;
6. pasa el bootstrap reproducible;
7. no anuncia como terminada una capacidad marcada experimental o planeada.

Hasta cumplir esas condiciones, el nombre correcto es **C-Forge 2.0 Draft**.
