# Distribución de C-Forge

## Estado real de los comandos

La automatización del repositorio genera archivos portables para macOS, Linux y
Windows, además de un paquete `.deb`. Publicar un tag como `v1.4.0` crea una
GitHub Release mediante `.github/workflows/release.yml`.

### Homebrew

La fórmula se genera desde `packaging/homebrew/Formula/cforgev.rb.template` con
el SHA-256 del código fuente del tag y se adjunta a cada lanzamiento.
Debe publicarse en un repositorio llamado `VemorisGroup/homebrew-cforgev`.
Desde ese momento funcionará el comando oficial del tap:

```bash
brew install VemorisGroup/cforgev/cforgev
```

Después de instalar el tap, también funcionará `brew install cforgev`. Para que
ese último comando funcione sin tap, Homebrew debe aceptar la fórmula en
`homebrew/core`; la licencia actual propietaria no cumple sus requisitos de
software libre.

### Windows

El lanzamiento genera un ejecutable autónomo con PyInstaller y también un ZIP
portable para desarrollo. Después se completa el manifiesto con:

```bash
python packaging/winget/generate.py 1.4.0 dist/cforge-1.4.0-windows-x64.exe
```

El manifiesto resultante debe enviarse a `microsoft/winget-pkgs`. Cuando sea
aceptado, la instalación será:

```powershell
winget install VemorisGroup.CForgev
```

### Debian y Ubuntu

Cada tag genera `cforgev_VERSION_all.deb`, instalable con:

```bash
sudo apt install ./cforgev_VERSION_all.deb
```

Para obtener `sudo apt install cforgev`, Vemoris Group debe alojar y firmar un
repositorio APT y el usuario debe agregarlo una vez. Entrar a los repositorios
oficiales de Debian/Ubuntu requiere revisión independiente de esas comunidades.

## Compatibilidad

El intérprete portable requiere Python 3.9 o posterior. Node.js, JDK, .NET y un
compilador C++ son dependencias opcionales para sus respectivos puentes. El
backend nativo C++ todavía necesita adaptación de sockets para Windows; la
ejecución interpretada sí se prueba en Windows.
