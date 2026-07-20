#!/bin/zsh

set -euo pipefail

root_dir="$(cd "$(dirname "$0")/../.." && pwd)"
app="$root_dir/outputs/Calculadora C-Forge.app"
contents="$app/Contents"

mkdir -p "$contents/MacOS" "$contents/Resources"
cp "$root_dir/packaging/macos-calculator/Info.plist" "$contents/Info.plist"
cp "$root_dir/packaging/macos-calculator/cforge-calculadora" "$contents/MacOS/cforge-calculadora"
cp "$root_dir/ejemplos/calculadora_app.cfv" "$contents/Resources/calculadora.cfv"
chmod +x "$contents/MacOS/cforge-calculadora"

source_icon="$root_dir/herramientas/vscode-cforgev/images/icon.png"
/usr/bin/sips -s format icns "$source_icon" --out "$contents/Resources/CForge.icns" >/dev/null

/usr/bin/codesign --force --deep --sign - "$app" >/dev/null
rm -f "$root_dir/outputs/calculadora-cforge-macos.zip"
ditto -c -k --sequesterRsrc --keepParent "$app" "$root_dir/outputs/calculadora-cforge-macos.zip"

echo "Aplicación creada: $app"
echo "Paquete creado: $root_dir/outputs/calculadora-cforge-macos.zip"
