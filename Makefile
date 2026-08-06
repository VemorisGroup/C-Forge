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
VERSION  := 3.2.0

# Detección de plataforma
UNAME := $(shell uname -s)
ARCH  := $(shell uname -m)

# OpenSSL
# En Linux hay que verificar TANTO la librería (.so) COMO los headers de desarrollo.
# Sistemas con libssl3 pero sin libssl-dev tienen el .so pero no openssl/sha.h,
# lo que hace que -DCFV_WITH_OPENSSL rompa la compilación.
ifeq ($(UNAME),Darwin)
    OPENSSL_PREFIX := $(shell brew --prefix openssl 2>/dev/null || echo /usr/local)
    INCLUDES  += -I$(OPENSSL_PREFIX)/include
    LIBS      += -L$(OPENSSL_PREFIX)/lib -lssl -lcrypto
    CXXFLAGS  += -DCFV_WITH_OPENSSL
else
    OPENSSL_LIB := $(firstword $(wildcard \
        /usr/lib/$(ARCH)-linux-gnu/libcrypto.so.3 \
        /usr/lib/x86_64-linux-gnu/libcrypto.so.3 \
        /usr/lib/aarch64-linux-gnu/libcrypto.so.3 \
        /usr/lib/libcrypto.so))
    # Buscar headers en rutas estándar Y en la instalación de Node (fallback común)
    OPENSSL_HDR := $(firstword $(wildcard \
        /usr/include/openssl/sha.h \
        /usr/local/include/openssl/sha.h \
        /usr/include/node/openssl/sha.h))
    ifneq ($(OPENSSL_LIB),)
    ifneq ($(OPENSSL_HDR),)
        OPENSSL_INC := $(patsubst %/openssl/sha.h,%,$(OPENSSL_HDR))
        INCLUDES  += -I$(OPENSSL_INC)
        LIBS      += $(OPENSSL_LIB)
        CXXFLAGS  += -DCFV_WITH_OPENSSL
    endif
    endif
endif

# ── Targets ────────────────────────────────────────────────────────────────────

.PHONY: all build debug release install uninstall test check stdlib-load-check \
	cli-check malformed-check sanitize-check backend-check install-check \
	bootstrap-bundle backend-core-bundle bootstrap-check backend-core-check ir-core-check object-lowering-check release-check clean help

# Regenera Stage 1 desde sus fuentes canónicas .cfv. Esto evita que el
# compilador empaquetado quede desfasado del lexer, parser o runtime reales.
bootstrap-bundle:
	@{ \
		echo '// C-Forge Stage 1 Bootstrap B4.'; \
		echo '// Archivo generado únicamente a partir de componentes escritos en .cfv.'; \
		for source in bootstrap/core_lexer.cfv bootstrap/core_ast.cfv \
			bootstrap/core_parser.cfv bootstrap/core_semantics.cfv \
			bootstrap/core_ownership.cfv \
			bootstrap/core_runtime.cfv bootstrap/core_emitter.cfv \
			bootstrap/core_driver.cfv; do \
			echo; echo "// ===== $$source ====="; cat "$$source"; \
		done; \
	} > bootstrap/stage1/cforge_stage1.cfv
	@echo "  GEN  bootstrap/stage1/cforge_stage1.cfv"

# Empaqueta los tres compiladores directos desde un único frontend y lowering.
# awk solo retira el CLI del emisor mínimo; la biblioteca binaria permanece .cfv.
backend-core-bundle:
	@set -e; \
	for platform in macho_arm64 elf_x64 pe_x64; do \
		output="bootstrap/direct/cforge_$${platform}_core.cfv"; \
		base="bootstrap/direct/cforge_$${platform}.cfv"; \
		backend="bootstrap/direct/cforge_$${platform}_core_backend.cfv"; \
		{ \
			for source in bootstrap/core_lexer.cfv bootstrap/core_ast.cfv \
				bootstrap/core_parser.cfv bootstrap/core_ir.cfv \
				bootstrap/core_object_lowering.cfv bootstrap/core_map_lowering.cfv \
				bootstrap/core_exception_lowering.cfv \
				bootstrap/core_ownership.cfv \
				bootstrap/core_modules.cfv; do cat "$$source"; echo; done; \
			awk '/^sea argumentos[^:]*: lista = argumentos_programa\(\)/ { exit } { print }' "$$base"; \
			cat "$$backend"; \
		} > "$$output"; \
		done
	@echo "  GEN  Mach-O ARM64, ELF x64 y PE x64 Core"

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
bootstrap-check: bootstrap-bundle
	@set -e; dir=$$(mktemp -d /tmp/cforge-bootstrap.XXXXXX); \
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic \
		bootstrap/stage0/cforge_bootstrap.cpp -o "$$dir/stage0"; \
	"$$dir/stage0" bootstrap/stage1/cforge_stage1.cfv -o "$$dir/stage1" >/dev/null; \
	"$$dir/stage1" bootstrap/stage1/cforge_stage1.cfv -o "$$dir/stage2" >/dev/null; \
	"$$dir/stage2" bootstrap/stage1/cforge_stage1.cfv -o "$$dir/stage3" >/dev/null; \
	cmp "$$dir/stage2" "$$dir/stage3"; \
	"$$dir/stage3" bootstrap/fixtures/minimal.cfv -o "$$dir/minimal" >/dev/null; \
	test "$$($$dir/minimal)" = "$$(printf 'C-Forge Core Bootstrap\n42')"; \
	"$$dir/stage3" bootstrap/fixtures/mapas_core.cfv -o "$$dir/mapas" >/dev/null; \
	test "$$($$dir/mapas)" = "$$(printf '8\n12\n443')"; \
	"$$dir/stage3" bootstrap/fixtures/excepciones_core.cfv -o "$$dir/excepciones" >/dev/null; \
	test "$$($$dir/excepciones)" = "capturado: saldo insuficiente"; \
	echo "  ✓ Stage 2 y Stage 3 son idénticos; compilador Core autoalojado"

## Verificar los backends Core B6.8 sin toolchain durante la emisión.
backend-core-check: backend-core-bundle
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
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_objects_b611.cfv -o "$$dir/objetos-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_objects_b611.cfv -o "$$dir/objetos.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_objects_b611.cfv -o "$$dir/objetos-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_methods_b612.cfv -o "$$dir/metodos-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_methods_b612.cfv -o "$$dir/metodos.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_methods_b612.cfv -o "$$dir/metodos-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_mutable_methods_b613.cfv -o "$$dir/mutables-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_mutable_methods_b613.cfv -o "$$dir/mutables.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_mutable_methods_b613.cfv -o "$$dir/mutables-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_lifecycle_b614.cfv -o "$$dir/ciclo-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_lifecycle_b614.cfv -o "$$dir/ciclo.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_lifecycle_b614.cfv -o "$$dir/ciclo-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_interfaces_b615.cfv -o "$$dir/interfaz-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_interfaces_b615.cfv -o "$$dir/interfaz.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_interfaces_b615.cfv -o "$$dir/interfaz-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/modules_b616/main.cfv -o "$$dir/modulos-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/modules_b616/main.cfv -o "$$dir/modulos.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/modules_b616/main.cfv -o "$$dir/modulos-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_maps_b617.cfv -o "$$dir/mapas-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_maps_b617.cfv -o "$$dir/mapas.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_maps_b617.cfv -o "$$dir/mapas-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_exceptions_b619.cfv -o "$$dir/excepciones-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_exceptions_b619.cfv -o "$$dir/excepciones.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_exceptions_b619.cfv -o "$$dir/excepciones-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_exception_calls_b620.cfv -o "$$dir/excepciones-funcion-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_exception_calls_b620.cfv -o "$$dir/excepciones-funcion.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_exception_calls_b620.cfv -o "$$dir/excepciones-funcion-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_ownership_b621.cfv -o "$$dir/ownership-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_ownership_b621.cfv -o "$$dir/ownership.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_ownership_b621.cfv -o "$$dir/ownership-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_nested_lifecycle_b621.cfv -o "$$dir/ambitos-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_nested_lifecycle_b621.cfv -o "$$dir/ambitos.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_nested_lifecycle_b621.cfv -o "$$dir/ambitos-elf" >/dev/null; \
	env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_exception_cleanup_b621.cfv -o "$$dir/limpieza-error-macho" >/dev/null; \
	env PATH=/nonexistent "$$dir/pe-core" \
		bootstrap/fixtures/machine_exception_cleanup_b621.cfv -o "$$dir/limpieza-error.exe" >/dev/null; \
	env PATH=/nonexistent "$$dir/elf-core" \
		bootstrap/fixtures/machine_exception_cleanup_b621.cfv -o "$$dir/limpieza-error-elf" >/dev/null; \
	for invalido in \
		bootstrap/fixtures/machine_ownership_use_after_move_b621.cfv \
		bootstrap/fixtures/machine_ownership_move_borrowed_b621.cfv \
		bootstrap/fixtures/machine_ownership_alias_b621.cfv; do \
		if env PATH=/nonexistent "$$dir/macho-core" "$$invalido" \
			-o "$$dir/ownership-invalido" >/dev/null 2>&1; then \
			echo "ownership inválido aceptado: $$invalido" >&2; exit 1; \
		fi; \
	done; \
	if env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/machine_interfaces_invalid_b615.cfv \
		-o "$$dir/interfaz-invalida" >/dev/null 2>&1; then \
		echo "la interfaz incompleta fue aceptada" >&2; exit 1; \
	fi; \
	if env PATH=/nonexistent "$$dir/macho-core" \
		bootstrap/fixtures/modules_b616/ciclo_a.cfv \
		-o "$$dir/ciclo-importacion" >/dev/null 2>&1; then \
		echo "el ciclo de importación fue aceptado" >&2; exit 1; \
	fi; \
	cmp "$$dir/uno-macho" "$$dir/dos-macho"; \
	cmp "$$dir/uno.exe" "$$dir/dos.exe"; \
	cmp "$$dir/uno-elf" "$$dir/dos-elf"; \
	file "$$dir/uno-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/uno.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/uno-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/objetos-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/objetos.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/objetos-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/metodos-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/metodos.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/metodos-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/mutables-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/mutables.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/mutables-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/ciclo-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/ciclo.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/ciclo-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/interfaz-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/interfaz.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/interfaz-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/modulos-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/modulos.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/modulos-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/mapas-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/mapas.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/mapas-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/excepciones-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/excepciones.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/excepciones-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/excepciones-funcion-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/excepciones-funcion.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/excepciones-funcion-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/ownership-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/ownership.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/ownership-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/ambitos-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/ambitos.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/ambitos-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	file "$$dir/limpieza-error-macho" | grep -q "Mach-O 64-bit executable arm64"; \
	file "$$dir/limpieza-error.exe" | grep -q "PE32+ executable.*x86-64"; \
	file "$$dir/limpieza-error-elf" | grep -q "ELF 64-bit LSB executable, x86-64"; \
	if [ "$(UNAME)" = "Darwin" ] && [ "$(ARCH)" = "arm64" ]; then \
		test "$$($$dir/uno-macho)" = "C-FORGE-B6.7-OK"; \
		test "$$($$dir/objetos-macho)" = "C-FORGE-B6.11-OBJECT-OK"; \
		test "$$($$dir/metodos-macho)" = "C-FORGE-B6.12-METHOD-OK"; \
		test "$$($$dir/mutables-macho)" = "C-FORGE-B6.13-MUTABLE-OK"; \
		test "$$($$dir/ciclo-macho)" = "$$(printf 'C-FORGE-B6.14-CONSTRUCTOR-OK\nC-FORGE-B6.14-DESTRUCTOR-OK')"; \
		test "$$($$dir/interfaz-macho)" = "C-FORGE-B6.15-INTERFACE-OK"; \
		test "$$($$dir/modulos-macho)" = "C-FORGE-B6.16-MODULES-OK"; \
		test "$$($$dir/mapas-macho)" = "C-FORGE-B6.17-MAPS-OK"; \
		test "$$($$dir/excepciones-macho)" = "$$(printf 'saldo insuficiente\ncaptura completada')"; \
		test "$$($$dir/excepciones-funcion-macho)" = "$$(printf 'saldo insuficiente desde funcion\npropagacion capturada')"; \
		test "$$($$dir/ownership-macho)" = "C-FORGE-B6.21-OWNERSHIP-OK"; \
		test "$$($$dir/ambitos-macho)" = "$$(printf 'dentro\ndestruir interior\nfuera\ndestruir exterior')"; \
		test "$$($$dir/limpieza-error-macho)" = "$$(printf 'limpieza antes del error\nerror controlado')"; \
	fi; \
	if [ "$(UNAME)" = "Linux" ] && [ "$(ARCH)" = "x86_64" ]; then \
		test "$$($$dir/uno-elf)" = "C-FORGE-B6.7-OK"; \
		test "$$($$dir/objetos-elf)" = "C-FORGE-B6.11-OBJECT-OK"; \
		test "$$($$dir/metodos-elf)" = "C-FORGE-B6.12-METHOD-OK"; \
		test "$$($$dir/mutables-elf)" = "C-FORGE-B6.13-MUTABLE-OK"; \
		test "$$($$dir/ciclo-elf)" = "$$(printf 'C-FORGE-B6.14-CONSTRUCTOR-OK\nC-FORGE-B6.14-DESTRUCTOR-OK')"; \
		test "$$($$dir/interfaz-elf)" = "C-FORGE-B6.15-INTERFACE-OK"; \
		test "$$($$dir/modulos-elf)" = "C-FORGE-B6.16-MODULES-OK"; \
		test "$$($$dir/mapas-elf)" = "C-FORGE-B6.17-MAPS-OK"; \
		test "$$($$dir/excepciones-elf)" = "$$(printf 'saldo insuficiente\ncaptura completada')"; \
		test "$$($$dir/excepciones-funcion-elf)" = "$$(printf 'saldo insuficiente desde funcion\npropagacion capturada')"; \
		test "$$($$dir/ownership-elf)" = "C-FORGE-B6.21-OWNERSHIP-OK"; \
		test "$$($$dir/ambitos-elf)" = "$$(printf 'dentro\ndestruir interior\nfuera\ndestruir exterior')"; \
		test "$$($$dir/limpieza-error-elf)" = "$$(printf 'limpieza antes del error\nerror controlado')"; \
	fi; \
	echo "  ✓ Mach-O ARM64, ELF x64 y PE x64 Core B6.21 con ownership y RAII"

## Verificar el IR común de objetos que consumirán los tres backends.
ir-core-check:
	@set -e; dir=$$(mktemp -d /tmp/cforge-ir-core.XXXXXX); \
	awk 'FNR==1 { print "" } { print }' \
		bootstrap/core_ast.cfv \
		bootstrap/core_ir.cfv \
		bootstrap/fixtures/ir_b69_driver.cfv > "$$dir/ir.cfv"; \
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic \
		bootstrap/stage0/cforge_bootstrap.cpp -o "$$dir/stage0"; \
	"$$dir/stage0" "$$dir/ir.cfv" -o "$$dir/ir-test" >/dev/null; \
	test "$$($$dir/ir-test)" = \
		'CFIR1[estructura Punto size=16 align=8 {x:numero@0,y:numero@8};clase Contador size=8 align=8 {valor:numero@0}]'; \
	echo "  ✓ C-Forge Core IR 1 determinista verificado"

## Verificar la reducción común de objetos antes de emitir Mach-O, ELF o PE.
object-lowering-check:
	@set -e; dir=$$(mktemp -d /tmp/cforge-object-lowering.XXXXXX); \
	awk 'FNR==1 { print "" } { print }' \
		bootstrap/core_ast.cfv \
		bootstrap/core_ir.cfv \
		bootstrap/core_object_lowering.cfv \
		bootstrap/fixtures/object_lowering_b610.cfv > "$$dir/lowering.cfv"; \
	$(CXX) -std=c++20 -O2 -Wall -Wextra -Wpedantic \
		bootstrap/stage0/cforge_bootstrap.cpp -o "$$dir/stage0"; \
	"$$dir/stage0" "$$dir/lowering.cfv" -o "$$dir/lowering-test" >/dev/null; \
	"$$dir/lowering-test" > "$$dir/ast.txt"; \
	grep -q 'p__campo__x' "$$dir/ast.txt"; \
	grep -q 'p__campo__y' "$$dir/ast.txt"; \
	! grep -q 'Estructura' "$$dir/ast.txt"; \
	echo "  ✓ objetos Core reducidos a almacenamiento común verificable"

## Gate único exigido antes de publicar una versión estable.
release-check: clean build check test stdlib-load-check cli-check malformed-check \
	backend-check install-check bootstrap-check backend-core-check ir-core-check \
	object-lowering-check sanitize-check
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
	@echo "  make object-lowering-check Verificar reducción común de objetos B6.10"
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
	@echo "  make ir-core-check Verificar el IR común de objetos B6.9"
	@echo "  make release-check Ejecutar todos los gates de estabilidad"
	@echo "  make bench        Benchmark fib(30)"
	@echo "  make clean        Limpiar artefactos"
	@echo ""
	@echo "  Variables:"
	@echo "  CXX=clang++       Cambiar compilador"
	@echo "  PREFIX=/opt/cf    Cambiar directorio de instalación"
	@echo ""
