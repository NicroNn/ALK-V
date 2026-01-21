#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 <file.alkv>"
  exit 1
fi

ROOT_DIR="$(pwd)"
INPUT_PATH="$1"

if [[ ! -f "$INPUT_PATH" ]]; then
  echo "Error: file not found: $INPUT_PATH"
  exit 1
fi

INPUT_BASENAME="$(basename "$INPUT_PATH")"      # file.alkv
STEM="${INPUT_BASENAME%.alkv}"                  # file
OUT_ALKB="${STEM}.alkb"

# 1) cd to alkv-java/src
cd "$ROOT_DIR/alkv-java/src"

# 2) run compiler (it generates .alkb, apparently into ROOT_DIR)
java -cp out alkv.cli.Main "$ROOT_DIR/$INPUT_PATH"

# 3) copy resulting .alkb from ROOT_DIR to runtime/build
cd "$ROOT_DIR"
if [[ ! -f "$ROOT_DIR/$OUT_ALKB" ]]; then
  echo "Error: expected output not found in root: $ROOT_DIR/$OUT_ALKB"
  exit 1
fi

mkdir -p "$ROOT_DIR/runtime/build"
cp -f "$ROOT_DIR/$OUT_ALKB" "$ROOT_DIR/runtime/build/$OUT_ALKB"

# 4) cd to runtime/build
cd "$ROOT_DIR/runtime/build"

# 5) run VM
./alkv_vm "$OUT_ALKB"