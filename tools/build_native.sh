#!/bin/bash
set -e
REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
./cforgev compilador_nativo.cfv "$1" "$2"
