#!/bin/bash
# Checks ELF header architecture, dynamic needed libraries, or binary symbol tables.
set -euo pipefail

BINARY="$1"
MODE="$2"
PATTERN="$3"

echo "Inspecting BINARY: $BINARY" >&2
if [[ ! -f "$BINARY" ]]; then
  echo "ERROR: binary $BINARY not found" >&2
  exit 1
fi

case "$MODE" in
  arch)
    echo "Verifying ELF architecture header matches '$PATTERN'..."
    readelf -h "$BINARY" | grep -q "$PATTERN" || {
      echo "ERROR: ELF header for $BINARY does not match expected architecture '$PATTERN'" >&2
      readelf -h "$BINARY" >&2
      exit 1
    }
    ;;
  symbol)
    echo "Verifying binary symbol table contains '$PATTERN'..."
    nm "$BINARY" | grep -q "$PATTERN" || {
      echo "ERROR: Symbol '$PATTERN' not found in $BINARY" >&2
      exit 1
    }
    ;;
  needed)
    echo "Verifying ELF NEEDED dynamic dependencies contain '$PATTERN'..."
    readelf -d "$BINARY" | grep -q "$PATTERN" || {
      echo "ERROR: Dynamic dependency '$PATTERN' not found in $BINARY" >&2
      readelf -d "$BINARY" >&2
      exit 1
    }
    ;;
  not_needed)
    echo "Verifying ELF NEEDED dynamic dependencies do NOT contain '$PATTERN'..."
    if readelf -d "$BINARY" | grep -q "$PATTERN"; then
      echo "ERROR: Unexpected dynamic dependency '$PATTERN' found in $BINARY" >&2
      readelf -d "$BINARY" >&2
      exit 1
    fi
    ;;
  *)
    echo "ERROR: Unknown verification mode '$MODE'" >&2
    exit 1
    ;;
esac

echo "SUCCESS: Verified $MODE '$PATTERN' in $BINARY."
