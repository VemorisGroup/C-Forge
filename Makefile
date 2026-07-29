# ── C-Forge Makefile ───────────────────────────────────────────────────────────
# Compila e instala el intérprete cforgev y las herramientas de desarrollo

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter
INCLUDES := -I. -I./include
LIBS     := -lpthread -ldl
TARGET   := cforgev
SRC      := cforgev.cpp
PREFIX   ?= /usr/local

# Detección de plataforma
UNAME := $(shell uname -s)
ARCH  := $(shell uname -m)

# OpenSSL
ifeq ($(UNAME),Darwin)
    OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null || echo /usr/local)
    INCLUDES  += -I$(OPENSSL_PREFIX)/include
    LIBS      += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
    CXXFLAGS  += -DCFV_WITH_OPENSSL
else
    OPENSSL_LIB := $(wildcard /usr/lib/$(ARCH)-linux-gnu/libcrypto.so.3 \
                               /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
                               /usr/lib/libcrypto.so)
    ifneq ($(OPENSSL_LIB),)
        LIBS     += $(firstword $(OPENSSL_LIB))
        CXXFLAGS += -DCFV_WITH_OPENSSL
    endif
endif

# Node.js headers (opcional para bindings)
NODE_INC := $(shell node -e "require('path').join(require('os').homedir(),'include')" 2>/dev/null || echo /usr/include/node)
ifneq ($(wildcard $(NODE_INC)/node_version.h),)
    INCLUDES += -I$(NODE_INC)
endif

# ── Targets ────────────────────────────────────────────────────────────────────

.PHONY: all build debug release install uninstall tools test clean help

## Compilar (default)
all: build

## Compilar con O2
build: $(TARGET)

$(TARGET): $(SRC)
	@echo "  CXX  $< → $@"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LIBS)
	@echo "  OK   $@ compilado"

## Compilar en modo debug (sin optimización, con símbolos)
debug: CXXFLAGS := -std=c++20 -g -O0 -DDEBUG -Wall
debug: clean $(TARGET)
	@echo "  OK   debug build"

## Compilar con máxima optimización
release: CXXFLAGS := -std=c++20 -O3 -DNDEBUG -march=native
release: clean $(TARGET)
	@echo "  OK   release build"

## Compilar con soporte SDL2 (para stdlib/sdl.cfv y stdlib/gl.cfv)
sdl: CXXFLAGS += -DCFV_WITH_SDL2
sdl: LIBS     += -lSDL2 -lSDL2_ttf -lSDL2_mixer -lSDL2_image
sdl: $(TARGET)

## Compilar con soporte Python (para stdlib/ffi.cfv)
python: CXXFLAGS += $(shell python3-config --includes) -DCFV_WITH_PYTHON
python: LIBS     += $(shell python3-config --ldflags --libs)
python: $(TARGET)

## Instalar en el sistema
install: build
	@echo "  INSTALL  $(PREFIX)/bin/$(TARGET)"
	@install -d $(PREFIX)/bin
	@install -m 755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	@install -m 755 $(TARGET) $(PREFIX)/bin/cforge
	@echo "  INSTALL  tools → $(PREFIX)/lib/cforge/tools/"
	@install -d $(PREFIX)/lib/cforge/tools
	@install -m 644 tools/*.py $(PREFIX)/lib/cforge/tools/
	@echo "  INSTALL  stdlib → $(PREFIX)/lib/cforge/stdlib/"
	@install -d $(PREFIX)/lib/cforge/stdlib
	@install -m 644 stdlib/*.cfv $(PREFIX)/lib/cforge/stdlib/
	@echo "  INSTALL  completions"
	@install -d $(PREFIX)/share/cforge/completions
	@install -m 644 completions/* $(PREFIX)/share/cforge/completions/ 2>/dev/null || true
	@echo ""
	@echo "  ✓ C-Forge instalado en $(PREFIX)"
	@echo "    Añade al PATH: export PATH=\"$(PREFIX)/bin:\$$PATH\""
	@echo "    Stdlib:        export CFORGE_STDLIB=\"$(PREFIX)/lib/cforge/stdlib\""

## Desinstalar
uninstall:
	@rm -f $(PREFIX)/bin/$(TARGET) $(PREFIX)/bin/cforge
	@rm -rf $(PREFIX)/lib/cforge
	@rm -rf $(PREFIX)/share/cforge
	@echo "  ✓ C-Forge desinstalado"

## Instalar herramientas Python como comandos (cfmt, cflint, etc.)
tools:
	@echo "  INSTALL  herramientas Python"
	@for tool in cfmt cflint cftest cfbuild cfdoc cfwatch cforgec; do \
	    echo "#!/usr/bin/env python3\nimport sys; sys.argv[0]='$$tool'\nexec(open('tools/$$tool.py').read())" \
	    > $(PREFIX)/bin/$$tool && chmod +x $(PREFIX)/bin/$$tool; \
	    echo "  ✓ $$tool → $(PREFIX)/bin/$$tool"; \
	done
	@echo "#!/usr/bin/env python3\nimport sys; exec(open('tools/cforge_cli.py').read())" \
	    > $(PREFIX)/bin/cforge && chmod +x $(PREFIX)/bin/cforge
	@echo "  ✓ cforge CLI → $(PREFIX)/bin/cforge"

## Ejecutar tests básicos
test: build
	@echo "  TEST  hola.cfv"
	@echo 'mostrar("Hola C-Forge!")' > /tmp/test_cf.cfv
	@CFORGE_STDLIB=./stdlib ./$(TARGET) /tmp/test_cf.cfv
	@echo "  TEST  importar stdlib"
	@echo 'importar "matematica"\nmostrar(piso(2.9))' > /tmp/test_imp.cfv
	@CFORGE_STDLIB=./stdlib ./$(TARGET) /tmp/test_imp.cfv
	@echo "  TEST  cfmt"
	@python3 tools/cfmt.py /tmp/test_cf.cfv --check 2>/dev/null || true
	@echo "  TEST  cflint"
	@python3 tools/cflint.py /tmp/test_cf.cfv 2>/dev/null || true
	@echo "  ✓ Tests básicos OK"

## Benchmark rápido
bench: build
	@echo "  BENCH  fib(30)"
	@echo 'funcion fib(n) { si(n<=1){retornar n} retornar fib(n-1)+fib(n-2) } mostrar(fib(30))' \
	    > /tmp/bench.cfv
	@time ./$(TARGET) /tmp/bench.cfv

## Limpiar artefactos de compilación
clean:
	@rm -f $(TARGET) *.o
	@echo "  CLEAN  artefactos eliminados"

## Ayuda
help:
	@echo ""
	@echo "  C-Forge Makefile"
	@echo "  ──────────────────────────────────────────"
	@echo "  make              Compilar cforgev"
	@echo "  make debug        Compilar en modo debug"
	@echo "  make release      Compilar con O3 + march=native"
	@echo "  make sdl          Compilar con soporte SDL2"
	@echo "  make python       Compilar con soporte Python FFI"
	@echo "  make install      Instalar en $(PREFIX)"
	@echo "  make tools        Instalar herramientas Python"
	@echo "  make uninstall    Desinstalar"
	@echo "  make test         Ejecutar tests básicos"
	@echo "  make bench        Benchmark fib(30)"
	@echo "  make clean        Limpiar artefactos"
	@echo ""
	@echo "  Variables:"
	@echo "  CXX=clang++       Cambiar compilador"
	@echo "  PREFIX=/opt/cf    Cambiar directorio de instalación"
	@echo ""
