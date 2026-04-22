#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

DELAY_SEC="${1:-10}"
OUT="${2:-flame.svg}"
FREQ=997
DELAY_MS=$((DELAY_SEC * 1000))

if ! command -v flamegraph >/dev/null; then
  echo "error: 'flamegraph' not found. Install with: cargo install flamegraph" >&2
  exit 1
fi

if [[ ! -x build-prof/citris ]]; then
  cmake -B build-prof -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT:?VCPKG_ROOT must be set}/scripts/buildsystems/vcpkg.cmake"
  cmake --build build-prof -j"$(nproc)" --target citris
fi

echo "Sampling starts after ${DELAY_SEC}s. Quit citris to finalize."
flamegraph -o "$OUT" \
  -c "record -F ${FREQ} --call-graph dwarf,16384 -g --delay=${DELAY_MS}" \
  -- ./build-prof/citris

echo "Wrote $OUT"
