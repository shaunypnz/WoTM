# VU flag-mask fix and full-fight validation

FMOR and FSOR now produce bitwise flag masks in the interpreter, generated C++ and emitted WebAssembly. They had incorrectly reduced the OR result to a comparison against all bits set. FMEQ/FSEQ and FCOR retain their predicate behavior. This follows the instruction behavior documented by the [PCSX2 implementation](https://github.com/PCSX2/pcsx2/blob/master/pcsx2/VUops.cpp); no emulator code was copied.

The first malformed XGKICK in the failing checkpoint occurs in VU1 entry 0x3470, key 5BEFFC443FEAE5B9, at packet address 282 after native frame 10000. Its FMOR result is used as a mask. The old behavior eventually overwrites VU packet data, submits an unterminated GIF stream, and causes vertex data to be consumed as A+D writes. SCANMSK becomes 2 and normal drawing disappears. The packet header is still bad after the routine returns; simply deferring that kick was not supported by this capture.

Both old compiled and interpreter paths reproduce the fault from the same saved state. After correction, both pass the failure window without a malformed kick. The corrected compiled replay reaches a genuine AI WINS screen and gExitDialog=1 at frame 10275. These are checkpoint diagnostics, not fresh-boot gate acceptance.

The final source is d4436fabb9528527f94db918de7293f51ac1d9c9. The trace-free native binary SHA-256 is 8ef008022d8ab87ad1f4727e7d5f384272fd5d15040491ddf42174d08a274111. The private quick-play ELF remains 00f15f25bb53dff3aae69ee67527e63053f507b16641822f267ec8225f60377a. The diagnostic traced binary was e5999e2f242db9dd8bd20a64ad2dd1e99e4a489247e2a6dd7af5cb287959eb0a.

All 164 VU programs were regenerated from private recordings. Only one generated expression changed (FMOR in vu1_004.cpp); every other generated VU source file is byte-identical. The diagnostic build reused the accepted EE archive objects and rebuilt the VU units and runtime. The game archive SHA-256 is 0287152a3e269fdbd2924adee4c2147df871c2fc8d1c0fe683acd5c2f3064215. A normal rebuild must run the updated vurecomp before compiling a game; relinking an old generated VU archive retains the bug.

The full runtime suite passes 12/12 CTest tests. A synthetic microprogram runs 200 input combinations independently through the interpreter, generated C++ and executable WebAssembly. Cases cover flag-mask bits, upper-bit masking, the high immediate bit, VI0, comparisons, clip predicates and the E delay slot. No retail microcode is part of the tests. Local builds used the bounded wrapper with one compiler job; the small GS GPU regression used software Vulkan.

The two trace-free fresh-boot repeats each ran 12,000 frames and exited 0 with the standard input and a fresh card. The strict fight1p gate **fails both repeats**:

| Run | Observed result | Strict gate |
| --- | --- | --- |
| `20261028-vu-flag-or-fresh-1` | Genuine AI WINS at frame 11,900; frame 12,000 result differs from the authentic result reference by 2.35 luma. | FAIL: no result by 9,600; the sampled 9,600 frame is still combat (result-reference difference 50.92). Fight frame difference 33.61. |
| `20261028-vu-flag-or-fresh-2` | Normal scene/HUD/combat remain visible at frame 12,000; no result state by the end. | FAIL: no result by 9,600; result-reference difference 58.3. |

The previously reproduced malformed-packet/striped-render failure is corrected in the checkpoint controls and is absent from the reviewed late fresh captures. Full single-player acceptance remains open because the unchanged normal-input script does not reliably complete a round by the required deadline. Task331 must not be marked fully accepted from this source fix; tasks324/303 and the remaining playable gates stay open. Do not increase the deadline or replace a genuine result witness with picture similarity.

The private kit now regenerates VU code before desktop builds and uses bounded single-job compiler commands (`c166110`, six new mocked ordering/failure tests plus the existing 220 kit checks). The deployment supervisor uses the same wrapper (`ed81426` on private infrastructure main; `2a19447` in the local infrastructure checkout). Its mocked deployment unit test and all three required infrastructure suites pass on both baselines. No manual live-preview deployment was performed.

Private diagnostics, states, captures, binary/source manifests and logs remain under the normal local work/gate and task-runs directories. No retail data or generated game C++ is committed. No forced graphics state, health/result writes, ignored failures or relaxed thresholds were used. Original comparison remains same-route/phase, rather than deterministic same-frame combat. Two-player, audio, input, saves and soak acceptance remain separate.

An additional fresh control, `20261028-vu-flag-or-sparse-control`, uses the same menu route, references, frame budget and result deadline, but keeps only the first TRIANGLE attack at 4,200. It exits 0 after 12,000 frames without a genuine result state; the strict check fails (9,600 result-reference difference 45.0). Reducing repeated attacks therefore does not resolve completion timing. The canonical input file is unchanged. A private checkpoint at 9,000 is retained for further diagnosis.
