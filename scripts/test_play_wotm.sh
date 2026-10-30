#!/usr/bin/env bash
# Offline tests for play-wotm.sh: disc checks, the game folder, rebuild stamps and the settings file, against small
# fake discs this script writes itself (no game data) and a stub toolchain (no compiler run, nothing translated).
#   bash scripts/test_play_wotm.sh
set -uo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fails=0
pass() { echo "ok   $1"; }
fail() { echo "FAIL $1"; fails=$((fails + 1)); }

# A source tree with the real launcher and disc tools, and stub tools that log their calls.
src="$work/src"
mkdir -p "$src/scripts" "$src/build/tools" "$src/third_party/wgpu-native/lib" "$work/bin"
touch "$src/CMakeLists.txt" "$src/third_party/wgpu-native/lib/libwgpu_native.a"
cp "$repo"/scripts/{play-wotm.sh,play-wotm.bat,disc.py,pnach.py,windows.py,cxx.sh} "$src/scripts/"
cp -r "$repo/scripts/wotm" "$src/scripts/"
calls="$work/calls.log"
cat > "$src/build/tools/eerecomp" <<EOF
#!/usr/bin/env bash
echo eerecomp >> "$calls"; echo "translated (stub)"
EOF
cat > "$src/build/tools/vurecomp" <<EOF
#!/usr/bin/env bash
echo vurecomp >> "$calls"; echo "vu (stub)"
EOF
cat > "$work/bin/clang++-99" <<'EOF'
#!/usr/bin/env bash
[ "${1:-}" = --version ] && { echo "clang version 99 (stub)"; exit 0; }
while [ $# -gt 0 ]; do [ "$1" = -o ] && { touch "$2"; chmod +x "$2"; }; shift; done
EOF
cat > "$work/bin/cmake" <<'EOF'
#!/usr/bin/env bash
while [ $# -gt 0 ]; do [ "$1" = -B ] && { mkdir -p "$2"; touch "$2/build.ninja"; }; shift; done
EOF
cat > "$work/bin/ninja" <<EOF
#!/usr/bin/env bash
echo ninja >> "$calls"
[ "\$1" = -C ] && printf '#!/bin/sh\necho "ps2run (stub) \$*"\n' > "\$2/ps2run" && chmod +x "\$2/ps2run"
EOF
chmod +x "$src/build/tools/"* "$work/bin/"*
export PATH="$work/bin:$PATH" CXX=clang++-99 HOME="$work/home"
mkdir -p "$HOME"

# fake_iso OUT BOOT_PROGRAM CRC_HEX: an ISO 9660 image with SYSTEM.CNF booting BOOT_PROGRAM and that program as a
# tiny ELF header padded so its PCSX2 CRC (XOR of its 32-bit words) is CRC_HEX.
fake_iso() {
  python3 -I - "$@" <<'EOF'
import struct, sys
out, program, want = sys.argv[1], sys.argv[2], int(sys.argv[3], 16)
elf = bytearray(b'\x7fELF\x01\x01\x01' + bytes(9))
elf += struct.pack('<HHIIIIIHHHHHH', 2, 8, 1, 0x100000, 52, 0, 0, 52, 32, 0, 40, 0, 0)
elf += bytes(12)
crc = 0
for (w,) in struct.iter_unpack('<I', bytes(elf)):
    crc ^= w
elf += struct.pack('<I', crc ^ want)
cnf = ('BOOT2 = cdrom0:\\%s;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n' % program).encode()
files = [('SYSTEM.CNF', cnf), (program, bytes(elf))]
def both16(v): return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)
def record(name, lba, size, flags):
    r = bytes([0, 0]) + both32(lba) + both32(size) + bytes(7) + bytes([flags, 0, 0]) + both16(1) + bytes([len(name)]) + name
    if len(r) % 2: r += b'\0'
    return bytes([len(r)]) + r[1:]
sectors = 19 + len(files)
root = record(b'\0', 18, 2048, 2) + record(b'\1', 18, 2048, 2)
for i, (name, data) in enumerate(files):
    root += record((name + ';1').encode(), 19 + i, len(data), 0)
pvd = bytearray(2048)
pvd[0:6] = b'\x01CD001'; pvd[6] = 1
pvd[40:72] = b'FAKE'.ljust(32)
pvd[80:88] = both32(sectors)
pvd[156:190] = record(b'\0', 18, 2048, 2)
term = bytearray(2048); term[0:6] = b'\xffCD001'; term[6] = 1
with open(out, 'wb') as f:
    f.write(bytes(16 * 2048) + bytes(pvd) + bytes(term) + root.ljust(2048, b'\0'))
    for _, data in files:
        f.write(data.ljust(2048, b'\0'))
EOF
}

run() { bash "$src/scripts/play-wotm.sh" "$@" > "$work/out.log" 2>&1; }
before=$(cd "$src" && find . -type f | sort)

fake_iso "$work/good.iso" SCUS_971.97 2EDE12D1
fake_iso "$work/other.iso" SLUS_200.00 2EDE12D1
fake_iso "$work/revision.iso" SCUS_971.97 12345678
head -c 40000 /dev/zero > "$work/not-a-disc.iso"
game="$work/Games/WoTM"

run "$work/missing.iso" --dir "$game" --build-only
[ $? -ne 0 ] && grep -q "no disc image at" "$work/out.log" && pass "a missing disc image is refused" || fail "missing disc image"
run "$work/not-a-disc.iso" --dir "$game" --build-only
[ $? -ne 0 ] && grep -q "not an ISO 9660 image" "$work/out.log" && pass "a file that is not a disc image is refused" || fail "not a disc image"
run "$work/other.iso" --dir "$game" --build-only
[ $? -ne 0 ] && grep -q "boots 'SLUS_200.00', not SCUS_971.97" "$work/out.log" && pass "another game's disc is refused" || fail "other disc"
run "$work/revision.iso" --dir "$game" --build-only
[ $? -ne 0 ] && grep -q "has CRC 12345678, expected 2EDE12D1" "$work/out.log" && pass "another revision is refused by CRC" || fail "revision CRC"
[ ! -e "$game/ps2/build/ps2run" ] && pass "nothing is built from a refused disc" || fail "a refused disc was built"

: > "$calls"
run "$work/good.iso" --dir "$game" --build-only
rc=$?
[ $rc -eq 0 ] && grep -q "War of the Monsters (USA) 2EDE12D1" "$work/out.log" && pass "the US disc is accepted and built" || { fail "good disc (rc $rc)"; cat "$work/out.log"; }
[ "$(cat "$game/disc.path" 2>/dev/null)" = "$work/good.iso" ] && pass "the disc path is remembered in the game folder" || fail "disc.path"
for f in play-wotm.sh play-ps2.sh play-pc.sh play-ps2.bat play-pc.bat README.txt local.env ps2/build/ps2run; do
  [ -e "$game/$f" ] || { fail "game folder lacks $f"; continue; }
done
[ -x "$game/play-ps2.sh" ] && grep -q "PS2_SCALE" "$game/local.env" && pass "the game folder carries its launchers and a settings file" || fail "game folder files"
grep -q "^#PS2_SCALE" "$game/local.env" && ! grep -qE "^[A-Z]" "$game/local.env" && pass "the settings file changes nothing until a line is uncommented" || fail "local.env defaults"
[ "$(grep -c eerecomp "$calls")" = 1 ] && pass "the first build translates once" || fail "first translation count"

: > "$calls"
run --dir "$game" --build-only --mode ps2
grep -q eerecomp "$calls" && fail "an unchanged disc was translated again" || pass "a second build with nothing changed skips translation"
: > "$calls"
run --dir "$game" --build-only --mode ps2 --rebuild
grep -q eerecomp "$calls" && pass "--rebuild translates again" || fail "--rebuild"

echo "PS2_SCALE=3" >> "$game/local.env"
run "$work/good.iso" --dir "$game" --build-only
grep -q "^PS2_SCALE=3" "$game/local.env" && pass "a rebuild keeps the player's settings" || fail "local.env overwritten"

# --warm-up: one unattended run of the PC edition without a window, recording into vu1/, then both editions rebuilt.
: > "$calls"
run --dir "$game" --mode pc --warm-up
rc=$?
log=$(ls "$game"/logs/warm-up-*.log 2>/dev/null | head -1)
[ $rc -eq 0 ] && [ -n "$log" ] && grep -q -- "--frames 7800" "$log" && ! grep -q -- "--window" "$log" && grep -q -- "--press 540:start:8" "$log" \
  && pass "--warm-up runs the fight route once without a window" || { fail "--warm-up run (rc $rc)"; cat "$work/out.log"; }
[ "$(grep -c ninja "$calls")" -ge 3 ] && ! ls "$game/logs" | grep -q "warm-up-card" && pass "--warm-up rebuilds both editions and leaves no scratch card" || fail "--warm-up rebuild"

# The .bat hand-over: WOTM_BAT_ARGS is split into words and never evaluated.
WOTM_BAT_ARGS="--dir '$game' --mode ps2 --dry-run -- --arg '\$(touch $work/pwned)'" bash "$game/play-wotm.sh" > "$work/out.log" 2>&1
[ ! -e "$work/pwned" ] && grep -q -- "--mode\|ps2run\|--disc" "$work/out.log" && pass "WOTM_BAT_ARGS is not evaluated as shell code" || { fail "WOTM_BAT_ARGS"; cat "$work/out.log"; }

# Launching (dry run) from the game folder alone: the remembered disc, the edition's card or autosave, settings.
bash "$game/play-ps2.sh" --dry-run > "$work/out.log" 2>&1
grep -q -- "--disc $work/good.iso" "$work/out.log" && grep -q -- "--memcard $game/cards/memcard1.ps2" "$work/out.log" \
  && pass "play-ps2.sh starts the remembered disc with the memory card" || { fail "play-ps2 dry run"; cat "$work/out.log"; }
mkdir -p "$game/vu1"
after=$(cd "$src" && find . -type f | sort)
[ "$before" = "$after" ] && pass "nothing is written into the source tree" || { fail "the source tree changed:"; diff <(echo "$before") <(echo "$after") | head; }
[ ! -e "$HOME/Games/WoTM-native" ] && pass "--dir is honoured (nothing in the default game folder)" || fail "--dir ignored"

echo
if [ "$fails" = 0 ]; then echo "PASS: play-wotm.sh"; else echo "FAIL: $fails play-wotm.sh check(s)"; exit 1; fi
