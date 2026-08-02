#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-1.6.0}"
ARCH="${2:-all}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGE="$ROOT/dist/deb-root"
PACKAGE="$ROOT/dist/cforge_${VERSION}_${ARCH}.deb"

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/lib/cforge/stdlib" "$STAGE/usr/bin"
"${CXX:-g++}" -std=c++20 -O2 -I"$ROOT" -I"$ROOT/include" \
  "$ROOT/cforgev.cpp" -o "$STAGE/usr/bin/cforge" -pthread -ldl
install -m 0644 "$ROOT"/stdlib/*.cfv "$STAGE/usr/lib/cforge/stdlib/"
ln -s cforge "$STAGE/usr/bin/cforgev"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: cforge
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Maintainer: Vemoris Group <hola@vemorisgroup.com>
Homepage: https://github.com/VemorisGroup/C-Forge
Description: Motor nativo Developer Preview del lenguaje C-Forge
 Ejecuta programas y pruebas .cfv sin Python, JVM, .NET ni Node.
EOF
dpkg-deb --root-owner-group --build "$STAGE" "$PACKAGE"
rm -rf "$STAGE"
echo "$PACKAGE"
