#!/bin/sh
set -e
version=${1:-v29.0.1.1}
here=$(cd "$(dirname "$0")/.." && pwd)
target="$here/third_party/wgpu-native"
mkdir -p "$target"
asset="wgpu-$(uname -s | sed "s/Darwin/macos/;s/Linux/linux/")-$(uname -m | sed "s/arm64/aarch64/")-release.zip"
curl -fsSL -o "$target/wgpu.zip" "https://github.com/gfx-rs/wgpu-native/releases/download/$version/$asset"
# The download is compiled into the game, so the supported one must match its pinned checksum.
if [ "$version-$asset" = "v29.0.1.1-wgpu-linux-x86_64-release.zip" ]; then
  echo "95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a  $target/wgpu.zip" | sha256sum -c --quiet - || { rm -f "$target/wgpu.zip"; echo "wgpu-native download failed its checksum" >&2; exit 1; }
else
  echo "warning: no pinned checksum for $version $asset" >&2
fi
unzip -o -q "$target/wgpu.zip" -d "$target"
rm "$target/wgpu.zip"
echo "wgpu-native $version in $target"
