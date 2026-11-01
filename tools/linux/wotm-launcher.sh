#!/usr/bin/env bash
# ABOUTME: One file installer, updater and launcher for Linux. Detects Ubuntu/Debian or Steam Deck, installs what the build needs,
# ABOUTME: builds the game from your own disc, adds icons, and on each start offers new releases. No game data is ever downloaded.
#
# Usage: wotm-launcher.sh [--iso FILE] [--dir FOLDER] [--no-legacy] [--yes] [--update] [--no-update] [--jobs N] [--detect]
#   First run: installs. Later runs: offer an update if there is one, then play. A failed update keeps the installed game.
#   The game goes in the folder this script is in, unless --dir says otherwise.
set -euo pipefail
REPO_URL=${WOTM_REPO_URL:-https://github.com/shaunypnz/WoTM}
OS_RELEASE=${WOTM_OS_RELEASE:-/etc/os-release}
APT_PACKAGES="ca-certificates git python3 cmake ninja-build g++ pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
libswresample-dev libgl-dev libx11-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev libxext-dev"
LEGACY_PACKAGES="curl unzip libsdl2-dev mesa-vulkan-drivers"
MIN_FREE_GB=10

SELF=$(realpath "$0")
ISO=""; DIR=""; LEGACY=""; YES=0; FORCE_UPDATE=0; NO_UPDATE=0; JOBS=""; ACTION=run; INNER_TAG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --iso) ISO=$2; shift 2 ;;
    --dir) DIR=$2; shift 2 ;;
    --jobs) JOBS=$2; shift 2 ;;
    --no-legacy) LEGACY=0; shift ;;
    --yes) YES=1; shift ;;
    --update) FORCE_UPDATE=1; shift ;;
    --no-update) NO_UPDATE=1; shift ;;
    --detect) ACTION=detect; shift ;;
    --inner-build) ACTION=inner; INNER_TAG=$2; shift 2 ;;   # the build itself, on this system or inside the container
    -h|--help) sed -n '5,7p' "$SELF" | cut -c3-; exit 0 ;;
    *) echo "unknown option: $1" >&2; exit 2 ;;
  esac
done
DIR=${DIR:-$(dirname "$SELF")}; DIR=$(realpath -m "$DIR")
CONF=$DIR/launcher.conf; LOG=$DIR/install.log; SRC=$DIR/src
[ -f "$CONF" ] && . "$CONF"   # VERSION, SAVED_ISO, SAVED_LEGACY, SKIP_TAG: written only by this script
LEGACY=${LEGACY:-${SAVED_LEGACY:-1}}; ISO=${ISO:-${SAVED_ISO:-}}

say() { printf '%s\n' "$*"; }
die() { message error "$*"; exit 1; }
has_gui() { [ -n "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ] && { command -v zenity >/dev/null || command -v kdialog >/dev/null; }; }
# Steam's Game Mode has no desktop to answer a question on, and a build there would look like a hang.
in_game_mode() { [ "${XDG_CURRENT_DESKTOP:-}" = gamescope ] || [ -n "${GAMESCOPE_WAYLAND_DISPLAY:-}" ]; }
message() {
  if [ -t 2 ] || ! has_gui; then say "$2" >&2
  elif command -v zenity >/dev/null; then zenity --"$1" --title="War of the Monsters" --text="$2" 2>/dev/null || true
  else kdialog --title "War of the Monsters" --"$([ "$1" = error ] && echo error || echo msgbox)" "$2" 2>/dev/null || true; fi
}
ask() {   # ask "question": yes by default, and --yes answers it
  [ "$YES" = 1 ] && return 0
  if [ -t 0 ]; then read -r -p "$1 [Y/n] " a; case "$a" in n*|N*) return 1 ;; esac; return 0; fi
  has_gui || return 1
  if command -v zenity >/dev/null; then zenity --question --title="War of the Monsters" --text="$1" 2>/dev/null
  else kdialog --title "War of the Monsters" --yesno "$1" 2>/dev/null; fi
}
typed_path() {   # what the prompt got as a shell would read it: tab completion leaves backslashes, and dragging a file in leaves quotes
  local p
  p=$(printf '%s' "$1" | sed -E "s/^[[:space:]]+//; s/[[:space:]]+\$//; s/^'(.*)'\$/\1/; s/^\"(.*)\"\$/\1/")
  [ -e "$p" ] || p=$(printf '%s' "$p" | sed -E 's/\\(.)/\1/g')
  printf '%s' "${p/#\~/$HOME}"
}
pick_iso() {
  if [ -f "$ISO" ]; then ISO=$(realpath "$ISO"); return 0; fi
  if [ -t 0 ]; then read -r -e -p "Path to your War of the Monsters US disc image (.iso): " ISO; ISO=$(typed_path "$ISO")
  elif command -v zenity >/dev/null && has_gui; then ISO=$(zenity --file-selection --title="Your War of the Monsters US disc image (.iso)" --file-filter="*.iso *.ISO" 2>/dev/null || true)
  elif command -v kdialog >/dev/null && has_gui; then ISO=$(kdialog --getopenfilename "$HOME" "*.iso *.ISO" 2>/dev/null || true); fi
  [ -f "$ISO" ] || die "No disc image at '$ISO'. A disc image is needed: pass --iso <file.iso>. No game data is included or downloaded."
  ISO=$(realpath "$ISO")
}
os_value() { sed -n "s/^$1=//p" "$OS_RELEASE" 2>/dev/null | head -1 | tr -d '"'; }
# deck (SteamOS on a Deck), steamos (SteamOS elsewhere), apt (Ubuntu, Debian and their relatives), other
detect_os() {
  local id like board
  id=$(os_value ID); like=$(os_value ID_LIKE)
  board=${WOTM_BOARD:-$(cat /sys/devices/virtual/dmi/id/board_name 2>/dev/null || true)}
  case "$board" in Jupiter|Galileo) echo deck; return ;; esac
  case " $id $like " in
    *" steamos "*) [ "$id" = steamos ] && echo steamos || echo other ;;
    *" ubuntu "*|*" debian "*) echo apt ;;
    *) echo other ;;
  esac
}
os_name() { case "$1" in deck) echo "Steam Deck" ;; *) os_value PRETTY_NAME ;; esac; }
latest_tag() {   # the release page GitHub calls latest; a redirect, so no API limit and no JSON
  local t
  [ -z "${WOTM_LATEST_TAG:-}" ] || { echo "$WOTM_LATEST_TAG"; return; }
  t=$(curl -fsSIL --max-time "${1:-5}" -o /dev/null -w '%{url_effective}' "$REPO_URL/releases/latest" 2>/dev/null || true)
  t=${t##*/}
  case "$t" in v*-linux) echo "$t" ;; esac
}
version_at_least() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -1)" = "$2" ]; }
default_jobs() {   # each compiler job can use about 3 GB
  local mem n
  mem=$(awk '/MemAvailable/ {print int($2/3145728)}' /proc/meminfo); n=$(nproc)
  [ "$mem" -lt "$n" ] && n=$mem; [ "$n" -gt 4 ] && n=4; [ "$n" -lt 1 ] && n=1
  echo "$n"
}
as_root() {
  if [ "$(id -u)" = 0 ]; then "$@"
  elif [ -t 0 ] && command -v sudo >/dev/null; then sudo "$@"
  elif command -v pkexec >/dev/null; then pkexec "$@"
  else die "Run this from a terminal: installing the build tools needs your password."; fi
}
legacy_packages() {   # the highest clang the system offers, at least 17, with its libc++
  local n
  n=$(apt-cache search --names-only '^clang-[0-9]+$' 2>/dev/null | sed -E 's/^clang-([0-9]+).*/\1/' | sort -n | tail -1)
  [ -n "$n" ] && [ "$n" -ge 17 ] || { echo "no clang 17 or newer in apt" >&2; return 1; }
  echo "clang-$n libc++-$n-dev libc++abi-$n-dev $LEGACY_PACKAGES"
}
install_packages() {
  local want="$APT_PACKAGES" lp missing="" p
  if [ "$LEGACY" = 1 ]; then
    command -v apt-cache >/dev/null && apt-cache show g++ >/dev/null 2>&1 || as_root apt-get ${APT_OPTS:-} update -qq
    if lp=$(legacy_packages); then want="$want $lp"; else say "The LEGACY tab will stay unbuilt."; LEGACY=0; fi
  fi
  for p in $want; do dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing="$missing $p"; done
  [ -n "$missing" ] || return 0
  say "== installing build tools:$missing"
  as_root env DEBIAN_FRONTEND=noninteractive sh -c "apt-get ${APT_OPTS:-} update && apt-get ${APT_OPTS:-} install -y --no-install-recommends $missing"
}
check_apt_versions() {   # older releases can't build this: say so before asking for a password
  local gxx cm
  gxx=$(apt-cache policy g++ 2>/dev/null | awk '/Candidate/ {print $2}' | sed -E 's/^[0-9]+://; s/[^0-9.].*//')
  cm=$(apt-cache policy cmake 2>/dev/null | awk '/Candidate/ {print $2}' | sed -E 's/^[0-9]+://; s/[^0-9.].*//')
  [ -z "$gxx$cm" ] && return 0   # the package lists aren't there yet (a new container)
  version_at_least "${gxx:-0}" 13 && version_at_least "${cm:-0}" 3.24 && return 0
  die "$(os_value PRETTY_NAME) is too old: the build needs g++ 13 and CMake 3.24 (Ubuntu 24.04 or Debian 13 or newer)."
}
bundle_libs() {   # the container's libraries the host may lack. Graphics, sound and C/C++ runtime stay the host's: they must match its driver
  local exe=$1 out=$2 lib
  mkdir -p "$out"
  ldd "$exe" | awk '/=> \// {print $3}' | while read -r lib; do
    case "$(basename "$lib")" in
      libc.so*|libm.so*|libdl.so*|librt.so*|libpthread.so*|ld-linux*|libstdc++*|libgcc_s*) continue ;;
      libGL*|libOpenGL*|libEGL*|libX*|libxcb*|libdrm*|libgbm*|libwayland*|libasound*|libpulse*|libz.so*|libzstd*|libexpat*|libvulkan*) continue ;;
    esac
    cp -Lf "$lib" "$out/"
  done
}
fetch_source() {
  if [ ! -d "$SRC/.git" ]; then
    say "== source $1"
    git clone --filter=blob:none "$REPO_URL" "$SRC"
  fi
  [ -z "${WOTM_IN_CONTAINER:-}" ] || git config --global --add safe.directory '*'
  git -C "$SRC" fetch -q --force --tags origin
  git -c advice.detachedHead=false -C "$SRC" checkout -q -f "$1"
}
inner_build() {
  local tag=$1
  [ -n "${WOTM_IN_CONTAINER:-}" ] && install_packages
  fetch_source "$tag"
  bash "$SRC/tools/linux/build.sh" --iso "$ISO" --dir "$DIR" --jobs "$JOBS"
  [ -n "${WOTM_IN_CONTAINER:-}" ] && bundle_libs "$DIR/wotm" "$DIR/lib"
  if [ "$LEGACY" = 1 ]; then
    # The original engine only fills the LEGACY tab: the game stays good if it fails
    say "== LEGACY engine"
    if bash "$SRC/tools/linux/build_legacy.sh" --iso "$ISO" --dir "$DIR" --jobs "$JOBS"; then
      [ -n "${WOTM_IN_CONTAINER:-}" ] && bundle_libs "$DIR/legacy/pc/build/ps2run" "$DIR/legacy/lib"
      say "LEGACY: original engine built"
    else
      say "LEGACY: the original engine did not build; the game is fine, its LEGACY tab shows NOT BUILT"
    fi
  fi
  [ -z "${WOTM_HOST_UID:-}" ] || chown -R "$WOTM_HOST_UID" "$DIR"
}
build_in_container() {
  local engine=${WOTM_CONTAINER:-} tag=$1 e glibc
  if [ -z "$engine" ]; then for e in podman docker; do command -v "$e" >/dev/null && { engine=$e; break; }; done; fi
  [ -n "$engine" ] || die "This system needs podman or docker for the build (SteamOS 3.5 or later has podman)."
  # The build's libraries come from Ubuntu 24.04, so the system must be at least as new
  glibc=$(ldd --version 2>/dev/null | sed -nE '1s/.* ([0-9]+\.[0-9]+).*/\1/p')
  version_at_least "${glibc:-0}" 2.39 || die "This system's glibc ${glibc:-unknown} is too old: the game needs 2.39 or newer."
  mkdir -p "$DIR"
  if [ "$engine" = none ]; then   # already inside a prepared environment (tests)
    WOTM_IN_CONTAINER=1 bash "$SELF" --inner-build "$tag" --iso "$ISO" --dir "$DIR" --jobs "$JOBS" $([ "$LEGACY" = 1 ] || echo --no-legacy)
    return
  fi
  local owner=(); [ "$engine" = docker ] && owner=(-e "WOTM_HOST_UID=$(id -u):$(id -g)")
  "$engine" run --rm --network=host "${owner[@]}" -e HOME=/root -e WOTM_IN_CONTAINER=1 -e WOTM_REPO_URL="$REPO_URL" \
    -v "$DIR:$DIR" -v "$(dirname "$ISO"):$(dirname "$ISO"):ro" -v "$SELF:/wotm-launcher.sh:ro" docker.io/library/ubuntu:24.04 \
    bash /wotm-launcher.sh --inner-build "$tag" --iso "$ISO" --dir "$DIR" --jobs "$JOBS" $([ "$LEGACY" = 1 ] || echo --no-legacy)
}
add_icons() {
  local entry=$DIR/wotm.desktop apps=$HOME/.local/share/applications
  cat > "$entry" <<DESK
[Desktop Entry]
Type=Application
Name=War of the Monsters Recompiled
Comment=Checks for updates, then plays
Exec="$DIR/launcher.sh"
Path=$DIR
Icon=applications-games
Terminal=false
Categories=Game;
DESK
  chmod +x "$entry"; mkdir -p "$apps"; cp -f "$entry" "$apps/wotm-recomp.desktop"
  if [ -d "$HOME/Desktop" ]; then
    cp -f "$entry" "$HOME/Desktop/wotm-recomp.desktop"
    gio set "$HOME/Desktop/wotm-recomp.desktop" metadata::trusted true 2>/dev/null || true
  fi
  if [ "$OS" = deck ] || [ "$OS" = steamos ]; then
    if command -v steamos-add-to-steam >/dev/null && steamos-add-to-steam "$DIR/launcher.sh" >/dev/null 2>&1; then
      say "Added to Steam: look under Non-Steam in Game Mode."
    else
      say "To play in Game Mode: in Steam, Games > Add a Non-Steam Game, and pick $DIR/launcher.sh"
    fi
  fi
}
save_conf() {
  { printf 'VERSION=%q\nSAVED_ISO=%q\nSAVED_LEGACY=%q\nSKIP_TAG=%q\n' "$1" "$ISO" "$LEGACY" "${SKIP_TAG:-}"; } > "$CONF"
}
with_percent() {   # every build line, led by the share of the whole install: each stage owns a range, and ninja's [n/m] moves through it
  local line pct=0 from=0 to=0 now legacy=$([ "$LEGACY" = 1 ] && echo 60 || echo 96)
  while IFS= read -r line; do
    case "$line" in
      "The LEGACY tab will stay unbuilt."*) legacy=96 ;;
      "== installing build tools"*) from=0; to=3 ;;
      "== source "*) from=3; to=5 ;;
      "== PS2Recomp"*) from=5; to=7 ;;
      "== game executable"*) from=7; to=8 ;;
      "== translating"*) from=8; to=18 ;;
      "== build (the translated"*) from=18; to=$legacy ;;
      "== game folder"*) from=$legacy; to=$((legacy + 1)) ;;
      "== LEGACY"*) from=$((legacy + 1)); to=$((legacy + 3)) ;;
      "== machine code"*) from=$((legacy + 3)); to=$((legacy + 10)) ;;
      "== build ("*) from=$((legacy + 10)); to=99 ;;
    esac
    now=$from
    if [[ $line =~ ^\[([0-9]+)/([0-9]+)\] ]] && [ "${BASH_REMATCH[2]}" -gt 0 ]; then now=$((from + (to - from) * BASH_REMATCH[1] / BASH_REMATCH[2])); fi
    [ "$now" -le "$pct" ] || pct=$now
    printf '[%3d%%] %s\n' "$pct" "$line"
  done
}
show_progress() {   # started from an icon there is no terminal, and a silent build would look like a hang
  if [ ! -t 1 ] && has_gui && command -v zenity >/dev/null; then
    { while IFS= read -r line; do p=${line#[}; p=${p%%%*}; printf '%d\n# %s\n' "$p" "$line"; done; echo 100; } | \
      zenity --progress --auto-close --no-cancel --no-markup --title="War of the Monsters" --text="Building the game. This takes a while." 2>/dev/null || cat >/dev/null
  else
    cat
  fi
}
build_tag() {   # builds a release and moves this install to it; on failure the installed game stays as it was
  local tag=$1 rc=0
  mkdir -p "$DIR"
  : "${JOBS:=$(default_jobs)}"
  has_gui && ! command -v zenity >/dev/null && [ ! -t 1 ] && message msgbox "Building $tag now. This takes a while; the game opens when it's done." || true
  say "Building $tag with $JOBS compiler jobs. Full log: $LOG"
  {
    case "$OS" in
      apt) check_apt_versions && install_packages && "$BASH" "$SELF" --inner-build "$tag" --iso "$ISO" --dir "$DIR" --jobs "$JOBS" $([ "$LEGACY" = 1 ] || echo --no-legacy) ;;
      *) build_in_container "$tag" ;;
    esac
  } > >(tee "$LOG" | with_percent | show_progress) 2>&1 || rc=$?
  wait 2>/dev/null || true
  [ "$rc" = 0 ] || return "$rc"
  # The launcher of the release just built replaces this one, so the launcher updates with the game
  cp -f "$SRC/tools/linux/wotm-launcher.sh" "$DIR/launcher.sh.new" && chmod +x "$DIR/launcher.sh.new" && mv -f "$DIR/launcher.sh.new" "$DIR/launcher.sh"
  save_conf "$tag"
  add_icons
}
preflight() {
  local free
  [ "$(uname -m)" = x86_64 ] && grep -qw avx2 /proc/cpuinfo || die "This game needs an x86-64 CPU with AVX2."
  mkdir -p "$DIR"
  free=$(df -BG --output=avail "$DIR" | tail -1 | tr -dc 0-9)
  [ "$free" -ge "$MIN_FREE_GB" ] || die "The build needs about $MIN_FREE_GB GB free in $DIR; there is ${free} GB."
  command -v curl >/dev/null || [ "$OS" = apt ] || [ -n "${WOTM_LATEST_TAG:-}" ] || die "curl is needed."
  if [ "$OS" = apt ] && ! command -v curl >/dev/null && [ -z "${WOTM_LATEST_TAG:-}" ]; then as_root sh -c "apt-get ${APT_OPTS:-} update -qq && apt-get ${APT_OPTS:-} install -y -qq curl ca-certificates"; fi
}
play() {
  [ -x "$DIR/play.sh" ] || die "The game isn't installed in $DIR."
  if [ ! -f "$ISO" ]; then pick_iso; save_conf "${VERSION:-}"; fi   # the disc image was moved
  export PS2X_CD_IMAGE="$ISO"
  exec "$DIR/play.sh"
}

OS=$(detect_os)
case "$ACTION" in
  detect) echo "$OS: $(os_name "$OS")"; exit 0 ;;
  inner) : "${JOBS:=$(default_jobs)}"; inner_build "$INNER_TAG"; exit 0 ;;
esac

if [ ! -x "$DIR/wotm" ]; then
  say "War of the Monsters Recompiled installer: $(os_name "$OS")"
  say "Installing in $DIR"
  preflight
  TAG=$(latest_tag 15); [ -n "$TAG" ] || die "Couldn't reach $REPO_URL to find the latest release."
  pick_iso
  build_tag "$TAG" || die "The build failed. See $LOG"
  say "Installed $TAG. Start it from the War of the Monsters icon, or $DIR/launcher.sh"
  exit 0
fi

if [ "$NO_UPDATE" = 0 ] && ! in_game_mode; then
  TAG=$(latest_tag 4)
  # A release declined or failed isn't offered at every start; the next one is, and --update always tries
  if [ "$FORCE_UPDATE" = 1 ] || { [ -n "$TAG" ] && [ "$TAG" != "${VERSION:-}" ] && [ "$TAG" != "${SKIP_TAG:-}" ]; }; then
    [ -n "$TAG" ] || die "Couldn't reach $REPO_URL to find the latest release."
    if [ "$FORCE_UPDATE" = 1 ] || ask "Update ${VERSION:-your build} to $TAG now? The game has to be rebuilt, which can take a while."; then
      preflight
      pick_iso
      if build_tag "$TAG"; then
        [ "$FORCE_UPDATE" = 1 ] && exit 0
        # the rest of this run belongs to the launcher just installed
        exec "$DIR/launcher.sh" --dir "$DIR" --no-update
      fi
      SKIP_TAG=$TAG; save_conf "${VERSION:-}"
      message warning "The update to $TAG failed, so your installed game stays as it was. See $LOG"
    else
      SKIP_TAG=$TAG; save_conf "${VERSION:-}"
    fi
  fi
fi
[ "$FORCE_UPDATE" = 1 ] && exit 0
play
