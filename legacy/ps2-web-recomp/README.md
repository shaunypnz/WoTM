# War of the Monsters - native (bring your own disc)

Play War of the Monsters (PlayStation 2, USA) as a native Linux program, built on your machine from a disc image
you own. One script checks your disc, translates the game's machine code to C++, compiles it and starts it in a
window. Two editions are built side by side in the same game folder:

| | `play-ps2.sh` - PS2 edition | `play-pc.sh` - PC edition |
| --- | --- | --- |
| Saves | memory card in `cards/memcard1.ps2` | saves itself every 5 minutes and when you close the window; the next launch carries on |
| Picture | 4:3, PS2 resolution | 16:9 widescreen, 2x resolution, edge smoothing (FXAA) and sharpening |
| Start | intro movies, unlock as you play | intro skipped, every arena and monster unlocked |

**No game data is included or downloaded.** This repository holds the installer, the recompiler, the runtime and
small patch files. The game itself only ever comes from your own disc image, which is read in place and never copied.

## Requirements

- Linux on x86-64 with a Vulkan driver (Mesa RADV/ANV or the vendor driver). The game draws through Vulkan only;
  with no Vulkan GPU it stops with a message naming the driver to install.
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
scripts/play-wotm.sh --mode pc --build-only --warm-up
~/Games/WoTM-native/play-ps2.sh        # or play-pc.sh
```

Everything goes into one game folder (`~/Games/WoTM-native`, or `--dir DIR`). After the build that folder is the
whole game: its `play-ps2.sh` / `play-pc.sh` start it without the source tree, and `--install-desktop` adds
app-menu and desktop icons. The `.bat` files beside them start the same game from Wine. `README.txt` in the game
folder explains the files there.

A new memory card goes in formatted, so the PS2 edition saves settings and progress from its first launch.

## Picture settings

`local.env` in the game folder holds this machine's settings; the first build writes it with every option
explained and commented out. Uncomment a line to use it:

| Setting | Values | PS2 edition | PC edition |
| --- | --- | --- | --- |
| `PS2_SCALE` | 1-4: internal resolution, times the PS2's 640x448 | 1 | 2 |
| `PS2_AA` | `fxaa` or `off`: smooth stair-stepped edges | off | fxaa |
| `PS2_SHARPEN` | 0-1: sharpen after scaling (0.3-0.5 suits most screens) | 0 | 0.3 |
| `VK_ICD_FILENAMES` | a Vulkan driver file, to choose the GPU when there are several | | |

A frame rendered above the window's size is averaged down (supersampling), so `PS2_SCALE=3` or `4` in a
1080p window also removes shimmer on thin edges, at a GPU cost that grows with the square of the scale. While
playing, F9 cycles the window filter: none, FXAA, FXAA with sharpening, sharpening.

`--warm-up` plays a short fight unattended, without a window, and compiles the vector unit programs it meets into
both editions. Without it a new install interprets them and runs slowly until it has been played once and
rebuilt. The game keeps recording programs it has not seen while you play; running the script again from the
source tree compiles those in too (`recorded N new vector unit programs` tells you when that is worth doing).

Keys: arrows move, Z/Space = X, X = O, A = square, S = triangle, Q/W = L1/R1, 1/2 = L2/R2, Enter = Start,
IJKL camera, F9 picture filter, F11 fullscreen. Any SDL game pad works. Buttons can be remapped in `local.env`
(`PS2_PAD_MAP=a:square,x:cross`, `PS2_KEY_MAP=j:square`; the file lists the names). `PS2_PROMPTS=x360` draws the
menus' button icons as Xbox 360 A/B/X/Y (Y keeps the game's pink: its palette has no yellow).

## Status

This is a preview. Verified on Linux with an AMD Radeon RX 570 (Mesa RADV): both editions build from this source
tarball into an empty game folder and run in a window; the PS2 edition plays the intro movies and the PC edition
reaches the 16:9 title screen; a single-player fight holds about 60 fps in our test route; the PC edition's
autosave writes the game state and the next launch resumes from it. The PS2 edition saves settings to a new memory card
and reads them back after a restart; `--warm-up` records and compiles the vector unit programs of a fight without a
window; the Vulkan-only renderer, the picture filters, 4x resolution and the Xbox 360 prompts run on the same card.
Not yet verified: a 30-minute session, two-player fights end to end, and physical game pads. The fight route holds about
60 fps on that card with the game window on a virtual display. Fight audio can clip at full volume.

Logs are in the game folder's `logs/`; include the newest one when you report a problem.

## How it works

The recompiler and runtime are a fork of [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) by Samir
Nuri: the Emotion Engine code is translated ahead of time to C++, vector unit 1 programs are compiled to SIMD C++,
the I/O processor runs the disc's own modules, and the Graphics Synthesizer is drawn with WebGPU compute shaders
(wgpu-native on Vulkan). See that project for the design and for porting other games.

## Licence

MIT, see [LICENSE](LICENSE). Third-party pieces and credits are listed in [NOTICE.md](NOTICE.md). War of the
Monsters is a trademark of its owners; this project is not affiliated with or endorsed by them.
