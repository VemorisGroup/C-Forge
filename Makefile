# ── C-Forge Makefile ───────────────────────────────────────────────────────────
# Compila e instala el motor autónomo de C-Forge.
# C++ se usa únicamente como bootstrap del ejecutable nativo. La biblioteca
# estándar, las pruebas y los programas distribuidos usan archivos .cfv.

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter \
            -Wno-unused-variable -Wno-unused-function
INCLUDES := -I. -I./include
LIBS     := -lpthread -ldl
TARGET   := cforge
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

# ── Targets ────────────────────────────────────────────────────────────────────

.PHONY: all build debug release install uninstall test check backend-check install-check clean help

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

## Instalar en el sistema
install: build
	@echo "  INSTALL  $(PREFIX)/bin/cforge"
	@install -d $(PREFIX)/bin
	@install -m 755 $(TARGET) $(PREFIX)/bin/cforge
	@ln -sf cforge $(PREFIX)/bin/cforgev
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

## Verificar todos los programas de prueba escritos en C-Forge
check: build
	@set -e; \
	for file in stdlib/*.cfv; do \
		echo "  CHECK $$file"; \
		CFORGE_STDLIB=./stdlib ./$(TARGET) check "$$file"; \
	done; \
	for file in tests/cfv/*.cfv; do \
		echo "  CHECK $$file"; \
		CFORGE_STDLIB=./stdlib ./$(TARGET) check "$$file"; \
	done; \
	for file in main.cfv ejemplos/*.cfv benchmarks/*.cfv bootstrap/direct/*.cfv; do \
		echo "  CHECK $$file"; \
		CFORGE_STDLIB=./stdlib ./$(TARGET) check "$$file"; \
	done

## Ejecutar pruebas nativas .cfv
test: build
	@set -e; total=0; \
	for file in tests/cfv/*.cfv; do \
		echo "  TEST  $$file"; \
		CFORGE_STDLIB=./stdlib ./$(TARGET) test "$$file"; \
		total=$$((total + 1)); \
	done; \
	echo "  ✓ $$total archivos de prueba C-Forge aprobados"

## Verificar emisores binarios directos y ejecutar el formato anfitrión
backend-check: build
	@set -e; fixture="$$(pwd)/bootstrap/fixtures/machine_hello_b6.cfv"; \
	./$(TARGET) bootstrap/direct/cforge_macho_arm64.cfv "$$fixture" -o /tmp/cforge-check-macho; \
	./$(TARGET) bootstrap/direct/cforge_elf_x64.cfv "$$fixture" -o /tmp/cforge-check-elf; \
	./$(TARGET) bootstrap/direct/cforge_pe_x64.cfv "$$fixture" -o /tmp/cforge-check.exe; \
	file /tmp/cforge-check-macho | grep -q "Mach-O 64-bit executable arm64"; \
	file /tmp/cforge-check-elf | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file /tmp/cforge-check.exe | grep -q "PE32+ executable.*x86-64"; \
	if [ "$(UNAME)" = "Darwin" ] && [ "$(ARCH)" = "arm64" ]; then \
		test "$$(/tmp/cforge-check-macho)" = "Hola maquina C-Forge"; \
	elif [ "$(UNAME)" = "Linux" ] && [ "$(ARCH)" = "x86_64" ]; then \
		test "$$(/tmp/cforge-check-elf)" = "Hola maquina C-Forge"; \
	fi; \
	echo "  ✓ Mach-O ARM64, ELF x64 y PE x64 verificados"

## Probar una instalación aislada sin modificar el sistema
install-check: build
	@set -e; prefix=/tmp/cforge-install-check; \
	rm -rf "$$prefix"; \
	$(MAKE) install PREFIX="$$prefix" >/dev/null; \
	test "$$($$prefix/bin/cforge --version)" = "C-Forge 2.6.0-dev"; \
	CFORGE_STDLIB="$$prefix/lib/cforge/stdlib" \
		"$$prefix/bin/cforge" tests/cfv/01_nucleo.cfv >/dev/null; \
	echo "  ✓ instalación aislada verificada en $$prefix"

## Benchmark rápido
bench: build
	@echo "  BENCH  fib(30)"
	@echo 'funcion fib(n) { si(n<=1){retornar n} retornar fib(n-1)+fib(n-2) } mostrar(fib(30))' \
	    > /tmp/bench.cfv
	@time ./$(TARGET) /tmp/bench.cfv

## Limpiar artefactos de compilación
clean:
	@rm -f cforge cforgev *.o
	@echo "  CLEAN  artefactos eliminados"

## Ayuda
help:
	@echo ""
	@echo "  C-Forge Makefile"
	@echo "  ──────────────────────────────────────────"
	@echo "  make              Compilar cforge"
	@echo "  make debug        Compilar en modo debug"
	@echo "  make release      Compilar con O3 + march=native"
	@echo "  make sdl          Compilar con soporte SDL2"
	@echo "  make install      Instalar en $(PREFIX)"
	@echo "  make uninstall    Desinstalar"
	@echo "  make check        Verificar sintaxis de pruebas .cfv"
	@echo "  make test         Ejecutar tests básicos"
	@echo "  make backend-check Verificar Mach-O, ELF y PE"
	@echo "  make install-check Probar instalación aislada"
	@echo "  make bench        Benchmark fib(30)"
	@echo "  make clean        Limpiar artefactos"
	@echo ""
	@echo "  Variables:"
	@echo "  CXX=clang++       Cambiar compilador"
	@echo "  PREFIX=/opt/cf    Cambiar directorio de instalación"
	@echo ""
