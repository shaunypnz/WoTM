Linux preview of [War of the Monsters Recompiled](https://github.com/yanm1103/War_of_the_Monsters_recomp), the native port by Yan Moura and contributors. This fork adds a Linux build. Almost all of the game work is theirs.

**Early release:** see "Not verified yet" below.

**You build it from your own disc.** No game data or game code is included or downloaded. You need a US disc image (SCUS-97197), an x86-64 CPU with AVX2, and about 10 GB free.

## Install
Download `wotm-launcher.sh` and run it in a terminal, on Ubuntu 24.04 or newer, Debian 13 or newer, or a Steam Deck in Desktop Mode:
```
curl -fLO https://github.com/shaunypnz/WoTM/releases/latest/download/wotm-launcher.sh
bash wotm-launcher.sh
```
It works out which system you're on and asks for your disc image. On Ubuntu and Debian it installs the build tools with apt, which asks for your password. On a Deck it builds in a throwaway Ubuntu container, so SteamOS isn't changed, and adds the game to Steam. Then it builds into `~/Games/WotM` and adds an icon. The first build takes a while; keep a Deck plugged in.

## Updates
Start the game from its icon. When there's a new release, it asks whether to update and rebuilds from the new source; a small update takes minutes. If an update fails, your installed game stays as it was. Steam's Game Mode skips the check, so update from Desktop Mode. Updates aren't signed: they come over HTTPS from this repository, and you say yes to each one. `--no-update` skips the check, and `--update` updates straight away.

## LEGACY tab
The launcher also builds our original engine, so Recompiled Options > LEGACY can start it. `--no-legacy` skips that. It needs a Vulkan driver, and its first run is slower because it hasn't recorded the game's vector programs yet.

## Build by hand
`tools/linux/build.sh --iso /path/to/your.iso --dir ~/Games/WotM`, then `~/Games/WotM/play.sh`. The packages it needs are listed at the top of the script.

## What works
- Native Linux build and installer from your own disc
- Starts in a 1280x720 window; F11 for fullscreen, Ctrl+Q to close
- Works over RustDesk: if RustDesk is running, the mouse isn't captured for the camera (F10 toggles this)
- Played on one machine with an AMD RX 570 and an Xbox 360 controller
- FXAA and sharpening filters, off by default (`WOTM_FXAA=1`, `WOTM_SHARPEN=0.5`)

## Not verified yet
Frame rate hasn't been measured. Adventure mode, saves and long sessions are untested on Linux, and only one machine has been tried. Rumble isn't implemented on Linux. The installer has run on a clean Ubuntu 24.04, but the Deck's container route hasn't run on a real Deck.

The game draws with OpenGL. A Vulkan renderer is in progress and isn't in this release.

War of the Monsters is a trademark of its owners. This project isn't affiliated with or endorsed by them. GPL-3.0; see `LICENSE`.
