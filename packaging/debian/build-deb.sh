#!/usr/bin/env bash
set -euo pipefail

VERSION="${1:-1.4.1}"
ARCH="${2:-all}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGE="$ROOT/dist/deb-root"
PACKAGE="$ROOT/dist/cforgev_${VERSION}_${ARCH}.deb"

rm -rf "$STAGE"
mkdir -p "$STAGE/DEBIAN" "$STAGE/usr/lib/cforgev" "$STAGE/usr/bin"
install -m 0644 "$ROOT/cforgev.py" "$ROOT/compilador_nativo.py" \
  "$ROOT/compilador_wasm.py" "$STAGE/usr/lib/cforgev/"
cp -R "$ROOT/include" "$ROOT/ejemplos" "$STAGE/usr/lib/cforgev/"

cat > "$STAGE/DEBIAN/control" <<EOF
Package: cforgev
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH
Depends: python3 (>= 3.9)
Maintainer: Vemoris Group <hola@vemorisgroup.com>
Homepage: https://github.com/VemorisGroup/C-Forge
Description: Intérprete experimental del lenguaje C-Forge
 C-Forge ofrece sintaxis propia, REPL, pruebas y compilación experimental.
EOF

cat > "$STAGE/usr/bin/cforge" <<'EOF'
#!/bin/sh
exec python3 /usr/lib/cforgev/cforgev.py "$@"
EOF
chmod 0755 "$STAGE/usr/bin/cforge"
ln -s cforge "$STAGE/usr/bin/cforgev"
dpkg-deb --root-owner-group --build "$STAGE" "$PACKAGE"
rm -rf "$STAGE"
echo "$PACKAGE"
