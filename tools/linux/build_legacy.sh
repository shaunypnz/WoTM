#!/bin/bash
# ABOUTME: Builds our original engine (legacy/ps2-web-recomp) into <game folder>/legacy, so the LEGACY tab of the options panel can start it.
# Usage: tools/linux/build_legacy.sh --iso <file.iso> [--dir ~/Games/WotM] [--jobs N]. Needs what legacy/ps2-web-recomp/README.md lists: clang 17+ and SDL2, among others.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ISO=""; DIR="$HOME/Games/WotM"; JOBS=2
while [ $# -gt 0 ]; do
  case "$1" in
    --iso) ISO=$2; shift 2 ;;
    --dir) DIR=$2; shift 2 ;;
    --jobs) JOBS=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ -n "$ISO" ] && [ -f "$ISO" ] || { echo "give your disc image: --iso <file.iso>" >&2; exit 2; }
L=$REPO/legacy/ps2-web-recomp
[ -f "$L/third_party/wgpu-native/lib/libwgpu_native.a" ] || sh "$L/scripts/fetch_wgpu.sh"
bash "$L/scripts/play-wotm.sh" "$ISO" --dir "$DIR/legacy" --mode pc --build-only --jobs "$JOBS"
echo "Done: open RECOMPILED OPTIONS > LEGACY in the game."
