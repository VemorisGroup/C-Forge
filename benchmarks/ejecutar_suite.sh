#!/bin/zsh

set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build/benchmarks"
mkdir -p "$build"

echo "C-Forge Benchmark Suite"
echo "Motor: $(cforge --version)"
echo "Hardware: $(uname -m) — $(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -p)"

for source in "$root"/benchmarks/[0-9][0-9]_*.cfv; do
  name="$(basename "$source" .cfv)"
  executable="$build/$name"
  echo
  echo "[C-Forge Benchmark] Compilando $name..."
  cforge --compilar "$source" -o "$executable"
  echo "[C-Forge Benchmark] Ejecutando $name..."
  "$executable"
done

echo
echo "[C-Forge Benchmark] Suite finalizada correctamente."
