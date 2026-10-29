#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"

usage() {
  cat <<'EOF'
usage: play-wotm.sh [War of the Monsters (USA).iso] [options] [-- runner arguments]

Plays War of the Monsters natively from a disc image you own. Run from the source tree it checks the
disc, translates the game to C++ and builds it; everything goes into ONE game folder (default
~/Games/WoTM-native, or --dir), together with copies of this launcher, so that folder is the whole
game: start it again from there with play-ps2.sh / play-pc.sh (Linux) or play-ps2.bat / play-pc.bat
(Wine or Windows). The disc image itself is only linked, never copied.

Two versions, built side by side in the same folder:
  --mode ps2   the original game: memory cards in cards/, 4:3, intro movies, unlock as you play
  --mode pc    PC edition: no memory card to manage (the game state saves itself every 5 minutes
               and when you close the window, and the next launch carries on from there),
               16:9 widescreen, 2x resolution (PS2_SCALE), intro skipped, every arena and monster
               unlocked. Later epic options (split screen, more monsters, bots, online) join here.

options:
  --dir DIR          the game folder (default: the launcher's own folder when run from one,
                     ~/Games/WoTM-native from the source tree)
  --mode ps2|pc      which version to build/play (default ps2)
  --build-only       prepare everything, do not start the game
  --rebuild          translate and build again even if nothing changed
  --fullscreen       start fullscreen (F11 toggles it either way)
  --new-game         pc: ignore the autosave once (it is kept until the next save)
  --jobs N           compiler jobs (default: all cores; PS2_JOBS works too)
  --install-desktop  add app-menu and desktop icons for both versions
  --dry-run          print what would run and stop

keys: arrows move, Z/Space = X, X = O, A = square, S = triangle, Q/W = L1/R1,
1/2 = L2/R2, Enter = Start, IJKL camera, F11 fullscreen; any SDL game pad works.
EOF
}

# play-*.bat under Wine hands its arguments over in WOTM_BAT_ARGS (start /unix keeps quotes literally)
if [ -n "${WOTM_BAT_ARGS+x}" ]; then eval "set -- $WOTM_BAT_ARGS"; unset WOTM_BAT_ARGS WOTM_LAUNCH; fi
iso=""; dir=""; mode=ps2; build_only=0; rebuild=0; fullscreen=0; new_game=0; desktop=0; dry=0
jobs="${PS2_JOBS:-}"; runner=()
while [ $# -gt 0 ]; do
  case "$1" in
    --dir) dir="$2"; shift ;;
    --mode) mode="$2"; shift ;;
    --build-only) build_only=1 ;;
    --rebuild) rebuild=1 ;;
    --fullscreen) fullscreen=1 ;;
    --new-game) new_game=1 ;;
    --jobs) jobs="$2"; shift ;;
    --install-desktop) desktop=1 ;;
    --dry-run) dry=1 ;;
    --) shift; runner=("$@"); break ;;
    -h|--help) usage; exit 0 ;;
    -*) echo "unknown option $1" >&2; exit 2 ;;
    *) iso="$1" ;;
  esac
  shift
done
case "$mode" in ps2|pc) ;; *) echo "--mode is ps2 or pc" >&2; exit 2 ;; esac

# Source tree: this script sits in <repo>/scripts next to the recompiler. Installed: it sits in the game folder.
src=""
[ -f "$here/../CMakeLists.txt" ] && [ -f "$here/disc.py" ] && src="$(cd "$here/.." && pwd)"
if [ -z "$dir" ]; then
  if [ -n "$src" ]; then dir="$HOME/Games/WoTM-native"; else dir="$here"; fi
fi
mkdir -p "$dir"
dir="$(cd "$dir" && pwd)"
if [ -n "$iso" ]; then iso="$(realpath "$iso")"; echo "$iso" > "$dir/disc.path"; fi
[ -n "$iso" ] || iso="$(cat "$dir/disc.path" 2>/dev/null || true)"
[ -n "$iso" ] || { usage; echo; echo "no disc image given and none remembered in $dir/disc.path" >&2; exit 2; }
[ -f "$iso" ] || { echo "no disc image at $iso" >&2; exit 1; }
out="$dir/$mode"
mkdir -p "$out" "$dir/disc" "$dir/vu1" "$dir/cards" "$dir/logs"

prepare() {
  local scripts="$src/scripts" tools="$src/build/tools"
  # The runtime uses clang builtins (__builtin_elementwise_*): GCC cannot build it, so pick clang even when
  # g++ would pass cxx.sh's <format> probe.
  if [ -z "${CXX:-}" ]; then
    for c in $(compgen -c clang++- | grep -E '^clang\+\+-[0-9]+$' | sort -t- -k2 -rn | uniq) clang++; do
      if command -v "$c" > /dev/null; then export CXX="$c"; break; fi
    done
  fi
  case "$("${CXX:-false}" --version 2>/dev/null)" in
    *clang*) ;;
    *) echo "needs clang 17 or newer (e.g. apt install clang-19 libc++-19-dev), or CXX pointing at one" >&2; exit 1 ;;
  esac
  if [ ! -x "$tools/eerecomp" ] || [ ! -x "$tools/vurecomp" ] || [ ! -f "$src/third_party/wgpu-native/lib/libwgpu_native.a" ]; then
    echo "== tools (once)"
    "$scripts/setup.sh" --native-only
  fi

  # The only supported disc: the US release, by its boot program and that program's CRC (the one PCSX2
  # names patch files after). Other regions/dumps would need their own patches and overrides.
  echo "== disc"
  python3 -I "$scripts/disc.py" extract "$iso" "$dir/disc" SYSTEM.CNF > /dev/null
  local boot program crc
  boot=$(sed -n 's/^BOOT2 *= *//p' "$dir/disc/SYSTEM.CNF" | tr -d '\r')
  program=${boot#*\\}; program=${program%;*}
  if [ "$program" != SCUS_971.97 ]; then
    echo "this disc boots '$program', not SCUS_971.97: only War of the Monsters (USA, SCUS-97197) is supported" >&2
    exit 1
  fi
  python3 -I "$scripts/disc.py" extract "$iso" "$dir/disc" "$program" > /dev/null
  crc=$(python3 -I "$scripts/pnach.py" crc "$dir/disc/$program")
  if [ "$crc" != 2EDE12D1 ]; then
    echo "$program has CRC $crc, expected 2EDE12D1: a different revision or a bad dump - refusing to build it" >&2
    exit 1
  fi
  echo "War of the Monsters (USA) $crc"

  # pc: our quick-play patch (intro skipped, everything unlocked) and the PCSX2 community 16:9 patch, applied
  # to the executable before translation; patches outside it go to patches.txt for the runner.
  if [ "$mode" = pc ]; then
    python3 -I "$scripts/pnach.py" apply "$dir/disc/$program" "$out/$program" "$out/patches.txt" \
      --section "Widescreen 16:9" "$scripts/wotm/SCUS-97197_2EDE12D1.quickplay.pnach" \
      "$scripts/wotm/SCUS-97197_2EDE12D1.widescreen.pnach"
  else
    cp "$dir/disc/$program" "$out/$program"
    : > "$out/patches.txt"
  fi
  local elf="$out/$program" generated="$out/generated" build="$out/build"
  mkdir -p "$generated"

  # A translation is stale when the executable or the recompiler changed.
  local stamp
  stamp=$(sha256sum "$elf" "$tools/eerecomp" | sha256sum | cut -c1-16)
  if [ "$rebuild" = 1 ] || [ "$(cat "$generated/.stamp" 2>/dev/null)" != "$stamp" ]; then
    echo "== machine code to C++ ($mode)"
    local modules=() overlays=() windows
    if grep -q SNR2 "$elf"; then python3 -I "$scripts/disc.py" modules "$iso" "$dir/modules"; modules=(--modules "$dir/modules"); fi
    if windows=$(python3 -I "$scripts/windows.py" list "$elf") && [ -n "$windows" ]; then mkdir -p "$out/overlays"; overlays=(--overlays "$out/overlays"); fi
    touch "$out/entries.txt"
    "$tools/eerecomp" "$elf" --entries "$out/entries.txt" "${modules[@]}" "${overlays[@]}" --out "$generated" | tail -1
    rm -f "$generated/.vu-stamp"
    echo "$stamp" > "$generated/.stamp"
  fi

  # Vector unit programs recorded by earlier launches (either version) go into the build.
  local vu_count
  vu_count=$(find "$dir/vu1" -name '*.bin' | wc -l)
  if [ "$vu_count" -gt 0 ] && { [ "$rebuild" = 1 ] || [ "$(cat "$generated/.vu-stamp" 2>/dev/null)" != "$vu_count" ]; }; then
    echo "== vector unit programs ($vu_count recorded)"
    "$tools/vurecomp" "$dir/vu1" "$generated" | tail -1
    echo "$vu_count" > "$generated/.vu-stamp"
  fi

  echo "== build ($mode)"
  if [ ! -f "$build/build.ninja" ]; then
    . "$scripts/cxx.sh"
    # PS2_CMAKE_ARGS: extra configure arguments (e.g. SDL2_DIR, PS2_NATIVE_X11) for machines without system SDL2
    # shellcheck disable=SC2086
    cmake -S "$src" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release "${cxx_flags[@]}" -DPS2_GENERATED="$generated" ${PS2_CMAKE_ARGS:-} > /dev/null
  fi
  ninja -C "$build" ${jobs:+-j "$jobs"} ps2run > "$build/ninja.log" 2>&1 || { tail -30 "$build/ninja.log"; exit 1; }
  echo "ready: $build/ps2run"

  # The game folder carries its own launchers, so it plays without the source tree.
  install -m 755 "$src/scripts/play-wotm.sh" "$dir/play-wotm.sh"
  local m
  for m in ps2 pc; do
    printf '#!/usr/bin/env bash\nexec "$(dirname "$0")/play-wotm.sh" --mode %s "$@"\n' "$m" > "$dir/play-$m.sh"
    chmod 755 "$dir/play-$m.sh"
    sed "s/@MODE@/$m/g" "$src/scripts/play-wotm.bat" > "$dir/play-$m.bat"
  done
  cat > "$dir/README.txt" <<'EOF'
War of the Monsters - native build of your own disc

  play-ps2.sh / play-ps2.bat   the original game, saves on the memory card in cards/memcard1.ps2
  play-pc.sh  / play-pc.bat    PC edition: saves itself (pc/autosave.state, every 5 minutes and on close) and
                               carries on there next time; 16:9, 2x resolution, intro skipped, all unlocked.
                               --new-game starts fresh once (the autosave is kept until the next save).
  local.env                    optional, this machine's settings: KEY=VALUE lines (GPU choice, PS2_SCALE=1..3)

The .bat files work under Wine (they hand over to the .sh beside them) and on Windows with WSL2.
Everything the game needs is in this folder except the disc image (its path is in disc.path).
logs/ has one log per launch; send the newest one when something goes wrong.
EOF
  # PS2_RUNTIME_LIBS: shared libraries the target machine lacks (e.g. a 64-bit SDL2), copied into lib/
  if [ -n "${PS2_RUNTIME_LIBS:-}" ]; then mkdir -p "$dir/lib"; cp -L "$PS2_RUNTIME_LIBS"/*.so* "$dir/lib/"; fi
}

install_desktop() {
  local m name entry
  mkdir -p "$HOME/.local/share/applications"
  for m in ps2 pc; do
    if [ "$m" = ps2 ]; then name="War of the Monsters (PS2 - memory cards)"; else name="War of the Monsters (PC edition)"; fi
    entry="wotm-native-$m.desktop"
    cat > "$dir/$entry" <<EOF
[Desktop Entry]
Type=Application
Name=$name
Comment=Native recompilation of your War of the Monsters disc ($m version)
Exec=$dir/play-$m.sh
Path=$dir
Icon=applications-games
Terminal=false
Categories=Game;
StartupNotify=true
StartupWMClass=ps2run
EOF
    install -m 644 "$dir/$entry" "$HOME/.local/share/applications/$entry"
    if [ -d "$HOME/Desktop" ]; then
      install -m 755 "$dir/$entry" "$HOME/Desktop/$entry"
      gio set "$HOME/Desktop/$entry" metadata::trusted true 2>/dev/null || true
    fi
  done
  echo "desktop icons: War of the Monsters (PS2 / PC edition)"
}

if [ -n "$src" ] && [ "$dry" = 0 ]; then prepare; fi
[ "$desktop" = 0 ] || install_desktop
[ "$build_only" = 0 ] || exit 0

bin="$out/build/ps2run"
[ -x "$bin" ] || { echo "the $mode version is not built in $dir yet: run scripts/play-wotm.sh --mode $mode from the source tree" >&2; exit 1; }
elf="$out/SCUS_971.97"
args=("$elf" --disc "$iso" --ntsc --window)
export PS2_GS="${PS2_GS:-gpu}" PS2_VU_RECORD="$dir/vu1"
if [ "$mode" = ps2 ]; then
  args+=(--memcard "$dir/cards/memcard1.ps2")
else
  # The game still talks to a card, but the player never sees one: a formatted card kept with the PC
  # edition (cards/blank.ps2 seeds it when present; otherwise the game offers to format it once).
  if [ ! -f "$out/system.ps2" ] && [ -f "$dir/cards/blank.ps2" ]; then cp "$dir/cards/blank.ps2" "$out/system.ps2"; fi
  args+=(--memcard "$out/system.ps2" --widescreen --autosave "$out/autosave.state:18000")
  [ -s "$out/patches.txt" ] && args+=(--patches "$out/patches.txt")
  if [ -f "$out/autosave.state" ] && [ "$new_game" = 0 ]; then args+=(--load-state "$out/autosave.state"); fi
  export PS2_SCALE="${PS2_SCALE:-2}"
fi
[ "$fullscreen" = 0 ] || args+=(--fullscreen)
if [ "$mode" = ps2 ]; then edition="PS2"; else edition="PC edition"; fi
export PS2_WINDOW_TITLE="${PS2_WINDOW_TITLE:-War of the Monsters ($edition) - arrows move | Z/Space X  X O  A []  S /\\  Q L1  W R1  1 L2  2 R2 | Enter Start | IJKL camera | F11 fullscreen}"
[ -d "$dir/lib" ] && export LD_LIBRARY_PATH="$dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# local.env: this machine's own settings, one KEY=VALUE per line (e.g. VK_ICD_FILENAMES to pick the GPU, PS2_SCALE)
if [ -f "$dir/local.env" ]; then
  while IFS='=' read -r key value; do
    case "$key" in ''|\#*) ;; *) export "$key=$value" ;; esac
  done < "$dir/local.env"
fi
if [ "$dry" = 1 ]; then printf '%q ' "$bin" "${args[@]}" "${runner[@]}"; echo; exit 0; fi

log="$dir/logs/$mode-$(date +%Y%m%d-%H%M%S).log"
vu_before=$(find "$dir/vu1" -name '*.bin' | wc -l)
set +e
"$bin" "${args[@]}" "${runner[@]}" > "$log" 2>&1
result=$?
set -e
new=$(( $(find "$dir/vu1" -name '*.bin' | wc -l) - vu_before ))
[ "$new" -le 0 ] || echo "recorded $new new vector unit programs: the next build from the source tree compiles them in"
echo "log: $log"
exit "$result"
