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
               16:9 widescreen, 2x resolution (PS2_SCALE) with edge smoothing (PS2_AA), intro skipped,
               every arena and monster unlocked. Later epic options (split screen, more monsters, bots,
               online) join here.

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
  --warm-up          from the source tree: play a short fight unattended without a window (about 10 minutes),
                     record the vector unit programs it meets, and compile them into both versions, so the
                     first real launch runs at full speed instead of interpreting them (a fresh install
                     otherwise runs slowly until it has been played once and rebuilt)
  --dry-run          print what would run and stop

keys: arrows move, Z/Space = X, X = O, A = square, S = triangle, Q/W = L1/R1,
1/2 = L2/R2, Enter = Start, IJKL camera, F9 picture filter, F11 fullscreen; any SDL game pad works.

Picture settings live in the game folder's local.env (written on the first build, every line explained):
PS2_SCALE=1..4 internal resolution, PS2_AA=fxaa|off edge smoothing, PS2_SHARPEN=0..1 sharpening.
EOF
}

# play-*.bat under Wine hands its arguments over in WOTM_BAT_ARGS (start /unix keeps quotes literally)
# (split as shell words, never evaluated: a file name holding $(...) stays a file name)
if [ -n "${WOTM_BAT_ARGS+x}" ]; then
  mapfile -d '' -t bat_args < <(python3 -I -c 'import os, shlex, sys; sys.stdout.write("".join(a + "\0" for a in shlex.split(os.environ["WOTM_BAT_ARGS"])))')
  set -- "${bat_args[@]}"
  unset WOTM_BAT_ARGS WOTM_LAUNCH bat_args
fi
iso=""; dir=""; mode=ps2; build_only=0; rebuild=0; fullscreen=0; new_game=0; desktop=0; dry=0; warm_up=0
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
    --warm-up) warm_up=1 ;;
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
  ninja -C "$build" ${jobs:+-j "$jobs"} ps2run 2>&1 | tee "$build/ninja.log"
  [ "${PIPESTATUS[0]}" = 0 ] || exit 1
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
  local.env                    this machine's settings, KEY=VALUE lines (resolution, picture filters, GPU)

The .bat files work under Wine (they hand over to the .sh beside them) and on Windows with WSL2.
Everything the game needs is in this folder except the disc image (its path is in disc.path).
logs/ has one log per launch; send the newest one when something goes wrong.
F9 in the game window cycles the picture filter (none, FXAA, FXAA + sharpening, sharpening); F11 is fullscreen.
EOF
  # The settings file is the player's: written once, never overwritten.
  if [ ! -f "$dir/local.env" ]; then
    cat > "$dir/local.env" <<'EOF'
# War of the Monsters - this machine's settings. One KEY=VALUE per line; remove the leading # to use a line.
# play-ps2.sh / play-pc.sh read this file at every launch, after their own defaults, so a line here wins.
#
# Internal resolution, 1-4 times the PS2's (PS2 edition default 1, PC edition default 2). Above the window's size
# the extra pixels become anti-aliasing. GPU time grows with its square: 3-4 want a mid-range GPU or better.
#PS2_SCALE=3
#
# Edge smoothing in the window (FXAA): fxaa or off (PC edition default fxaa, PS2 edition off).
#PS2_AA=off
#
# Sharpening after scaling, 0 (off) to 1 (strong) (PC edition default 0.3, PS2 edition 0). F9 cycles filters.
#PS2_SHARPEN=0.4
#
# Button remapping: <controller button or key>:<PS2 button>, comma-separated (PS2 buttons: cross circle square
# triangle l1 r1 l2 r2 l3 r3 select start up down left right, or none). Controller buttons use SDL's names
# (a b x y leftshoulder rightshoulder back start leftstick rightstick dpup dpdown dpleft dpright), keys too
# (z, space, return, left shift, ...). Example: swap jump and the light attack on the pad, J for square on keys.
#PS2_PAD_MAP=a:square,x:cross
#PS2_KEY_MAP=j:square
#
# Xbox 360 button prompts in menus: the game's Cross/Circle/Square/Triangle icons drawn as A/B/X/Y.
#PS2_PROMPTS=x360
#
# The GPU to use when there are several (a Vulkan ICD file from /usr/share/vulkan/icd.d), e.g. AMD with Mesa:
#VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/radeon_icd.json
EOF
  fi
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

# --warm-up: the PC edition's quick-play route (title, Free-for-All, one CPU, a fight with a punch every 8 s) with
# no window, recording every vector unit program into vu1/; then both editions are rebuilt with them compiled in.
# The programs come from the player's own disc at run time; nothing recorded is shipped.
warm_up_run() {
  local pc="$dir/pc" card presses=() f
  [ -x "$pc/build/ps2run" ] || "$src/scripts/play-wotm.sh" --dir "$dir" --mode pc --build-only ${jobs:+--jobs "$jobs"}
  for f in 540:start 660:cross 780:down 900:cross 1020:down 1140:cross 1260:cross 1380:cross 1500:cross 1740:cross \
           4200:triangle 4680:triangle 5160:triangle 5640:triangle 6120:triangle 6600:triangle 7080:triangle 7560:triangle; do
    presses+=(--press "$f:8")
  done
  card=$(mktemp "$dir/logs/warm-up-card.XXXXXX")
  rm -f "$card"
  local log="$dir/logs/warm-up-$(date +%Y%m%d-%H%M%S).log" before
  before=$(find "$dir/vu1" -name '*.bin' | wc -l)
  echo "== warm-up: a short unattended fight without a window (about 10 minutes), recording vector unit programs"
  [ -f "$dir/local.env" ] && while IFS='=' read -r key value; do
    case "$key" in ''|\#*) ;; *) export "$key=$value" ;; esac
  done < "$dir/local.env"
  local patches=()
  [ -s "$pc/patches.txt" ] && patches=(--patches "$pc/patches.txt")
  [ -d "$dir/lib" ] && export LD_LIBRARY_PATH="$dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
  PS2_GS="${PS2_GS:-gpu}" PS2_VU_RECORD="$dir/vu1" "$pc/build/ps2run" "$pc/SCUS_971.97" --disc "$iso" --ntsc \
    --memcard "$card" --widescreen "${patches[@]}" --frames 7800 "${presses[@]}" > "$log" 2>&1 || {
    rm -f "$card"; tail -5 "$log"; echo "warm-up run failed, see $log" >&2; exit 1; }
  rm -f "$card"
  echo "recorded $(( $(find "$dir/vu1" -name '*.bin' | wc -l) - before )) new vector unit programs ($log)"
  local m
  for m in ps2 pc; do
    [ -x "$dir/$m/build/ps2run" ] || [ "$m" = "$mode" ] || continue
    "$src/scripts/play-wotm.sh" --dir "$dir" --mode "$m" --build-only ${jobs:+--jobs "$jobs"}
  done
}

if [ -n "$src" ] && [ "$dry" = 0 ]; then prepare; fi
if [ "$warm_up" = 1 ]; then
  [ -n "$src" ] || { echo "--warm-up needs the source tree (it compiles what it records): run scripts/play-wotm.sh from there" >&2; exit 2; }
  [ "$dry" = 1 ] || warm_up_run
fi
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
  export PS2_SCALE="${PS2_SCALE:-2}" PS2_AA="${PS2_AA:-fxaa}" PS2_SHARPEN="${PS2_SHARPEN:-0.3}"
fi
[ "$fullscreen" = 0 ] || args+=(--fullscreen)
if [ "$mode" = ps2 ]; then edition="PS2"; else edition="PC edition"; fi
export PS2_WINDOW_TITLE="${PS2_WINDOW_TITLE:-War of the Monsters ($edition) - arrows move | Z/Space X  X O  A []  S /\\  Q L1  W R1  1 L2  2 R2 | Enter Start | IJKL camera | F9 filter | F11 fullscreen}"
[ -d "$dir/lib" ] && export LD_LIBRARY_PATH="$dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
# local.env: this machine's own settings, one KEY=VALUE per line (PS2_SCALE, PS2_AA, PS2_SHARPEN, VK_ICD_FILENAMES)
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
