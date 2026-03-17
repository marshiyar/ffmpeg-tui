#!/bin/bash
#
# Optimized build script for up60p_restore_beast_main.c
#

set -e

SOURCE_FILE="up60p_restore_beast_main.c"
OUTPUT_BIN="up60p_restore_beast"

if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: Source file '$SOURCE_FILE' not found!"
    exit 1
fi

echo "Compiling $SOURCE_FILE with optimizations..."

clang -std=c11 -O3 -Wall -Wextra \
  -o "$OUTPUT_BIN" \
  "$SOURCE_FILE"

echo "Build complete. Binary '$OUTPUT_BIN' created."
echo "To run it: ./$OUTPUT_BIN"
