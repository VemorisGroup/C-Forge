"""Script para compilar cforgev.cfv a binario nativo. Fuerza UTF-8 en Windows."""
import sys
import io
import os
from pathlib import Path

# Añadir el directorio raíz del repo a sys.path (este script vive en tools/)
repo_root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(repo_root))

# Forzar UTF-8 en stdout/stderr antes de cualquier import
if hasattr(sys.stdout, 'buffer'):
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
if hasattr(sys.stderr, 'buffer'):
    sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8', errors='replace')

os.environ.setdefault('PYTHONUTF8', '1')

from compilador_nativo import compile_native

if len(sys.argv) < 3:
    print("uso: build_native.py <fuente.cfv> <binario_salida>")
    sys.exit(1)

source = Path(sys.argv[1])
output = Path(sys.argv[2])

print(f"Compilando {source} -> {output} ...")
result = compile_native(source, output)
print(f"OK: {result}")
