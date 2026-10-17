#!/usr/bin/env bash
# Builds dist/LoFi100.g3a in build-cg/ inside a Docker image that contains the
# fxSDK (sh-elf-gcc 14.1, gint at commit 2bc6f7e) under /root/.local.
#   bash build.sh
#   FXSDK_IMAGE=my/fxsdk:tag bash build.sh
# With the fxSDK installed natively, `fxsdk build-cg` builds the same file.
set -euo pipefail
cd "$(dirname "$0")"
IMAGE="${FXSDK_IMAGE:-cgba-fxsdk:14.1-gint-2bc6f7e}"
docker run --rm --network none \
  -v "$PWD:/workspace" -w /workspace \
  --entrypoint /bin/bash "$IMAGE" -eu -c "
  cmake -S . -B build-cg \
    -DCMAKE_TOOLCHAIN_FILE=/root/.local/lib/cmake/fxsdk/FXCG50.cmake \
    -DCMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk \
    -DFXSDK_CMAKE_MODULE_PATH=/root/.local/lib/cmake/fxsdk
  cmake --build build-cg --parallel 4
  sh-elf-size build-cg/lofi100
  sha256sum dist/LoFi100.g3a
  "
