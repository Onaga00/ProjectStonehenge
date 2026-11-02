# Open research questions

Questions that are still open. Checks that need PCSX2 have their steps in `pcsx2/procedures.md` (numbers in brackets). Answered questions are at the end with a pointer to the doc that holds the answer.

## Need PCSX2

| Question | Doc | Procedure |
|---|---|---|
| Movie upscaling: does FSR 1 RCAS sharpening improve the EASU output on the real movies (ADR-006 §5 point 3)? Compare `--movie-upscale 2` screenshots with and without RCAS | `decisions/ADR-006-presentation-resolution.md` | User, visual (no PCSX2) |
| libmpeg `sceMpeg.frameCount` and `flags`: PCSX2 shows 46 where the port counts 47 pictures returned in the chunk, and flags 0x19B where the port writes 0. The game reads neither (expected `sdk` differences). One more data point would fix the counting rule (a state mid-chunk at a known picture) | `src/sce/libmpeg.cpp`, `port/validation.md` S3-3 | low priority, no procedure |
| VU0 micro skinning test vectors for part types 0 and 3 (type 1 captured, `pcsx2/sessions.md` run 05; feeds STATUS decision 14) | `subsystems/vu-programs.md` | [1] |
| Folder memory card: format against PCSX2 v2.8.2, round trip of a save, file times | `formats/folder-memcard.md` | needs the port's `sceMc*` layer first |
| I/O latency: PCSX2 reaches scene 6 sub 7 one frame later with an unformatted card than with no card, and its DATA 333 load takes ~17 frames (port: 3); the title data DATA 0xB0 takes about 20 frames longer than in the port (S2a: the menu fade at title sub 6 is 20 frames further on, `port/validation.md` S2a-3). A latency model for CD reads and libmc that matches PCSX2 frame for frame needs more cold-boot runs; until then frame counters are `timing` differences at checkpoints | `port/validation.md` S1-1, S2a-3, `port/ramdiff-expected-s1.csv`, `-s2a.csv` | before the input-recording slices (S5c) |
| VAG.IRX timing: the port runs `VagMain` from the mixer's SPU2 IRQ, so its phase against the game step (and the ring index / played bytes in each reply) can differ from PCSX2 by one half-buffer period (10 ms); not separable at the S2a checkpoints, because the stream positions there also differ by music time (Start pressed at another moment); recheck from a recording with an input-aligned checkpoint | `subsystems/sound.md` VAG.IRX, `port/validation.md` S2a-3 | first input-recording slice |
| Menu frame filter (zoom blur) in the port differs from PCSX2 in a radial pattern: centre G/B −1.5/−2.8, edges and corners +1 to +3. PCSX2's software and hardware renderers agree (within 0.3), so the port is the outlier. Bilinear rounding tests shift the level but keep the shape, so it is not only blend rounding. 2026-10-22 (`port/validation.md` S2a-6): glow texture and CLUT now equal PCSX2's GS memory (a PSMT8 table error was fixed on the way, no visible change here), and the exact integer blend makes the pattern worse, so blend rounding is ruled out. Next: compare Ad of the frame buffer the blur reads (needs the port's render target read back) and the magnified glow sampling | `port/validation.md` S2a-4, S2a-6 | run 08 |
| Port determinism under host load: five port instances run in parallel reached the S4 title checkpoint with different SMV chunk buffers (1-1.5 MB of movie data at other positions) than a single run, so the movie stream's progress depends on host timing (presumably the mixer/audio thread draining the SMV PCM in real time). Game logic was unaffected at the checkpoint; it matters for lockstep LAN play (ADR-008) and for recording replays. Find where host time enters. Checked 2026-10-16 (`port/slices.md` §B P1): the SPU model already runs per step; 1 solo + 4 parallel runs to the title card were identical except the pad analog bytes (live device overlaid on the pad script, sampled per presented frame); the movie-buffer case did not reproduce. Also seen in the hangar performance work: a run that took a screenshot every 60 vblanks at 6 fps reached the hangar with the camera at another orbit phase than runs without (`port/validation.md` S4-6) | `port/validation.md` S4-1, `subsystems/sound.md` | before the input-recording slices (S5c) |
| User check from slice S1: controller input in the port after the start-up enumeration fix (the session log now lists the devices SDL sees) | `port/validation.md` S1-2 | User, any time |

## Static analysis (Ghidra, tools)

- **Renderer: Z buffer sampled as a texture.** Answered 2026-10-22: a half-size copy of the scene's Z buffer for the half-resolution strips at the cloud transitions (no-save intro) and in mission 12; built as a depth copy (`port/validation.md` §2ab ZC). Still open: a PCSX2 frame where those strips pass behind geometry, to compare.
- **Formats:** VPK container layout; Namco sound-bank header format (DATA 0x164/0x165) and its pairing with BD.CDP bodies; terrain blocks (MISSION 5–18, 31), unit placement records (22) and message system (34/35); what the shared mission packets 0 and 26 draw; VIF vertex layout inside model node packets; model animation key format and vertex-animation block; AIRCRAFT block 6 fields and which item of block 2 is the cockpit.
- **SMV audio:** the exact sample layout of the 0x4000-byte PCM blocks (H: SPU2 core-0 input, L/R alternating in 0x200-byte blocks; `subsystems/sound.md`). Can be checked offline on the audio blocks of one SMV file.
- **Pad value PadExt +0x1C:** the view zoom (`view_camera.cpp`, pad type 5/6 only) reads it, but no entry of the mapped-button tables 0x0029FF20/40/60 writes it in normal play. Find any other writer (EASY control type, replay, VS) (`subsystems/pad-input.md`).
- **Weapons:** weapon id → in-game weapon name (special-weapon tables in AIRCRAFT data); whether any aircraft carries special-weapon id 15.
- **AI:** identity of the class 8/9 cannons (H: Stonehenge) and of the class 10 scripted objects (type 0x4C objects react to the strike event zones).
- **Rendering:** what each VU1 program computes; whether the first pool table of the flight frame receives packets from outside `BuildFlightDrawLists`.
- **Effects:** names of the effect types marked (H).
- **Sound and radio:** mapping of radio event ids to the probability table 0x00409DA8; SOUND `delay_timer` / `spu_register_timer`; the writer of VAG reply words +0x50/+0x60 (0x200 measured).
- **VS mode:** which `ShowCenterMessage` ids (0x11–0x13) are win / lose / draw; which stage-data field sets the split-screen flag through `game+0x20`; whether any VS stage spawns AI units (`subsystems/vs-mode.md` §7, VS slice).
- **Input recordings:** frame offset between a PCSX2 `.p2m2` record and the step whose `scePadRead` returns it (PS2 pad latency; calibrated in the first gameplay slice, `port/validation-strategy.md` §3.2).
- **Flight:** meaning of the constants at 0x0040DF68–0x0040DFBC.
- **Main context:** `unk_310` (0x310–0xA0F) and the pad sub-structs; whether the struct really ends at 0xA20; result record +0x0E (u16 unit count at creation vs. byte "players" in the results code).
- **Memory:** game code that relies on `lq`/`sq` ignoring the low 4 address bits; absolute addresses inside the remaining packet sub-blocks.
- **Executable:** contents of `.data`; whether the 34 unreferenced `bad_alloc` strings mark g++ translation units (module boundaries, `platform/executable.md`); a full list of the libgcc double-precision call sites (13–15 per routine; known: flight damping, `func_00137930`, `func_00215D20`, `func_00216D88`, `func_00224848`, `func_001FD398`; `port/coverage.md`).
- **SDK names still hypothesis:** libmpeg `sceMpegSetImageBuff` 0x00270B58 is called as `(mp, bitstream, size)` (AddBs-like) and 0x00270CA8 is polled as "bitstream consumed", so both names need rechecking; libkernl internals `_InitSys`, `scePrintf`, `sceFsInit`, `sceFsReset`, `_lf_bind`, `_SifLoadModule`, `sceSifExitRpc`; libpkt variant (Vif1/Gif/Vif0) of the 5 functions at 0x00262E08–0x00262E98; syscall numbers 0xFD/−0xFE of two uncalled alarm stubs (ps2sdk numbers differ).
- **IRX:** import the `.mdebug` STABS info of the IRX modules (needs `stdump`); re-import the other 7 IRX as R3000 if needed.
- **Ghidra:** rename the `MainContext` and `CdFileEntry` fields to snake_case by hand (the ghidra-mcp plugin re-adds type prefixes, `decisions/naming-convention.md`).

## Answered

| Question | Answer | Doc |
|---|---|---|
| Effect trail `func_001F3D38` glow-alpha division trap | no trap in practice: the divisor is span x (span x 4) (stack words 0x78 / 0xD8, re-read while translating the dead copy 0x001F5280), not span x 2 - n; span is 6 for every kind, so it is never 0. The earlier translation divided by span x 2 - n and was corrected (S8, 2026-10-19) | `src/game/effect_draw.cpp` |
| SOUND id-6 reply (0x00410D08, SPU limit for bank registration) | min(ESA0, ESA1) read after the command with libsd's work-area table; core 1 mode 2 = 0x001FE0C0, so the reply is core 0's 0x001DFFF0 (PCSX2 SPU2 state; the old 0x001DB7C0 was core 0 in mode 3), 2026-10-16 | `port/validation.md` S4-7 |
| VAG.IRX mode 4 vs. mode 8, `VagMain` ring hand-off, fno 1 command fields | command 1 (mode 4) streams while the EE keeps reading; command 2 (mode 8) = all data already read, the channel ends (0x40) or chains (0x80); a ring segment is handed back when 0x10000 bytes per voice are played; command fields {id, slot, bytes per voice, voice count, frequency word} (S2a, 2026-10-14) | `subsystems/sound.md` VAG.IRX |
| SOUND voice record offsets | voice byte +0x0E, modify flags +0x10 (corrected from +0x12 / +0x14, S2a) | `subsystems/sound.md` |
| EE `add.s`/`sub.s` in PCSX2 | not plain round-to-nearest: PCSX2's `FPU_ADD_SUB` (always on) masks the smaller operand to 25 − d significand bits before the SSE add; `fpu::Add`/`Sub` model it and all translated float adds use them (S2a, 2026-10-14) | `port/validation.md` S2a-3 |
| `gp` value | 0x004159F0, matches crt0 and Ghidra; the only gp write is crt0 | `platform/executable.md` |
| SDK version, source file names, type-info strings | SDK 2.3.0 (libmc 2.3.2); no source names; only libstdc++ classes | `platform/executable.md` |
| `main`, main loop, scene table | `main` 0x001001F0, loop 0x00100290–0x001003BC, 8 scenes; scenes 1 and 3 have no requester | `game/boot.md`, `game/boot.md` |
| VU microcode upload | inline every frame via DMA `ref` tags; 5 VU1 programs + 1 VU0 program | `subsystems/vu-programs.md` |
| TBL/CDP, SMV formats | — | `formats/CDP.md`, `formats/SMV.md` |
| MISxx.SMV ↔ mission | MISnn is the briefing movie of campaign mission nn (`StartMenuMovie(n − 1 + 0xD)`) | `subsystems/frontend.md` |
| Mission entries `id + 1` / `id + 0x52` | chosen by the player's aircraft (mask 0x00204114); variants share the map and differ in units, unit models and messages | `formats/MISSION.md` |
| EE↔IOP RPC, IOP side of SOUND/VAG | — | `game/iop-interface.md`, `subsystems/sound.md` |
| Threading, frame rate | single game thread, 59.94 Hz everywhere, SMV 29.97 fps; HDD threads never created | `game/threads-timing.md` |
| HDD save module | complete but unreachable on any hardware (init has no caller) | `subsystems/save.md` |
| Main context | `MainContext` 0xA20; floats at 0x80 are the brightness scale | `game/boot.md` |
| Code gaps, dead code | switch cases, created functions, STL copies, `jr ra` stubs | `port/dead-code.md` |
| Skeleton PCSX2 values (heap, first replies, USB joystick) | — | `pcsx2/sessions.md` run 03 |
| EE FPU rounding mode for the port | round to nearest, as PCSX2 runs AC04 (GameDB `eeRoundMode: 0`); User decision 2026-10-13 | `port/validation.md` E-2 |
| Carrier takeoff after a rearm (subs 0x19–0x1B, 0x1D) | confirmed on screen: 0x19 → 0x1A → 0x1B → 0x0B, control normal afterwards | `pcsx2/sessions.md` run 05 |
| Does the in-engine intro play only on the first boot? | no: it plays at every boot where card slot 1 has no `BASLUS-20152AC04` directory (sceMcGetDir count at save+0x4D8) | `subsystems/results-startup.md` |
| VU0 skinning part types | type 0 → 0x002250E8, type 1 → 0x00225110, type 3 (morph) → 0x00225140; earlier labels had 0 and 1 swapped | `subsystems/vu-programs.md` |
| `sceGsSyncV` return value | `CSR.FIELD` when interlaced, else 1 | `port/skeleton-readiness.md` |
| Scratchpad → GIF ring users | none in live code | `subsystems/rendering.md` |
| SOUND command 0xC | no EE writer: dead | `subsystems/sound.md` |
| `QueueCdRead` chunked path | every caller passes `chunk = −1` | `port/dead-code.md` |
| `C_PCCON` profiler | dead game code: translated, unreachable | `port/dead-code.md` |
| Naming convention | accepted (no type prefixes) | `decisions/naming-convention.md` |
| Model format, GIM, DMA-tag sub-blocks | `SM` models with relative offsets; GIM textures; pre-built DMA packets | `formats/MODEL.md`, `formats/GIM.md`, `formats/MISSION.md` |
| GS dumps of whole frames | triangle fans/strips with STQ and fog, trilinear mipmapping, CT24, Z24 as texture, destination-alpha blends | `platform/gs-subset.md`, `pcsx2/sessions.md` run 04 |
| In-menu movie pacing | one loop iteration = 2 fields (frame counter +2) | `game/threads-timing.md`, `pcsx2/sessions.md` run 04 |
| Half-resolution pass `func_00142460` | additive glow of the aircraft in external view; skipping it shows no visible change in the tested scene | `subsystems/rendering.md` |
| CONTROL TYPE option bit | 0x002A15D0 bit 0 set = EASY (Euler path), clear = NORMAL (matrix path) | `subsystems/flight.md` |
| Player view byte +0x149 | 0 HUD only, 1 cockpit, 2 external | `subsystems/flight.md` |
| Scene 2 subs 0x14–0x1D | 0x14–0x18 carrier landing, 0x1D rearm menu (START during the approach), 0x19–0x1B carrier takeoff (reachable from the rearm menu) | `subsystems/mission-flow.md` |
| Pause-menu HUD item (flag 0x20000) | shown only when the game has been cleared (0x002A0318 bit 0) | `game/boot.md` |
| Strike event `func_00135008` consumer | class 10 scripted objects of type 0x4C (`func_00134810`) read the zone bits | `subsystems/ai.md` |
| Sea surface | flat scrolling tiles clipped on the CPU; the noise table is filled from `rand()` but read only by dead code | `subsystems/rendering.md`, `game/random.md` |
| HUD warning texts | read from the executable (tables 0x002B7450 / 0x002B73D8); ids 1 and 9–12 are never selected | `subsystems/camera-hud.md` |
| Loaders for DATA 291–332 and BD 2 | none: every DATA and BD load call site was checked; indices are constants or bounded below 291; BD uses 0, 1, 3–12 | `formats/CDP.md`, `port/dead-code.md` |
| DATA = id + 1 for aircraft outside mask 0x00204114 | yes: `LoadFlightDataEntry` takes id + 0x52 only when the id < 0x13 and the mask bit is set (disassembly 0x0010AF48–0x0010AF70 checked), else id + 1 | `subsystems/mission-flow.md` |
| `func_00226710` / `func_0017BE38` in trial missions | aircraft unlock (state 2) for the mission; best-run track for the trial HUD comparison | `subsystems/results-startup.md` |
| VS split-screen layout | side by side: two 256×448 viewports, projection width halved (320, or 426.67 with SCREEN RATIO), height 480 | `subsystems/vs-mode.md` |
| VS win rules | first to the target score `game[0x2E]` (3 options per stage, table 0x003AEEF0), else higher score at time-out or when both are down; leaving the area loses; winner in `game[0x2D]` | `subsystems/vs-mode.md` |
| PCSX2 input recording format | `.p2m2` v1: 0x23A-byte header, 36 bytes per vsync (2 ports × the 18 `scePadRead` bytes 2–19) | `formats/p2m2.md` |
| Result mode names | the mode has no on-screen name; it selects the title (`TRIAL MISSION nn` / `MISSION nn`), the reward lines and the background | `subsystems/results-startup.md` |
| Movie audio half, IPU alpha, PCM layout (S3) | the SOUND reply status is the half being played (reader +0x70 equal mid-movie); alpha 0x80 on every pixel; L/R in 0x200-byte blocks (capture correlation 0.97-0.995) | `port/validation.md` S3-3, S3-5 |
| Presentation rate at ~50 fps in the 2026-10-22 measurement runs | the windows were not in focus (User); taken as Windows throttling, not re-measured | `port/renderer.md` §2.5–2.6 |
