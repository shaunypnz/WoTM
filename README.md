# War of the Monsters - native (bring your own disc)

Play War of the Monsters (PlayStation 2, USA) as a native Linux program, built on your machine from a disc image
you own. One script checks your disc, translates the game's machine code to C++, compiles it and starts it in a
window. Two editions are built side by side in the same game folder:

| | `play-ps2.sh` - PS2 edition | `play-pc.sh` - PC edition |
| --- | --- | --- |
| Saves | memory card in `cards/memcard1.ps2` | saves itself every 5 minutes and when you close the window; the next launch carries on |
| Picture | 4:3, PS2 resolution | 16:9 widescreen, 2x resolution |
| Start | intro movies, unlock as you play | intro skipped, every arena and monster unlocked |

**No game data is included or downloaded.** This repository holds the installer, the recompiler, the runtime and
small patch files. The game itself only ever comes from your own disc image, which is read in place and never copied.

## Requirements

- Linux on x86-64 with a Vulkan driver (Mesa RADV/ANV or the vendor driver).
- clang 17 or newer with libc++ (for example `apt install clang-19 libc++-19-dev libc++abi-19-dev`), CMake, Ninja,
  Python 3, curl, unzip and the SDL2 development files (`libsdl2-dev`).
- A disc image of **War of the Monsters (USA), SCUS-97197**. Other regions and revisions are refused: the script
  checks the boot program and its CRC (`2EDE12D1`).
- About 1 GB of disk space for both editions, and time: the first build compiles roughly 90 MB of generated C++
  per edition. Use `--jobs N` to limit compiler jobs (each job can need several GB of memory).

## Install and play

```bash
tar xf wotm-native-<version>.tar.gz && cd wotm-native-<version>
scripts/play-wotm.sh "War of the Monsters (USA).iso" --mode ps2 --build-only
scripts/play-wotm.sh --mode pc --build-only
~/Games/WoTM-native/play-ps2.sh        # or play-pc.sh
```

Everything goes into one game folder (`~/Games/WoTM-native`, or `--dir DIR`). After the build that folder is the
whole game: its `play-ps2.sh` / `play-pc.sh` start it without the source tree, and `--install-desktop` adds
app-menu and desktop icons. The `.bat` files beside them start the same game from Wine. `README.txt` in the game
folder explains the files there, and `local.env` holds per-machine settings such as `VK_ICD_FILENAMES` to choose
the GPU or `PS2_SCALE=1..3`.

The game records the vector unit programs it meets while you play; running the script again from the source tree
compiles them in (`recorded N new vector unit programs` tells you when that is worth doing).

Keys: arrows move, Z/Space = X, X = O, A = square, S = triangle, Q/W = L1/R1, 1/2 = L2/R2, Enter = Start,
IJKL camera, F11 fullscreen. Any SDL game pad works.

## Status

This is a preview. Verified on Linux with an AMD Radeon RX 570 (Mesa RADV): both editions build from this source
tarball into an empty game folder and run in a window; the PS2 edition plays the intro movies and the PC edition
reaches the 16:9 title screen; a single-player fight holds about 60 fps in our test route; the PC edition's
autosave writes the game state and the next launch resumes from it. Not yet verified: a 30-minute session, two-player fights
end to end, and physical game pads. Heavy scenes can still drop below 60 fps. On a new install the game finds an
unformatted memory card (in the PC edition too) and offers to format it once.

Logs are in the game folder's `logs/`; include the newest one when you report a problem.

## How it works

The recompiler and runtime are a fork of [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) by Samir
Nuri: the Emotion Engine code is translated ahead of time to C++, vector unit 1 programs are compiled to SIMD C++,
the I/O processor runs the disc's own modules, and the Graphics Synthesizer is drawn with WebGPU compute shaders
(wgpu-native on Vulkan). See that project for the design and for porting other games.

## Licence

MIT, see [LICENSE](LICENSE). Third-party pieces and credits are listed in [NOTICE.md](NOTICE.md). War of the
Monsters is a trademark of its owners; this project is not affiliated with or endorsed by them.
