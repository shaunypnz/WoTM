Linux preview of [War of the Monsters Recompiled](https://github.com/yanm1103/War_of_the_Monsters_recomp), the native port by Yan Moura and contributors. This fork adds a Linux build. Almost all of the game work is theirs.

**Early release:** see "Not verified yet" below.

**This is source only.** You build it on your own machine from your own US disc image (SCUS-97197). No game data or game code is included or downloaded.

## Install
```
git clone --branch {{TAG}} https://github.com/shaunypnz/WoTM.git
cd WoTM
tools/linux/build.sh --iso /path/to/your.iso --dir ~/Games/WotM
~/Games/WotM/play.sh
```
You need Linux on x86-64 and a CPU with AVX2, plus git, Python 3, CMake 3.24+, Ninja, g++, pkg-config, and the FFmpeg and X11/GL development packages. The Ubuntu install command is at the top of `tools/linux/build.sh`. The first build compiles the whole translated game and takes a while.

## Steam Deck
Not yet tried on a real Deck. In Desktop Mode, in Konsole, clone as above without building, then run `tools/linux/steamdeck.sh --iso /path/to/your.iso`. It builds in a throwaway container and adds the game to Steam. Expect a long build, and please report how it goes.

## What works
- Native Linux build and installer from your own disc
- Starts in a 1280x720 window; F11 for fullscreen, Ctrl+Q to close
- Works over RustDesk: if RustDesk is running, the mouse isn't captured for the camera (F10 toggles this)
- Played on one machine with an AMD RX 570 and an Xbox 360 controller
- FXAA and sharpening filters, off by default (`WOTM_FXAA=1`, `WOTM_SHARPEN=0.5`)

## Not verified yet
Frame rate hasn't been measured. Adventure mode, saves and long sessions are untested on Linux, and only one machine has been tried. Rumble isn't implemented on Linux.

The game draws with OpenGL. A Vulkan renderer is in progress and isn't in this release.

War of the Monsters is a trademark of its owners. This project isn't affiliated with or endorsed by them. GPL-3.0; see `LICENSE`.
