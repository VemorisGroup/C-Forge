#!/usr/bin/env bash
# run_smoke.sh — Smoke test oficial de instalación de C-Forge
# Uso: ./tests/install-smoke/run_smoke.sh [ruta-al-binario]
#
# Simula la experiencia de un usuario nuevo que acaba de instalar C-Forge.
# Requiere que cforge esté en PATH o se pase como argumento.

set -euo pipefail

CFORGE="${1:-cforge}"
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PASS=0
FAIL=0
RESULTS=""

ok()   { PASS=$((PASS+1)); RESULTS+="  [PASS] $1\n"; }
fail() { FAIL=$((FAIL+1)); RESULTS+="  [FAIL] $1\n"; }

echo ""
echo "C-Forge Install Smoke Test"
echo "Binario: $CFORGE"
echo "────────────────────────────────────────"

# 1. cforge --version
VERSION_OUT=$("$CFORGE" --version 2>&1)
if echo "$VERSION_OUT" | grep -qE '[0-9]+\.[0-9]+\.[0-9]+'; then
  ok "--version → $VERSION_OUT"
else
  fail "--version no devolvió un número de versión"
fi

# 2. cforge --help
HELP_OUT=$("$CFORGE" --help 2>&1)
if echo "$HELP_OUT" | grep -qi "uso\|usage\|cforge"; then
  ok "--help muestra ayuda"
else
  fail "--help no muestra ayuda útil"
fi

# 3. cforge check hello.cfv (sintaxis válida)
if "$CFORGE" check "$DIR/hello.cfv" > /dev/null 2>&1; then
  ok "check hello.cfv → sintaxis válida"
else
  fail "check hello.cfv falló (debería ser sintaxis válida)"
fi

# 4. cforge run hello.cfv
RUN_OUT=$("$CFORGE" run "$DIR/hello.cfv" 2>&1)
if echo "$RUN_OUT" | grep -q "Hola C-Forge"; then
  ok "run hello.cfv → '$RUN_OUT'"
else
  fail "run hello.cfv no produjo 'Hola C-Forge' (obtuvo: '$RUN_OUT')"
fi

# 5. cforge hello.cfv (sin subcomando)
RUN2=$("$CFORGE" "$DIR/hello.cfv" 2>&1)
if echo "$RUN2" | grep -q "Hola C-Forge"; then
  ok "cforge hello.cfv (sin run) → '$RUN2'"
else
  fail "cforge hello.cfv sin run falló (obtuvo: '$RUN2')"
fi

# 6. Sintaxis inválida produce exit code != 0
if ! "$CFORGE" check "$DIR/syntax_error.cfv" > /dev/null 2>&1; then
  ok "check syntax_error.cfv → exit code != 0 (correcto)"
else
  fail "check syntax_error.cfv debería fallar pero retornó 0"
fi

# 7. Programa inline (variante de ejecución directa)
TMP=$(mktemp /tmp/cforge-smoke-XXXX.cfv)
echo 'mostrar("smoke-inline-ok")' > "$TMP"
INLINE=$("$CFORGE" "$TMP" 2>&1)
rm -f "$TMP"
if echo "$INLINE" | grep -q "smoke-inline-ok"; then
  ok "ejecución inline → '$INLINE'"
else
  fail "ejecución inline falló (obtuvo: '$INLINE')"
fi

# 8. cforge new (crear proyecto)
TMPDIR_PROJ=$(mktemp -d)
cd "$TMPDIR_PROJ"
if "$CFORGE" new smoke-proj > /dev/null 2>&1 && [ -f "smoke-proj/main.cfv" ] && [ -f "smoke-proj/cforge.json" ]; then
  ok "cforge new smoke-proj → main.cfv y cforge.json creados"
else
  fail "cforge new smoke-proj no creó los archivos esperados"
fi
cd - > /dev/null
rm -rf "$TMPDIR_PROJ"

# 9. cforge doctor
DOCTOR=$("$CFORGE" doctor 2>&1)
if echo "$DOCTOR" | grep -qiE 'ok|warn|info|diagnos|cforge'; then
  ok "cforge doctor → produce diagnóstico"
else
  fail "cforge doctor no produce salida útil"
fi

# Resumen
echo ""
echo "────────────────────────────────────────"
printf "$RESULTS"
echo "────────────────────────────────────────"
echo "PASS: $PASS  FAIL: $FAIL"
echo ""

if [ $FAIL -gt 0 ]; then
  echo "SMOKE TEST FAILED"
  exit 1
else
  echo "SMOKE TEST PASSED"
  exit 0
fi
