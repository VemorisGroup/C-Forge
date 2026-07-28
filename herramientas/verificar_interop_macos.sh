#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

mkdir -p build
clang++ -std=c++17 -dynamiclib -DCFV_NO_AUTO_REGISTER \
  ejemplos/interop/native_math.cpp -I include -o build/libnative_math.dylib

./cforgev --compilar ejemplos/interop/interoperabilidad.cfv \
  --vincular ejemplos/interop/native_math.cpp -o build/interop-1.0
./build/interop-1.0

if [ -f build/csharp-native/CSharpNative.dylib ]; then
  ./cforgev ejemplos/interop/csharp_aot.cfv
  ./cforgev --compilar ejemplos/interop/csharp_aot.cfv -o build/csharp-call-1.0
  ./build/csharp-call-1.0
else
  echo "C# Native AOT: omitiendo; primero publica CSharpNative.dylib"
fi
