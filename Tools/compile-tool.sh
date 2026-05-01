#!/bin/bash

set -e

TOOL_DIR="$1"

if [ -z "$TOOL_DIR" ]; then
  echo "Usage: $0 <tool_directory>"
  exit 1
fi

TOOL_NAME=$(basename "$TOOL_DIR")
SRC_DIR="$TOOL_DIR/src"
OUTPUT="$TOOL_DIR/$TOOL_NAME.dylib"

if [ ! -d "$SRC_DIR" ]; then
  echo "Error: no src/ directory in $TOOL_DIR"
  exit 1
fi

SOURCES=$(find "$SRC_DIR" -name "*.cpp")

if [ -z "$SOURCES" ]; then
  echo "Error: no .cpp files found in $SRC_DIR"
  exit 1
fi

NLOHMANN_INCLUDE="/Users/architect/Documents/Projects/Prometheus/third_party"

clang++ -std=c++20 -shared -fPIC -O2 \
  $SOURCES \
  -I/opt/homebrew/include \
  -I"$NLOHMANN_INCLUDE" \
  -L/opt/homebrew/lib \
  -lcurl \
  -o "$OUTPUT"

echo "Built: $OUTPUT"
