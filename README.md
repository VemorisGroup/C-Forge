# C-Forge

![C-Forge](assets/cforgev-logo.svg)

**[c-forge.org](https://c-forge.org)** · [GitHub](https://github.com/VemorisGroup/C-Forge) · [Releases](https://github.com/VemorisGroup/C-Forge/releases)

C-Forge es un lenguaje de programación creado por Vemoris Group. Su objetivo es ofrecer una sintaxis clara, ejecución nativa y una biblioteca estándar escrita en el propio lenguaje.

> **C-Forge 3.7.0** — El núcleo, CLI y biblioteca estándar superan el gate reproducible `make release-check` en macOS ARM64, Linux x64 y Windows x64. Las capacidades marcadas como experimentales no forman parte del contrato estable.

---

## Instalación rápida

### macOS y Linux (desde fuente, recomendado)

```sh
curl -fsSL https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/install.sh | bash
```

El script detecta la versión más reciente, descarga las fuentes, compila con el compilador C++20 disponible e instala el binario en `/usr/local/bin/cforge`.

Después de instalar, agrega al `.bashrc` / `.zshrc`:

```sh
export CFORGE_STDLIB="/usr/local/lib/cforge/stdlib"
```

### macOS (desde archivo)

1. Descarga `cforge-3.7.0-macos-arm64.tar.gz` desde [Releases](https://github.com/VemorisGroup/C-Forge/releases/tag/v3.7.0).
2. Extrae e instala:

```sh
tar xzf cforge-3.7.0-macos-arm64.tar.gz
sudo cp cforge-3.7.0-macos-arm64/cforge /usr/local/bin/
sudo mkdir -p /usr/local/lib/cforge
sudo cp -r cforge-3.7.0-macos-arm64/stdlib /usr/local/lib/cforge/
echo 'export CFORGE_STDLIB="/usr/local/lib/cforge/stdlib"' >> ~/.zshrc
source ~/.zshrc
```

> **Nota:** El binario no está firmado con Apple Developer ID. Si macOS bloquea la ejecución, ve a *Ajustes del sistema → Privacidad y Seguridad → Permitir de todos modos*.

### Linux x64 (desde .deb)

```sh
wget https://github.com/VemorisGroup/C-Forge/releases/download/v3.7.0/cforge_3.7.0_amd64.deb
sudo dpkg -i cforge_3.7.0_amd64.deb
echo 'export CFORGE_STDLIB="/usr/lib/cforge/stdlib"' >> ~/.bashrc
source ~/.bashrc
```

### Linux x64 (desde .tar.gz)

```sh
tar xzf cforge-3.7.0-linux-x64.tar.gz
sudo cp cforge-3.7.0-linux-x64/cforge /usr/local/bin/
sudo mkdir -p /usr/local/lib/cforge
sudo cp -r cforge-3.7.0-linux-x64/stdlib /usr/local/lib/cforge/
echo 'export CFORGE_STDLIB="/usr/local/lib/cforge/stdlib"' >> ~/.bashrc
source ~/.bashrc
```

### Windows x64

1. Descarga `cforge-3.7.0-windows-x64.zip` desde [Releases](https://github.com/VemorisGroup/C-Forge/releases/tag/v3.7.0).
2. Extrae el archivo ZIP.
3. Añade la carpeta extraída al `PATH` del sistema (Panel de control → Variables de entorno).
4. Define la variable de entorno `CFORGE_STDLIB` apuntando a la carpeta `stdlib` dentro del ZIP extraído.
5. Abre una nueva PowerShell o CMD y verifica:

```powershell
cforge --version
```

### Desinstalar

**macOS / Linux:**
```sh
sudo rm /usr/local/bin/cforge /usr/local/bin/cforgev
sudo rm -rf /usr/local/lib/cforge
```

**Debian:**
```sh
sudo dpkg -r cforge
```

---

## Verificar la instalación

```sh
cforge --version
# → C-Forge 3.7.0

cforge --help
# → muestra todos los comandos disponibles

cforge doctor
# → diagnóstico del entorno
```

---

## Hello World

Crea un archivo `hola.cfv`:

```cfv
mostrar("Hola C-Forge")
```

Ejecútalo:

```sh
cforge hola.cfv
# → Hola C-Forge
```

O con el subcomando explícito:

```sh
cforge run hola.cfv
# → Hola C-Forge
```

---

## Ejemplo: funciones y clases

```cfv
funcion factorial(n: numero): numero {
    si (n <= 1) { retornar 1 }
    retornar n * factorial(n - 1)
}

clase Saludo {
    funcion constructor(nombre: texto): nulo {
        esto.nombre = nombre
    }
    funcion decir(): nulo {
        mostrar("Hola desde " + esto.nombre)
    }
}

mostrar(factorial(6))         // 720
sea s = nuevo Saludo("C-Forge")
s.decir()                     // Hola desde C-Forge
```

---

## CLI — comandos oficiales

| Comando | Función |
|---------|---------|
| `cforge archivo.cfv` | Ejecutar un programa |
| `cforge run archivo.cfv` | Ejecutar un programa (forma explícita) |
| `cforge repl` | Consola interactiva |
| `cforge check archivo.cfv` | Verificar sintaxis |
| `cforge test archivo.cfv` | Ejecutar pruebas `.cfv` |
| `cforge fmt archivo.cfv` | Validar formato sin modificar |
| `cforge new <nombre>` | Crear proyecto nuevo |
| `cforge init` | Inicializar proyecto en directorio actual |
| `cforge doctor` | Diagnosticar entorno |
| `cforge --version` | Mostrar versión |
| `cforge --help` | Mostrar ayuda |

---

## Extensión de Visual Studio Code

1. Descarga `c-forge-3.7.0.vsix` desde [Releases](https://github.com/VemorisGroup/C-Forge/releases/tag/v3.7.0).
2. En VS Code: **Extensions** (⇧⌘X) → **···** → **Install from VSIX…** → selecciona el archivo.
3. Los archivos `.cfv` tendrán resaltado de sintaxis, snippets y diagnósticos automáticos al guardar.

La extensión busca `cforge` en el PATH del sistema. Si el binario no está en el PATH, los comandos *Ejecutar* y *Comprobar* mostrarán un aviso.

---

## Crear un proyecto nuevo

```sh
cforge new mi-app
cd mi-app
cforge main.cfv
```

Esto genera:
```
mi-app/
  main.cfv        ← programa de entrada
  cforge.json     ← manifiesto del proyecto
  .gitignore
```

---

## Biblioteca estándar

Los módulos están en [`stdlib/`](stdlib/). Importa con:

```cfv
importar "matematica"
mostrar(piso(3.7))   // 3
```

Módulos estables: `matematica`, `lista`, `mapa`, `colecciones`, `algoritmos`, `texto`, `json`, `errores`, `io`, `fecha`, `numero`, `base64`, `regex`, `concurrencia`, `aleatorio`, `sdl`.

Módulos experimentales (presentes, sin garantías de producción): `web`, `db`, `crypto`, `gl`, `red`, `redis`, `audio`, `auth`.

---

## Construir desde el código fuente

Requiere un compilador C++20 (g++ ≥ 12 o clang++ ≥ 14):

```sh
git clone https://github.com/VemorisGroup/C-Forge.git
cd C-Forge
make build
./cforge --version
make release-check   # gate completo
```

---

## Plataformas verificadas en 3.7.0

| Plataforma | Build CI | Tests | Distribuible |
|-----------|----------|-------|-------------|
| macOS ARM64 (Apple Silicon) | ✅ | ✅ | `.tar.gz` |
| Linux x64 | ✅ | ✅ | `.tar.gz`, `.deb` |
| Windows x64 | ✅ | ✅ | `.zip` |

---

## Seguridad

Reporta vulnerabilidades siguiendo [`SECURITY.md`](SECURITY.md).

---

## Licencia

Consulta [`LICENSE`](LICENSE). Copyright © 2026 Vemoris Group.

El estado verificable de cada componente está en [`capabilities.json`](capabilities.json).
