# Publicación de C-Forge

**Sitio oficial:** [c-forge.org](https://c-forge.org)
**Documentación:** [docs.c-forge.org](https://docs.c-forge.org)
**Playground:** [play.c-forge.org](https://play.c-forge.org)
**Paquetes:** [pkg.c-forge.org](https://pkg.c-forge.org)

## GitHub

Antes de crear el repositorio, revisa que el nombre público sea `C-Forge` y que
la licencia propietaria incluida represente la decisión de Vemoris Group.

```bash
git init
git add .
git commit -m "Release C-Forge 3.2.0"
git branch -M main
git remote add origin https://github.com/VemorisGroup/C-Forge.git
git push -u origin main
```

La configuración `.gitignore` evita subir builds, SDKs locales, cachés,
temporales y paquetes de distribución duplicados.

Para una versión descargable, crea y publica el tag `v2.6.0`. El workflow
`Publicar C-Forge nativo` ejecuta los gates y genera automáticamente:

- Binario nativo para macOS.
- Binario nativo para Linux x64.
- Ejecutable nativo para Windows x64.
- Paquete `.deb` para Debian/Ubuntu.
- Archivo `SHA256SUMS` para verificar las descargas.

Consulta [`DISTRIBUCION.md`](DISTRIBUCION.md) para publicar los índices de cada
gestor. Los catálogos oficiales tienen procesos de revisión externos.

## Visual Studio Marketplace

La extensión está en `herramientas/vscode-cforgev`. Su identificador esperado es
`vemoris-group.cforgev-language`. El identificador de publicador debe existir en
Marketplace antes de publicar; si Microsoft no permite `vemoris-group`, cambia
únicamente el campo `publisher` de `package.json` por el ID real.

El icono configurado es PNG porque Marketplace no acepta SVG como icono. El SVG
maestro permanece en `assets/cforgev-logo.svg` fuera del paquete.

```bash
cd herramientas/vscode-cforgev
npx @vscode/vsce package
```

Instalación local para revisión:

```bash
code --install-extension cforgev-language-1.3.2.vsix
```

Publicación después de crear el publicador y configurar la autenticación:

```bash
npx @vscode/vsce publish
```

No guardes tokens de Azure DevOps o Microsoft Entra ID en el repositorio.
