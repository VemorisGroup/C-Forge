#!/bin/bash
# install.sh -- Instalador de C-Forge
# Uso: curl -fsSL https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/install.sh | bash

set -e

REPO="VemorisGroup/C-Forge"
VERSION="2.5.1"
INSTALL_DIR="/usr/local/bin"
TOOLS_DIR="/usr/local/lib/cforge/tools"
BINARY_NAME="cforgev"
STDLIB_DIR="/usr/local/lib/cforge/stdlib"
CFPKG_NAME="cfpkg"

# Colores
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo ""
echo -e "${BLUE}  C-Forge Language Installer${NC}"
echo -e "${BLUE}  github.com/${REPO}${NC}"
echo ""

# Detectar OS
OS="$(uname -s)"
ARCH="$(uname -m)"
echo -e "  Sistema: ${OS} ${ARCH}"

# Verificar compilador C++
CXX=""
if command -v g++ &>/dev/null; then
    CXX="g++"
elif command -v clang++ &>/dev/null; then
    CXX="clang++"
else
    echo -e "${RED}Error: se necesita g++ o clang++ para instalar C-Forge.${NC}"
    echo ""
    echo "  macOS:  xcode-select --install"
    echo "  Ubuntu: sudo apt install g++"
    echo "  Fedora: sudo dnf install gcc-c++"
    exit 1
fi
echo -e "  Compilador: ${CXX}"

# Verificar permisos de instalacion
if [ ! -w "$INSTALL_DIR" ]; then
    echo -e "${YELLOW}  Se necesitan permisos de administrador para instalar en ${INSTALL_DIR}${NC}"
    SUDO="sudo"
else
    SUDO=""
fi

# Directorio temporal
TMP_DIR=$(mktemp -d)
trap "rm -rf $TMP_DIR" EXIT

echo ""
echo -e "  ${BLUE}Descargando fuentes...${NC}"
RAW="https://raw.githubusercontent.com/${REPO}/main"

curl -fsSL "$RAW/cforgev.cpp" -o "$TMP_DIR/cforgev.cpp"
echo -e "  OK cforgev.cpp"

# Descargar stdlib (v2.5.1 — todos los módulos)
mkdir -p "$TMP_DIR/stdlib"
for mod in aleatorio algoritmos async base64 benchmark colecciones concurrencia \
           crypto csv db email enum errores esquema eventos fecha ffi framework \
           generadores gl http_cliente interfaz io json lista log mapa matematica \
           numero orm os pkgmgr pruebas regex sdl streams texto tipado tipos \
           validar web yaml; do
    curl -fsSL "$RAW/stdlib/${mod}.cfv" -o "$TMP_DIR/stdlib/${mod}.cfv" 2>/dev/null && \
        echo -e "  OK stdlib/${mod}.cfv" || echo -e "  -- stdlib/${mod}.cfv (omitido)"
done

# Descargar herramientas Python
mkdir -p "$TMP_DIR/tools"
for tool in cfmt cflint cftest cfdoc cfwatch cforgec cforge_cli dap_server lsp_server pkg_registry repl cfbuild; do
    curl -fsSL "$RAW/tools/${tool}.py" -o "$TMP_DIR/tools/${tool}.py" 2>/dev/null && \
        echo -e "  OK tools/${tool}.py" || true
done

# Descargar cfpkg
curl -fsSL "$RAW/cfpkg" -o "$TMP_DIR/cfpkg"
chmod +x "$TMP_DIR/cfpkg"
echo -e "  OK cfpkg"

echo ""
echo -e "  ${BLUE}Compilando interprete...${NC}"
echo -e "  ${CXX} -std=c++20 -O2 ..."

# Detectar OpenSSL
OPENSSL_FLAGS=""
if [ "$OS" = "Darwin" ]; then
    # macOS: Homebrew OpenSSL
    for prefix in /opt/homebrew/opt/openssl@3 /usr/local/opt/openssl@3 /opt/homebrew/opt/openssl /usr/local/opt/openssl; do
        if [ -f "$prefix/include/openssl/sha.h" ]; then
            OPENSSL_FLAGS="-DCFV_WITH_OPENSSL -I$prefix/include -L$prefix/lib -lcrypto -lssl"
            echo -e "  OpenSSL encontrado en $prefix"
            break
        fi
    done
elif [ "$OS" = "Linux" ]; then
    # Linux: buscar libcrypto.so.3
    for lib_dir in /usr/lib/aarch64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib; do
        if [ -f "$lib_dir/libcrypto.so.3" ]; then
            NODE_INC=""
            [ -d /usr/include/node/openssl ] && NODE_INC="-I/usr/include/node"
            OPENSSL_FLAGS="-DCFV_WITH_OPENSSL $NODE_INC $lib_dir/libcrypto.so.3 $lib_dir/libssl.so.3"
            echo -e "  OpenSSL encontrado en $lib_dir"
            break
        fi
    done
    # Fallback: pkg-config
    if [ -z "$OPENSSL_FLAGS" ] && command -v pkg-config &>/dev/null && pkg-config --exists openssl 2>/dev/null; then
        OPENSSL_FLAGS="-DCFV_WITH_OPENSSL $(pkg-config --cflags --libs openssl)"
        echo -e "  OpenSSL via pkg-config"
    fi
fi
if [ -z "$OPENSSL_FLAGS" ]; then
    echo -e "  ${YELLOW}OpenSSL no encontrado — compilando sin criptografia AES (sha256 puro-C++ disponible)${NC}"
fi

# Detectar SQLite3
SQLITE_FLAGS=""
if pkg-config --exists sqlite3 2>/dev/null; then
    SQLITE_FLAGS="-DCFV_WITH_SQLITE $(pkg-config --libs sqlite3)"
    echo -e "  SQLite3 encontrado (pkg-config)"
elif [ -f "/usr/include/sqlite3.h" ] || [ -f "/usr/local/include/sqlite3.h" ]; then
    SQLITE_FLAGS="-DCFV_WITH_SQLITE -lsqlite3"
    echo -e "  SQLite3 encontrado"
elif [ "$OS" = "Darwin" ] && [ -f "$(brew --prefix sqlite 2>/dev/null)/include/sqlite3.h" ]; then
    SQLITE_PREFIX="$(brew --prefix sqlite)"
    SQLITE_FLAGS="-DCFV_WITH_SQLITE -I$SQLITE_PREFIX/include -L$SQLITE_PREFIX/lib -lsqlite3"
    echo -e "  SQLite3 encontrado (Homebrew)"
fi
if [ -z "$SQLITE_FLAGS" ]; then
    echo -e "  ${YELLOW}SQLite3 no encontrado — compilando sin soporte de base de datos${NC}"
    echo -e "  Para instalarlo: Ubuntu: sudo apt install libsqlite3-dev | macOS: brew install sqlite"
fi

PYTHON_FLAGS=$(python3-config --includes --ldflags 2>/dev/null || echo "")

# Intentar con todas las combinaciones — primero completo, luego degradar
if $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" \
    $OPENSSL_FLAGS $SQLITE_FLAGS -lpthread \
    2>/dev/null; then
    echo -e "  OK compilado con OpenSSL + SQLite"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" \
    $OPENSSL_FLAGS -lpthread \
    2>/dev/null; then
    echo -e "  OK compilado con OpenSSL (sin SQLite)"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" \
    $SQLITE_FLAGS -lpthread \
    2>/dev/null; then
    echo -e "  OK compilado con SQLite (sin OpenSSL)"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" \
    $PYTHON_FLAGS \
    2>/dev/null; then
    echo -e "  OK compilado con Python"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" 2>&1; then
    echo -e "  OK compilado (basico)"
else
    echo -e "${RED}  Error al compilar. Reporta el error en github.com/${REPO}/issues${NC}"
    exit 1
fi

echo ""
echo -e "  ${BLUE}Instalando...${NC}"

# Instalar binario
$SUDO cp "$TMP_DIR/${BINARY_NAME}" "${INSTALL_DIR}/${BINARY_NAME}"
$SUDO chmod +x "${INSTALL_DIR}/${BINARY_NAME}"
echo -e "  OK ${INSTALL_DIR}/${BINARY_NAME}"

# Instalar cfpkg
# Parchear cfpkg para apuntar a la stdlib instalada
sed "s|STDLIB_DIR=\"\$(dirname \"\$0\")/stdlib\"|STDLIB_DIR=\"${STDLIB_DIR}\"|g" \
    "$TMP_DIR/cfpkg" > "$TMP_DIR/cfpkg_patched"
sed -i.bak "s|CFORGEV_BIN=\"\$(dirname \"\$0\")/cforgev_new\"|CFORGEV_BIN=\"${INSTALL_DIR}/${BINARY_NAME}\"|g" \
    "$TMP_DIR/cfpkg_patched" 2>/dev/null || \
sed -i '' "s|CFORGEV_BIN=\"\$(dirname \"\$0\")/cforgev_new\"|CFORGEV_BIN=\"${INSTALL_DIR}/${BINARY_NAME}\"|g" \
    "$TMP_DIR/cfpkg_patched"
$SUDO cp "$TMP_DIR/cfpkg_patched" "${INSTALL_DIR}/${CFPKG_NAME}"
$SUDO chmod +x "${INSTALL_DIR}/${CFPKG_NAME}"
echo -e "  OK ${INSTALL_DIR}/${CFPKG_NAME}"

# Instalar stdlib
$SUDO mkdir -p "$STDLIB_DIR"
$SUDO cp "$TMP_DIR/stdlib/"*.cfv "$STDLIB_DIR/" 2>/dev/null || true
echo -e "  OK stdlib -> ${STDLIB_DIR}"

# Instalar herramientas Python
$SUDO mkdir -p "$TOOLS_DIR"
$SUDO cp "$TMP_DIR/tools/"*.py "$TOOLS_DIR/" 2>/dev/null || true
echo -e "  OK tools -> ${TOOLS_DIR}"

# Instalar wrappers de herramientas en PATH
for tool in cfmt cflint cftest cfdoc cfwatch cforgec; do
    $SUDO tee "${INSTALL_DIR}/${tool}" > /dev/null << EOF
#!/usr/bin/env python3
import sys
sys.path.insert(0, "${TOOLS_DIR}")
exec(open("${TOOLS_DIR}/${tool}.py").read())
EOF
    $SUDO chmod +x "${INSTALL_DIR}/${tool}"
    echo -e "  OK ${INSTALL_DIR}/${tool}"
done

# Instalar CLI unificado cforge
$SUDO tee "${INSTALL_DIR}/cforge" > /dev/null << 'EOF'
#!/usr/bin/env python3
import sys, os
sys.path.insert(0, "/usr/local/lib/cforge/tools")
exec(open("/usr/local/lib/cforge/tools/cforge_cli.py").read())
EOF
$SUDO chmod +x "${INSTALL_DIR}/cforge"
echo -e "  OK ${INSTALL_DIR}/cforge (CLI unificado)"

# Completions de shell
COMP_DIR="/usr/local/share/cforge/completions"
$SUDO mkdir -p "$COMP_DIR"
for shell_comp in bash zsh fish; do
    curl -fsSL "$RAW/completions/cforge.${shell_comp}" -o "$TMP_DIR/cforge.${shell_comp}" 2>/dev/null && \
        $SUDO cp "$TMP_DIR/cforge.${shell_comp}" "$COMP_DIR/" && \
        echo -e "  OK completions/${shell_comp}" || true
done
echo ""
echo -e "  Shell completions en ${COMP_DIR}"
echo -e "  bash:  source ${COMP_DIR}/cforge.bash"
echo -e "  zsh:   fpath+=${COMP_DIR} && compinit"
echo -e "  fish:  cp ${COMP_DIR}/cforge.fish ~/.config/fish/completions/"

echo ""
echo -e "  ${GREEN}C-Forge v${VERSION} instalado correctamente!${NC}"
echo ""
echo -e "  Añade al .bashrc / .zshrc:"
echo -e "    export CFORGE_STDLIB=\"${STDLIB_DIR}\""
echo ""
echo -e "  Comandos disponibles:"
echo -e "    cforge run archivo.cfv  -- ejecutar un programa"
echo -e "    cforge repl             -- REPL interactivo"
echo -e "    cforge build            -- compilar proyecto"
echo -e "    cforge test             -- ejecutar tests"
echo -e "    cforge fmt archivo.cfv  -- formatear código"
echo -e "    cforge lint archivo.cfv -- analizar código"
echo -e "    cforge docs             -- generar documentación"
echo -e "    cforge pkg install pkg  -- instalar paquete"
echo -e "    cfpkg install u/repo    -- gestor de paquetes"
echo ""
echo -e "  Documentacion: https://github.com/${REPO}"
echo ""
