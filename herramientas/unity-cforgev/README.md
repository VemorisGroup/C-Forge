# C-Forge Scripting Plugin para Unity

Este plugin embebe el interprete de C-Forge en Unity via P/Invoke (native plugin),
permitiendo escribir logica de juego en archivos `.cfv` sin recompilar el proyecto.

## Instalacion

1. Compilar `libcforgev` como native plugin:
   ```bash
   cd C-Forge/
   # macOS:
   g++ -std=c++20 -O2 -shared -fPIC \
       -DCFV_WITH_OPENSSL \
       -o libcforgev.bundle cforgev.cpp \
       -I./include \
       -I$(brew --prefix openssl)/include \
       -L$(brew --prefix openssl)/lib -lcrypto

   # Linux:
   g++ -std=c++20 -O2 -shared -fPIC \
       -DCFV_WITH_OPENSSL \
       -o libcforgev.so cforgev.cpp \
       -I./include -I/usr/include/node \
       /usr/lib/x86_64-linux-gnu/libcrypto.so.3

   # Windows (desde Developer Command Prompt):
   cl /std:c++20 /O2 /LD /DCFV_WITH_OPENSSL cforgev.cpp /link /OUT:cforgev.dll
   ```

2. Copiar los archivos de este directorio a tu proyecto Unity:
   ```
   Assets/
     CForge/
       Scripts/
         CForgeRuntime.cs      -- P/Invoke wrapper
         CForgeComponent.cs    -- MonoBehaviour para scripting
         CForgeManager.cs      -- Singleton global
       Plugins/
         libcforgev.bundle     -- macOS
         libcforgev.so         -- Linux
         cforgev.dll           -- Windows
   ```

3. En Player Settings → Other Settings → Scripting Backend: Mono o IL2CPP.
   Para IL2CPP necesitas marcar la dll como "Include in build".

## Uso desde C# / Inspector

```csharp
// Agregar CForgeComponent a cualquier GameObject.
// Asignar ScriptPath en el Inspector: "Scripts/enemigo.cfv"
// El script se carga en Start().
```

## Ejemplo de script (enemigo.cfv)

```cfv
sea vida = 100
sea velocidad = 3.5

funcion on_inicio(): nulo {
    mostrar("Enemigo iniciado con {vida} de vida")
}

funcion on_colision(objeto: texto): nulo {
    si (objeto == "Bala") {
        vida -= 25
        si (vida <= 0) {
            mostrar("Enemigo destruido!")
        }
    }
}

funcion on_actualizar(delta: numero): numero {
    // Retorna la nueva posicion X
    retornar velocidad * delta
}
```
