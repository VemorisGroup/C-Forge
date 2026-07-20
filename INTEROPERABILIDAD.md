# Interoperabilidad de C-Forge 1.2

## Contrato ABI común

El archivo `include/cforgev_ffi.h` define el límite binario experimental de 1.2.
Una función extranjera recibe `CfvValue[]` y devuelve un `CfvValue`. Se admiten
`nulo`, entero de 64 bits, decimal de 64 bits y texto UTF-8.

Las funciones deben usar la firma `CfvForeignFunction`. Un resultado puede incluir
`owner` y `release`: C-Forge copia el valor y un guard RAII invoca `release(owner)`
exactamente una vez, incluso si después ocurre una excepción. Si ambos son nulos,
el texto se considera prestado. Las colecciones y objetos aún no cruzan el ABI.

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
