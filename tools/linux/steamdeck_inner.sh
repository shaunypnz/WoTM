#!/bin/bash
# ABOUTME: Runs inside the Ubuntu container of steamdeck.sh: installs the build tools, builds, and copies the libraries the game needs
# ABOUTME: into the game folder. Graphics, sound and C/C++ runtime libraries stay the host's: they must match its driver.
set -euo pipefail
REPO=$1; ISO=$2; DIR=$3; JOBS=$4
export DEBIAN_FRONTEND=noninteractive
if ! command -v cmake >/dev/null; then
  apt-get ${APT_OPTS:-} update -qq
  apt-get ${APT_OPTS:-} install -y -qq --no-install-recommends ca-certificates git python3 cmake ninja-build g++ pkg-config \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev libgl-dev libx11-dev libxrandr-dev libxi-dev \
    libxinerama-dev libxcursor-dev libxext-dev >/dev/null
fi
git config --global --add safe.directory '*'
bash "$REPO/tools/linux/build.sh" --iso "$ISO" --dir "$DIR" --jobs "$JOBS"
rm -f "$DIR/play.sh"
mkdir -p "$DIR/lib"
ldd "$DIR/wotm" | awk '/=> \// {print $3}' | while read -r lib; do
  case "$(basename "$lib")" in
    libc.so*|libm.so*|libdl.so*|librt.so*|libpthread.so*|ld-linux*|libstdc++*|libgcc_s*) continue ;;
    libGL*|libOpenGL*|libEGL*|libX*|libxcb*|libdrm*|libgbm*|libwayland*|libasound*|libpulse*|libz.so*|libzstd*|libexpat*) continue ;;
  esac
  cp -Lf "$lib" "$DIR/lib/"
done
echo "bundled $(ls "$DIR/lib" | wc -l) libraries"
