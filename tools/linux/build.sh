#!/bin/bash
# ABOUTME: Linux build, from your own ISO to a game folder: same steps as tools/setup_runtime.ps1 + build_runtime.ps1, with gcc.
# ABOUTME: Runs again safely (skips what exists). Nothing from the game goes into the repo: disc/, recomp/ and runtime/ are ignored.
#
# Usage: tools/linux/build.sh --iso <file.iso> [--dir ~/Games/WotM] [--jobs N]
#   Needs git, python3, cmake 3.24+, ninja, gcc 13+, pkg-config, and the FFmpeg and X11/GL development packages, on Ubuntu:
#   sudo apt install git python3 cmake ninja-build g++ pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
#     libswresample-dev libgl-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev
#   Without root: unpack those packages into a folder and pass WOTM_DEV_PREFIX=<folder>.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ELF_SHA1=1ed7f8457436a9170d8f85386e267369c7fcdd4f   # SCUS_971.97 of the NTSC-U disc (SCUS-97197)
PS2RECOMP_COMMIT=2c5fbb9389e11dd95693385969490c9e8e6f57b4   # base of tools/runtime_patch/wotm-runtime.patch
ISO=""; DIR="$HOME/Games/WotM"; JOBS=$(( $(nproc) < 4 ? $(nproc) : 4 ))
while [ $# -gt 0 ]; do
  case "$1" in
    --iso) ISO=$2; shift 2 ;;
    --dir) DIR=$2; shift 2 ;;
    --jobs) JOBS=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ -n "$ISO" ] && [ -f "$ISO" ] || { echo "give your disc image: --iso <file.iso>" >&2; exit 2; }
ISO=$(realpath "$ISO")
for t in git python3 cmake ninja g++ pkg-config; do command -v "$t" >/dev/null || { echo "missing: $t" >&2; exit 1; }; done

RT=$REPO/runtime; PR=$RT/PS2Recomp; WORK=$REPO/recomp/work; RETAIL=$REPO/recomp/retail
CM_ARGS=()
if [ -n "${WOTM_DEV_PREFIX:-}" ]; then
  S=$(realpath "$WOTM_DEV_PREFIX"); M=$(gcc -dumpmachine)
  export PKG_CONFIG_SYSROOT_DIR=$S PKG_CONFIG_LIBDIR=$S/usr/lib/$M/pkgconfig:$S/usr/share/pkgconfig
  export CFLAGS="-isystem $S/usr/include -isystem $S/usr/include/$M" CXXFLAGS="-isystem $S/usr/include -isystem $S/usr/include/$M"
  export LDFLAGS="-L$S/usr/lib/$M -Wl,-rpath-link,/usr/lib/$M"
  CM_ARGS+=("-DCMAKE_PREFIX_PATH=$S/usr")
fi

echo "== PS2Recomp"
mkdir -p "$RT"
if [ ! -d "$PR/.git" ]; then
  git clone -q https://github.com/ran-j/PS2Recomp.git "$PR"
  git -c advice.detachedHead=false -C "$PR" checkout -q "$PS2RECOMP_COMMIT"
fi
for p in wotm-runtime.patch ps2recomp-fpu-sqrt.patch; do
  if git -C "$PR" apply --whitespace=nowarn --reverse --check "$REPO/tools/runtime_patch/$p" 2>/dev/null; then continue; fi
  git -C "$PR" apply --whitespace=nowarn "$REPO/tools/runtime_patch/$p" || { echo "$p does not apply to $PR" >&2; exit 1; }
done

echo "== game executable"
mkdir -p "$WORK" "$RETAIL"
python3 -I "$REPO/tools/linux/iso_extract.py" "$ISO" SCUS_971.97 "$WORK/game.elf"
[ "$(sha1sum "$WORK/game.elf" | cut -d' ' -f1)" = "$ELF_SHA1" ] || { echo "this port only supports the NTSC-U disc (SCUS-97197)" >&2; exit 1; }

if [ ! -f "$RETAIL/register_functions.cpp" ]; then
  echo "== translating the game"
  cmake -S "$PR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -B "$RT/build_tools"
  cmake --build "$RT/build_tools" -j "$JOBS"
  ( cd "$WORK"
    "$RT/build_tools/ps2xAnalyzer/ps2_analyzer" game.elf config.toml >/dev/null
    sed -i 's#^output = .*#output = "../retail/"#' config.toml
    "$RT/build_tools/ps2xRecomp/ps2_recomp" config.toml >/dev/null )
fi

echo "== build (the translated game takes a while)"
B=$RT/build_dev; mkdir -p "$B/wotm_gen"
# VU0 microprograms in C++, generated from the ELF: game code, so it stays in the build folder. Must exist before the first compile
[ -f "$B/wotm_gen/wotm_vu0_native.gen.inc" ] || python3 "$REPO/tools/vu0native.py" "$WORK/game.elf" "$B/wotm_gen/wotm_vu0_native.gen.inc" >/dev/null
VER="#define WOTM_VERSION \"$(git -C "$REPO" describe --tags --always --dirty 2>/dev/null || echo dev)\""
[ "$(cat "$B/wotm_gen/wotm_version.gen.h" 2>/dev/null)" = "$VER" ] || echo "$VER" > "$B/wotm_gen/wotm_version.gen.h"
[ -f "$B/build.ninja" ] || cmake -S "$PR" -G Ninja -DCMAKE_BUILD_TYPE=Release "${CM_ARGS[@]}" -DGLFW_BUILD_WAYLAND=OFF \
  -DWOTM_NATIVE_DIR="$REPO/native" -DPS2X_FAST_LINK=ON -DPS2X_ENABLE_AGRESSIVE_LOGS=OFF -DPS2X_ENABLE_IOP_RPC_TRACE=OFF \
  -DPS2X_BUILD_RECOMP=OFF -DPS2X_BUILD_ANALYZER=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF -DPS2X_ENABLE_DEBUG_UI=OFF \
  -DPS2X_RUNNER_DIR="$RETAIL" -B "$B"
cmake --build "$B" --target wotm -j "$JOBS"

echo "== game folder: $DIR"
mkdir -p "$DIR"
cp -f "$B/ps2xRuntime/wotm" "$DIR/wotm"
cp -f "$WORK/game.elf" "$DIR/SCUS_971.97"   # the game runs this copy; it is yours, never share the folder
cp -rf "$REPO/mods" "$DIR/"
cat > "$DIR/play.sh" <<PLAY
#!/bin/sh
# Starts the game; settings, saves and wotm.log stay in this folder. Pass KEY=VALUE options through the environment.
cd "\$(dirname "\$0")"
export PS2X_CD_IMAGE="\${PS2X_CD_IMAGE:-$ISO}"
[ -f wotm.log ] && mv -f wotm.log wotm.prev.log
exec ./wotm "\$@" > wotm.log 2>&1
PLAY
chmod +x "$DIR/play.sh"
echo "Done: $DIR/play.sh"
