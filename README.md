<img width="2113" height="744" alt="War of The Monsters recompiled LOGO" src="https://github.com/user-attachments/assets/6160bebd-48f0-47b5-bd28-f74d23c662fd" />

# War of the Monsters Recompiled!

A native port of **War of the Monsters** (PS2, 2003), built from the statically recompiled game code and easy to mod.

> **Untested preview.** This fork is published as work in progress. It has only been run on one Linux machine with a software renderer. Speed on a real graphics card, adventure mode, saves and game pads are unverified. Use it at your own risk.

**This is a Linux-only fork.** The port, the decompilation and almost everything here is the work of [Yan Moura](https://github.com/yanm1103) and contributors in [War_of_the_Monsters_recomp](https://github.com/yanm1103/War_of_the_Monsters_recomp). Credit for the game work belongs there.

**What this fork adds:** a native Linux build and installer from your own disc (see [Building the port (Linux)](#building-the-port-linux)) and FXAA and sharpening filters. Planned: 8-player online play over WebRTC, and a Vulkan renderer. New upstream releases get merged in, with our changes on top.

**Where this fork came from:** we were separately building our own Linux port of the game. When we found this project, it was further along, so we moved our work here. That original engine and its history are in [`legacy/`](legacy/README.md).

## Play
1. Build it from your own disc: `tools/linux/build.sh --iso <your.iso> --dir ~/Games/WotM`
2. Run `~/Games/WotM/play.sh`

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
`tools/linux/build.sh --iso <your.iso> [--dir ~/Games/WotM]` builds from your own ISO and writes a game folder with `play.sh`. The packages it needs are listed at the top of the script. Rumble is not implemented on Linux yet; first-run and options work.

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
