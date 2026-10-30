#!/bin/bash
# ABOUTME: Builds and runs the Vulkan backend's pixel test (native/rlvk/test_rlvk.cpp) on the machine's Vulkan GPU, without a window.
# Set VK_ICD_FILENAMES to choose the card. RLGL_DIR = folder with raylib's rlgl.h (default: the one the runtime build fetched).
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
RLGL_DIR=${RLGL_DIR:-$REPO/runtime/build_dev/_deps/raylib-src/src}
sh "$REPO/tools/linux/fetch_wgpu.sh"
W=$REPO/third_party/wgpu-native; O=$(mktemp -d)
g++ -std=c++20 -O1 -Wall -I"$RLGL_DIR" -I"$W/include" "$REPO/native/rlvk/rlvk.cpp" "$REPO/native/rlvk/test_rlvk.cpp" "$W/lib/libwgpu_native.a" -ldl -lpthread -lm -o "$O/test_rlvk"
"$O/test_rlvk"; rc=$?; rm -rf "$O"; exit $rc
