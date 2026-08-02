# ── C-Forge Dockerfile ─────────────────────────────────────────────────────────
# Multi-stage build: compila cforge y empaqueta todo en imagen mínima
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
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Un solo motor nativo: no requiere Python, JVM, .NET ni Node.
COPY --from=builder /src/cforge /usr/local/bin/cforge
RUN chmod +x /usr/local/bin/cforge && \
    ln -s /usr/local/bin/cforge /usr/local/bin/cforgev

# Biblioteca estándar escrita en C-Forge
COPY stdlib/  /opt/cforge/stdlib/

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
# Verificar:
#   docker run --rm -v $(pwd):/work cforge check /work/script.cfv
