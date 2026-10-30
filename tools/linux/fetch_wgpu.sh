#!/bin/sh
# ABOUTME: Downloads the wgpu-native release the Vulkan backend builds against into third_party/wgpu-native (not in git), checksum pinned.
set -e
here=$(cd "$(dirname "$0")/../.." && pwd); target="$here/third_party/wgpu-native"
[ -f "$target/lib/libwgpu_native.a" ] && exit 0
mkdir -p "$target"
curl -fsSL -o "$target/wgpu.zip" https://github.com/gfx-rs/wgpu-native/releases/download/v29.0.1.1/wgpu-linux-x86_64-release.zip
echo "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a  $target/wgpu.zip" | sha256sum -c --quiet -
unzip -o -q "$target/wgpu.zip" -d "$target"; rm "$target/wgpu.zip"
