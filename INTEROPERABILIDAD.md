# Interoperabilidad de C-Forge 1.6

## Contrato ABI común

El archivo `include/cforgev_ffi.h` define el límite binario experimental de 1.6.
Una función extranjera recibe `CfvValue[]` y devuelve un `CfvValue`. Se admiten
`nulo`, entero de 64 bits, decimal de 64 bits y texto UTF-8.
También se admite `booleano` mediante `CFV_BOOLEAN`, usando `integer` como 0/1.

Las funciones deben usar la firma `CfvForeignFunction`. Un resultado puede incluir
`owner` y `release`: C-Forge copia el valor y un guard RAII invoca `release(owner)`
exactamente una vez, incluso si después ocurre una excepción. Si ambos son nulos,
el texto se considera prestado. Este ABI dinámico histórico conserva escalares.
El ABI LLVM directo añade vistas numéricas y retornos propietarios descritos más
adelante; los objetos nominales aún no poseen un layout público estabilizado.

### ABI dinámico V2

Las bibliotecas nuevas pueden registrar `CfvForeignFunctionV2` mediante
`cfv_register_function_v2`. `CfvValueV2` contiene `struct_size`, flags, longitud
explícita, datos y contrato `owner/release`. La función recibe `CFV_ABI_V2` y debe
comprobarlo antes de acceder a los argumentos.

Esto permite textos con NUL interno y evolución verificable del layout. Si un
nombre existe en V2 y V1, el runtime elige V2; V1 permanece disponible para
compatibilidad binaria. C-Forge verifica `struct_size`, copia resultados
propietarios y ejecuta `release(owner)` exactamente una vez.

V2 define además `CFV_LIST`, `CFV_MAP` y `CFV_RECORD`. Las listas contienen
`CfvValueV2[length]`, los mapas `CfvMapEntryV2[length]` y los objetos nominales
un `CfvRecordV2` con campos etiquetados. Los argumentos llevan
`CFV_V2_BORROWED` y solo son válidos durante la llamada. Un resultado puede
llevar `CFV_V2_OWNED`; en ese caso el liberador del valor raíz debe poseer todo
el grafo y se ejecuta exactamente una vez. Se rechazan liberadores anidados,
profundidades superiores a 64 y colecciones desproporcionadas.

Este layout ya está implementado y probado en el adaptador C/C++ compilado.
Python, JVM, .NET y JavaScript conservan todavía sus conversiones propias; por
eso no se declara aún ABI V2 uniforme para los seis runtimes.

## Python embebido

```text
sea resultado = use_python("math", "sqrt", [81]);
```

Al detectar `use_python`, el compilador localiza `Python.h`, enlaza la biblioteca
embebible, inicializa Python con `Py_Initialize`, importa mediante
`PyImport_ImportModule` y ejecuta la función solicitada.
Cada `PyObject*` se administra mediante un wrapper RAII que equilibra referencias
adquiridas y transferidas. Las excepciones se extraen con `PyErr_Fetch` y se
convierten en errores C-Forge sin imprimir ni abortar el proceso.
Los textos y claves de diccionario usan las variantes de la API Python con
longitud explícita, de modo que un NUL interno no se trunca durante ida o retorno.

## Bibliotecas dinámicas y C# Native AOT

```text
sea resultado = use_native("MiBiblioteca.dylib", "mi_funcion", [10, 20]);
```

macOS/Linux usan `dlopen/dlsym`; Windows usa `LoadLibraryA/GetProcAddress`.
Una exportación C# debe usar Native AOT, `UnmanagedCallersOnly`, convención C y la
estructura ABI exacta. Hay un proyecto en `ejemplos/interop/CSharpNative`.
El intérprete realiza estas llamadas en un proceso auxiliar para aislar el ciclo de
vida de Native AOT; un programa compilado usa la biblioteca directamente.

Para producirlo en un Mac ARM64 con .NET 8 instalado:

```sh
dotnet publish ejemplos/interop/CSharpNative -r osx-arm64 -c Release
```

En Windows se utiliza `-r win-x64` y se carga la DLL resultante con `use_native`.
El ejemplo exporta operaciones con enteros, decimales y textos, todas verificadas
desde el intérprete y desde un ejecutable nativo C-Forge.
Las exportaciones C# nunca dejan cruzar una excepción administrada por el límite
C: la capturan y escriben el mensaje UTF-8 en el búfer de error.

## C++ vinculado y registrado

Una fuente C++ incluye `cforgev_ffi.h`, registra adaptadores durante su
inicialización y se pasa al compilador:

```sh
./cforgev --compilar programa.cfv --vincular biblioteca.cpp -o programa
```

El código C-Forge llama `use_cpp("nombre", [argumentos])`. Una función C++ con
una firma arbitraria necesita un adaptador pequeño al ABI; no es seguro invocarla
directamente porque C++ no posee una ABI universal para tipos y excepciones.

## Seguridad

Una biblioteca nativa se ejecuta dentro del proceso y posee sus mismos permisos.
Solo deben cargarse archivos confiables. C-Forge valida tipos y copia textos en
la frontera, pero no puede volver segura una DLL maliciosa o defectuosa.
Los textos no pueden contener bytes NUL en la versión 1.2.

## C ABI directo del backend LLVM

Una declaración directa usa la firma C indicada por sus tipos:

```cfv
extern_c funcion native_add(a: numero, b: numero): numero
```

Para funciones que pueden fallar debe usarse el contrato comprobado:

```cfv
extern_c segura funcion native_divide(a: numero, b: numero): numero
```

La exportación correspondiente retorna un estado `int` y recibe al final un
puntero de salida y un puntero de error UTF-8:

```c
int native_divide(double a, double b, double *out, const char **error);
```

El estado cero indica éxito. Un estado distinto de cero exige que `*error`
apunte a un texto válido durante la llamada; C-Forge lo convierte en excepción
capturable. Ninguna excepción C++ puede atravesar esta frontera. La ABI directa
1.6 acepta `numero`, `booleano`, `texto` y parámetros `lista<numero>`. Una lista
numérica se presenta como una vista prestada:

```c
typedef struct CfvNumberSlice {
    const double *data;
    uint64_t length;
} CfvNumberSlice;
```

El búfer no se copia, solo permanece válido durante la llamada y la función
extranjera no puede liberarlo ni conservarlo.

Una función `extern_c segura` también puede devolver `lista<numero>` usando:

```c
typedef void (*CfvReleaseFunction)(void *owner);
typedef struct CfvOwnedNumberList {
    const double *data;
    uint64_t length;
    void *owner;
    CfvReleaseFunction release;
} CfvOwnedNumberList;
```

C-Forge valida `data` y `length`, copia los elementos a una lista administrada
y ejecuta `release(owner)` exactamente una vez. La copia evita mezclar
asignadores desconocidos; otros tipos de colección todavía se rechazan.

Los retornos `texto` de `extern_c segura` emplean el mismo patrón propietario:

```c
typedef struct CfvOwnedText {
    const char *data;
    uint64_t length;
    void *owner;
    CfvReleaseFunction release;
} CfvOwnedText;
```

El runtime valida puntero, longitud y ausencia de NUL internos, agrega el
terminador de su representación local y llama al liberador tanto en éxito como
cuando el resultado inválido se convierte en una excepción capturable.

Los parámetros `mapa<numero>` se prestan sin copia como dos arreglos paralelos:

```c
typedef struct CfvNumberMapView {
    const char *const *keys;
    const double *values;
    uint64_t length;
} CfvNumberMapView;
```

Las claves y valores pertenecen a C-Forge y solo son válidos durante la llamada.

Las estructuras y clases cuyos campos sean únicamente `numero`, `texto` o
`booleano` pueden prestarse como un registro nominal:

```c
typedef struct CfvRecordField {
    const char *name;
    CfvValue value;
} CfvRecordField;

typedef struct CfvRecordView {
    const char *type_name;
    const CfvRecordField *fields;
    uint64_t field_count;
} CfvRecordView;
```

Cada campo conserva nombre y etiqueta `CfvType`. Los objetos que poseen listas,
mapas, opciones u otros objetos se rechazan por ahora, evitando publicar un
layout recursivo antes de estabilizar su ownership.
