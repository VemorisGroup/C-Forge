# C-Forge Scripting Plugin para Unreal Engine 5

Este plugin embebe el interprete de C-Forge dentro de Unreal Engine 5,
permitiendo escribir logica de juego en archivos `.cfv` sin recompilar el proyecto.

## Instalacion

1. Compilar `libcforgev` como shared library:
   ```bash
   cd C-Forge/
   g++ -std=c++20 -O2 -shared -fPIC \
       -DCFV_WITH_OPENSSL \
       -o libcforgev.so cforgev.cpp \
       -I./include \
       /usr/lib/x86_64-linux-gnu/libcrypto.so.3
   # macOS:
   g++ -std=c++20 -O2 -shared -fPIC \
       -DCFV_WITH_OPENSSL \
       -o libcforgev.dylib cforgev.cpp \
       -I./include \
       -I$(brew --prefix openssl)/include \
       -L$(brew --prefix openssl)/lib -lcrypto
   ```

2. Copiar esta carpeta a `[TuProyectoUE]/Plugins/CForgeScripting/`

3. Copiar `libcforgev.so` / `libcforgev.dylib` a:
   `[TuProyectoUE]/Plugins/CForgeScripting/Binaries/`

4. Habilitar el plugin en el Editor: Edit → Plugins → buscar "CForge"

5. Recompilar el proyecto desde el Editor (o Visual Studio).

## Uso desde Blueprint / C++

```cpp
// En tu Actor C++:
#include "CForgeComponent.h"

// Agregar como componente:
UCForgeComponent* Script;

// En BeginPlay:
Script->RunFile("Content/Scripts/enemigo.cfv");

// Llamar funcion C-Forge desde C++:
Script->CallFunction("on_atacar", {Damage});

// Leer variable de C-Forge:
float Vida = Script->GetNumber("vida");
```

## Ejemplo de script enemigo.cfv

```cfv
sea vida = 100
sea velocidad = 150

funcion on_inicio(): nulo {
    mostrar("Enemigo listo, vida={vida}")
}

funcion on_atacar(dano: numero): nulo {
    vida -= dano
    si (vida <= 0) {
        mostrar("Enemigo muerto!")
    }
}

funcion on_actualizar(delta: numero): nulo {
    // Logica de movimiento, IA, etc.
}
```

## Arquitectura

```
CForgeScripting/
  CForgeScripting.uplugin          -- Descriptor del plugin UE5
  Source/CForgeScripting/
    CForgeScripting.Build.cs       -- Reglas de compilacion
    Public/
      CForgeComponent.h            -- Componente UCForgeComponent
      CForgeSubsystem.h            -- Subsistema global del interprete
    Private/
      CForgeComponent.cpp          -- Implementacion
      CForgeSubsystem.cpp          -- Init/Shutdown del interprete
  Binaries/
    libcforgev.so / libcforgev.dylib / cforgev.dll
```
