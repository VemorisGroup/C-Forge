#!/bin/bash
# install.sh -- Instalador de C-Forge
# Uso: curl -fsSL https://raw.githubusercontent.com/VemorisGroup/C-Forge/main/install.sh | bash

set -e

REPO="VemorisGroup/C-Forge"
INSTALL_DIR="/usr/local/bin"
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

# Descargar stdlib
mkdir -p "$TMP_DIR/stdlib"
for mod in aleatorio base64 colecciones fecha io json lista mapa matematica pkgmgr regex texto tipos; do
    curl -fsSL "$RAW/stdlib/${mod}.cfv" -o "$TMP_DIR/stdlib/${mod}.cfv" 2>/dev/null && \
        echo -e "  OK stdlib/${mod}.cfv" || echo -e "  -- stdlib/${mod}.cfv (omitido)"
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

PYTHON_FLAGS=$(python3-config --includes --ldflags 2>/dev/null || echo "")

# Intentar con Python + OpenSSL, luego combinaciones, luego basico
if $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" \
    $PYTHON_FLAGS $OPENSSL_FLAGS \
    2>/dev/null; then
    echo -e "  OK compilado con Python + OpenSSL"
elif $CXX -std=c++20 -O2 \
    -o "$TMP_DIR/${BINARY_NAME}" \
    "$TMP_DIR/cforgev.cpp" \
    $OPENSSL_FLAGS \
    2>/dev/null; then
    echo -e "  OK compilado con OpenSSL"
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

echo ""
echo -e "  ${GREEN}C-Forge instalado correctamente!${NC}"
echo ""
echo -e "  Version: $(${INSTALL_DIR}/${BINARY_NAME} --version 2>/dev/null || echo 'v2.0.0')"
echo ""
echo -e "  Comandos disponibles:"
echo -e "    cforgev archivo.cfv    -- ejecutar un programa"
echo -e "    cforgev               -- REPL interactivo"
echo -e "    cfpkg install u/repo  -- instalar un paquete"
echo ""
echo -e "  Documentacion: https://github.com/${REPO}"
echo ""
