# C-Forge para iOS

App iOS nativa con SwiftUI que embebe el interprete C-Forge via Objective-C++.
Escribe la logica en archivos `.cfv` incluidos como recursos del bundle.

## Requisitos

- Xcode 15+
- iOS 16+ target
- macOS con Apple Silicon o Intel

## Estructura

```
ios-cforgev/
  CForgeApp/
    Sources/
      CForge/
        cforgev.cpp            -- Interprete C-Forge (copiar del repo raiz)
        CForgeRuntime.h        -- Header ObjC
        CForgeRuntime.mm       -- Bridge ObjC++ → C-Forge C API
        CForgeApp-Bridging-Header.h
      CForgeApp/
        CForgeAppMain.swift    -- @main SwiftUI entry point
        ContentView.swift      -- UI SwiftUI con REPL integrado
    Resources/
      main.cfv                 -- Script principal (bundle resource)
  CMakeLists.txt               -- Para compilar como framework iOS
  README.md
```

## Setup en Xcode

1. Abrir Xcode > Create New Project > App (SwiftUI)
2. Agregar al proyecto:
   - `CForgeRuntime.h` y `CForgeRuntime.mm` (grupo CForge)
   - `cforgev.cpp` (copiar del repo raiz, mismo grupo)
   - `main.cfv` (Resources, marcar "Add to target")
3. En Build Settings:
   - **Swift Compiler - General > Objective-C Bridging Header**:
     `$(SRCROOT)/CForgeApp/Sources/CForge/CForgeApp-Bridging-Header.h`
   - **C++ Language Dialect**: C++20
   - **Enable C++ Exceptions**: YES
   - **Enable C++ RTTI**: YES
4. Build & Run en simulador o dispositivo

## Uso desde Swift

```swift
// Ejecutar script del bundle
CForgeRuntime.runBundleScript("main", error: nil)

// Evaluar expresion
let r = CForgeRuntime.evalNumber("calcular_prestamo(100000, 12.5, 60)")

// Llamar funcion definida en .cfv
let msg = CForgeRuntime.evalString("on_boton_presionado()")
```

## Ejemplo main.cfv

```cfv
sea contador = 0

funcion on_boton_presionado(): texto {
    contador += 1
    retornar "Tap #{contador}"
}

funcion calcular_prestamo(monto: numero, tasa: numero, meses: numero): numero {
    sea t = tasa / 100.0 / 12.0
    retornar monto * t / (1.0 - potencia(1.0 + t, -meses))
}
```

## Compilar para dispositivo (Release)

En Xcode: Product > Archive → Distribute App → App Store Connect o Ad Hoc.

Para compilar con CMake (alternativo):
```bash
# Instalar ios-cmake toolchain
brew install cmake

git clone https://github.com/leetal/ios-cmake
cmake -B build-ios \
  -DCMAKE_TOOLCHAIN_FILE=ios-cmake/ios.toolchain.cmake \
  -DPLATFORM=OS64 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-ios
```
