<img width="2113" height="744" alt="War of The Monsters recompiled LOGO" src="https://github.com/user-attachments/assets/6160bebd-48f0-47b5-bd28-f74d23c662fd" />

# War of the Monsters Recompiled!

A native port of **War of the Monsters** (PS2, 2003), built from the statically recompiled game code and easy to mod.

> **Preview.** This fork is lightly tested: it has been played on one Linux machine with an AMD RX 570 and an Xbox 360 controller. Long sessions, adventure mode and saves are still unverified, and frame rate hasn't been measured. Use it at your own risk.

**This is a Linux-only fork.** The port, the decompilation and almost everything here is the work of [Yan Moura](https://github.com/yanm1103) and contributors in [War_of_the_Monsters_recomp](https://github.com/yanm1103/War_of_the_Monsters_recomp). Credit for the game work belongs there.

**What this fork adds:** a native Linux build and installer from your own disc (see [Building the port (Linux)](#building-the-port-linux)) and FXAA and sharpening filters. The fork maintains the Linux side: new upstream releases get merged in, with our changes on top. A Vulkan renderer is in progress ([docs/guides/VULKAN.md](docs/guides/VULKAN.md)); the game itself still draws with OpenGL.

**Where this fork came from:** we were separately building our own Linux port of the game. When we found this project, it was further along, so we moved our work here. That original engine and its history are in [`legacy/`](legacy/README.md).

## Play
1. Download `wotm-launcher.sh` from the [latest release](https://github.com/shaunypnz/WoTM/releases/latest) into the folder where you want the game and run it there with `bash wotm-launcher.sh`. It detects Ubuntu, Debian or a Steam Deck, installs what the build needs, and builds from your own disc
2. Run `./play.sh` in that folder

The game starts in a 1280x720 window (F11 for fullscreen). Ctrl+Q closes it. If RustDesk runs on the machine, the mouse isn't captured for the camera, so a remote session keeps its pointer; F10 toggles that.

Found a bug? Open an [issue](https://github.com/shaunypnz/WoTM/issues) and attach `wotm.log` (along with your save files).

## Mods
PNG Textures, skins and custom monsters go in the `mods/` folder next to `wotm`. The build ships with Goliath Prime and Cerebulon as playable custom monsters (these are just for fun, don't open issues if they can't jump or can't do any other specific actions!).

To make your own mods, check the reference: [docs/guides/MODS.md](docs/guides/MODS.md).

## How it works
1. **Static recompilation.** [PS2Recomp](https://github.com/ran-j/PS2Recomp) translates the whole game executable (VU0 included) into C++ that runs natively. That C++ is game code, so it **never goes into this repository**: everyone generates it locally from their own disc.
2. **Native layer** ([native/](native/)). Instead of emulating the VU1, GS and IOP, the port cuts at the engine boundary and draws with the PC's GPU; sound, FMVs and input are native too. It is not an emulator.

Architecture: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The executable: [docs/ANALYSIS.md](docs/ANALYSIS.md). File formats: [docs/FORMATS.md](docs/FORMATS.md).

## Building the port (Linux)
`tools/linux/wotm-launcher.sh` installs the build tools, builds, adds an icon, and offers new releases each time it starts. To do it by hand, `tools/linux/build.sh --iso <your.iso> [--dir ~/Games/WotM]` builds from your own ISO and writes a game folder with `play.sh`. The packages it needs are listed at the top of the script. Rumble is not implemented on Linux yet; first-run and options work.

**Our original engine:** the LEGACY tab in RECOMPILED OPTIONS shows the port we built first, on a separate recompiler. The launcher builds it; by hand, use `tools/linux/build_legacy.sh --iso <your.iso> --dir <game folder>` (needs clang 17+ and SDL2) and the tab can start it.

**Steam Deck (not yet tried on a real Deck):** in Desktop Mode, run the launcher in Konsole. It builds inside a throwaway Ubuntu container, so nothing is installed on SteamOS, then adds the game to Steam. Use the overlay to quit in Game Mode. Updates are offered in Desktop Mode only. The build takes a long time, so keep the Deck plugged in.

To test a build, `tools/linux/scenarios.py --game <folder>` runs bot-only scenarios in private windows and fails on a crash, an early exit, or a blank or frozen picture (`--list` shows them). `--spectate` lets you watch the bots on your own screen.

Picture filters at the window output: `WOTM_FXAA=1` (edge smoothing) and `WOTM_SHARPEN=0..1` (contrast-adaptive sharpening, after AMD's CAS). Both are off by default.

## Building the decompilation (Ubuntu)
```sh
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install binutils-mips-linux-gnu ninja-build python3-venv wine wine32:i386
python3 -m venv ~/.venvs/wotm && ~/.venvs/wotm/bin/pip install "splat64[mips]" pyelftools unicorn
sh tools/setup_toolchain.sh        # downloads ee-gcc 2.95.2 (SN ProDG) into tools/cc/
```
1. Put your ISO in `ISO/` and extract it to `disc/` (for example `7z x -odisc ISO/*.iso`).
2. `~/.venvs/wotm/bin/python configure.py --split`, then `ninja` (ends with `build/SCUS_971.97.rom: OK`).

Before committing changes to decompiled code, run `sh tools/wsl/gate.sh` (build + SHA1 check). Tools and known build pitfalls: [docs/guides/TOOLS.md](docs/guides/TOOLS.md),
[docs/guides/PITFALLS.md](docs/guides/PITFALLS.md).

## Contributing
See [CONTRIBUTING.md](CONTRIBUTING.md). The rules in [CLAUDE.md](CLAUDE.md) apply to humans too: no game files or game assembly in the repo, issues or PRs, and the ROM must keep matching the retail SHA1. The reference docs are in English; `CLAUDE.md`, `TODO.md`, commit messages and the dated notes in `docs/investigacoes/` are in Portuguese.

## License
The code in this repository is [GPL-3.0](LICENSE). It only covers what is here (decompilation, tools, native layer) and grants no rights over the game, which still belongs to its owners.
