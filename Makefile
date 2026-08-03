# ── C-Forge Makefile ───────────────────────────────────────────────────────────
# Compila e instala el motor autónomo de C-Forge.
# C++ se usa únicamente como bootstrap del ejecutable nativo. La biblioteca
# estándar, las pruebas y los programas distribuidos usan archivos .cfv.

CXX      ?= g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -Wpedantic
INCLUDES := -I. -I./include
LIBS     := -lpthread -ldl
TARGET   := cforge
SRC      := cforgev.cpp
PREFIX   ?= /usr/local
VERSION  := 2.6.0

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

.PHONY: all build debug release install uninstall test check stdlib-load-check \
	cli-check malformed-check sanitize-check backend-check install-check \
	bootstrap-check backend-core-check release-check clean help

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
	for file in main.cfv ejemplos/*.cfv benchmarks/*.cfv \
		bootstrap/direct/cforge_macho_arm64.cfv \
		bootstrap/direct/cforge_elf_x64.cfv \
		bootstrap/direct/cforge_pe_x64.cfv; do \
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

## Importar y ejecutar realmente cada módulo publicado de la biblioteca.
## Esto detecta dependencias rotas que un análisis sintáctico aislado no ve.
stdlib-load-check: build
	@set -e; total=0; \
	for file in stdlib/*.cfv; do \
		module=$$(basename "$$file" .cfv); \
		fixture="/tmp/cforge-load-$$module.cfv"; \
		printf 'importar "%s"\n' "$$module" > "$$fixture"; \
		CFORGE_STDLIB=./stdlib ./$(TARGET) "$$fixture" >/dev/null; \
		total=$$((total + 1)); \
	done; \
	echo "  ✓ $$total módulos C-Forge cargados realmente"

## Contrato público mínimo del CLI.
cli-check: build
	@set -e; \
	test "$$(./$(TARGET) --version)" = "C-Forge $(VERSION)"; \
	./$(TARGET) --help | grep -q "cforge check"; \
	./$(TARGET) check tests/cfv/01_nucleo.cfv >/dev/null; \
	if ./$(TARGET) check /tmp/no-existe-cforge.cfv >/tmp/cforge-cli-out 2>/tmp/cforge-cli-err; then \
		echo "  ERROR el CLI aceptó un archivo inexistente"; exit 1; \
	fi; \
	grep -q "C-Forge" /tmp/cforge-cli-err; \
	echo "  ✓ contrato CLI verificado"

## Entradas dañadas deben fallar limpiamente, nunca abortar ni generar traceback.
malformed-check: build
	@set -e; total=0; \
	for source in '}{' '@@@@' 'sea =' 'mostrar("sin cerrar)' 'si (verdadero) {'; do \
		printf '%s\n' "$$source" > /tmp/cforge-malformed.cfv; \
		if ./$(TARGET) /tmp/cforge-malformed.cfv >/tmp/cforge-malformed-out 2>/tmp/cforge-malformed-err; then \
			echo "  ERROR entrada inválida aceptada: $$source"; exit 1; \
		fi; \
		grep -q "C-Forge" /tmp/cforge-malformed-err; \
		if grep -Eqi 'traceback|segmentation fault|addresssanitizer|undefinedbehavior' \
			/tmp/cforge-malformed-out /tmp/cforge-malformed-err; then \
			echo "  ERROR fallo interno expuesto"; exit 1; \
		fi; \
		total=$$((total + 1)); \
	done; \
	echo "  ✓ $$total entradas dañadas rechazadas limpiamente"

## Ejecutar el núcleo con AddressSanitizer y UndefinedBehaviorSanitizer.
sanitize-check:
	@echo "  SANITIZE cforgev.cpp"
	@$(CXX) -std=c++20 -O1 -g -fno-omit-frame-pointer \
		-fsanitize=address,undefined $(INCLUDES) -o /tmp/cforge-sanitize \
		$(SRC) $(LIBS)
	@set -e; total=0; \
	for file in tests/cfv/*.cfv; do \
		CFORGE_STDLIB=./stdlib /tmp/cforge-sanitize test "$$file" >/dev/null; \
		total=$$((total + 1)); \
	done; \
	echo "  ✓ $$total pruebas aprobadas con sanitizadores"

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
	test "$$($$prefix/bin/cforge --version)" = "C-Forge $(VERSION)"; \
	CFORGE_STDLIB="$$prefix/lib/cforge/stdlib" \
		"$$prefix/bin/cforge" tests/cfv/01_nucleo.cfv >/dev/null; \
	echo "  ✓ instalación aislada verificada en $$prefix"

## Verificar el frontend autoalojado de C-Forge Core.
## Stage 0 construye Stage 1; Stage 1 construye Stage 2; Stage 2 construye
## Stage 3. Los dos últimos artefactos deben ser idénticos byte por byte.
bootstrap-check:
	@set -e; dir=$$(mktemp -d /tmp/cforge-bootstrap.XXXXXX); \
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic \
		bootstrap/stage0/cforge_bootstrap.cpp -o "$$dir/stage0"; \
	"$$dir/stage0" bootstrap/stage1/cforge_stage1.cfv -o "$$dir/stage1" >/dev/null; \
	"$$dir/stage1" bootstrap/stage1/cforge_stage1.cfv -o "$$dir/stage2" >/dev/null; \
	"$$dir/stage2" bootstrap/stage1/cforge_stage1.cfv -o "$$dir/stage3" >/dev/null; \
	cmp "$$dir/stage2" "$$dir/stage3"; \
	"$$dir/stage3" bootstrap/fixtures/minimal.cfv -o "$$dir/minimal" >/dev/null; \
	test "$$($$dir/minimal)" = "$$(printf 'C-Forge Core Bootstrap\n42')"; \
	echo "  ✓ Stage 2 y Stage 3 son idénticos; compilador Core autoalojado"

## Verificar los backends Core B6.8 sin toolchain durante la emisión.
backend-core-check:
	@set -e; dir=$$(mktemp -d /tmp/cforge-backend-core.XXXXXX); \
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic \
		bootstrap/stage0/cforge_bootstrap.cpp -o "$$dir/stage0"; \
	"$$dir/stage0" bootstrap/direct/cforge_macho_arm64_core.cfv \
		-o "$$dir/macho-core" >/dev/null; \
	"$$dir/stage0" bootstrap/direct/cforge_pe_x64_core.cfv \
		-o "$$dir/pe-core" >/dev/null; \
	"$$dir/stage0" bootstrap/direct/cforge_elf_x64_core.cfv \
		-o "$$dir/elf-core" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_runtime_b6.cfv -o "$$dir/uno-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_runtime_b6.cfv -o "$$dir/dos-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_runtime_b6.cfv -o "$$dir/uno.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_runtime_b6.cfv -o "$$dir/dos.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_runtime_b6.cfv -o "$$dir/uno-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_runtime_b6.cfv -o "$$dir/dos-elf" >/dev/null; \
	cmp "$$dir/uno-macho" "$$dir/dos-macho"; \
	cmp "$$dir/uno.exe" "$$dir/dos.exe"; \
	cmp "$$dir/uno-elf" "$$dir/dos-elf"; \
	file "$$dir/uno-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/uno.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/uno-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	if [ "$(UNAME)" = "Darwin" ] && [ "$(ARCH)" = "arm64" ]; then \
		test "$$($$dir/uno-macho)" = "C-FORGE-B6.7-OK"; \
	fi; \
	if [ "$(UNAME)" = "Linux" ] && [ "$(ARCH)" = "x86_64" ]; then \
		test "$$($$dir/uno-elf)" = "C-FORGE-B6.7-OK"; \
	fi; \
	echo "  ✓ Mach-O ARM64, ELF x64 y PE x64 Core B6.8 sin toolchain externa"

## Gate único exigido antes de publicar una versión estable.
release-check: clean build check test stdlib-load-check cli-check malformed-check \
	backend-check install-check bootstrap-check backend-core-check sanitize-check
	@echo ""
	@echo "  ✓ GATE DE ESTABILIDAD C-FORGE COMPLETO"

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
	@echo "  make stdlib-load-check Cargar realmente cada módulo"
	@echo "  make cli-check    Verificar contrato del CLI"
	@echo "  make malformed-check Rechazar entradas dañadas"
	@echo "  make sanitize-check Ejecutar ASan y UBSan"
	@echo "  make backend-check Verificar Mach-O, ELF y PE"
	@echo "  make install-check Probar instalación aislada"
	@echo "  make bootstrap-check Verificar autoalojamiento Stage 1→2→3"
	@echo "  make backend-core-check Verificar backends Core B6.8 sin toolchain"
	@echo "  make release-check Ejecutar todos los gates de estabilidad"
	@echo "  make bench        Benchmark fib(30)"
	@echo "  make clean        Limpiar artefactos"
	@echo ""
	@echo "  Variables:"
	@echo "  CXX=clang++       Cambiar compilador"
	@echo "  PREFIX=/opt/cf    Cambiar directorio de instalación"
	@echo ""
