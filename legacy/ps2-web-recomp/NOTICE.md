# Notices and credits

- **ps2-web-recomp** by Samir Nuri, MIT licence (see [LICENSE](LICENSE)). The recompiler, runtime and native host
  in this repository are a fork of it.
- **Widescreen patch** `scripts/wotm/SCUS-97197_2EDE12D1.widescreen.pnach`: from the PCSX2 project's
  [pcsx2_patches](https://github.com/PCSX2/pcsx2_patches) collection ("Widescreen hacks by Aced14"), kept
  unchanged apart from a source line. It changes a few values in the game's executable when you build the PC edition.
- **wgpu-native** (MIT / Apache-2.0) is downloaded from its GitHub releases by `scripts/fetch_wgpu.sh` when you
  first build; it is not part of this repository.
- **SDL2** (zlib licence) is used from your system.
- **Picture filters** in `runtime/src/gs/gpu/post.wgsl` are written for this project after published algorithms:
  FXAA 3.11 by Timothy Lottes (NVIDIA) and Contrast Adaptive Sharpening from AMD FidelityFX (MIT). No code from
  either is included.

No part of War of the Monsters (disc image, executable, IOP modules, audio, video, textures or other assets) is
included in this repository or its releases. You need your own copy of the game.
