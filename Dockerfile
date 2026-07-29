# ── C-Forge Dockerfile ─────────────────────────────────────────────────────────
# Multi-stage build: compila cforgev y empaqueta todo en imagen mínima
# Uso:
#   docker build -t cforge .
#   docker run --rm -v $(pwd):/work cforge run /work/mi_script.cfv

# ── Stage 1: compilar el intérprete ───────────────────────────────────────────
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ \
        libssl-dev \
        make \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY cforgev.cpp .
COPY include/      ./include/
COPY Makefile      .

# Compilar con OpenSSL
RUN make build

# ── Stage 2: imagen de ejecución ──────────────────────────────────────────────
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 \
        python3 \
        python3-pip \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Binario del intérprete
COPY --from=builder /src/cforgev /usr/local/bin/cforgev
RUN chmod +x /usr/local/bin/cforgev

# Herramientas Python y stdlib
COPY tools/   /opt/cforge/tools/
COPY stdlib/  /opt/cforge/stdlib/

# Instalar herramientas Python como comandos del sistema
RUN pip3 install --no-cache-dir --break-system-packages cforge-tools 2>/dev/null || true && \
    for tool in cfmt cflint cftest cfdoc cfwatch cforgec; do \
        printf '#!/usr/bin/env python3\nimport sys; sys.path.insert(0, "/opt/cforge"); exec(open("/opt/cforge/tools/%s.py").read())\n' "$tool" \
            > "/usr/local/bin/$tool" && chmod +x "/usr/local/bin/$tool"; \
    done && \
    printf '#!/usr/bin/env python3\nimport sys; sys.path.insert(0, "/opt/cforge"); exec(open("/opt/cforge/tools/cforge_cli.py").read())\n' \
        > /usr/local/bin/cforge && chmod +x /usr/local/bin/cforge

# Alias cforge → cforgev para compatibilidad
RUN ln -sf /usr/local/bin/cforgev /usr/local/bin/cforgev_interp

# Variables de entorno
ENV CFORGE_STDLIB=/opt/cforge/stdlib
ENV PATH="/usr/local/bin:$PATH"

WORKDIR /work

ENTRYPOINT ["cforge"]
CMD ["--help"]

# ── Uso rápido ────────────────────────────────────────────────────────────────
# Ejecutar un script:
#   docker run --rm -v $(pwd):/work cforge run /work/script.cfv
#
# REPL interactivo:
#   docker run --rm -it cforge repl
#
# Formatear:
#   docker run --rm -v $(pwd):/work --entrypoint cfmt cforge /work/script.cfv
#
# Compilar proyecto:
#   docker run --rm -v $(pwd):/work cforge build --release
