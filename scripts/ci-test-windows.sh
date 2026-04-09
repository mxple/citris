#!/usr/bin/env bash
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

HOST_UID=$(id -u) HOST_GID=$(id -g)
docker run --rm -v "$PWD:/src" -w /src archlinux:latest bash -c "
  set -euo pipefail
  pacman -Syu --noconfirm base-devel git mingw-w64-gcc cmake vim zip unzip curl tar pkg-config

  git clone --depth 1 https://github.com/microsoft/vcpkg /vcpkg
  /vcpkg/bootstrap-vcpkg.sh

  VCPKG_ROOT=/vcpkg cmake -B build-ci \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE=/vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/src/cmake/mingw-w64-x86_64.cmake \
    -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic

  cmake --build build-ci -j\$(nproc) || true
  cmake --install build-ci --prefix build-ci/dist
  cd build-ci && cpack

  chown -R $HOST_UID:$HOST_GID /src/build-ci
"

echo "Done. Test with: cd build-ci/dist && wine citris.exe"
