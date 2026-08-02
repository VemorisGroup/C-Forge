# Forge Shared Arena ABI 1.0

Forge Shared Arena es el transporte binario experimental de C-Forge para compartir
datos entre procesos mediante un archivo mapeado en memoria. Los procesos no
intercambian punteros virtuales: intercambian offsets `uint64` relativos al inicio
del mapeo. Así, cada runtime puede mapear el archivo en una dirección distinta sin
corromper referencias.

## Distribución física

```text
[ArenaHeader alineado a 64 bytes]
[RecordHeader alineado a 64 bytes]
[relleno de alineación]
[payload]
[siguiente RecordHeader]
...
```

`ArenaHeader` contiene magic, versión ABI, tamaño de cabecera, capacidad, bytes
usados, generación, registros vivos y un mutex compartido entre procesos.
`RecordHeader` contiene tipo, tamaño, offset del payload, generación, contador de
referencias atómico, estado y checksum FNV-1a.

## Reglas de seguridad

- Todo offset y tamaño se valida antes de formar una vista.
- Los registros son append-only en ABI 1.0; liberar no reutiliza inmediatamente
  su memoria y evita el problema ABA.
- `retain` y `release` son atómicos. Al llegar a cero el registro queda ilegible.
- El mutex de asignación es `PTHREAD_PROCESS_SHARED` en macOS/Linux y un mutex
  nombrado en Windows.
- El payload se verifica con checksum antes de entregarlo al adaptador.
- La Arena transporta bytes y expone vistas validadas mediante offsets relativos.

## Uso dentro de C-Forge

La Arena es una primitiva interna del runtime para buffers compartidos de
C-Forge. Los antiguos adaptadores a runtimes extranjeros fueron retirados y no
forman parte de ABI 1.0. Ninguna función se enruta por nombre hacia otro
lenguaje.

## Estado de ABI 1.0

La implementación de referencia está en `include/cforge_shared_arena.h`. La ABI
de cabeceras, offsets, sincronización y ciclo de vida está operativa y probada.
Los adaptadores externos deben validar `magic`, versión, tipo, límites y checksum
antes de construir una vista del payload. No deben conservar una vista después de
liberar su offset.
