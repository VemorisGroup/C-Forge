# Distribución Web — Guía para c-forge.org

Este documento describe la infraestructura de distribución de C-Forge para que
el equipo de `c-forge.org` pueda construir la página de descargas con información
actualizada de forma automática.

---

## Mecanismo para detectar la última versión

### Opción A — GitHub Releases API (recomendada)

```
GET https://api.github.com/repos/VemorisGroup/C-Forge/releases/latest
```

Respuesta relevante:

```json
{
  "tag_name": "v3.3.0",
  "published_at": "2026-08-06T...",
  "assets": [...]
}
```

La URL es estable mientras el repositorio no cambie de nombre. No requiere autenticación
para repositorios públicos (límite de 60 req/hora sin token, 5000 con token).

### Opción B — latest.json (adjunto en cada release)

Cada release incluye un archivo `latest.json`:

```
https://github.com/VemorisGroup/C-Forge/releases/download/v{VERSION}/latest.json
```

Contenido:

```json
{
  "version": "3.3.0",
  "released_at": "2026-08-06T00:00:00Z",
  "manifest": "https://github.com/VemorisGroup/C-Forge/releases/download/v3.3.0/release-manifest.json"
}
```

**Problema:** la URL de `latest.json` contiene la versión, lo que crea una dependencia circular.
Usar la GitHub API (Opción A) para obtener primero el tag, luego construir la URL.

### Flujo recomendado para c-forge.org/download

```
1. GET /repos/VemorisGroup/C-Forge/releases/latest
   → extrae tag_name (ej: "v3.3.0")
2. version = tag_name.replace("v", "")
3. GET /releases/download/v{version}/release-manifest.json
   → tiene URLs, sha256 y metadatos de todos los artifacts
4. Detectar plataforma del visitante (navigator.platform / User-Agent)
5. Mostrar el artifact recomendado con URL directa
```

---

## Estructura de release-manifest.json

Generado automáticamente en cada release por el workflow `release.yml`.
Versión del schema: `1`.

```json
{
  "schema": 1,
  "version": "3.3.0",
  "channel": "stable",
  "released_at": "2026-08-06T00:00:00Z",
  "minimum_supported_version": null,
  "install_script": "https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/install.sh",
  "changelog": "https://github.com/VemorisGroup/C-Forge/blob/main/CHANGELOG.md",
  "downloads": {
    "macos": {
      "arm64": {
        "archive": {
          "type": "tar.gz",
          "filename": "cforge-3.3.0-macos-arm64.tar.gz",
          "url": "https://github.com/VemorisGroup/C-Forge/releases/download/v3.3.0/cforge-3.3.0-macos-arm64.tar.gz",
          "sha256": "<hash>"
        }
      }
    },
    "linux": {
      "x64": {
        "archive": {
          "type": "tar.gz",
          "filename": "cforge-3.3.0-linux-x64.tar.gz",
          "url": "...",
          "sha256": "<hash>"
        },
        "package": {
          "type": "deb",
          "filename": "cforge_3.3.0_amd64.deb",
          "url": "...",
          "sha256": "<hash>"
        }
      }
    },
    "windows": {
      "x64": {
        "portable": {
          "type": "zip",
          "filename": "cforge-3.3.0-windows-x64.zip",
          "url": "...",
          "sha256": "<hash>"
        }
      }
    }
  },
  "editor": {
    "vscode": {
      "vsix": {
        "filename": "c-forge-3.3.0.vsix",
        "url": "...",
        "sha256": "<hash>"
      },
      "marketplace": null
    }
  },
  "sha256sums": "https://github.com/VemorisGroup/C-Forge/releases/download/v3.3.0/SHA256SUMS"
}
```

---

## Artifacts disponibles por plataforma

| Plataforma | Archivo | Uso |
|-----------|---------|-----|
| macOS ARM64 | `cforge-{VER}-macos-arm64.tar.gz` | Instalar manualmente o via install.sh |
| Linux x64 | `cforge-{VER}-linux-x64.tar.gz` | Instalar manualmente o via install.sh |
| Linux x64 | `cforge_{VER}_amd64.deb` | `dpkg -i` en Debian/Ubuntu |
| Windows x64 | `cforge-{VER}-windows-x64.zip` | Portable — extraer y añadir al PATH |
| VS Code | `c-forge-{VER}.vsix` | Install from VSIX en VS Code |
| Checksums | `SHA256SUMS` | Verificación de integridad |
| Manifest | `release-manifest.json` | Consumo por la web |
| Latest ptr | `latest.json` | Puntero a la versión actual |

---

## Detección de plataforma en el navegador (JavaScript)

```js
function detectPlatform() {
  const ua = navigator.userAgent.toLowerCase();
  const platform = navigator.platform.toLowerCase();

  if (platform.includes('mac') || ua.includes('mac')) {
    // macOS — asumir ARM64 (Apple Silicon es la mayoría desde 2021)
    return 'macos-arm64';
  }
  if (ua.includes('win')) {
    return 'windows-x64';
  }
  if (ua.includes('linux')) {
    return 'linux-x64';
  }
  return null; // mostrar todas las opciones
}
```

---

## Artifact recomendado por plataforma

| Plataforma detectada | Artifact primario | Alternativa |
|---------------------|------------------|-------------|
| `macos-arm64` | `cforge-{VER}-macos-arm64.tar.gz` + install.sh | — |
| `linux-x64` (Debian/Ubuntu) | `.deb` | `.tar.gz` |
| `linux-x64` (otros) | `.tar.gz` | install.sh |
| `windows-x64` | `.zip` | — |

---

## Verificación de checksums

Cada release incluye `SHA256SUMS` con hashes de todos los artifacts.
Formato: `sha256sum --check SHA256SUMS`.

Ejemplo de verificación:

```sh
# Descargar artifact y checksums
wget https://github.com/VemorisGroup/C-Forge/releases/download/v3.3.0/cforge-3.3.0-linux-x64.tar.gz
wget https://github.com/VemorisGroup/C-Forge/releases/download/v3.3.0/SHA256SUMS

# Verificar (el hash debe coincidir)
sha256sum -c SHA256SUMS --ignore-missing
```

---

## Versiones anteriores

Las versiones anteriores permanecen disponibles en GitHub Releases:
```
https://github.com/VemorisGroup/C-Forge/releases
```

No se eliminan releases publicadas. El `release-manifest.json` de cada versión
apunta solo a sus propios artifacts.

---

## Requisitos externos pendientes para distribución completa

| Elemento | Descripción | Prioridad |
|----------|-------------|-----------|
| Apple Developer ID | Firma y notarización del binario macOS | Alta |
| Authenticode Windows | Firma del binario/ZIP de Windows | Media |
| VS Code Marketplace | Publicar `.vsix` con `vsce publish` | Alta |
| dominio c-forge.org | DNS + hosting funcionando | Crítica |
| CDN propio (opcional) | Mirrors para descargas fuera de GitHub | Baja |

### Secrets de GitHub necesarios para firma macOS

```
APPLE_DEVELOPER_ID_CERT      # Certificado .p12 en base64
APPLE_DEVELOPER_ID_PASSWORD  # Contraseña del certificado
APPLE_ID                     # Apple ID para notarización
APPLE_ID_PASSWORD            # App-specific password
APPLE_TEAM_ID                # Team ID de Apple Developer
```

### Secrets de GitHub necesarios para firma Windows

```
WINDOWS_CERT                 # Certificado Authenticode .pfx en base64
WINDOWS_CERT_PASSWORD        # Contraseña del certificado
```

### Secrets para VS Code Marketplace

```
VSCE_TOKEN                   # Personal Access Token de Azure DevOps
```

---

## Ejemplo de página de descarga (pseudocódigo)

```js
async function loadDownloadPage() {
  // 1. Obtener versión latest
  const rel = await fetch('https://api.github.com/repos/VemorisGroup/C-Forge/releases/latest');
  const { tag_name } = await rel.json();
  const version = tag_name.replace('v', '');

  // 2. Obtener manifest
  const mf = await fetch(
    `https://github.com/VemorisGroup/C-Forge/releases/download/${tag_name}/release-manifest.json`
  );
  const manifest = await mf.json();

  // 3. Detectar plataforma y mostrar artifact recomendado
  const platform = detectPlatform(); // 'macos-arm64' | 'linux-x64' | 'windows-x64'
  const dl = manifest.downloads[platform.split('-')[0]]?.[platform.split('-')[1]];

  renderDownloadButton(dl?.archive ?? dl?.portable ?? dl?.package, manifest.sha256sums);
}
```

---

## Cadencia de releases

- Los releases se publican mediante `git tag vX.Y.Z && git push origin --tags`.
- El workflow `release.yml` se activa automáticamente con cualquier tag `v*`.
- El job `validate-version` bloquea el release si el tag no coincide con `VERSION` en Makefile.
- Todos los jobs de gate deben pasar antes de que se publiquen los artifacts.
