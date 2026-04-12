#!/bin/bash
set -e

if ! command -v emcmake &>/dev/null && [ -x /usr/lib/emscripten/emcmake ]; then
  export PATH="/usr/lib/emscripten:$PATH"
fi

emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web -j$(nproc)
echo "Serve build-web/ with a local HTTP server, e.g.:"
echo "  python3 -m http.server -d build-web 8080"
echo "Then open http://localhost:8080/citris.html"
