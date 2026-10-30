# Our original engine

`ps2-web-recomp/` is the Linux port of War of the Monsters we built before moving to this project: a separate static recompiler and Vulkan host, forked from [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp) by Samir Nuri (MIT, see its `LICENSE` and `NOTICE.md`). The history of its 0.1.0, 0.2.0 and 0.3.0 previews is in this repository's git log.

It isn't part of the current build. It's here as the record of that work and as source for things to reuse: the Vulkan output and picture filters (`runtime/src/gs/gpu`) and the launcher ideas (`scripts/play-wotm.sh`). No game data is included.
