#!/bin/bash
# ABOUTME: Steam Deck install: builds the game from your own ISO inside a throwaway Ubuntu container (SteamOS has no compiler and a
# ABOUTME: read-only system), bundles the libraries it needs, and adds it to Steam. Run it in Desktop Mode, from Konsole.
#
# Usage: tools/linux/steamdeck.sh --iso <file.iso> [--dir ~/Games/WotM] [--jobs 3]
# Needs podman (included in SteamOS 3.5 and later) or docker, network, and about 5 GB free. The build takes a long time on the Deck:
# keep it plugged in. Nothing is installed on the system, and your disc image and the built game stay on this machine.
set -euo pipefail
REPO=$(cd "$(dirname "$0")/../.." && pwd)
ISO=""; DIR="$HOME/Games/WotM"; JOBS=3
while [ $# -gt 0 ]; do
  case "$1" in
    --iso) ISO=$2; shift 2 ;;
    --dir) DIR=$2; shift 2 ;;
    --jobs) JOBS=$2; shift 2 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
[ -n "$ISO" ] && [ -f "$ISO" ] || { echo "give your disc image: --iso <file.iso>" >&2; exit 2; }
ISO=$(realpath "$ISO"); mkdir -p "$DIR"; DIR=$(realpath "$DIR")
ENGINE=${WOTM_CONTAINER:-}
if [ -z "$ENGINE" ]; then
  for e in podman docker; do command -v "$e" >/dev/null && { ENGINE=$e; break; }; done
fi
[ -n "$ENGINE" ] || { echo "needs podman or docker (SteamOS 3.5 or later has podman)" >&2; exit 1; }

if [ "$ENGINE" = none ]; then   # already inside a prepared environment (tests)
  bash "$REPO/tools/linux/steamdeck_inner.sh" "$REPO" "$ISO" "$DIR" "$JOBS"
else
  "$ENGINE" run --rm --network=host -e HOME="$HOME" -v "$REPO:$REPO" -v "$DIR:$DIR" -v "$(dirname "$ISO"):$(dirname "$ISO"):ro" \
    docker.io/library/ubuntu:24.04 bash "$REPO/tools/linux/steamdeck_inner.sh" "$REPO" "$ISO" "$DIR" "$JOBS"
fi

cat > "$DIR/play.sh" <<PLAY
#!/bin/sh
# Starts the game with the libraries built for it; settings, saves and wotm.log stay in this folder.
cd "\$(dirname "\$0")"
export LD_LIBRARY_PATH="\$PWD/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}"
export PS2X_CD_IMAGE="\${PS2X_CD_IMAGE:-$ISO}"
[ -f wotm.log ] && mv -f wotm.log wotm.prev.log
exec ./wotm "\$@" > wotm.log 2>&1
PLAY
chmod +x "$DIR/play.sh"
cat > "$DIR/wotm.desktop" <<DESK
[Desktop Entry]
Type=Application
Name=War of the Monsters Recompiled
Exec=$DIR/play.sh
Path=$DIR
Icon=applications-games
Terminal=false
Categories=Game;
DESK
chmod +x "$DIR/wotm.desktop"
if command -v steamos-add-to-steam >/dev/null; then
  steamos-add-to-steam "$DIR/play.sh" && echo "Added to Steam: look under Non-Steam in Game Mode." || echo "Add $DIR/play.sh to Steam yourself: Games > Add a Non-Steam Game."
else
  echo "To play in Game Mode: in Steam, Games > Add a Non-Steam Game, and pick $DIR/play.sh"
fi
echo "Done: $DIR/play.sh"
