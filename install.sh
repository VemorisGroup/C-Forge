#!/bin/bash
# install.sh -- Instalador de C-Forge
# Uso: curl -fsSL https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/install.sh | bash

set -e

REPO="VemorisGroup/C-Forge"
INSTALL_DIR="/usr/local/bin"

# Auto-detectar la versión más reciente desde GitHub Releases API
echo ""
echo "  Detectando última versión disponible..."
VERSION=$(curl -fsSL "https://api.github.com/repos/${REPO}/releases/latest" \
    | grep '"tag_name"' \
    | head -1 \
    | sed 's/.*"v\?\([^"]*\)".*/\1/')
if [ -z "$VERSION" ]; then
    VERSION="3.7.0"
    echo "  (no se pudo consultar GitHub — usando versión por defecto: ${VERSION})"
else
    echo "  Versión: ${VERSION}"
fi
BINARY_NAME="cforge"
STDLIB_DIR="/usr/local/lib/cforge/stdlib"

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
mkdir -p "$TMP_DIR/include"
curl -fsSL "$RAW/include/cforge_shared_arena.h" -o "$TMP_DIR/include/cforge_shared_arena.h"
echo -e "  OK include/cforge_shared_arena.h"

# Descargar únicamente la biblioteca estándar validada por el gate 2.6.
mkdir -p "$TMP_DIR/stdlib"
for mod in aleatorio algoritmos assets audio auditoria auth base64 colecciones \
           colision concurrencia crypto db errores fecha fisica2d fisica3d gl \
           input io json lista mapa matematica nlp numero os particulas redis \
           regex sdl web; do
    curl -fsSL "$RAW/stdlib/${mod}.cfv" -o "$TMP_DIR/stdlib/${mod}.cfv" 2>/dev/null && \
        echo -e "  OK stdlib/${mod}.cfv" || echo -e "  -- stdlib/${mod}.cfv (omitido)"
done

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

# Intentar con todas las combinaciones — primero completo, luego degradar
if $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    -I"$TMP_DIR/include" "$TMP_DIR/cforgev.cpp" \
    $OPENSSL_FLAGS $SQLITE_FLAGS -lpthread \
    2>/dev/null; then
    echo -e "  OK compilado con OpenSSL + SQLite"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    -I"$TMP_DIR/include" "$TMP_DIR/cforgev.cpp" \
    $OPENSSL_FLAGS -lpthread \
    2>/dev/null; then
    echo -e "  OK compilado con OpenSSL (sin SQLite)"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    -I"$TMP_DIR/include" "$TMP_DIR/cforgev.cpp" \
    $SQLITE_FLAGS -lpthread \
    2>/dev/null; then
    echo -e "  OK compilado con SQLite (sin OpenSSL)"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    -I"$TMP_DIR/include" "$TMP_DIR/cforgev.cpp" 2>&1; then
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
$SUDO ln -sf "${INSTALL_DIR}/${BINARY_NAME}" "${INSTALL_DIR}/cforgev"
echo -e "  OK ${INSTALL_DIR}/cforgev (alias de compatibilidad)"

# Instalar stdlib
$SUDO mkdir -p "$STDLIB_DIR"
$SUDO cp "$TMP_DIR/stdlib/"*.cfv "$STDLIB_DIR/" 2>/dev/null || true
echo -e "  OK stdlib -> ${STDLIB_DIR}"

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
echo -e "    cforge run archivo.cfv   -- ejecutar un programa"
echo -e "    cforge repl              -- REPL interactivo"
echo -e "    cforge new <nombre>      -- crear nuevo proyecto"
echo -e "    cforge init              -- inicializar proyecto en directorio actual"
echo -e "    cforge doctor            -- diagnosticar entorno"
echo -e "    cforge check archivo.cfv -- verificar sintaxis"
echo -e "    cforge test archivo.cfv  -- ejecutar pruebas"
echo -e "    cforge fmt archivo.cfv   -- verificar formato"
echo ""
echo -e "  Documentacion: https://github.com/${REPO}"
echo ""
