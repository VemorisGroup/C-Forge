# C-Forge para Android

App Android nativa que embebe el interprete de C-Forge via JNI.
Escribe toda la logica de tu app en archivos `.cfv` dentro de `assets/scripts/`.

## Requisitos

- Android Studio Hedgehog o superior
- Android NDK r25+
- SDK minimo: API 24 (Android 7.0)
- CMake 3.22+

## Compilar y ejecutar

1. Abrir `android-cforgev/` en Android Studio
2. Sync Gradle
3. Build > Make Project
4. Run en emulador o dispositivo

## Estructura

```
android-cforgev/
  app/
    src/main/
      cpp/
        cforgev.cpp         -- Interprete C-Forge (copiar del repo raiz)
        cforgev_jni.cpp     -- Bridge JNI: Java ↔ C-Forge
        CMakeLists.txt      -- Config NDK
      java/com/vemoris/cforgeapp/
        MainActivity.java   -- Activity principal
        CForgeRuntime.java  -- Wrapper Java para JNI
      assets/scripts/
        main.cfv            -- Script principal de la app
        ui.cfv              -- Logica de UI
      res/layout/
        activity_main.xml   -- Layout Android
    build.gradle            -- Config Gradle del modulo
  build.gradle              -- Config Gradle raiz
  settings.gradle
```

## Ejemplo: app.cfv

```cfv
// assets/scripts/main.cfv

sea contador = 0

funcion on_inicio(): nulo {
    mostrar("App C-Forge iniciada en Android!")
}

funcion on_boton_presionado(): texto {
    contador += 1
    retornar "Presionado {contador} veces"
}

funcion on_texto_cambiado(texto_nuevo: texto): nulo {
    mostrar("Usuario escribio: {texto_nuevo}")
}

funcion calcular_interes(capital: numero, tasa: numero, anos: numero): numero {
    retornar capital * potencia(1.0 + tasa / 100.0, anos) - capital
}
```

## Llamar codigo Android desde C-Forge (extern Java)

```cfv
extern("java") {
    android.widget.Toast.makeText(context, "Hola desde C-Forge!", 0).show();
}
```

## Compilar como AAR (libreria Android)

```bash
cd android-cforgev/
./gradlew assembleRelease
# Output: app/build/outputs/apk/release/app-release.apk
```
