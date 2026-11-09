# Validation record

Every validation run of the project, newest last. How slices are validated: `validation-strategy.md`.

| Section | Contents |
|---|---|
| §1, §2, §3, Samples | the decompilation records (2026-10-11): integrity checks, blind re-review, recommendations |
| §2b–§2d | skeleton milestones B, C, E: executable load, boot, RAM at the first `SceneStep`, FPU rounding |
| §2e–§2i | slices S1, S2a, S2b, S3, S4 against PCSX2 |
| §2j | slice 5 prerequisites and missions (S5-P, S5-A, S5-T) |
| §2k | internal resolution 1-8x, 16:9, clean-up (2026-10-19; IR-1 to IR-4) |
| §2l | opening fixes, GRAPHICS page, Modern control types, caches and leaks (2026-10-20; OP-1 to OP-5) |
| §2p | engine flames back on the nozzles (2026-10-21; EF-1) |
| §2q | hands-on run from zero to mission 1 and debug overlay test (2026-10-22; UX-1 to UX-4) |
| §2r | no-save intro: cloud transitions, stutter; PSMT8 table; renaming (2026-10-22; IN-1, IN-2, RN-1; S2a-6) |
| §2s | GPU texture decoding, generation-based texture cache, texture images (2026-10-22; GD-1 to GD-3) |
| §2t | release 0.9 preparation: development parameters, version, file properties, startup image, no game data (2026-10-22) |
| §2u | no-save intro stutters: terrain uploads written block by block (2026-10-22) |
| §2v | QUIT GAME in the main menu (2026-10-22) |
| §2w | save texts for a computer (2026-10-22) |
| §2x | defaults: v-sync, FXAA, internal resolution of the display (2026-10-22) |
| §2y | sprites at internal scale > 1: native sample point (2026-10-22) |
| §2z | explosions at internal scale > 1, sun flare palette drawn on the GPU (2026-10-22) |
| §2aa | in-engine cutscenes: ticks per frame with v-sync (2026-10-22) |
| §2ab | depth copy for the cloud pass; SPU2 reverb (2026-10-22) |
| §2ac | frame interpolation (2026-10-22) |
| §2ad | frame interpolation speed; explosions at 7× (2026-10-23; FI-2, EX-1) |
| §2ae | invalid guest reads from the player aircraft's light colours; mission 6 crash (2026-10-23; IR-1, CR-1) |
| §2af | mission 6 report: takeoff after a rearm, vibration after the landing, explosion fire, QUIT GAME in the campaign menu (2026-10-23/24; TO-1, VB-1, EX-2, EX-3, QG-1) |
| §2ag | second mission 6 crash: a unit loop with a garbage count; invalid-access log names the caller (2026-10-24; CR-2) |
| §2ah | third mission 6 crash, recorded: an inverted unit test in the track stream overran the radio header; fixed (2026-10-25; CR-3) |
| §2ai | GS pipeline hitches on a new GPU driver: warm-up at startup and a pipeline cache file; where the game tick's time goes (2026-10-25; PC-1, RT-1) |
| §2aj | render thread: GS processing, interpolation and presentation beside the game ticks (2026-10-25; RT-2) |
| §2ak | engine sound missing in the external view (2026-10-26; ES-1) |
| §2al | GPU work structure: counters, narrow barriers, partial frame-buffer copies (2026-10-26; GW-1 to GW-3) |
| §2am | GS processing: one vertex buffer per draw list, pipeline reuse, blended copies replayed (2026-10-27; GP-1 to GP-4) |
| §2an | terrain cells black or patched with other tiles up close: mirrored tiles flipped through a GPU-only sprite; warning for other reads of GPU-only drawings (2026-10-27; TT-1, TT-2) |
| §2ao | GPU time per section (timestamps, `--gpu-profile`), draw-state counters; the flight passes alternate depth and alpha state on almost every draw; ADR-009 (`VK_EXT_extended_dynamic_state`, shared pipelines) and why it gains little on AMD (2026-10-30; GT-1 to GT-6) |
| §2ap | Host memory leak scan: code review of allocations and Vulkan/SDL/Win32 objects, memory sampling of long runs; one MPEG-2 decoder kept per movie (fixed), the timer handle and period (fixed); guest RAM unchanged (2026-11-01; ML-1 to ML-3) |
| §2aq | Hangar aircraft and weapons tinted teal (player report, NVIDIA, 0.9.5/0.9.6): the light-map pass, not reproduced on AMD with the player's save; GPU decode check; one `VK_ERROR_DEVICE_LOST` under `--vk-validation`; player's tests rule out the texture decode; reproduced: NaN from a leftover light colour in the VU1 HLE (2026-11-03; HT-1 to HT-6) |
| §2ar | Integrated GPUs (Intel UHD session): per-pixel draw cost; integrated-GPU defaults (1×, AA off) and GS pipelines without dual-source blending; conversions limited to the texels read; game-thread timing; hangar CPU work (2026-11-04; IG-1 to IG-9) |
| §2o | new memory card and first save, startup messages (2026-10-20; NS-1, NS-2) |
| §2n | anti-aliasing (FXAA, SMAA) before the HUD (2026-10-20; AA-1 to AA-4) |
| §2m | settings tests (GRAPHICS page, live combinations, controller and save round trip), texture residency, sprite edges at N×, defaults and the frame-rate overlay (2026-10-20; ST-1 to ST-8) |

## Validation of the decompilation records (2026-10-11)

The validation had two parts:
- **Integrity checks** of the records: tool-based and complete.
- **Blind re-review** of a random sample of functions, compared against the docs.

All corrections are already applied to the docs, the coverage CSV and the Ghidra plate comments (logged in `../rename-log.csv`).

## 1. Integrity checks

Tool: `tools/research/validate.py`, read-only. It needs `out/research/funcs.json` (from `ghidra_dump.py`) and `reach.json` (from `reach.py`). Final run: **0 problems**.

| Check | Result |
|---|---|
| `symbols.csv` names vs Ghidra | All 541 rows match: functions by address, `data`/`global` rows against Ghidra labels. Hypothesis rows keep the Ghidra default name or the hypothesis name. No duplicate names |
| Coverage CSV names vs Ghidra | All 2,884 rows match |
| Totals | 2,884 = 2,612 understood + 0 partly + 0 unknown + 272 dead. Per-subsystem sums match `../STATUS.md` and `coverage.md` |
| The User's figure 2,884 = 2,229 + 389 + 266 | Correct for the committed version (HEAD). The last research pass promoted all 389 "partly" functions to understood. The 15 "partly" functions of the blind sample come from those 389 |
| Classification | Ghidra has 3,877 functions, all inside `.text`: 2,884 in the game range (each has a coverage row) and 993 library functions outside it. Every `sdk`/`libc`/`libgcc`/`kernel` symbol is outside the game range; every `game` symbol is inside |
| Dead code vs call graph | Raw graph (all data pointers as roots): 2,619 reachable. Without scenes 1 and 3 as roots (they are only in the scene table and never requested): 2,612 reachable, which is exactly the 2,612 non-dead functions |

Fixed by these checks:
- **V-2 (scene 3 classification).**
  - Problem: scene 1's handler was marked dead, but scene 3 was still "understood". Scene 3 = `SceneMovie` 0x00211CA8, its 4 sub-states, and `SetRequestFlags` 0x00100AF8 (used only by scene 3). Both scenes are reachable only through the scene table and never requested.
  - Fix: the 6 scene-3 functions are now **dead**. New totals: 272 dead, 2,612 understood; the movie subsystem is 48 understood / 9 dead.
  - The dead-code rule now covers this case (`dead-code.md`, `coverage.md`).
  - `SetRequestFlags` had an empty note: it stores `ctx->req = a0`.
- `STATUS.md` symbol counts were off by one (540/264 → 541/265). Corrected.
- `coverage-functions.csv` contained 87 double-encoded UTF-8 sequences (`Â±`, `Ã—`, …) from an earlier rewrite (not present in HEAD). Repaired.

## 2. Blind re-review

### Method

- **Sample.** 60 functions drawn with `random.Random(20261011)`, stratified by subsystem (proportional, largest remainder):
  - **40** understood in HEAD;
  - **15** "partly" in HEAD and promoted to understood in the last pass;
  - **5** dead.
  - The partly pool is mostly effects, so 11 of the 15 are effects functions.
- **Reviewer.** One subagent analysed each function from Ghidra only. Its rules:
  - do not read `docs/`, the CSVs or tool output, and ignore plate comments;
  - check the disassembly for MMI, VU0 and 128-bit code;
  - report any reliance on existing names.
- **Ghidra stayed read-only.** One exception to check: a single `disassemble_bytes` call on existing code at 0x001F3910. It changes nothing; the User can confirm when saving the program.
- **Scoring.** I compared each description with the CSV note and the subsystem docs. Every major and every disputed constant was checked in Ghidra before scoring.
  - **agree**: same behaviour. A terse note counts as agree when it is correct.
  - **minor**: an omission, or a wrong constant or label, that does not change what the function is.
  - **major**: the doc describes different behaviour or a different meaning of the data.
- **Wider re-review rule.** A subsystem with more than 5 % major differences gets a wider re-review.
  - Effects, frontend and mission exceeded it.
  - 50 more functions were drawn with `random.Random(20261012)` and reviewed the same way: effects 18 (10 promoted + 8 understood), frontend 16, mission 16.

Limits:
- The reviewer saw the Ghidra names we gave. It reported each reliance, and a match that rested only on a name did not count.
- One reviewer did all batches.
- Behaviour that needs runtime context to interpret (which screen, which mission) was scored on mechanics only.

### Results

- First sample (60): **40 agree, 16 minor, 4 major** (6.7 % major). By group:
  - understood: 31 agree / 7 minor / 2 major;
  - promoted from partly: 6 / 7 / 2;
  - dead: 3 / 2 / 0.
- Wider re-review (50): **41 agree, 9 minor, 0 major**.
- All 110: 81 agree (74 %), 25 minor (23 %), 4 major (3.6 %).

| Subsystem | Sample (agree / minor / major) | Major % | With wider re-review | Major % |
|---|---|---|---|---|
| ai | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| camera | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| collision | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| core | 4 (3 / 1 / 0) | 0 % | 4 (3 / 1 / 0) | 0.0 % |
| effects | 16 (10 / 4 / 2) | 12 % | 34 (25 / 7 / 2) | 5.9 % |
| flight | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| frontend | 8 (6 / 1 / 1) | 12 % | 24 (20 / 3 / 1) | 4.2 % |
| hdd | 2 (2 / 0 / 0) | 0 % | 2 (2 / 0 / 0) | 0.0 % |
| hud | 2 (1 / 1 / 0) | 0 % | 2 (1 / 1 / 0) | 0.0 % |
| mission | 3 (1 / 1 / 1) | 33 % | 19 (13 / 5 / 1) | 5.3 % |
| movie | 1 (0 / 1 / 0) | 0 % | 1 (0 / 1 / 0) | 0.0 % |
| radio | 2 (1 / 1 / 0) | 0 % | 2 (1 / 1 / 0) | 0.0 % |
| render | 4 (3 / 1 / 0) | 0 % | 4 (3 / 1 / 0) | 0.0 % |
| replay | 2 (1 / 1 / 0) | 0 % | 2 (1 / 1 / 0) | 0.0 % |
| results | 1 (1 / 0 / 0) | 0 % | 1 (1 / 0 / 0) | 0.0 % |
| sequence | 3 (2 / 1 / 0) | 0 % | 3 (2 / 1 / 0) | 0.0 % |
| sound | 4 (3 / 1 / 0) | 0 % | 4 (3 / 1 / 0) | 0.0 % |
| unreached | 2 (0 / 2 / 0) | 0 % | 2 (0 / 2 / 0) | 0.0 % |
| weapons | 2 (2 / 0 / 0) | 0 % | 2 (2 / 0 / 0) | 0.0 % |

Effects and mission stay just above 5 % only because their small first samples are included. The wider samples had no major difference. The cause of each major was then fixed across all notes, not only in the sampled function (V-6, V-8).

### Findings and fixes

| Id | Where | Finding | Fix |
|---|---|---|---|
| V-1 | `func_00224180` / `func_00224410` / `func_00224848` (sequence) | Skinning part types 0 and 1 were swapped in three notes and in `../pcsx2/procedures.md`. Found through the PCSX2 hit (`ra` 0x002246DC) | Notes, `vu-programs.md`, the procedure and the Ghidra plate of 0x002236A0 corrected |
| V-2 | Scene 3 | See section 1 | 6 functions marked dead |
| V-3 | `func_00105178` (core, minor) | The MMI scale-matrix builder leaves the upper word of the sign-extended `mfc1` in row0.z, row1.w and row2.w. That word is 0 for positive scales and 0xFFFFFFFF for negative ones. A translation must keep it | Note |
| V-4 | `func_0014B3F8` (frontend, **major**) | The note said "VU1 light/colour constants". The packet actually uploads one vector plus the camera's world→screen and world→clip matrices (+0xE0..+0x15F) to VU1 and runs MSCAL 0. The light set is written separately | Note |
| V-5 | `func_0010FB30` (mission, **major**) | The note said "at mission end: kill bookkeeping". It is the **mission phase change** (rules state flag 8 → 4). It activates the next phase's units (flags48 0x100; with 0x8000000 only when `g_GameState[0x28]` ≥ 2) and retires the others | Notes of 0x0010FB30 and 0x0011A208, `mission-flow.md` |
| V-6 | Effect draw setup (effects; **major** in `func_001FF818`, minor in `func_002034A8`) | Three mislabels:<br>• Effect work +0x62498/+0x62494 were called "frame range" in **31 notes**. They are the near/far depth distances: `InitEffects` sets 1.0 / 65536.0, and the terrain fog setup rewrites the far value.<br>• +0x624E0 was called "light matrix". It is the effect clip matrix (`func_00155900`).<br>• `func_001FF818` builds the frustum clip structure 0x002BB990, not "trail parameters / colour table" | All 31 notes relabelled; 0x001FF818 rewritten |
| V-7 | `func_001E88C0` (effects, **major**) | The note said "afterburner sprite writer". It is a two-pass **heat-haze distortion**. Pass 1 writes a mask into framebuffer alpha. Pass 2 draws displaced strips of the **framebuffer sampled as a texture** (CT32 1024×1024 at TBP 0), blended with destination alpha | Notes of 0x001E88C0 / 0x001E81F0; added to `../platform/gs-subset.md` and `renderer.md` §1, because the renderer must handle this framebuffer feedback read |
| V-8 | Random-table reads (effects, mission, ai, weapons) | 17 of the 67 `NextRandomFloat` callers had no mention of the read in their note, e.g. the spawn `func_0019C278` with 9 call sites. The behaviour docs listed the gameplay users, but not every effect spawn | Every note now states its call-site count. The full list of 67 functions / 152 call sites is appended to `../game/random.md` |
| — | Minor items | Omitted details:<br>• the target marker on the HUD ladder;<br>• the restore of unit flags after player placement;<br>• the first colour stage;<br>• the addition before the pan wrap;<br>• the libc `rand()` call per fired proximity trigger;<br>• the fade level at sequence start.<br>Also: wrong constants in replay camera styles 8 and 9; a contradictory queue number in the radio matcher; empty notes for `StoreFlightDataPointers` and two unreached functions | Notes corrected |

### What this says about the records

- Mechanics (control flow, offsets, constants, call order) were right in nearly every function.
- The errors were in two areas:
  - **interpretation of data**: what a matrix or a pair of floats means;
  - **omitted side effects**: random reads, extra outputs.
- The promoted-from-partly group had the most problems: 2 major and 7 minor out of 15, all in effect draw code documented at interface level. That matches the stated depth of those notes (`coverage.md`): they are good enough to find code, but the translator must read the packet code itself.
- Gameplay subsystems (ai, weapons, flight, collision, hud) had no major difference.

## 2b. Skeleton milestone B checks (2026-10-11)

### B-1. Executable load against PCSX2

The port loads `SLUS_201.52` from the ISO into guest memory (`src/platform/elf_loader.cpp`). A development-build dump taken right after loading (`--dump-ram-after-load`) was compared with three PCSX2 save states (`bp_00100220`, `bp_00100130`, `mainMenu`) using `tools/research/ramdiff.py`:

| Region | Result |
|---|---|
| `.rodata`, `.lit4`, `.vutext`, `.vudata`, `.gcc_except_table` | identical |
| `.text` | identical except 48 bytes at 4 places, the same in all three states (B-2) |
| `.data`, `.sdata`, `.sbss`, `.bss`, heap, stack, low memory | differ as expected: the states were taken after crt0, the SDK init and part of the game's init had run (the RAM check at the first `SceneStep` is milestone E) |

Loader facts confirmed: one PT_LOAD segment at 0x00100000 (0x31172C bytes from the file, 0x3CC74C in memory, ending exactly at the end of `.bss` 0x004CC74C), NOBITS `.sbss` 0x00411780 (+0x1BC) and `.bss` 0x00411980 (+0xBADCC) zeroed, entry 0x00100008, gp 0x004159F0, executable SHA-1 as in `../platform/executable.md`.

Initial guest stack pointer: crt0's `SetupThread(stack = −1, size 0x100000)` returns **sp = 0x01FFED60**. Measured from `bp_00100220.p2s`: the EE register file sits at offset 0x162 of `PCSX2 Internal Structures.dat` (PCSX2 2.8.2; 32 GPRs × 16 bytes), sp = 0x01FFED40 inside `main`, whose prologue reserves 0x20 bytes.

### B-2. PCSX2 patches three VU0 flag reads

The 48 bytes are PCSX2 game patches (its game database), not game writes: they are present at the start of `main` already. Each patch swaps a `cfc2 rX, $17` (read of the VU0 **MAC flag**) with the vector instruction three `vnop`s before it:

| Site | Function | Original order | Patched (PCSX2) |
|---|---|---|---|
| 0x0010FFC8 / 0x0010FFDC | `func_0010FFB8`, project a point with clip test | `vmaddw.xyzw vf9, vf7, vf8` … 3 × `vnop` … `cfc2 v0, $17` (then `andi 0x11`) | `cfc2` first, `vmaddw` last |
| 0x0017C0C4 / 0x0017C0D4 | `func_0017C020`, point-to-triangle distance (collision) | `vsub.x vf0, vf9, vf10`; `vsub.x vf0, vf12, vf11`; 3 × `vnop`; `cfc2 v1, $17`; `cfc2 a0, $17` (then `andi 0x80` each) | `cfc2 v1` moved before the second `vsub` |
| 0x0017C6C4 / 0x0017C6D4 | `func_0017C688`, AABB overlap test (collision) | same pattern | same swap |

Meaning: on the real EE the MAC flag reaches `cfc2` with a pipeline delay, so each read returns the flags of an **earlier** op. In `func_0017C020` the intent is visible: `v1` receives the sign of `vf9 − vf10` and `a0` the sign of `vf12 − vf11`. PCSX2 updates flags immediately and reorders the code to get the hardware result.

Port consequence: the translation follows the hardware result (= the PCSX2-patched order), never the naive "flags of the last op". Rule added to `.claude/rules/game-code.md`; risk added to `risks.md`; notes on the three functions in `coverage-functions.csv`. Other `cfc2` flag reads in game code need the same check when they are translated (a scan for `cfc2 …, $16/$17/$18` close after vector ops is a later research task).

RAM comparisons with PCSX2 must ignore these 48 bytes (expected difference).

## 2c. Skeleton milestone C checks (2026-10-12)

### C-1. Boot run

`ProjectStonehenge --iso <image>` (development build, RelWithDebInfo) runs `Entry` → `main` on the game thread. The log shows the nine IOP modules replaced, the first vsync wait reached during `InitSubsystems`/`LoadBootData`, then `entered scene 6 (SceneStartup)` about 0.55 s (≈33 vblanks) after start. The main loop then idles in the scene 6 stub: 318 vblanks in 5.3 s, 0 ticks dropped. Scene 0 (`SceneBootLoader`) runs translated: BD.TBL, DATA 0x163/0x164 (sound data), the BD entry 0 transfer to the IOP, `RequestScene(6)`.

The frame count to reach scene 6 is not expected to match PCSX2: CD reads complete one poll after they are issued, while a real drive takes a variable number of frames.

### C-2. Expected RAM differences at the first `SceneStep` (for milestone E)

These come from the SDK layer, not from translated game code. Milestone E checks the rest against PCSX2.

| Area | Why |
|---|---|
| newlib heap headers around 0x004CC758 and malloc's internal state | the port's `malloc` keeps its bookkeeping on the host; it returns the same block (0x004CC760, 0x01A318A4) |
| SIF client structs (`sceSifClientData` at 0x0033E58C, 0x00353F94, 0x00420400): rpcd, `serve` | `serve` holds a stand-in value; the other words are not written by the port |
| pad DMA buffers 0x002A0080 / 0x002A0180 | libpad's driver writes them on the PS2; the port answers `scePadRead` directly |
| `sceCdlFILE` date bytes (on the guest stack, below sp) and the TOC buffer 0x0029C980 | TOC contents are a hypothesis (procedures.md); dates left zero |
| GS GParam 0x003FAAA0 (+6 version, +8 handler) | revision 0x1B assumed (PCSX2's CSR.REV) |
| SDK/kernel data (libpad, libcdvd, libmc, fileio, `_InitSys`, thread/semaphore tables) | the SDK internals are not translated |
| guest stack below sp | frame contents differ (host locals stay on the host) |
| SOUND reply buffers 0x00429800/0x00429E00 and the voice-status bytes 0x00424FD0 | stand-in replies: transfers finish at once, voices never end on their own (no mixer yet) |
| reverb work-area address 0x00410D08 | stand-in value 0x001FE0C0 (procedures.md) |
| frame counters, timer 0 | boot timing differs (C-1) |
| constructor 0x0027C680 (C++ runtime) effects | not run by the port |

This was the prediction before the comparison. The measured result is E-1; the TOC, GParam and reverb rows are settled there.

## 2d. Skeleton milestone E checks (2026-10-12)

### E-1. RAM at the first `SceneStep` against PCSX2

Inputs:
- **PCSX2 side:** `scenestep_first.p2s`, PCSX2 2.8.2, cold boot, Execute breakpoint at `SceneStep` 0x00100B18, first hit. `frame_count` (0x0029F3A8) = 0, so both sides are at the same point: after the boot code, before the first scene handler.
- **Port side:** `ProjectStonehenge --dump-ram-at-first-scenestep` (development build). The dump is taken at the entry of the first `SceneStep`, on the game thread.
- **Comparison:** `python -I tools/research/ramdiff.py <user data>\dumps\ram_first_scenestep.bin scenestep_first.p2s --expected`. The `--expected` table is `ramdiff-expected.csv` (address ranges and reasons only, no memory contents).

**Before the fixes:** 817 differing ranges. 700 of them are in the EE kernel area below 0x00100000; 117 are in the game image, heap and stack.

**Three port errors found and fixed:**

| Address | Found | Cause | Fix |
|---|---|---|---|
| 0x00411784 `g_WorkHeapSize` | port 0x01A317A4, PCSX2 0x01A318A4 | The port's `malloc` rounded requests to 8 bytes, so 0x01A318A4 became 0x01A318A8 and no longer fit; `ProbeLargestMallocSize` then stopped one 0x100 step lower. **Game state difference:** the work heap was 256 bytes smaller. | `malloc` now uses newlib's EE chunk sizes: (n + 4 + 15) & ~15, at least 16. Arena 0x01A318B0 bytes from chunk 0x004CC758. The size word (0x01A318B1) is written before the block as newlib does. Unit test "malloc: newlib chunk sizes…" |
| 0x0029C98C–0x0029C997 TOC | port zeros; PCSX2 `01 02 01 00` at +0x0C and `00 1D 7C 0F` at +0x14 | The TOC hypothesis covered only the first 6 bytes and byte 0x11 | `sceCdGetToc` writes PCSX2's single-layer DVD layout: the physical-format bytes `01 02 01 00`, start PSN 0x030000, and end PSN 0x30000 + sectors − 1 (= 0x1D7C0F for the 1,735,696-sector image), both big-endian. Copy size 0x810. Unit test "libcdvd over a synthetic ISO…" |
| SOUND id-6 reply → 0x00410D08 | PCSX2 0x001DB7C0 after the reply arrives (`scenestep_first_2.p2s`); port stand-in 0x001FE0C0 | The value was a hypothesis | Stand-in returns the measured 0x001DB7C0: the effect end 0x001FFFFF (the game's initial value at 0x00410D08) minus a 0x24840-byte area, plus 1. The game sends id 6 only as (core 1, mode 2). Unit test "SOUND stand-in: reverb mode reply…" |

Confirmed unchanged: GParam 0x003FAAA0 = `01 00 02 00 01 00 1B 00` (interlace, omode 2, ffmode 1, version 0x1B; unit test) and the radio timers 0x004BE0B0. Both are identical in the two states and in the port's dump. The scratchpad is identical.

**After the fixes:** 814 ranges, all classified by `ramdiff-expected.csv`, **0 unexplained**. After the rounding switch of E-2: **796 ranges, 0 unexplained**; the `fpu-rounding` rows were removed from the CSV.

| Category | Ranges | Bytes | Meaning |
|---|---|---|---|
| `kernel` | 701 | 167,572 | EE kernel RAM below 0x00100000, and the kernel thread area above the initial sp 0x01FFED60. The port has no EE kernel and the game never reads either |
| `sdk` | 70 | 1,800 | SDK and runtime internals, each region identified by its xrefs in Ghidra: SIF client structs (`rpcd`), SIF/IOP-heap/libpad/libmc/libcdvd `.data` and `.bss` (search-file cache, TOC copy 0x004CB300), libpad DMA buffers 0x002A0080/0x002A0180, newlib malloc and sbrk state, libgcc frame object, the crt0 argument block 0x004C55C0 (`main` never reads argc/argv), C++ runtime constructor data, the last 16 bytes of the TOC copy (IOP leftovers; the game passes 0x0029D180 to `InitCdFileSystem` but never uses it), and the newlib top-chunk header 0x01EFE00C |
| `stack` | 14 | 919 | below `main`'s sp 0x01FFED40 (dead frames), and `main`'s register-save slots. The port keeps saved registers and host locals on the host |
| `timing` | 6 | 84 | CD poll counter 0x0029F1D0 (7 polls on PCSX2, 1 in the port); pad record 0x004121E0 (PS2: the pad is not stable yet, state 0; port: stable at once, state 0x28; at 10 s PCSX2 shows state 99 / id 7 like the port); SOUND and VAG fno 1 replies (async on the PS2, still unanswered at this point; immediate in the port) |
| `pcsx2-patch` | 4 | 48 | PCSX2 GameDB COP2 reorder patches (B-2) |
| `fpu-rounding` | 18 | 1,618 | before E-2 only: 1-ULP float differences, gone after the switch to round-to-nearest |
| `stack-garbage` | 1 | 1 | 0x00425024: byte +8 of the SOUND id-7 record (reverb depth) is never written by `func_0021CFA0`. On the PS2 it keeps the 2 that the id-6 record left at the same stack slot; `GuestFrame` zero-fills. SOUND.IRX does not read the field |

Other findings:
- At 10 s (`scenestep_first_2.p2s`) the SOUND voice-status bytes 0x00424FFA–0x00424FFF (voices 42–47) are 4, meaning "released, envelope still sounding". These are probably the 6 VAG.IRX streaming voices, which SOUND sees as not keyed on but audible. HYPOTHESIS until the audio backend exists; the stand-in reports 0 for them. Noted in `../subsystems/sound.md`.
- The GameDB entry for SLUS-20152 also sets `eeRoundMode: 0` ("Fixes autopilot being silly and crashing the plane"), which E-2 depends on.

Correction (slice S1, 2026-10-14): the 0x001DB7C0 of the id-6 row comes from a **later** id-6 reply in `scenestep_first_2.p2s` (10 s), not from the boot command. SOUND.IRX replies with the lower of the two cores' effect start addresses as read before the new mode takes effect: the boot command (core 1, mode 2) replies 0x001DFFF0 (core 0 still in mode 0), measured in all three cold boots of run 06; a later id 6 then sees core 1's mode-2 area 0x001DB7C0. The stand-in now models both values (`iop_services.cpp`; unit test "SOUND stand-in: reverb mode reply…"). The first-SceneStep comparison stays at 0 unexplained ranges (the reply has not arrived there).

Second correction (2026-10-16, S4-7): the PCSX2 SPU2 state shows why. In `scenestep_first_2.p2s` **core 0** is in reverb mode 3 (ESA 0x001DB7C0), not core 1 in mode 2; in every later reference state core 1 is in mode 2 with ESA **0x001FE0C0** (work area 0x1F40) and core 0 in mode 0 (0x001DFFF0). The reply is min(ESA0, ESA1) read **after** the command (sceSdSetEffectAttr writes the ESA registers synchronously), with libsd's per-mode work-area table (LIBSD.IRX 0x4148). The "value from before the command" hypothesis and the 0x24840 mode-2 size were wrong; the stand-in now uses the table.

### E-2. EE FPU rounding: PCSX2 runs AC04 with round-to-nearest (decided 2026-10-13: nearest)

All 1-ULP float differences have one cause:
- **Random table 0x00411998** (512 × `rand() * 2^-31`; the multiplier 0x30000000 is an exact power of two). `cvt.s.w` rounds the 31-bit integer: PCSX2 matches round-to-nearest in 512 of 512 entries. The port's `fpu::IntToFloat` truncates, so 247 entries are 1 ULP lower.
- **Sound category volumes 0x0042CA50** (`x / 255.0f`): PCSX2 matches round-to-nearest division; `fpu::Div` truncates.

Cause: PCSX2's GameDB sets `roundModes: eeRoundMode: 0` (Nearest) for SLUS-20152. The reason given is that the autopilot crashes the plane under PCSX2's default Chop mode. So the reference behaviour (PCSX2) is round-to-nearest for the whole EE FPU in this game, while `ee_math.h` and `.claude/rules/game-code.md` model round-toward-zero. Hardware is not IEEE in either mode, so neither is bit-exact by construction. The values that matter for the port are PCSX2's, and those are nearest.

**Decision (User, 2026-10-13): round to nearest.** `ee_math.h` now rounds add/sub/mul/div/sqrt/`cvt.s.w` results to nearest (ties to even), keeping the EE rules: no NaN/Inf, denormals flushed to zero, overflow and x/0 clamped to ±FLT_MAX. `cvt.w.s` still truncates. The rule in `.claude/rules/game-code.md` is updated. Rerun of E-1: the random table and the sound volumes match PCSX2 byte for byte (796 ranges, 0 unexplained). Unit test "EE FPU: round to nearest…".

**Addendum (S2a, 2026-10-14):** add and sub are not plain round-to-nearest in PCSX2. The smaller operand is masked first (`FPU_ADD_SUB`, see S2a-3). `fpu::Add`/`fpu::Sub` now model this, and all translated float adds/subs use them.

## 2e. Slice S1 checks (2026-10-14)

Reference: `../pcsx2/sessions.md` run 06 (6 save states, 5 screenshots, cards S/E). Method: `validation-strategy.md` §2.

### S1-1. RAM checkpoints

| Checkpoint | PCSX2 state | Port run | Result |
|---|---|---|---|
| 0x00261378 sub 7, card S | `s1_S_sub7` | `--memcard <card S> --dump-ram-at 261378` | 0 unexplained |
| 0x00261378 sub 7, card E | `s1_E_sub7` | card E | 0 unexplained |
| 0x00261378 sub 7, no card | `s1_N_sub7` | `--no-memcard` | 0 unexplained |
| 0x002613D0 sub 9, card S | `s1_S_sub9` | card S | 0 unexplained |
| 0x00260B78 `EnterMenu(0)`, card S | `s1_S_exit` | card S | 0 unexplained |
| 0x00260B54 `RequestScene(2)`, card E | `s1_E_exit` | card E | 0 unexplained |

Fixed on the way (all in the IOP/SDK stand-ins; the translated game code needed no fix after review):
- libmc: GetInfo completes one vblank after the request, GetDir after one more poll (the game's poll counters 0x004118A8 / 0x004118CC must match); results 0 / -2 (unformatted: empty `_pcsx2_superblock`) / -11 (no card); free space 7925 KB for the AC04 save = 8001 - file clusters - directory-entry clusters; times are UTC.
- SOUND reply: one persistent 0x600-byte reply area (leftovers of earlier replies visible to the game); records 7-9 write only the id and a status byte at +8; transfer record = last finished ticket (one batch later) and status 0; voices 42-47 report 4; id-6 address (see the E-1 correction).
- VAG fno 1 reply: words 5-8 = 1 and ring indices 2.
- Pads: neutral analog 0x7F as PCSX2's DualShock 2, `scePadRead` byte 31 = 0x5A.

Remaining differences, all classified: frame counters and frame-parity effects (`ramdiff-expected-s1.csv`: the port boots to scene 6 8 frames earlier and loads DATA 333 in 3 frames instead of ~17), newlib malloc bins (`sdk`), the User's stick values (`input`). Note: PCSX2's own I/O timing differs by a frame between cold boots (no card: sub 7 at frame 58, unformatted card: 59), so frame-exact matching needs an I/O latency model calibrated on more runs (`../open-questions.md`).

### S1-2. Screenshots (`tools/research/imgdiff.py`)

| Screen | PCSX2 | Mean diff R/G/B | Pixels > 16 | Note |
|---|---|---|---|---|
| splash 1, card S | `s1_S_splash1` | 0.23 / 0.29 / 0.32 | 0.65 % | best at offset 0 |
| splash 0, card E | `s1_E_splash0` | 0.30 / 0.31 / 0.30 | 0.66 % | |
| no-card message 1 | `s1_N_msg_1` | 0.56 / 0.56 / 0.56 | 1.0 % | glyph edges |
| splash 0, card S | `s1_S_splash0` | 1.05 / 1.24 / 1.45 | 3.0 % | PCSX2 image is 0.80 as bright: taken during the fade-in (card E's splash 0 matches) |
| no-card message 2 | `s1_N_msg_2` | 0.32 / 0.33 / 0.33 | 0.59 % | reached with `--pad-script 320:cross` |

User feedback (2026-10-14): the audio sounds right; with `--no-memcard` the controller gave no input. Cause: the input backend opened gamepads only on hot-plug events, and none arrived in that session (no "port 1: gamepad" line in the log). Fix: `InputBackend::Start` logs every device SDL sees and opens the gamepads at start-up (an Xbox One controller is now opened at once). Also from the User: on PCSX2 the brightness option sits at its default until a save is loaded (game behaviour); the port now shows the option's effect (ADR-006 §4 implementation note).

A bug found on the way: the texture decoder added a bilinear margin to nearest-sampled sprites, so glyphs at texel 0 decoded the whole 1024×1024 font and overflowed the per-frame atlas (first 16 glyphs of each line missing). Fixed (margin only with TEX1.MMAG).

## 2f. Slice S2a checks (2026-10-14)

### S2a-1. S1 checkpoints after the SOUND/VAG changes

S2a corrected the SOUND voice record offsets (voice +0x0E, flags +0x10) and replaced the voice 42-47 special case with SOUND.IRX's `get_ch_stat` rule driven by the mixer (ENDX, ENVX), with the VAG.IRX stream voices now keyed on by the VAG stand-in. The six S1 RAM checkpoints (card S sub 7 / sub 9 / exit, card E sub 7 / exit, no card sub 7) were re-run against `pcsx2/sessions.md` run 06: **0 unexplained ranges** each. An intermediate state (VAG voices keyed but status taken from the mixer's key state) gave 1 instead of 4 for voices 42-47 at 0x00424FFA and in the SOUND reply copies: the measured 4 is now explained by the code.

### S2a-2. Title and main menu (port only, before the PCSX2 batch)

- Title screen with logo, PRESS START and copyright; Start opens the main menu (NEW GAME / LOAD / TUTORIAL / VS MODE / OPTIONS with help line); X on NEW GAME opens the difficulty column with its connector lines (`--pad-script 1200:start,1450:cross`, card S).
- Music (`--dump-audio`): the title track from VAG channel 0 about 1 s after the title appears, then the menu track after Start (crossfade to channel 1). Level only checked by RMS; by ear against PCSX2 in the batch.
- Renderer additions needed for the filters: `(Cs−0)·Ad+Cd`, alpha-test fail modes, bilinear texel footprint (`platform/gs-subset.md`).

### S2a-3. RAM checkpoints against PCSX2 (run 07, `../pcsx2/sessions.md` run 07)

| Checkpoint | PCSX2 state | Port run (card S) | Result |
|---|---|---|---|
| 0x0022DF00 title sub 6 | `s2a_title_sub6` | `--dump-ram-at 22DF00` | 0 unexplained |
| 0x0022D748 Start accepted | `s2a_start` | `--pad-script 1200:start --dump-ram-at 22D748` | 0 unexplained |
| 0x0022D84C menu active | `s2a_menu_active` | as above, `22D84C` | 0 unexplained |
| 0x0022D880 confirm | `s2a_menu_confirm` | `--pad-script 1200:start,1500:down,1515:up,1863:cross --dump-ram-at 22D880` (the User's Down/Up, confirm 644 frames after "menu active" as in PCSX2) | 0 unexplained |

Expected differences: `ramdiff-expected.csv`, `ramdiff-expected-s1.csv` and the new `ramdiff-expected-s2a.csv`. Four classes, each checked on the values:
- **CD latency** (`timing`): PCSX2 loads the title data (DATA 0xB0) about 20 frames slower, so it enters sub 6 with 30 menu-fade steps where the port has 10. At port hit 21 of 0x0022DF00 the fade object is identical byte for byte (time 0x3EFFFFF8, value 0x3F7F4C7E). The menu load-queue timer differs for the same reason.
- **Frame counter / parity** (`timing`): the menu draw packets in both buffer halves (scanline band positions from `BuildScanlineBands`, pulse colours from `GetMenuPulse`, DMA tags), the menu bucket pointers and the VAG send buffers.
- **Music time** (`timing`): VAG slot stream positions, the VAG reply area, and one VAG refill read more or less in the CD queue and request counters. They depend on how long the music has played (boot frames, title time).
- **Input timing** (`input`): title/idle time and the logo ease-out (`g_TitleMenu` +0x688..+0x697), the pad repeat counter, and the SE requests (counter 0x00410D00, the voice table, the SOUND reply status, the menu SE record). The number of type-out ticks depends on when the cursor moved during the help-line type-out: 18 in the port against 17 in PCSX2, with the same sound types on the same voices.

**EE add/sub (fixed in this check).** The fade value showed that PCSX2's `add.s` is not plain round-to-nearest. PCSX2 2.8.2 (`pcsx2/x86/iFPU.cpp`, `FPU_ADD_SUB`, compile-time `FPU_CORRECT_ADD_SUB 1`, not a setting) models the EE adder's missing guard bits. Before an `add.s`/`sub.s`/`adda`/`suba`/`madd`/`msub`, the operand with the smaller exponent keeps only its top 25 − d significand bits (d = exponent difference; sign only when d ≥ 25). The sum is then rounded to nearest. Thirty additions of 1/60 give 0x3EFFFFF8 in PCSX2 and with this model, against 0x3F000003 with plain rounding. So the menu fade ended one frame early in the port. `fpu::Add`/`fpu::Sub` (`src/common/ee_math.h`) now implement the model (unit test "EE FPU: add/sub mask…"). By the User's decision (2026-10-14), every float add/sub-type instruction in the translated code now goes through them: 83 instructions in 30 functions, counted from the disassembly. The game-code rule and the `port-function` skill require it from now on. `mul.s`/`div.s` stay plain round-to-nearest (PCSX2 uses SSE with the round mode for them). The S1 checkpoints were re-run after the change: still 0 unexplained.

### S2a-4. Screens

PCSX2 shows the 224-line field output deinterlaced. On the title and menu, with their fine dot-pattern background, its screenshots match the port's 448-line image with each row averaged with the row above (`imgdiff.py --blend-fields`). This also improves the S1 splash (mean 0.05 instead of 0.23).

| Screen | PCSX2 | Port vblank | Plain: mean R/G/B, > 16 | `--blend-fields`: mean R/G/B, > 16 |
|---|---|---|---|---|
| title | `s2a_title` | 720 | 1.04 / 3.05 / 4.07, 2.5 % | 0.21 / 1.52 / 2.20, 0.00 % |
| main menu | `s2a_menu` | 1376 | 0.97 / 2.63 / 3.86, 1.1 % | 0.78 / 1.45 / 1.96, 0.00 % |
| NEW GAME opened | `s2a_menu_open` | 1996 | 0.76 / 3.12 / 4.64, 2.2 % | 0.22 / 1.67 / 2.78, 0.09 % (help-line type-out glyphs) |

No structural difference: same primitives, textures, positions and blend. One difference remains open: a G/B bias of −1 to −2 levels in the screen centre (x 128–512, y 64–384), where the frame filter's zoom-blur copies overlap. The filter is a feedback effect (each frame samples and blends the previous frame buffer). The port blends with fixed-function float blending, the GS with integer `((A−B)·C >> 7) + D`, so per-frame rounding differences settle into a steady bias. PCSX2's hardware renderer is not GS-exact either, so the reference renderer had to be known first.

**S2a-R (run 08, `../pcsx2/sessions.md` run 08).** The User's settings: renderer Automatic (Vulkan, hardware), blending accuracy Basic, deinterlacing Automatic. The same main-menu frame with the **software** renderer (`s2a_menu_sw`) agrees with the hardware shot within 0.3 levels in the centre (signed mean G +0.34, B +0.26). So both PCSX2 renderers agree and the port is the outlier. Against the software shot the port's difference is **not a uniform bias** but a radial pattern: G/B −1.5/−2.8 in the centre, +1 to +3 at the edges and corners (32×32 block map). Brightness is not involved: both sides have brightness 3 (0x002A15CD) at that point, and the port's native screenshot applies the same field-copy scale. The zoom blur samples a 60×44 PSMT8 glow texture (TBP 0x3E90, CLUT 0x3E84) magnified about 10× with bilinear filtering and adds four scaled copies with `Cs·Ad + Cd`. Tests in the shader: rounding the bilinear result instead of flooring it raises everything by about 1 level, and unquantised weights instead of the GS 4-bit weights deepen the centre deficit (−2.4 instead of −1.6). Neither removes the radial shape, so blend rounding alone does not explain it. Next step (open, `../open-questions.md`): compare the alpha channel (Ad of the zoom blur) and the glow texture in GS local memory with the save state's `GS.bin`, which needs a port GS-memory dump.

### S2a-5. S1 bug: green band on the AC04 PROJECT splash (User report, run 07)

The User saw a green area in the lower part of the AC04 PROJECT splash (splash 1, card S). Image 1 is a PSMCT24 texture (210×432, 3 bytes per pixel, DATA 333). Its upload is split over several IMAGE packets of whole qwords, and 16 is not a multiple of 3. `GsMemory::WriteImage` handled each packet separately and dropped the 1–2 bytes of the pixel split across a packet boundary, so every later pixel was shifted by a byte (port RGB = PCSX2 GBR). In the native screenshot this is faint (rows 414–430, values 0–5): the S1 tolerance (mean ≤ 1) did not catch it, but the presented window made it visible. Fix: `GsState::ImageData` keeps the split pixel's bytes for the next packet (cleared at TRXDIR). Unit test "GIF: CT24 IMAGE transfer with a pixel split across two packets". Splash 1 now differs by at most 2 per channel in that band. Lesson: the screen check also looks at the maximum difference per region, not only the mean (`validation-strategy.md` §2.2).

### S2a-6. Follow-up of S2a-R: GS memory against `s2a_menu_active` (2026-10-22)
- Port GS memory at the main menu (`--dump-vram`, vblank 1376) against `GS.bin` of `s2a_menu_active` (VRAM at offset 0x1A9 of the GS state): the zoom-blur CLUT (0x3E84) is equal; the glow texture blocks 0x3E90–0x3E9D (uploaded every menu entry as PSMT8 64×58, DBW 2) held the same bytes in another order: 214 of 3,584 bytes moved, all in PSMT8 rows 2–3 of each 16-row group.
- Cause: `kCol8` rows 2 and 3 (`gs_memory.cpp`, and the same table in `tools/research/gsvram.py`) had pixels 2–5 and 10–13 in the wrong order; the correct rows follow the same half-swap rule as rows 6–7 (`{33, 37, 49, 53, 1, 5, 17, 21, …}`). With the corrected table the predicted PCSX2 blocks equal the real ones in all 3,584 bytes; the blocks equal to PCSX2's byte for byte rose from 1,313 to 2,420 (every PSMT8 upload). HYPOTHESIS (not rechecked): the 2026-10-17 column-order check matched its 48 T8 blocks because their bytes were equal within those row pairs (HUD font with large uniform areas).
- Effect on screens: none for textures written and read as PSMT8 (both sides used the same wrong table); it matters where a frame buffer or other data is read as PSMT8 (`gs_conv.comp` uses the same layout).
- S2a screens with the current build (`--blend-fields`, mean abs R/G/B): title 0.16/1.39/2.06, main menu 0.78/1.44/1.93, NEW GAME opened 0.21/1.64/2.73, no pixel over 16 except the help-line glyphs (0.09 %). Signed centre G/B −2.1/−3.3 (title), −0.1/−0.4 (menu), −2.3/−4.0 (opened). Unchanged by the table fix; inside the frame-filter exception of `validation-strategy.md` §2.2.
- Blend test: drawing the zoom blur (`Cs·Ad + Cd`, ALPHA 0x58) with the exact integer GS blend in the shader (`../platform/gs-subset.md`, "All blend equations") instead of the fixed-function DST_ALPHA mapping deepens the centre deficit (menu G/B −1.2/−2.1); the fixed-function mapping stays. The remaining radial pattern therefore comes from the blur's inputs (Ad of the frame, the magnified glow sampling), not from blend rounding: still open (`../open-questions.md`).

## 2g. Slice S2b checks (2026-10-14)

Reference: run 08 (`../pcsx2/sessions.md` run 08), card S, one continuous session. The port replays the User's button sequence with `--pad-script` on a fresh copy of card S (press spacing 30-150 frames; the PERSONAL DATA confirm is shifted by 8 frames so that `frame_count & 0xF` is 6 as in PCSX2, because the records page pads its allocation by `((frame & 0xF) + 1) * 0x10` bytes; quirk in `../subsystems/frontend.md`).

### S2b-1. RAM checkpoints

| Checkpoint | PCSX2 state | Port (`--dump-ram-at`) | Result |
|---|---|---|---|
| CONTROLLER SETUP entry 0x002437C8 | `s2b_ctrl_enter` | `2437C8` | 0 unexplained |
| CONTROLLER SETUP exit 0x00226450 | `s2b_ctrl_exit` | `226450,2` | 0 unexplained |
| SOUND SETTINGS entry 0x00244410 | `s2b_snd_enter` | `244410` | 0 unexplained |
| SOUND SETTINGS exit | `s2b_snd_exit` | `226450,4` | 0 unexplained |
| PERSONAL DATA entry 0x002423A0 | `s2b_rec_enter` | `2423A0` | 0 unexplained |
| LOAD entry 0x0023EB68 | `s2b_load_enter` | `23EB68` | 0 unexplained |
| LOAD exit (save loaded) | `s2b_load_exit` | `226450,8` | 0 unexplained |
| SCREEN SETTINGS entry 0x00240F68 | `s2b_opt_enter` | `240F68` | 0 unexplained |
| SCREEN SETTINGS exit (16:9 set) | `s2b_opt_exit` | `226450,10` | 0 unexplained |

Expected differences: the S1 and S2a lists plus `ramdiff-expected-s2b.csv`. Classes, each checked on the values:
- **Frame parity and animation** (`timing`): the page draw packets beyond the S2a range in both buffer halves, the current-half pointers, the tab fade levels in `g_MenuFade`.
- **CD latency** (`timing`): the records page data-load timer (4/60 s in the port, 16/60 s on PCSX2) and a page-data read still in flight on PCSX2; a VAG stream position.
- **Input timing** (`input`): at SOUND SETTINGS exit the pad script pressed X 30 frames after the last change, while the help line was still typing (last SE 9 in the port, 2 on PCSX2; the message box state; the held-button word); a stale main-menu item scale (+0x3FC of the object at 0x00CCFF80, which the options page reuses without clearing) still decaying from 2 to 1.
- **User sequence** (`input`): between LOAD entry and LOAD exit PCSX2 made two more page-data reads (one is DATA 0xB3 into the save page buffer 0x00CD0640), 81 SE requests against 37, more VAG commands, and slot 0 of the menu BGM record holds track 4 (a `PlayMenuBgm(4)` followed by the LOAD page's `FadeOutMenuBgm`). That is the trace of more main-menu → LOAD visits: the User opened the LOAD page several times while checking why breakpoint 0x0023D4FC did not fire (confirmed by the User). From SCREEN SETTINGS entry on, the BGM record is identical again.

Procedure error: breakpoint 0x0023D4FC (`s2b_load_done`) is in the LOAD page's own read of the save image when the page opens (state 5 → 6, port hit at frame 3291, 25 frames after the page entry). In the procedure it was set 2 s after the entry, so it never fired; not a game difference. The libmc request delay therefore stays uncalibrated (hypothesis, `../formats/folder-memcard.md` §10.1).

### S2b-2. Screens

All ten PCSX2 screenshots, compared with `imgdiff.py --blend-fields` against port screenshots of the same replay:

| Screen | Port vblank | Mean R/G/B | > 16 |
|---|---|---|---|
| `s2b_ctrl` | 1780 | 0.63 / 1.66 / 2.31 | 0.55 % |
| `s2b_snd` | 2260 | 0.68 / 2.04 / 3.36 | 0.21 % |
| `s2b_rec` | 2740 | 0.24 / 1.57 / 2.22 | 0.32 % |
| `s2b_rec_2` | 2950 | 0.38 / 1.82 / 2.80 | 0.48 % |
| `s2b_load` | 3460 | 0.29 / 1.61 / 2.21 | 0.15 % |
| `s2b_menu_loaded` | 3760 | 0.49 / 2.11 / 3.08 | 0.77 % |
| `s2b_opt` | 4181 | 0.54 / 2.39 / 3.67 | 2.18 % |
| `s2b_opt_changed` | 4541 | 0.57 / 2.37 / 3.74 | 2.38 % |
| `s2b_menu_169` | 4690 | 0.33 / 2.13 / 3.32 | 0.69 % |
| `s2b_ctrl_169` | 4900 | 0.44 / 2.71 / 5.50 | 7.16 % |

Same layout, texts, values and colours on every screen, including the 16:9 mode and CONTINUE after LOAD. The thin reddish rectangle on the option pages and the empty usage bars on PERSONAL DATA are in the PCSX2 screenshots too (original behaviour). The means are the menu frame filter's G/B pattern (S2a-4). Pixels over 16 are the moving crosshair lines, the pulsing cursor glow (SCREEN SETTINGS) and the panel opening animation (16:9 CONTROLLER SETUP): the screenshots are not at the same animation frame. No pixel offset improves the 16:9 match (±1 in x or y: 19-20 % over 16).

## 2h. Slice S3 checks (2026-10-14, port only; the PCSX2 batch is pending)

### S3-1. SS01 in the port
Run: card S, `--pad-script 1200:start,1500:cross,1700:cross` (Start, NEW GAME, NORMAL).
- Pictures and subtitles shown (screenshots at vblanks 2200-10500; 4:3, letterboxed 640 × 336).
- Pacing: at `MovieFrameSync` hit 100 frame_count 1926, hit 300 frame 2326, hit 1000 frame 3726: 2 frames per picture; 1000 pictures take 33.4 s of wall time (real time). First build: the timer-0 model let a vblank pass on every poll, so the wait took 2 vblanks (3 per picture, 66 % speed). Fixed: a poll is one H-blank, the vblank only comes at the end of the field (`EeRegisters::AdvanceLine`; unit test "RCNT0: a busy-wait on 0x118 H-blanks spans one vblank").
- End: `MenuMovieEnd` first hit at frame 10090 with exactly 4170 pictures (the file has 4170 picture start codes); then page 2 (mission title, slice S4 stub).
- Skip: Start at vblank 2400 → end at frame 2458 (1 s fade), 354 pictures.
- Sound: mixer capture (`--dump-audio --exit-at 3600`): silence before the movie, menu music, then movie audio from 29 s (RMS 1200-5400). PCM layout: 0x200-byte L/R blocks (`../formats/SMV.md`).
- Decode cost: 2.2 ms per picture (libmpeg2 portable C, RelWithDebInfo).
- Regression: the 6 S1 and 4 S2a checkpoints still 0 unexplained (timer model and SOUND reply changes); 62 unit tests pass (new: IPU colour conversion, RCNT0 busy wait).

- User report after the first build: frames flicker, colours break, and vertical lines split the screen when the view moves. Two causes, both fixed (2026-10-14):
  - **Column seams** (renderer): `gs.vert` moves geometry by half a pixel, so a 1:1 sprite's texture coordinate at the pixel centre is an exact integer. Float interpolation gave values like 0.9999, and `floor` picked the texel on the left. In each 16-pixel movie column, texel 0 was shown twice and texel 15 was lost (gradient at column offset 1: 7.8 against ≈21 elsewhere; at offset 0: 26.8). Fix: `gs.frag` rounds texture coordinates to the GS's 1/16-texel fixed point before sampling. After it the column profile is flat (the remaining bumps at offsets 0 and 8 are the MPEG 8×8 block grid). Effect on the validated screens, same frames against PCSX2 (`--blend-fields`): title 0.21/1.52/2.20 (max 15) → 0.16/1.39/2.06 (max 7); main menu 0.43/1.58/2.29 (max 14) → 0.43/1.56/2.25 (max 13). Both closer.
  - **Corrupt pictures at chunk changes** (decoder): open GOPs across chunks (`../formats/SMV.md`); `sceMpegReset` did a full libmpeg2 reset and lost the reference picture. Fix: partial reset. 90 frames around three open-GOP chunk starts (pictures 214, 615, 2115) contain no corrupt blocks (before: over 8,000 saturated-yellow pixels in two pictures at frames 5984-5987). The pictures per chunk equal the file's, and the movie end comes at the same time as before.
  - Regression after both fixes: 10 S1/S2a checkpoints 0 unexplained, 63 unit tests pass.

### S3-2. Movie upscaling stage (ADR-006 §5)
Run: as S3-1 with `--movie-upscale 2` (and 3), `--screenshot-at 3000,5000`.
- ×2: one EASU pass (640×336 → 1280×672), ×3: two passes (log). Output looks clean: no seams at the 16-pixel column edges, colours unchanged.
- The ×2 output averaged back 2×2 matches the native picture best at the letterbox offset y = 56 (mean absolute difference 3.0 levels; 7-14 levels at offsets 52-60). That is the expected EASU edge reconstruction.
- The native screenshots at vblank 5000 are byte-identical with and without `--movie-upscale`: the stage is presentation-only. No Vulkan validation messages.
- Regression after the side-path changes: the 10 S1/S2a checkpoints are still at 0 unexplained. 63 unit tests pass; the new one covers the GIF source marks and `MovieTexelOrigin`.
- RCAS is not applied. Whether it helps is a visual judgement for the User on the stage output.

### S3-3. RAM checkpoints against the PCSX2 batch (run 09, `../pcsx2/sessions.md` run 09)
Run: card S, `--pad-script 1200:start,1500:cross,1700:cross`, expected lists S1, S2a, S2b and the new `ramdiff-expected-s3.csv`.

| Checkpoint | PCSX2 state | Port | Result |
|---|---|---|---|
| 0x00239248 `MenuMoviePlay`, first hit | `s3_ss01_start` | `--dump-ram-at 239248` (frame 1728; PCSX2 12310) | 0 unexplained |
| 0x00238870 `MovieFrameSync` | `s3_ss01_mid` | `--dump-ram-at 238870,668`: hit chosen by movie position, both at 662 pictures / 22.03 s (PCSX2's page counter is at 689: its movie started 22 iterations later) | 0 unexplained |
| 0x002392A8 `MenuMovieEnd` | `s3_ss01_end` | `--dump-ram-at 2392A8` (frame 10090; PCSX2 20714) | 0 unexplained |

- Same SMV reader state, picture count, sceMpeg width/height and chunk position.
- Image buffer: the same picture. Mean difference 0.5 levels, max 10: libmpeg2's IDCT against the IPU's rounding. **Alpha 0x80 on every pixel in both: the hypothesis is confirmed.**
- New expected differences, all without game effect:
  - `sdk`: libmpeg's globals, work area, IPU DMA tags and scratchpad buffers (the port decodes in host memory), the SIF DMA id encoding, and `sceMpeg.frameCount`/`flags` (PCSX2 46/0x19B, port 47/0; the game reads neither).
  - `timing`: the SOUND batch fill and the VAG all-idle volume request at the movie start (IOP latency), the subtitle-font load timer (CD latency), frame-buffer parity, the CD read ticket, and the audio half at the end (silence blocks while draining).
- Pacing: from the mid checkpoint to `MenuMovieEnd`, 7028 frames on both sides, so pictures and the end are frame-exact. Before the first picture the port is 42 frames earlier: CD and IOP latency, the known open question.

### S3-4. Screens
| Screen | PCSX2 | Port frame | Plain: mean R/G/B, > 16 | Notes |
|---|---|---|---|---|
| star field, about 5 s | `s3_ss01_a` | 2036 | 0.83 / 0.73 / 0.99, 0.03 % | best match among frames 1960-2140 |
| TV "LIVE" with subtitle, about 20 s | `s3_ss01_b` | 2960 | 2.45 / 2.20 / 2.50, 2.4 % | the differing pixels are inside the animated TV picture (one movie picture apart), on the picture's top and bottom edge lines (deinterlacing), and in the subtitle |

- Subtitle: same text, position and line breaks; PCSX2's glyph edges are a little crisper.
- Not deinterlace-blended: movie pictures have no fine dot pattern, and `--blend-fields` makes both shots worse.

### S3-5. Movie sound against the PCSX2 capture
`s3_ss01_audio.mp4` (FLAC, decoded by `tools/research/flac_mp4.py`) against the port's mixer output (`--dump-audio --exit-at 4800`). Aligned by envelope correlation, then at sample level.
- Same channel: correlation 0.97-0.995 (L and R, at 2/10/20/35 s). Port L against PCSX2 R: 0.76-0.93. **The 0x200-byte L/R block layout is confirmed.**
- Alignment offset −99 samples at 5 s and −100 at 35 s: no drift, so the rate is exact.
- Level: the port is 1-3 % louder (RMS ratio 1.006-1.031). Not investigated further; within PCSX2's output path differences.

## 2i. Slice S4 checks (2026-10-16; PCSX2 run A of the S4 batch)

Port pad script matching the User's run A: `1200:start,1500:cross,1700:cross,2000:cross,2100:start,2372:cross,10786:start,11400:cross` (superseded by the frame-aligned script of S4-1) (Start skips SS01, Start leaves the map, X on START MISSION). Frame counter 0x0029F3A8: PCSX2 value + 934 = port value at the title, briefing and campaign checkpoints; the map starts 66 frames earlier in the port (PCSX2's briefing stream waits on CD reads; the movie state at map entry is identical).

### S4-1. RAM checkpoints (`ramdiff.py` with `ramdiff-expected-s1..s4.csv`)
Final run 2026-10-16 (S4-7): pad script aligned to the PCSX2 run A frame counters, `--pad-script 700:start,850:cross,950:cross,1166:start,1438:cross,9918:start,10844:cross`, card S, each checkpoint in its own run.
| Checkpoint | Frame (PCSX2 / port) | Unexplained | Notes |
|---|---|---|---|
| title card 0x00230790 | 1232 / 1232 | 0 | |
| briefing movie 0x00238380 | 5094 / 5094 | 0 | |
| briefing map 0x00238440 (hit 1) | 8073 / 8007 | 0 | the port's map starts 66 frames earlier (PCSX2's briefing stream waits on CD reads) |
| campaign menu 0x00230FC8 | 9925 / 9925 | 0 | |
| hangar 0x00235AB8 | 10861 / 10861 | 0 | |
| VS mission select 0x0023C708 | — | 0 | S4-5 |
S1 sub 7 (card S) and the three S3 movie checkpoints rerun with these changes: still 0. Before the alignment (script `1200:start,1500:cross,1700:cross,2000:cross,2100:start,2372:cross,10786:start,11400:cross`) the port's frame counter was 934 frames ahead and the checkpoints had 6/10/8/12/9 unexplained ranges.

### S4-2. Hangar rendering
- VU1 programs B/C (CPU HLE) against the PCSX2 hangar GS dump (`tools/research/gifstrips.py`): per texture the same packet and vertex counts within the camera difference (e.g. 11821 vs 11708 strips for the room/aircraft texture, 6320 vs 6320 for the light-texture pass), the same screen and Z ranges, and mean vertex colours within 1-5.
- The picture matches PCSX2 (aircraft, canopy, floor reflection, room, ceiling lights, panels, texts) after adding the depth buffer and the Z-buffer-as-colour clear (`../platform/gs-subset.md`, S4). Fog (FGE) is not drawn yet.
- Mission select (VS MODE) runs: list, records, thumbnail, help line; checked against PCSX2 in S4-5.

### S4-3. Briefing map frames 2 and 3: VU0 macro mode rounds to nearest (PCSX2 run C)
States `s4_map2`/`s4_map3` (2nd and 3rd hit of 0x00238440), port `--dump-ram-at 238440,2` / `,3`.
- Before the fix the map camera (object 0x00CCFF80 +0x250) differed in 23 words by 1-2 ulp, e.g. the view matrix entry 0x394CE000 (PCSX2) against 0x394CDFFE, 51200 against 51199.996, while its inputs (look-at record +0x410) were identical. The values do not change between frames 2 and 3: the camera is still (map state 2).
- Cause: `_sceVu0ecossin` for the pitch -1.57060099. t = pi/2 + a = 1639 * 2^-23 exactly; the polynomial adds c3 t^3 (-1.2e-12) and smaller terms. With chop rounding each negative term pulls the sum 1 ulp down (0x394CDFFE); with round to nearest it stays t (0x394CE000). A Python model of the code (`out/`, not kept) gives exactly the two values.
- Why nearest although the User's PCSX2 has VU0 Roundmode = 3 (chop): PCSX2 compiles COP2 macro instructions into the EE block (microVU_Macro.inl) with plain SSE add/sub/mul (microVU_Upper.inl `mVU_FMACa`: `SSE_ADDPS`/`SSE_ADDSS`, no operand masking) and never loads the VU0 MXCSR there, so they run under the EE's mode, round to nearest from the GameDB (`eeRoundMode: 0`). Only VU micro programs use the VU rounding setting.
- Fix: `vu0_math.h` macro helpers (`vu0::`) round to nearest; the chop helpers moved to `vu0::micro::` for the VU1 program HLE. After it: the whole map object matches PCSX2 except timing fields; the map packet area (port half A against PCSX2 half B, frame parity) is byte-identical in the terrain sprites: the 803 halfwords that differed by exactly -1 (1/16 pixel) are gone. Left: DMA tags pointing into the other half and the typing text layer (timing).
- Hangar picture at the same vblank before/after the fix: no visible change in the 3D scene (the difference is the menu filter dot pattern and animated gauges, frame phase).

### S4-4. Hangar purchase commit (PCSX2 run B)
User's run: card S, LOAD slot 1, CONTINUE, Start x3 (title card, briefing, map), BUY > AIRCRAFT, X X on the F-5E (52,000; credits 161,000 -> 109,000), Triangle. 0x00233F10 fired on leaving (ra 0x00235C00: the cancel path of `AircraftSelectRun`); `s4_commit_before`/`s4_commit_after`.
- Function test vector (`tests/test_vectors_s4.cpp`, reads the states from `AC04_TEST_STATES`, skipped without it): the entry state's RAM, `CommitHangarSelection(a0 = 0x00CCFF80)`, compared with the exit state over all 32 MiB except the original's stack frame: **0 bytes differ**. The function's writes: game[0] (0x0029F350, 2 -> 1), the purchase state back in save order (h+0x40 and its copy 0x002A0320: F-5E bit set) and the credits 0x002A0D30 = 109,000.
- End to end: the port replays the run (`--pad-script 1200:start,1500:down,1560:cross,1800:cross,1950:cross,2150:up,2200:cross,2600:start,3100:start,3600:start,3800:down,3830:down,3900:cross,4100:cross,4600:cross,4800:cross,5200:triangle --dump-ram-at 233F10`) and reaches the commit with the same purchase state, owned list, selection (h+0xA0..+0xF3), credits, checksum, save copies, game state and campaign selection as PCSX2. Other differences come from the replay's different timing, not from game logic: the hangar timer h+0x164 (7.2 against 18.05 s), viewer camera and object animation, and stale briefing-movie chunks in freed heap (the pad script let the briefing movie run longer before Start). No expected-list entries were added for this run: it is not a fixed-input checkpoint.
- Shop screen after leaving (`s4_shop.png`, BUY highlighted) against the port at vblank 6360 with `--blend-fields`: 0.26/2.20/3.61, 0.44 % over 16, 0.04 % over 32 (pulsing BUY glow, help line typing).

### S4-5. VS mission select (PCSX2 run C)
Port: card S, `--pad-script 1200:start,1500:down,1530:down,1560:down,1620:cross,2300:right --dump-ram-at 23C708`.
- RAM at the first `MissionSelectRun` 0x0023C708: **1 unexplained range** before classification: the TUTORIAL item scale of the main menu object (+0x3AC) still easing from 2 to 1 on PCSX2 (the User's Down presses were faster than the script's 30 frames); added to `ramdiff-expected-s4.csv` (input). 0 unexplained after.
- Screens with `--blend-fields`: `s4_vs.png` against vblank 2180: 0.26/1.45/2.10, 0.21 % over 16, 0.06 % over 32; `s4_vs_right.png` against vblank 2540: 0.21/1.40/1.99, 0.02 % over 16. Left: pulsing button icons, crosshair position, cursor box (animation phase). Same layout, texts, thumbnails (VS STAGE 01/02) and slide animation.

### S4-6. Hangar performance (User report 2026-10-16: about 5 fps)
Measured with temporary stage timers and an in-process sampling profiler (both removed), F-5E hangar, development build. Before: ~200 ms per presented frame (catch-up of ~3.4 ticks): VU1 HLE 15 ms, GIF processing 22 ms, Vulkan recording 16 ms per tick, the last almost all inside the validation layer. Changes (none changes guest RAM or game logic; all verified bit-exact as stated):
| Change | Where | Check |
|---|---|---|
| VU1 micro arithmetic in the host's round-toward-zero mode (MXCSR RZ + FTZ + DAZ, `vu0::micro::ChopScope`, `#pragma fenv_access(on)`) instead of double-precision chop emulation | `vu0_math.h`, `vu1_programs.cpp` | in-run A/B against the former code: every program run of two whole sessions (title to both hangars), output GIF bytes and VU memory identical |
| matrix constants of the model programs read once per strip (re-read per vertex when the backface history at VU 0/1 overlaps them); XGKICK appends a packet at once; `Vu1State::F` inline | `vu1_programs.cpp`, `vif1.h` | same A/B |
| VIF UNPACK fast path (no mask, no ROW mode, not V4-5) | `vif1.cpp` | in-run A/B against the generic loop, both sessions: identical VU memory |
| decoded-texture cache keyed by a content hash of the pages instead of their write generation (the hangar cycles textures through shared GS memory every frame: 723 decodes per frame before, 2 after) | `gs_memory.cpp`, `gs_state.cpp` | same-vblank screenshots identical |
| CLUT loads skipped when the CLUT buffer already holds that palette (raw source bytes compared), and a cache of 8 recent 8-bit palettes (the two passes alternate): 12,420 reloads of 256 entries per frame before | `gs_state.cpp` | in-run self-check (full decode on every hit) over a whole session: 0 mismatches |
| draw state marked dirty only when a register value changes | `gs_state.cpp` | same-vblank screenshots identical |
| GIF step buffers recycled (a hangar step is ~3 MB; it was regrown every tick) | `libgraph.cpp`, `renderer.cpp`, `sce_host.h` | — |
| redundant `vkCmdSetScissor` / `vkCmdBindPipeline` / `vkCmdPushConstants` skipped | `gs_vulkan.cpp` | — |
| Vulkan validation opt-in: `--vk-validation` (development builds; User decision 2026-10-16) | `command_line.cpp` | unit test |
After, per tick: game 5.4 ms (VU1 2.5), GIF 3.3 ms, recording 2.0 ms: a steady 60 fps with ~6 ms of the 16.7 ms frame left. With the validation layer the recording still takes ~11 ms per tick (about 1 µs per Vulkan call; the model and reflection passes alternate per strip with different blend states, so their draws cannot be merged without reordering) and the hangar stays at ~13 fps: validation is therefore off by default and enabled with `--vk-validation`.
Screenshots of different runs cannot be compared for this purpose: host timing reaches the game state (CD and stream timing, `../open-questions.md`), so the camera orbit is at another phase.

### S4-7. The remaining differences (2026-10-16)
- **Input timing (most of them).** The frame counter (0x0029F3A8, counted from boot) was 934 frames higher in the port at every checkpoint: run A's buttons came much sooner after the title screen than the pad script's (the title screen is ready at frame 597 in PCSX2). The menu music therefore played ~15.6 s longer, i.e. ~6-8 more VAG refill reads (one per ~2.4 s), which moved the CD queue-0 tickets, VAG slot 4's ticket and the briefing reader's ticket, and the SS01/briefing movie state words. With the presses moved so that the frame counter matches (title card 1232, briefing 5094, campaign 9925, hangar 10861), those differences disappear; one interleaving of a VAG refill with an SMV chunk read is left (classified `timing`).
- **SOUND transfer ticket (port fix).** Client +0x2C (0x0033E584, the last finished IOP->SPU transfer ticket) was one behind in PCSX2 after every movie. SOUND.IRX: `main_TransBd` (0x135C) starts the transfer and `handler_DmaInt` (0x1300), the SD transfer interrupt, is the only writer of the finished ticket. The movie-audio stop `{src 0, dst 0}` is `sceSdBlockTrans` in mode 2: no DMA, no interrupt, so its ticket is never reported. The SOUND stand-in now does the same (`iop_services.cpp`; unit test "SOUND stand-in: transfer and voice-status records").
- **libmpeg internals (`sdk`).** 0x003FD940 and 0x004CC550-0x004CC55B are read and written only inside libmpeg (Ghidra: `FUN_00276FC8` and its interrupt-handler code at 0x00276E9C-0x00276F84); 0x00FD8300-0x00FD8AFF is libmpeg's decoder block at the start of the briefing movie's work area; the briefing reader's sceMpeg frameCount and flags are the S3 rules at another address. The port's libmpeg works at the call boundary (libmpeg2) and keeps none of this; the game never reads it.
- **0x00410D08 (port fix).** Labelled `g_CdFsState+0xA0` by the tool; it is the SPU RAM limit for sound bank registration (`func_00218A00`). SOUND id 6 (reverb mode) replies with min(ESA core 0, ESA core 1) read with `sceSdGetAddr` right after `sceSdSetEffectAttr` (SOUND.IRX 0xEB8-0xF14), which writes the ESA registers synchronously (LIBSD 0x0900, setter 0x1BA8). The PCSX2 SPU2 state (`SPU2.bin`, core 0 ESA/EEA at 0x211F60, core 1 at 0x213F20, in 16-bit units) shows core 1 in mode 2 at 0x001FE0C0 and core 0 in mode 0 at 0x001DFFF0 in runs 06, 09 and 10, so every reply is 0x001DFFF0; the old 0x001DB7C0 came from core 0 in mode 3 in `scenestep_first_2.p2s`. The stand-in had a wrong mode-2 size (0x24840, derived from that state) and a "read before the command" rule; it now applies the command first and uses libsd's work-area table (LIBSD.IRX 0x4148: mode 2 = 0x1F40 bytes, mode 3 = 0x4840). Unit test "SOUND stand-in: reverb mode reply…".

## 2j. Slice 5 prerequisites

### S5-P1. Determinism (2026-10-16)
- Finding: the only host input that reached guest RAM in the S4 flow was the live pad (analog bytes 0x004121F4/0x00412214 differed between parallel runs: the script was overlaid on the real controller, sampled once per presented frame).
- Change: per-tick pad latching with ticks released one at a time (`main.cpp` `TickInputFeed`), validation runs without live devices, `--record-input` / `--replay-input` (`src/platform/input_log.*`), `--hash-log` (`src/platform/hash_log.*`) with `tools/research/hashdiff.py`, `--stress-host`.
- Results (pad script to the S4 title card and on to vblank 3000, Xbox controller connected): calm run vs `--stress-host` run identical at all 3000 vblanks; recorded run vs its replay identical at all 3000 vblanks. S1 sub 7 and the S4 title-card and hangar checkpoints still 0 unexplained.

### S5-P2. Input-recording calibration (2026-10-17)
- Data: `s5_cal.p2m2` (power-on recording, 2098 frames, `../pcsx2/sessions.md` run 11): Start held in records 1001–1002, X in 1722–1723 (and 1830–1841, after the confirm); breakpoints 0x0022D748 at record 1002 and 0x0022D880 at record 1723; `frame_count` (0x0029F3A8) in the two states 837 and 1558 (difference 721 = 1723 − 1002, so one count per record).
- Port: `--replay-p2m2 s5_cal.p2m2 --dump-ram-at <bp>` with offsets 165 and 153. Offset 165 reaches both breakpoints with `frame_count` 825 / 1546 (12 low); offset **153** gives 837 / 1558, equal to PCSX2. Both checkpoints with offset 153: 0 unexplained ranges (`ramdiff.py --expected` with the S1, S2a, S2b files; remaining timing ranges are the boot-time counters already listed there).
- Result: `--p2m2-offset 153` for power-on recordings: port tick *t* reads record *t* + 153. The 153 frames are PCSX2's BIOS and boot before the game's first vblank minus the port's 12-vblank head start before `frame_count` starts; the pad path adds no extra frame on either side (both accept Start on the record that holds its second pressed frame).
- Limit: valid up to the first load whose length differs between PCSX2 and the port (S4: the briefing map starts 66 frames earlier in the port). Gameplay recordings still need the anchor method of `validation-strategy.md` §3.2, with this offset applied after the anchor.

### S5-A. First mission frames with `--freeze-sim` (2026-10-16, port only)
- Run: card S copy, `--freeze-sim --pad-script 700:start,850:cross,950:cross,1166:start,1438:cross,9918:start,10844:cross,11300:cross,11500:cross,11700:cross` (S4 script to the hangar, X on the aircraft, X on the weapon, X on the loading screen's PRESS START BUTTON). New game mission 1 with the F-4E is mission id 0x25: after sub 8 the scene goes to sub 0x13 (opening sequence), which `--freeze-sim` also covers.
- Correction (2026-10-17, S5-A2): the frame described here is the opening's world (mission id 0x25), not mission 1.
- Visible: the loading screen (01 "SITTING DUCK", objectives, PRESS START BUTTON) and, in the mission, the sky dome gradient over the whole 512 × 448 frame. With the terrain program T and programs A/E implemented (same day) the frame also gets the terrain (333 fans per frame from the start camera, high above the map: a uniform, strongly fogged surface below the horizon) and no "not reimplemented" warning remains. Whether the terrain's texturing and fog match cannot be judged from this camera (the opening sequence's camera is not run under `--freeze-sim`): needs the S5-A GS dump. Effects, HUD, radio and sequence drawers are logged as not translated.
- Bugs found on the way: `func_00138C10` read the `.vudata` qword count as a word (0x60000108 instead of 0x0108), which turned the `ref` tag into an `end` tag and cut the VIF1 chain after the terrain table (no sky, no world); the renderer's frame-as-texture source picked the menus' 640-wide FBP 0 target (`../platform/gs-subset.md`). Both fixed.
- New renderer warnings in the mission (features still missing): destination alpha test (DATE), PABE, COLCLAMP 0, CLUT CSM2, texture PSM 0x5, a reserved PRIM type 7 kick, local→host transfer, a GS blend equation not mapped, VIF1 UNPACK fill mode (WL > CL) approximated, VIF1 code 0x08.
- Not yet done: RAM checkpoints 0x0010A230 / 0x0010BD68 against PCSX2 (`OnGuestCheckpoint` added at both; batch S5-A), GS-level comparison with the PCSX2 gameplay dump.

### S5-A2. Mission 1 load against PCSX2 (2026-10-17)
- Flow found on the way: New Game mission 1 with the default aircraft is mission id **0x25**, an in-engine special (kind 4) with its own load: subs 0–8 load the opening's world, sub 0x13 plays the carrier sequence, and its end (`UpdateInEngineSequence` 0x00177058: `game[0] = 2`, `game[0x27] = 1`, `game[0x2A] = 0`, `RequestScene(2)`) loads mission 1 proper, which reaches sub 0xA (0x0010A230, the PCSX2 state `s5_m1_start`). The narration screen the User saw after the carrier scene is mission 1's loading screen. The frozen frame of S5-A was the opening's world, not mission 1.
- Port run: S5-A pad script plus `11850:start` (`--freeze-sim` keeps the original skip rule in sub 0x13, `command-line.md`), `--dump-ram-at 0x0010A230 --log-allocs`, twice with `--heap-fill 0x00` / `0xFF`; checkpoint at vblank 11978.
- **Allocation exact:** work-heap offset 0x019C5180 (98.4 % of 0x01A318A4) and the retry mark S+0x1664 = 0x012F9480, both equal to PCSX2: every allocation size and its order match for the whole mission 1 load (3,108 live allocations, 27 MB).
- **Heap contents:** 4.79 MB were never written since the scene change (leftovers; masked). Of the written bytes, 8,145 differed in 19 allocations: the AI states' byte +0xF5 and effect pool 19's +0xFC (and one pool-11/12 field), all from `NextRandomFloat`. The random cursor (0x00411780) was 0x22E1 in the port and 0x10A45 in PCSX2: the opening sequence PCSX2 played consumed reads the skipped one did not. Started at the cursor PCSX2 must have had (0x10A45 − 0x1152 reads of the load = 0xF8F3; temporary experiment, removed), **every random-derived byte matched**; the remaining 10 bytes are timing: the double-buffer parity (S+0x1540 bucket pointers, `AllocDoubleBuffer` +0x224), the loading-screen frame counter S+0x166C (PCSX2 350 frames, port 95: CD latency) and S+0x1684.
- **Static areas:** 118 unexplained ranges remain, from the opening's history: the sequence state (0x00410B18, 0x00420730), the terrain holder's per-frame draw state in `.data` (holder 0x002A3110 = *0x00410694: +0x60..+0x6C world window and +0x11C camera cell from the per-view set-up `func_0013A110`, +0x13C draw class, +0x150.. strip bytes, all from the last frame the opening drew), image-like rows at 0x00365F00–0x00385000, and dead stack frames. Also 31 bytes of the holder depend on `--heap-fill`: the frozen opening drew with view-camera words the skipped sequence camera (0x00178DF8) never set. Missions 3 and 4 (no opening; PCSX2 batch S5-A run 2) are the clean check for these areas.
- Tools: `tools/research/heapdiff.py`, `--log-allocs`, `--heap-fill`.

### S5-T2. Mission 1 GS dump through the port's renderer (2026-10-17)
- PCSX2 dump `s5_m1_frame` (zstd-compressed by PCSX2; unpacked for research with Git for Windows' `libzstd.dll` into `out/gs/`, not committed). Statistics (`gsdump.py`, `gifstrips.py`): the port's own mission 1 frame has the same four 3D groups (terrain T8 0x2900 fans, the second context-2 pass with 0x2500, the map GIM 0x3A00, the far triangles with 0x2500), the same primitives and the same mean terrain colour (90 89 90); counts and screen ranges differ with the camera (the port's frozen start camera against a frame 2 s into the flight).
- `--play-gsdump` on the dump first drew the sky, sea and HUD lines but no HUD text and no cloud texture. Causes found and fixed (`../platform/gs-subset.md`): local memory in row-major block order (the HUD font is uploaded as T8 and read as T4), REGLIST PRE ignored, textures read from frame buffers that a transfer had overwritten. After the fixes the frame matches PCSX2's screenshot (HUD text, cyan colours, clouds, MISSION START, radar) except a dark speckled patch near the bottom centre (DATE draws, not implemented).
- Regression: the six S5-T1 screenshots (title to hangar and the opening) are identical to before; the port's own mission 1 frame now shows the island's terrain texture, the sea and the cloud shadow. Open: a black striped band at the top left of that frame (a terrain cell or object drawn wrongly; to investigate).
- Fast-forward cost of the column order: about 2 s over the 11,990-vblank route (CT32 uploads write pixel pairs).

### S5-T1. `--fast-until` (2026-10-17, port only)
- Runs: the S5-A route and pad script (card S copy, `--freeze-sim`), `--hash-log`, screenshots at vblanks 1300, 3000, 6000, 10000, 11000, 11950, exit at 12000: once at normal speed, once with `--fast-until 11990`.
- Result: the RAM hash logs are identical at all 12000 vblanks (`hashdiff.py`); the six screenshots are identical in size and pixels (the normal run took the last one at 11951, a frame with two ticks; the frozen world gives the same picture).
- Time to vblank 11990: about 25 s without `--hash-log` (3 min 20 s at normal speed); the briefing movie (MPEG decode into guest RAM) and the flight frames (about 5.5 ms per tick on the CPU) take most of it.
- Found on the way: what a frame shows depends on how ticks are grouped into frames (the briefing alternates layers between ticks), so the last 60 ticks before a screenshot run one per frame; and the present fallback took its size and its pixels from different FBP-0 targets, so at normal speed the briefing was shown cut to 512 columns (`../platform/gs-subset.md`, fixed).
- Tool: `tools/research/sample_profile.py` (sampling profiler, dbghelp) used to find the cost.

### S5-A3. Missions 3 and 4 against PCSX2; mission renderer features (2026-10-17)
Reference: S5-A run 2, missions 3 and 4 (`../pcsx2/sessions.md` run 12): states `s5_m3_start`, `s5_m4_start` at 0x0010A230, GS dumps `s5_m3_frame`, `s5_m4_frame` (zstd again, unpacked into `out/gs/`), loading and frame screenshots.
- **Routes in the port** (card S copy, `--freeze-sim`): mission 3 `--pad-script 1200:start,1500:down,1560:cross,1800:cross,1950:cross,2150:up,2200:cross,2600:start,3100:start,3600:start,4200:cross,4700:cross,5000:cross,5400:cross` (LOAD file 1, CONTINUE, Start through title card, briefing and map, START MISSION, X on aircraft and weapon, X on PRESS START BUTTON); mission 4 the same with `1650:down,1680:down,1710:down` before the file is picked (file 4, MISSION 04 / BLOCKADE). Both reach 0x0010A230 at vblank 5434 with the game state of the User's runs (mission id 3 / 0x10, aircraft 2, colour 0, weapon 0, mode 1, difficulty 3).
- **Heap** (`heapdiff.py --mask`, fills 0x00/0xFF): mission 3 3,171 live allocations (27.1 MB), mission 4 4,345: allocation sizes, order and addresses equal; differing bytes only in the packet double buffers (loading-screen packets, frame phase), the flight scene's loading counters (+0x166C..+0x1687) and effect-block records that add the effect clock (block +0x6249C).
- **Effect clock (leftover read by the original).** `InitEffects` reads the clock before anything in the scene writes it (`../subsystems/mission-flow.md`), so record start times include whatever the memory held: PCSX2 0x80000000 (mission 3), 0x80312B1F (mission 1), 0 (mission 4); the port 0x808D5853 (mission 3) — RGBA pixels with alpha 0x80 left by the briefing movie's picture buffer, which depend on the moment the briefing was skipped. Listed as `leftover` for the mission 3 layout. An exact match needs the same skipped picture (pad timing and IDCT rounding, S3-3).
- **Static areas:** the ranges left in S5-A2 (terrain holder draw state, sequence state, image rows) do not occur in missions 3 and 4: they were the opening's history. Remaining differences, all classified in the new `ramdiff-expected-s5a.csv`: briefing state at the skip and the movie audio feeder's fade (input timing), VAG slots 2/4 and the VAG command queue (the loading screen's length: CD latency), libkernl internals, newlib's stdio buffer after the work heap (debug printf text), deeper dead stack frames. **0 unexplained ranges** for both missions outside the scene heap, with the expected lists S1, S2a, S2b, S3, S4, S5a.
- **Black screen before gameplay** (User's `*_loading_2.png`): the loading screen fades to black after Start, 0x0010A230 runs on the black frame and the mission fades in over about 30 frames (port: Start at 5400, black at 5430, checkpoint 5434, picture from 5440). The PCSX2 screenshot was taken while paused at the breakpoint.
- **GS dumps through the port's renderer** (`--play-gsdump`, compared with the User's screenshots, `imgdiff.py --blend-fields`). New tool `tools/research/gsdraws.py` (every draw with its state and screen box). Missing features found and implemented (`../platform/gs-subset.md` 2026-10-17): fog (FGE), TEX1 filter choice with trilinear mipmaps (MTBA), DATE, AA1 blending, frame buffers read and written in other formats or widths (compute conversion and target sync), a frame buffer drawn earlier in the same frame as texture source, presented height at most 448 lines.

| Dump | before (mean R/G/B, % over 16) | after |
|---|---|---|
| mission 1 | 9.7 / 10.9 / 14.2, 40.7 % | 1.5 / 1.8 / 2.0, 1.0 % |
| mission 3 | 18.8 / 18.0 / 16.4, 41.8 % | 4.0 / 4.4 / 4.3, 5.8 % (camera moved between the dump and the screenshot: edge differences only) |
| mission 4 | 11.9 / 12.8 / 16.0, 18.2 % | 1.2 / 1.5 / 1.8, 2.2 % |

- The black striped band of the port's own mission 1 frame (S5-T2) is gone (it was the island drawn without fog, mipmaps and the cloud-cover texture). The S5-T1 screenshots at 1300, 3000, 6000, 10000, 11000 are pixel-identical to before; 71 unit tests pass.
- Left: the HUD's AA1 edge coverage, the GS's integer blend rounding, PABE, COLCLAMP 0, CSM2, partial FBMSK (none visible in these frames).

### S5-T3. `--mission` and the parallel regression suite (2026-10-17, port only)
- `--mission <n>` (`command-line.md`): all 18 campaign missions started from card S with `--freeze-sim --pad-script 800:cross,...`: 17 reach 0x0010A230 at vblank 834 with the expected game state (mission id from `GetMissionId(0, n)`, aircraft 2, mode 1, difficulty 2, entry kind 1) and draw their world from the start camera with no renderer warning. Mission 12 (id 0x0A) stops at its first sub-state: the scripted sequence 0x0010B500 (block C) is not translated yet (logged as an unregistered indirect call).
- `tools/research/runmany.py` with `tools/research/regress.json` (24 jobs: front-end route screenshots, the mission 1/3/4 routes with their checkpoints, calm vs `--stress-host` hash logs to vblank 6000, the 18 missions): 111 s with 8 instances in parallel on the development PC. A second run is pixel-identical to the saved baseline for every screenshot, and the calm and stressed hash logs are identical at every common vblank.

## 2k. Internal resolution, 16:9 and code clean-up (2026-10-19)

Reference: the User's Slices folder (S1-S5 PCSX2 screenshots and GS dumps). Tools: `tools/research/full.json` + `runmany.py` (logic oracle), `pcsx2_compare.json` + `pcsx2_compare.py` (screens).

### IR-1. Logic oracle: RAM and hash logs at every scale
- Baseline saved from the build before any change of this session (`out/runs/baseline_full`, not committed): 30 jobs without `--freeze-sim` (S1 card S / card E / no card, S2a, SS01, the S4 front end to the hangar, VS select, the shop commit, the mission 3 and 4 routes, a replayed gameplay run on mission 4, the mission 1 opening, all 18 missions): 47 screenshots, 27 RAM checkpoints, 5 per-vblank RAM hash logs (up to 12,000 vblanks).
- A second run of the unchanged build: identical RAM and hashes; screenshots identical except where the shot was taken at normal speed (one vblank late), so every job now uses `--fast-until` up to its last shot.
- Final build at internal scale 1: every RAM checkpoint and hash log identical, every screenshot identical (the loading fade may differ by up to 6 levels with host presentation timing, `--tolerance 8`). At 2, 4 and 8: every RAM checkpoint and hash log identical to the 1x baseline, the same renderer warnings, no errors. **The internal scale changes no guest state.**
- Known, dev tool only: with `--fast-until` the frames before the 60-tick settle window are not drawn, so which frame buffers the renderer has seen depends on host timing; a shot in the mission 1 opening can show the 224-line display buffer (2 of 6 runs). RAM is unaffected; normal-speed play draws every step.

### IR-2. Screens against PCSX2 (Slices folder)
Best match per reference over a vblank window (`pcsx2_compare.py`); at N > 1 at the 1x best vblank, with the port's N x N box average (`native_<vblank>.png`). Mean R/G/B difference:

| Screens | 1x | 2x | 8x |
|---|---|---|---|
| S1 splash 0/1, card messages (5) | 0.01-0.09 | 0.4-0.9 | same as 2x |
| S2a title, menu, NEW GAME (3) | 0.2-0.7 / 1.4-1.6 / 1.9-2.7 | 1.0-1.6 / 3.1-3.6 / 4.2-5.1 | same |
| S2b pages incl. LOAD, CONTINUE (7) | 0.1-0.5 / 1.5-2.0 / 2.0-3.2 | 0.6-3.7 / 3.8-6.3 / 5.4-7.7 | same |
| S2b 16:9: SCREEN RATIO, menu, CONTROLLER SETUP (3) | 0.3-0.5 / 1.9-2.6 / 2.9-5.0 | 1.2-4.9 / 3.9-8.4 / 5.7-10.6 | same |
| SS01 (2), mission 1 movie | 0.4-0.9 | 1.1-4.0 | same |
| S4 title card, campaign, shop, VS (5) | 0.1-0.3 / 1.1-2.1 / 1.7-3.4 | 0.9-2.7 / 2.6-5.0 / 3.5-6.1 | same |
| S4 briefing map | 2.1 / 3.1 / 3.5 | 4.3 / 6.1 / 6.7 | same |
| S5 loading screens m3/m4 | 1.2 / 4.1 / 5.6 | 1.2 / 3.8 / 5.1 | 1.0 / 3.5 / 5.0 |
| S5 GS dumps m1/m3/m4 (`--play-gsdump`) | 1.2-4.0 / 1.5-4.4 / 1.8-4.3 | 1.7-4.4 / 3.2-5.7 / 3.1-5.5 | same |

- 1x equals the earlier slice results (S1-2 to S5-A3): nothing regressed. The G/B bias is the open menu-filter rounding question (S2a-4).
- Not matched by timing (as before): the hangar (orbit camera phase, 10-12 levels) and the briefing map's second frame (map animation phase, `s4_map`, 2-10 levels).
- At N > 1 the means rise by 1-5 levels: lines one native pixel wide are N host pixels (the average blurs their edges less), the menu filter's feedback blur samples the scaled buffer (the fine dot pattern averages out), and movies come from the FSR picture. 2x, 4x and 8x give the same numbers within 0.3, so the scaling is consistent. Visually checked: flight HUD and terrain, menus, CONTROLLER SETUP, SS01 at 2x/4x (no seams between movie columns or atlas cells).

### IR-3. 16:9 (the game's SCREEN RATIO option)
- Before: `Renderer::SetDisplayAspect` was never called; the port always presented 4:3.
- Now the main loop reads ctx flag 0x10000 after each frame. Run (card S): LOAD slot 1, OPTIONS > SCREEN SETTINGS > SCREEN RATIO > 16:9 (`pcsx2_compare.json` run `s2b_169`): the log shows `display aspect 16:9` when the option is confirmed; the output pass then fits a 16:9 rectangle (`FitAspect`). The anamorphic menus and CONTROLLER SETUP match PCSX2 (IR-2).

### IR-4. Performance (development build, RX 9070 XT)
- Sampling profile of mission 4 gameplay: texture decoding (`GsState::DecodeTexture`, one call through `PixelAddress`/`ReadPixel` per texel) was the largest item, 18 % of all samples (the game simulation about 5 %). `GsMemory::ReadRect` (one loop per format, inline addressing) brought it to 8 %; unit test `ReadRect gives the ReadPixel values for every format`, and the 1x screenshots are pixel-identical.
- Normal speed, hangar route to vblank 12,000: 0 dropped ticks at 1x and at 8x. Mission 4 with a replayed recording: the same 23-24 dropped ticks at 1x and 8x, right after the fast-forward, so not from the scale.

## 2l. Opening fixes, port menu features, caches and leaks (2026-10-20)

User requests of 2026-10-20 (the opening's black aircraft and corrupted frames; performance and caching; clean-up; GRAPHICS page; Modern control types; settings rules; memory leaks). Port features as built: `port-features.md`.

### OP-1. Mission 1 opening: corrupted frames (game-code translation error)
- Seen: about a third of the opening's frames black with a strip of garbage pixels near line 300, and coloured speckles over the sea in the last carrier shots. `gsdraws.py` on a `--dump-dma-at` capture: after a deck-crew strip, 1,747 garbage sprites (TEX0 TBW 53, PSM 0x6); the GIF stream lost sync there, so the field copy at the end of the frame never came.
- Cause: `func_002206C0` (sequence character parts' GS register list) writes the A+D GIF tag at part +0 (`sd v0,0(a0)` at 0x002208AC, NLOOP = count − 1); the translation had it at +0x70. `func_002238E0` copies the list from +0, so the GS read the stale qword at +0 as a tag.
- After the fix: no garbage draws, every frame of the opening presented, the deck crew textured. Guest RAM changes where the opening runs (the character parts and the packet buffers that copy them, heap 0x5F0000–0x630000 and 0xDB0000–0xDD0000): the translation now writes what the original writes.

### OP-2. Mission 1 opening: black F-4E (renderer)
- The player aircraft's texture is T8H at block 0xE00 = FBP 0x70 × 32; the opening draws RGB into FBP 0x70 with FBMSK 0xFF000000, so the renderer took the aircraft's texture from that target, whose top byte never held the indices (`../platform/gs-subset.md`, "Textures in the bits a frame buffer never writes"). Vertex colours (mean 118 per channel) and the light set (ambient 0.48) were fine.
- After the fix the F-4E has its blue camouflage in every carrier shot; no other screen of the regression suite changed (OP-4).

### OP-3. Performance, memory and leaks (development build, RX 9070 XT)
- Sampling profile, mission 4 with the replayed recording, normal speed: main thread 19.8 % → 15.9 % of samples, GIF processing 15.2 % → 10.6 %, `DecodeTexture` 8.4 % → 5.2 %. Cause: the terrain textures (T8 512 × 512 with 3 mip levels at 0x2900) are rebuilt every frame from tile uploads; every change re-decoded the whole texture (11,839 decodes in 4,500 vblanks). Now only the 256-byte blocks written since the last decode are decoded again; unit test `re-decoding only the written blocks equals a full decode` (T8, T4, CT32, unaligned texture, patches across blocks, outside the texture, and a mostly rewritten one).
- Memory: the decoded-texture cache was bounded by 4096 entries only, and the terrain made a new 1.4 MB version almost every frame: private memory climbed to about 6 GB within a minute of flight before the cache emptied. Now bounded at 256 MiB as well: 400–740 MB over the same run (unit test `the decoded-texture cache stays within its byte budget`).
- Dropped ticks in that run: 35–37 → 0. The backend's per-frame lists (texture placement, uploads, vertices) are reused instead of allocated every frame.
- Leaks: no raw `new` / `malloc` in host code; every file opened is closed; a session with `--vk-validation` (menus, the GRAPHICS page changing the internal resolution, clean exit) reports no Vulkan error, including no objects left at device destruction. New Debug-build tests with the CRT debug heap (`tests/test_leaks.cpp`): settings store and GS state allocate nothing that outlives them.

### OP-4. Regression suite
- `full.json` against the baseline of 2026-10-19 (`out/runs/baseline_full`), 1x: 27 of 30 jobs identical (screens, RAM checkpoints, hash logs). Different, all explained: the opening (OP-1 and OP-2: screens and hash log), `s4_frontend` (its last 268 vblanks are the opening: same heap ranges; its opening screenshot shows the fixed aircraft), `m3_route` (one pixel column of the loading fade, at most 6 levels, also in the 2026-10-19 runs: host timing, `--tolerance 8` in IR-1).
- New baseline from this build: `out/runs/baseline_s9` (not committed).
- At 8x against the new baseline (`--shots-only`): every hash log and 29 of 30 RAM checkpoints identical. The other one, `s1_cardE`, is the only job without a pad script, so it reads live devices: 8 bytes of pad 1's raw and converted analog values (0x004121F4, 0x00412214; a connected gamepad's stick at 0x78 / 0x7D instead of the centred 0x7F, the input class of `ramdiff-expected-s1.csv`), not the scale.
- PCSX2 screens (`pcsx2_compare.py`, runs `s2b` and `s2b_169`): every S2b page as before (IR-2) except the two pages that changed on purpose: SCREEN SETTINGS 0.33 / 2.19 / 3.43 (the extra GRAPHICS line) and CONTROLLER SETUP 1.41 / 2.67 / 3.32, 16:9 2.07 / 4.14 / 6.34 (the two extra squares, the longer CONTROL TYPE name and, with Modern, the swapped shoulder rows).

### OP-5. Port menu features (port only)
- GRAPHICS (SCREEN SETTINGS, after HUD UNIT): opened with card S, rows typed out as the BRIGHTNESS view, values changed with left/right; ACCEPT applied 4x at once and wrote `internal_scale: 4` to `settings.json`; CANCEL left the file unchanged; reopening shows the stored values and the list returns with the cursor on GRAPHICS.
- CONTROLLER SETUP: four CONTROL TYPE squares; left from NORMAL (MODERN) runs EASY (CLASSIC), NORMAL (CLASSIC), EASY (MODERN) and back, the game's type bit stepping as over the original two squares; the table shows the swapped shoulder rows with Modern; ACCEPT wrote `modern_controls: [false, true]`. Unit test for the swap (`modern control types: L1/L2 and R1/R2 swapped, buttons and pressures, nothing else`).
- `settings.json`: a file with an invalid value, an unknown key, another version or broken JSON is rejected and replaced with the defaults (unit tests in `test_settings.cpp`).

## 2m. Settings, texture residency, sprite edges at N×, frame-rate overlay (2026-10-20)

User requests of 2026-10-20 (second batch): texture loader, settings defaults and immediate application, the frame-rate overlay, tests of the graphics and controller settings and of saving, the deck crew of the mission 1 opening. Port features as built: `port-features.md`. Runs under `out/cc/` and `out/runs/cc_x1`, `cc_x4` (not committed). Display of the test machine: 3840 × 2160 at 60 Hz (so the defaults are fullscreen with v-sync on), RX 9070 XT.

### ST-1. GRAPHICS page in the menus (normal speed)
- Route with card S: OPTIONS > SCREEN SETTINGS > GRAPHICS (`--pad-script` …2550:cross,2750:up,2900:cross; up from the first item wraps to GRAPHICS). Then every row was changed, CANCEL, every row changed again, ACCEPT, reopened, DEFAULT, CANCEL.
- Each change applied at once (log: `presentation: …` per change, `window …`, `swapchain …, present mode …`): fullscreen 3840 × 2160 ↔ windowed 1280 × 960; v-sync on = FIFO, off = IMMEDIATE (this driver has no MAILBOX in that mode); internal scale 1 → 2 → 3; filter sharp → smooth.
- CANCEL restored the values the page opened with (all four at once); ACCEPT wrote `settings.json` (`windowed`, `vsync false`, `frame_cap 240`, `internal_scale 2`, `smooth`); DEFAULT applied the display defaults (fullscreen, v-sync on, 1×, sharp) and the next CANCEL went back to the saved values.
- With v-sync on, the FRAME RATE CAP row shows "V-SYNC" without squares and the cursor skips it; with v-sync off it shows its value and squares (screenshots 3150, 3300).
- Presented rates per period (`previous presentation: …` lines): v-sync 60.2 fps; cap 360: 355 fps; cap 240: 239.7–240.2 fps; 3× with cap 240: 230.8 fps.

### ST-2. Combinations during gameplay (normal speed)
Mission 4 with the replayed PCSX2 recording from vblank 1500; `settings.json` rewritten every 7 s while the port ran (the watcher reloaded each version within a second).

| Combination | Presented | Ticks dropped |
|---|---|---|
| defaults (fullscreen, v-sync, 1×, sharp) | 59.0 fps (period includes the fast-forward) | 0 |
| fullscreen, v-sync, 4×, smooth | 59.2 fps | 5 |
| fullscreen, v-sync, 8×, sharp | 54.8 fps | 35 |
| windowed, cap 144, 2×, smooth | 142.1 fps | 0 |
| windowed, cap 360, 8×, sharp | 323.4 fps | 31 |
| fullscreen, cap 60, 3×, smooth | 59.3 fps | 1 |
| windowed, v-sync, 6×, sharp | 57.4 fps | 16 |
| fullscreen, cap 360, 1×, sharp | 357.1 fps | 0 |

The dropped ticks are one hitch at the switch to a higher internal scale (render targets rebuilt, grown on the first frames with a device wait), not a lower sustained rate: 385 presented + 35 dropped = 420 = 7 s × 60. 8× windowed still presents 323 fps. Session total 88 dropped ticks, all at switches.

### ST-3. Controller settings and the save data
- CONTROLLER SETUP (card S, after LOAD): NORMAL (MODERN) → EASY (MODERN) → NORMAL (CLASSIC); the button table follows (Modern: L1 YAW LEFT / L2 THROTTLE DOWN; EASY: the yaw rows "------"; Classic: the original rows); REVERSE ON swaps PITCH UP / PITCH DOWN. ACCEPT wrote `modern_controls: [false, true]` to `settings.json`.
- Then CONTINUE > campaign menu > SAVE > slot 01 > overwrite YES: "Save complete". The card file `BASLUS-20152AC04` differs from the card before in exactly two places: player 1's controller word (slot 1 +0x12B8: 0x0008 → 0x000A, REVERSE bit) and the checksum at +0x0C, which is valid. The Modern flag never reaches the card.
- A new session with the same card and `settings.json`: LOAD, then CONTROLLER SETUP shows NORMAL (CLASSIC), REVERSE ON.

### ST-4. Texture residency (performance)
Renderer summary at exit (averages per drawn frame, `--fast-until` part included), same build with residency switched off for the "before" column (a temporary switch, removed):

| Run | Recording ms (before → after) | Textures uploaded per frame | Bytes uploaded per frame |
|---|---|---|---|
| mission 4 replay, 1× | 0.26 → 0.10 | 74 → 0.13 whole + 0.01 in place | 108 KiB → 1 KiB |
| mission 1 opening, 1× | 1.53 → 1.39 | 14.2 → 0.19 | 3,165 KiB → 18 KiB |
| mission 1 opening, 4× | 1.54 → 1.36 | 14.4 → 0.19 | 3,103 KiB → 18 KiB |

GS processing (decoding the game's GIF stream and textures) is unchanged at 0.48 ms (mission 4) and 3.7 ms (opening). In the opening most of that is the game's own per-frame image uploads into GS memory (`GsMemory::WriteImage`) and primitive building; no atlas reset in any run. Unit test: `re-decoding only the written blocks equals a full decode` now also checks that the changed rectangles applied to the previous decode give the new one.

### ST-5. Seams at internal scale 2× and above (renderer fix)
- Seen in the mission 1 opening at 2× and 4× (and in flight): dark vertical lines every 32 native pixels over the whole picture below line ~30. `gsdraws.py --verts` (new option) on a capture of vblank 1700: the glow pass's upsampling draws 2836/2837 are 16 sprites 32 pixels wide that end at x = 31.5 and start at x = 32.0. The GS draws pixel 31 with the first and pixel 32 with the second (sample points at whole coordinates); at N× the host pixels between 31.5 and 32.0 belong to native pixel 32 but neither sprite covered them.
- Fix (`renderer.md` §1 fix 9): at N > 1 sprite edges move to the borders of the native pixels they cover. After the fix the 4× opening has no seams.
- Regression: `full.json` at 4× (`--shots-only`) against `baseline_s9`: all 30 jobs identical in RAM checkpoints and hash logs.

### ST-6. Regression at 1×
`full.json` against `out/runs/baseline_s9`: 29 of 30 jobs identical (screenshots, RAM checkpoints, hash logs). `m4_route` screenshot 5300 differs in one pixel column (x = 409, rows 20–419, at most 12 levels): the loading-fade column of IR-1 (host timing). `m3_route` logged "texture atlas full for this frame" once; the same warning is in earlier runs of that job (`full_check`, `full_s9_x8`) and depends on how ticks were grouped into frames, not on the residency (a frame whose own textures do not fit would have failed before as well).

### ST-7. Defaults, SCREEN RATIO and the frame-rate overlay
- Start-up log: `display: 3840x2160, 60 Hz (wider than 4:3)`; a new `settings.json` gets `fullscreen`, `vsync true`. Unit test `settings: defaults follow the display` covers 50/60/75/120/144/165/240/500 Hz and unknown rates, and the wide-display rule.
- Fresh user data with the port's own saves folder (no save) and live input: `SCREEN RATIO default: 16:9 (monitor wider than 4:3)`, `display aspect 16:9`; the no-save intro runs in the game's 16:9 mode. Every validation run above logged `SCREEN RATIO default: 4:3 (validation run: PS2 default)`.
- `--fps-overlay` in fullscreen 4:3 (desktop capture of the top right corner): "59 FPS / 17.00 ms" in small white text in the pillarbox; the game image and screenshots are unaffected.

### ST-8. Deck crew in the mission 1 opening (User report)
- With this build the deck crew is drawn in every carrier shot of the opening at 1×, 2× and 4× (`--mission-id 0x25`, screenshots every 50 vblanks from 850 to 2400), and in the New Game route's opening (`s4_frontend` screenshot 11950). The crew was missing or garbage before OP-1 (the GIF tag of `func_002206C0`, fixed earlier on 2026-10-20), so the User's observation probably came from a build before that fix. A PCSX2 reference of the opening (S5-C batch, `../pcsx2/procedures.md`) is still needed to compare shot by shot.

## 2n. Anti-aliasing: FXAA and SMAA before the HUD (2026-10-20)

User request: FXAA and SMAA if their licences are GPL-3.0 compatible, with the HUD drawn after the anti-aliasing pass. Licences: SMAA MIT; FXAA 3.11 BSD-3-Clause in NVIDIA's 2014-2015 GraphicsSamples release (the older 2010-2011 header grants no licence and is not used); both compatible (`../decisions/dependencies.md`). Feature as built: `port-features.md` §2.

### AA-1. Where the pass runs
- Mission 4 and the mission 1 opening: the point is built once per view (`BuildFlightDrawLists`, overlay table tag 0x006133B0 / 0x00629E30, rectangle 0 0 511 447), the DMA walker marks the GIF data there (offset 1,491,696 / 1,362,592 of the step), the draw list gets the `aa_point` and the backend finds the target FBP 0, FBW 8, CT32 (temporary log lines, removed).
- HUD: a crop of the mission 4 HUD (GUN / MSSL / UGBM / DMG) is pixel-identical with `off`, `fxaa` and `smaa`: the HUD is drawn after the pass.
- Unit tests: `anti-aliasing point: a mark becomes a draw-list entry between the 3D and the HUD` (position, FRAME_1, rectangle, no merging across the point; a movie mark adds nothing), `settings: anti-aliasing` (values, round trip, an invalid value rejects the file).

### AA-2. Effect
- Pixels changed against `off` (1×): mission 4 frame 2500 (sky, terrain, HUD): FXAA 1,533, SMAA 33 of 229,376; the opening frame 1700 (carrier deck, F-4E, crew): FXAA 15,969, SMAA 4,245. FXAA softens edges and some texture detail; SMAA smooths the stair steps of geometric edges and leaves the rest (zoomed crops, `out/cc/aa/`). At 1× the opening is already soft from the game's glow pass, so the gain is small; at 4× SMAA visibly cleans the strut and gear outlines.
- 4× with `--vk-validation`, FXAA and SMAA: no Vulkan validation message.

### AA-3. Regression
- `full.json`, anti-aliasing off (default), against `baseline_s9`: 29 of 30 jobs identical; `m4_route` screenshot 5300 differs only in the loading-fade column x = 409 (at most 9 levels, ST-6 / IR-1).
- `full.json` with `--antialiasing smaa` (`--shots-only`): every RAM checkpoint and hash log identical in all 30 jobs: the pass changes presentation only.

### AA-4. Movies are not anti-aliased (User rule)
- SS01 (New Game route with card S, screenshots at vblanks 2036, 2960, 3500), `--antialiasing smaa` against `off`: native screenshots identical at 1×; at 2× the internal-resolution screenshots, the native ones and the FSR 1 stage output (`movie_x2_*`) identical. The movies get only the FSR 1 stage.

## 2o. New memory card, first save, startup messages (2026-10-20)

User requests: create a new save from scratch when there is none (compatible with the PCSX2 folder format), make sure the SMV movies are not anti-aliased (AA-4 in §2n), clearer pop-up messages.

### NS-1. A new card and the game's first save
- Fresh user data (no `saves` folder): log `no memory card in …\saves: a new one was created (PCSX2 folder card, 8 MB, formatted)`; the folder holds `_pcsx2_superblock` (0x2000 bytes).
- Superblock: bytes 0x000–0x153 equal those of the User's two PCSX2-formatted cards (`Memory Card Folder`); the rest differs (their leftover block 0 data, not read by PCSX2, `../formats/folder-memcard.md` §9).
- New Game route (`--pad-script 900:start,1050:cross,1200:cross,2200:start,2900:start,3600:start,5200:start,6800:start,7400:start,7800:down ×5 (30 apart),8000:cross,8300:cross,8600:cross,8900:cross,9200:cross`): campaign menu, SAVE, "No AC04 files found on memory card (PS2). Create save file?" YES, File 1, "Save to File 1. Is this OK?" SAVE, "Save complete".
- Files: `BASLUS-20152AC04/` with `icon.sys` (964 bytes) and `icon1.ico` (52,120) byte-identical to the PCSX2-made save, `BASLUS-20152AC04` (19,216; magic `AC04`, size 0x4B10, checksum valid) and `_pcsx2_index` in PCSX2's layout and write order (`icon.sys` 1, `icon1.ico` 2, `BASLUS-20152AC04` 3, `$ROOT` times).
- New session on that folder: LOAD shows "MISSION 01 / SITTING DUCK, NORMAL", load from File 1, CONTINUE goes to the mission 01 title card.
- Unit test `folder card: a new card made from scratch has PCSX2's format fields`.

### NS-2. Startup messages
- Fresh user data without a disc image: the box "AC04 Port - Disc image" shows "No disc image selected", the explanation, "What to do:" with two numbered steps, the supported disc and the settings file, buttons **Select ISO...** and **Quit** (desktop capture; an earlier, longer button label was cut off by the Windows message box and was shortened).
- Select ISO... (Enter), the path typed into the file picker: log `disc image chosen: …`, `settings.json` `paths.iso` set, the SHA-1 computed (17.9 s), a new memory card created, the game ran to vblank 400 and exited cleanly.
- Unit test `disc image messages: headline, steps and details in one layout`.

## 2p. Engine flames off the nozzles (2026-10-21)

User report with a PCSX2 screenshot: in the mission 1 opening (mission id 0x25, the close-up of the F-4E nozzles before the launch) the afterburner flames were drawn far from the nozzles.

### EF-1. Cause and fix
- The flames are pool +0x22C30 flash records (`func_001E43D8`) placed by `UpdatePlayerTrails` 0x00126970 at the engine nodes 0x42 + e: point = body rotation × (node matrix × `one`) + unit position, with `one` = vf0 = (0, 0, 0, 1).
- Port bug (stack layout, not a game behaviour): the node matrix of the engine loop was at frame +0x70, so its translation row overlapped `one` at +0xA0. `func_00149EC8` overwrote `one` with the node translation t, and the point became R·t + t, about twice the nozzle offset. Logged values at vblank 1950: nozzle offsets (7.2, −2.7, −19.6) and (17.1, −2.7, −12.0) instead of (3.56, −1.80, −9.77) and (8.51, −1.80, −5.97).
- Fix: the matrix moved to frame +0xD0 (`src/game/player_flight.cpp`; the original keeps it at sp+0x140, apart from vf0 at sp+0x150).
- Result: `--mission-id 0x25 --mission 1 --pad-script 800:cross --screenshot-at 1950` shows the flame discs inside both nozzles and the haze trailing behind, as in the PCSX2 screenshot. The same path runs in gameplay for every player aircraft (external views), so the flames there move back to the nozzles too.
- Guest RAM: the flash record positions (effect block +0x22C30) now differ from earlier port runs. Earlier `--hash-log` baselines that cover flames need to be recorded again.

## 2q. Hands-on run from zero and debug overlay test (2026-10-22)

User request before the first release (branch `test`): play from zero to mission 1 gameplay at normal speed and test every debug overlay function and its user experience. Development build (RelWithDebInfo), fresh `--user-data`, no other parameters; 3840×2160 60 Hz display (fullscreen, v-sync, 1×), AMD RX 9070 XT; Xbox One controller connected but unused. Driven with real keyboard and mouse input (Windows `SendInput`), observed through desktop captures (`out/ux/`, not in the repo).

### UX-1. Route from zero (passed)
- First start: "No disc image selected" box, **Select ISO...**, file picker, image chosen and verified (SHA-1 computed in 7.8 s), new memory card created, game boots.
- No-save intro (mission id 0x23), title, NEW GAME, NORMAL, opening movie (played to the end), story slides, mission 1 briefing, campaign menu, SAVE to a new file (create, File 1, "Save complete"; folder card files as NS-1), START MISSION, F-4E, weapon, PRESS START, the mission 1 opening (0x25), gameplay: HUD, radio, timer, keyboard flight. A crash into the sea (aircraft left banked with no input) led to the replay and the RETRY / QUIT MISSION / RETURN TO TITLE menu.
- Second start on the same user data: no box, `verified … (cached result)`, LOAD File 1, CONTINUE, mission 1 title card, briefing (skipped with Start), hangar, gameplay. Both sessions ended with `clean exit`.

### UX-2. Debug overlay functions
| Function | Result |
|---|---|
| `\` toggle (open/close) | works, **until an ImGui text field has been used** (UX-3 a) |
| Panel checkboxes, × close, collapse, move, resize, wheel scroll | work; × unticks the checkbox |
| Performance | values live; FPS overlay checkbox works (session only, by design) |
| Scene | scene/sub-state/flags/counters/mission id correct through intro, menus, briefing and flight; request log scrolls |
| Memory | hex address, `0x` prefix, symbol name (`g_MissionNo`), "not found" for an unknown name, `-- not RAM/scratchpad --` past 32 MB and at 0x12000000, scratchpad at 0x70000000, bytes/u32/f32, ±0x100 |
| Session log | lines, colours, filter |
| Keyboard routing | a focused panel (also right after opening) takes the keyboard; clicking the game image gives it back, then flight keys reach the game with the panels open |

### UX-3. Problems found (a–g fixed 2026-10-22, UX-5)
a. **Toggle key dead after using a text field.** `imgui_impl_sdl3` calls `SDL_StopTextInput` when an ImGui text field loses focus (backend lines 191–193); the port starts text input only once (`src/platform/window.cpp:28`) and the toggle listens to `SDL_EVENT_TEXT_INPUT`, so after typing in the Memory address or log filter field `\` no longer opens or closes the overlay until restart. Reproduced twice. Fix idea: start text input again after each ImGui frame when it is off.
b. **No DPI scaling.** At 3840×2160 the panels use the 13 px default font (about 1/5 of the screen width for all four panels); the FPS overlay text is equally small. No `FontGlobalScale`/`ScaleAllSizes` call in `overlay.cpp`.
c. **Default panel layout overlaps**: the Session log (8, 420) covers the bottom of Performance and the left of Memory (360, 330).
d. **Truncated columns at default sizes**: Performance lines ("worst 24.36 m…", the texture KiB), the Scene request column (`RequestSubSt…`, the argument only shows after widening), the Memory label column (`g_MissionNo` without the `+offset`); the Scene request table keeps about 8 rows when the window is made taller.
e. **Log filter is case-sensitive** ("warn" finds nothing, "WARN" finds the warnings).
f. Memory: rows after a small global are all labelled with it (`g_MissionNo+F8` …), because `symbols.csv` has no sizes; the looked-up address is not highlighted. Mission id is shown in decimal (35 for 0x23).
g. Opening the overlay gives a panel keyboard focus, so Start/flight keys go to ImGui until the game image is clicked (UX-2 last row): surprising while playing.

### UX-4. Other observations
- **Renderer warnings in the no-save intro (both sessions):** `texture atlas full for this frame (draws skipped)` and `GS blend equation not mapped (normal alpha blend used)` during the seagull shots; there the Performance panel showed 40–50 fps, GS CPU 34–37 ms, atlas resets rising. Frame times measured during desktop captures are inflated, but the atlas overflow is logged by the renderer itself. Worth a look before the release.
- First-start polish: the ISO box uses the error icon and the log records `ERROR iso No disc image selected` for the normal first start; while the SHA-1 runs (about 8 s) the window shows the milestone-B test pattern with no progress text; the file picker opens at the top-left of the screen. The window title still reads "AC04 Port".
- `pad vibration output not implemented yet` with a gamepad connected: no rumble.
- Not compared with PCSX2 (open): a white band across the hangar MOBILITY row, and the jagged "shattered skies" letters in the no-save intro.

### UX-5. Fixes (2026-10-22, User go-ahead: debugger, intro, texture atlas, renaming, vibration)
- **Debugger** (`port-features.md` §8.7.1): text input is switched on again after every ImGui frame (UX-3 a); UI scale from the window height with the scalable font (b); a layout without overlaps (c); wider defaults, a request table that fills the window, mission id in hex (d, f); case-insensitive log filter (e); the looked-up memory row highlighted (f); no panel focused when the overlay opens (g). Checked at 3840×2160: panels readable, nothing cut off, Start reaches the game with the panels open, `\` closes and reopens them after typing in the Memory field, "sound" finds the SOUND lines.
- **Texture atlas**: the seagull frame needs 34 textures (19 of 1024×1024, several 768×512), about 23.2 M texels, more than a 4096² atlas holds; the atlas now grows once to 8192² (`port-features.md` §6) and rebuilt atlases are packed tallest first. Intro run at normal speed: `texture atlas grown to 8192x8192`, no `atlas full`; average recording time unchanged (1.80 ms against 1.86 ms).
- **Intro blending**: ALPHA 0x54 and 0x56 (and every other equation fixed-function blending cannot express) are blended in the shader from a copy of the target (`../platform/gs-subset.md`, "All blend equations"); no blend warning is logged in the intro any more. Not compared with PCSX2.
- **Renaming**: the window title, message boxes, log banner and Vulkan application name read "ProjectStonehenge"; the executable and the user data folder kept their old names that day and were renamed the same evening (§2r, RN-1).
- **Vibration** (`port-features.md` §1): switching VIBRATION off and on in CONTROLLER SETUP sends motors (1, 255) through `scePadSetActDirect` to the Xbox One controller (`port 1: vibration started`). In 2,400 vblanks of mission 1 flight (afterburner, missiles, gun) the game requested no rumble: AC04 rumbles for damage, nearby explosions, stalls and high G, which that flight did not reach. Felt on the pad: to be confirmed by the User.
- Seen once, not reproduced in four attempts: a fresh-user-data start hung at "verifying disc image 0%" (no CPU, window not responding) while synthetic keys and focus changes were being sent; no debugger was available to take stacks.
- Build: development and release build clean, `ctest` passes.

## 2r. No-save intro: transitions and stutter; renaming (2026-10-22)

User report: in the no-save intro's in-engine cutscenes with the seagulls, wrong colours cover the picture at the transitions, and the playback stutters. Found with screenshots every 10 vblanks (600–2600), then every vblank, a `--dump-dma-at` capture of a transition frame and the draw list (`tools/research/gsdraws.py`, plus the uploads in stream order).

### IN-1. Orange noise over the picture at the two cloud transitions
- Vblanks about 1540–1700 and 2270–2420: the screen filled with orange/red banded noise. The frame (capture at 1560) uploads a 200×200 PSMT8 cloud texture to 0x2500 and its palette to 0x3E00 (CT32), then draws large rotating alpha-blended quads of it over the picture (`(Cs−Cd)·As+Cd`, As from the palette's alpha ramp 0–128).
- The draws' TEX0 has **CPSM 1 (PSMCT24)**, not a documented CLUT format. The port took every CPSM other than 0 and 0xA as CT16 and read the 32-bit palette as 16-bit entries. The GS selects 16-bit entries with CPSM bit 1 (PCSX2's CLUT writers treat CT24 as CT32); fixed in `gs_state.cpp` (`Clut16`): CPSM 1 now reads 32-bit entries with their alpha.
- Result: white clouds fade in over the feathers and out to the city (first transition) and over the F-22s to the seagulls (second). Not compared with a PCSX2 picture of these frames yet.

### IN-2. Stutter
- At normal speed only 31 of 101 requested screenshots (vblanks 900–1000) were written: the port presented about 31 frames for 101 game ticks. A per-frame log showed frames 912–929 with 40–50 ms of GS processing and 20–40 ms of recording, 30–50 texture decodes (3–4 M texels) and an atlas restart every frame.
- Cause 1: triangles decode their whole texture (`gs-subset.md`, S4); the seagull models use T4 textures declared 1024×1024 but in REGION_CLAMP to 16×16 texels, so each draw decoded a million texels. Triangles now decode only the [MIN, MAX] range of a REGION_CLAMP axis (not with mip levels).
- Cause 2: a decoded texture was keyed by a hash of every GS page in the linear span from its first to its last texel (`RegionHash`): for a 1024×1024 texture that span includes the terrain tiles and cloud textures the intro uploads every frame, so the textures were decoded again every frame. Level 0 is now keyed by the pages under the decoded rectangle only (`GsMemory::RectHash`, pages found on a 32-pixel grid).
- Result (normal speed, vblanks 0–2600): the run of slow frames at the seagull close-ups (about vblank 912–929, 40–50 ms of GS processing each) is gone; 2,460 frames presented for 2,600 ticks (2,219 before); the atlas no longer needs to grow. 55 frames still take over 15 ms (41 over 25 ms, worst 77 ms), almost all in one stretch: the seagull shot after the second cloud transition (about vblank 2390–2460).
- Still open there: the terrain below draws many triangles with different 64 × 64 REGION_CLAMP slots of one mipmapped 512 × 512 T8 texture (0x2900), and the game streams new terrain data into that memory between draws: 600–990 of its 1,360 blocks change between consecutive decodes, about 4 full CPU decodes (with mip levels) and atlas uploads per frame. Tried and reverted: decoding only the REGION_CLAMP slot with mip levels (over 1,000 small textures per frame, 230–250 ms per frame), decoding triangles by vertex range (model draws stop merging, about 260 textures per frame), and caching the texel indices across palettes (no measurable gain). The fix would be decoding paletted textures on the GPU (palette applied in the shader, as `gs_conv.comp` already does for frame buffers): a renderer design change for the User to decide. Done the same day (User approval): §2s, the stretch now peaks at 12.5 ms of CPU work per frame instead of 41.

### Regression (`full.json` against `out/runs/baseline_s9`, final build)
- All 30 RAM checkpoints equal; all 18 missions' screenshots equal.
- Hash logs differ in the jobs that reach a mission with the player aircraft (front end, mission 3 and 4 routes, mission 4 fire, opening): first difference at the mission 1 opening (vblank 11734, heap 0x00CD0000), the flame records of EF-1 (§2p).
- Screenshots: card E boot (vblank 600) now shows the narration text "The flames of hatred scorch the skies… Igniting Gaia's funeral pyre." where the baseline had red noise (from the PSMT8 table or the CT24 CLUT fix; which one was not isolated); mission 1 opening (front end 11950): the deck and background behind the aircraft differ slightly (HYPOTHESIS: the glow/depth pass reads frame buffers as PSMT8, S2a-6), not compared with PCSX2; mission 4 route 5300: the known one-pixel column (IR-1).

### RN-1. Renaming finished
- Executable `ProjectStonehenge.exe` (CMake project and target `ProjectStonehenge`), user data folder `%LOCALAPPDATA%\ProjectStonehenge\` (ADR-005 updated; Linux/macOS names for later: `projectstonehenge`, `ProjectStonehenge`), usage text, comments, docs, rules and the research tools' job files. No migration code (first release); on the User's PC the old folder's `settings.json`, `iso_cache.json` and (empty) `saves\` were moved to the new folder and the old folder with its stale log and a RAM dump was deleted. `ac04::` namespaces, `AC04_` macros and `ac04_*` libraries keep their names (they name the game, User decision 2026-10-22). Two records above (§2o NS-2, §2q UX-4) quote the old window title as it was shown that day.

## 2s. GPU texture decoding, generation-based texture cache, texture images (2026-10-22)

User-approved plan `renderer.md` §2 (all three items). Built: GS textures are decoded on the GPU (`shaders/gs_decode.comp`) from a 4 MiB GPU copy of local memory that `GsState` keeps in step through `TextureOp` (memory blocks, then decodes, in stream order); the decoded-texture cache keeps one entry per texture, valid while the GS blocks it read keep their generations (the page content hashes are gone); one image per decoded texture replaces the atlas. The CPU decoder stays as the reference (`--cpu-texture-decode`, `--verify-texture-decode`).

### GD-1. Correctness
- Tests: 87 test cases pass (development build). New: a decode stays valid until a block it reads is written (writes elsewhere and rewrites of the same bytes keep it); the GPU ops of a stream that rewrites its texture between draws, replayed on a second memory and decoded there with the reference decoder, give each texture's CPU decode (T8, T4 with CT32 and CT16 palettes, CT32, CT24, CT16, CT16S, with TEXA). The incremental-decode and cache-budget tests were adapted (no atlas slot updates; one entry per key).
- `--verify-texture-decode` on the six scenes of `renderer.md` §2.5 (no-save intro at 1× and 8×, hangar/shop route, mission 1 opening, mission 1, mission 4 replay): 666,000 GPU decodes, every one byte-identical to the CPU decode. The first run found one difference: the intro samples its Z24 buffer (TBP 0x1500 = FBP 0xA8) as a texture; the CPU decoder had always given 0 for Z formats (an accident of its default branch), the shader the raw word. Both now give 0 explicitly with a log-once warning, so the picture is unchanged; depth sampled as a texture remains unimplemented (`../open-questions.md`).
- Release build: two command-line tests fail (`determinism options`, `--mission options`): a development-only option with a value (`--replay-input D:/in.ac4i`) counts as two errors in release builds (the option, then its value as an unknown argument). Not related to this change; fixed in §2t.

### GD-2. Regression (`full.json`, run `gpu_1022`)
- Against last session's final run (`out/runs/final_1022`): all 27 RAM checkpoints and all 6 RAM hash logs identical; 75 of 77 screenshots pixel-identical; `m3_route` and `m4_route` screenshot 5300 differ only in column x = 409 (400 pixels, at most 5 levels): the loading-fade column of IR-1 (host timing).
- Against `baseline_s9`: the differences listed in §2r (card E narration, mission 1 opening at 11950, hash logs of the jobs with the player aircraft, the x = 409 column), nothing new.

### GD-3. Speed (`renderer.md` §2.7)
Same six runs as step 1 (§5 there), same conditions (windows not focused); CPU = game tick + GS processing + recording per presented frame.

| Scene | CPU mean | Worst frame | Frames with CPU > 16 ms |
|---|---|---|---|
| No-save intro 1× | 9.0 → 6.6 ms | 91.5 → 21.2 ms | 200 → 4 |
| No-save intro 8× | 10.0 → 7.4 ms | 93.0 → 30.6 ms | 286 → 173 |
| Hangar/shop route | 4.0 → 3.6 ms | 36.2 → 30.8 ms | 579 → 60 |
| Mission 1 opening | 9.5 → 8.1 ms | 30.9 → 26.5 ms | 86 → 80 |
| Mission 1 | 9.0 → 8.4 ms | 34.0 → 35.2 ms | 310 → 300 |
| Mission 4 replay | 10.7 → 8.2 ms | 39.3 → 20.9 ms | 621 → 245 |

- The intro stretch still open in §2r IN-2 (seagull shot after the second cloud transition, vblank 2390–2460): CPU 14.4 → 9.8 ms per frame on average, worst 41.2 → 12.5 ms.
- Hangar viewer (vblank 4160–4650): the game cycles several textures through the same memory every frame; without the content hash each return is a new GPU decode (about 158 per frame, before 6 CPU decodes). GS processing 9.1 → 6.5 ms, recording 1.8 → 2.5 ms (per-decode descriptor set and dispatch), CPU 16.8 → 15.3 ms per frame.
- Remaining frames over 16 ms in the missions and at 8× carry two game ticks (the unfocused windows presented at ~50 Hz, so many frames ran two ticks): game tick 6–13 ms plus GS processing about 7 ms. Not texture work.

## 2t. Release 0.9 preparation (2026-10-22)

- **Development parameters in release builds**: every development-only parameter (23 branches of `command_line.cpp`, among them `--vk-validation` and `--replay-input`) is rejected in release builds and enables nothing. Their values are now skipped (`dev_only`), so `--replay-input D:/in.ac4i` gives one error instead of two; the two release-build test failures of §2s GD-1 are fixed by this. Development and release tests: 87 test cases pass in both.
- **Version 0.9.0** (`CMakeLists.txt`); `--version`: `ProjectStonehenge 0.9.0 (<commit>, development build)`.
- **File properties** (`src/platform/win32/version.rc.in`, configured to `build/.../generated/version.rc`): read back from the release executable: CompanyName and ProductName `ProjectStonehenge`, FileDescription `Unofficial native PC port of Ace Combat 04: Shattered Skies`, FileVersion and ProductVersion `0.9.0`, LegalCopyright `Copyright (C) ProjectStonehenge. Licensed under the GNU GPL v3.`, OriginalFilename `ProjectStonehenge.exe`.
- **Startup image**: until the GS renderer has a frame, release builds show black; development builds keep the test pattern (`Renderer::UploadTestPattern`).
- **No game data in the executable** (release build, 7.5 MB): no VAG, TIM2, GIM, PS2I or IOPRP signatures and no embedded PS2 ELF; 0 of about 26,000 sampled 64-byte runs of every section of `SLUS_201.52` (code, `.data`, `.rodata`, VU code and data) occur in it. Two 4-byte MPEG start codes in `.rdata` are inside constant tables (not followed by a sequence header). The executable reads all game data from the user's disc image at runtime.
- **Release folder**: the build writes `ProjectStonehenge.exe` next to build leftovers (`ac04_core.lib`, `ac04_platform.lib`) and a stale `ac04port.exe` from before the rename (2026-10-22 00:50); there is no packaging step yet, so a release archive must be put together by hand from `ProjectStonehenge.exe` (plus `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md`).
- **Packaging step** (User request, same day): `cmake --build --preset release --target package_release` (`cmake/PackageRelease.cmake`) writes `out/release/ProjectStonehenge-0.9.0-win64.zip` with `ProjectStonehenge.exe`, `LICENSE`, `README.md` and `THIRD_PARTY_NOTICES.md` in a `ProjectStonehenge-0.9.0-win64/` folder (3.2 MB; checked by listing the archive). The script refuses other configurations (checked with `--preset dev`) and stops on game-data file names, the VAG, TIM2 and IOPRP signatures and embedded PS2 ELF headers (checked with a test file holding `VAGp`). The stale `ac04port.exe` and every other build leftover under the old name (its intermediate folder, project files, the old solution) were deleted from `build/` (User approval).

## 2u. No-save intro: terrain uploads written block by block (2026-10-22)

User report: stutters remain in the first in-engine cutscene (the no-save intro). Measured with a temporary per-frame probe and `tools/research/sample_profile.py` (development build, normal speed, window not focused):
- No single game tick needed more than 16.1 ms of CPU, but the end of the intro (vblank 2500–2620) needed about 15 ms per tick (game tick 4.8, GS processing 7.0, recording 3.7 ms), leaving almost nothing for presentation. The profile of that stretch: `GsMemory::WriteImage` alone was about 40 % of all CPU work (about 83 % of GS processing): the game streams terrain tiles into texture memory every frame (about 1.8 MB per frame), written pixel by pixel.
- Pipeline creation is not a cause: 14 pipelines in the whole intro, 2.4 ms in total.
- Fix (`gs_memory.cpp`): an upload that arrives in one piece with its rectangle on block boundaries is written block by block (the 256 bytes of a block assembled in the GS layout, compared and stored at once); everything else takes the run loop as before. Same bytes, same changed blocks and generations: new test `GS memory: whole-block image writes equal pixel-by-pixel writes, with the same block changes` (every format, into zeros and over existing contents with some blocks unchanged).
- Result (same run): `WriteImage` from about 12 % to 3 % of the samples; vblank 2500–2620 at 11.7 ms of CPU per tick on average (GS processing 3.7 ms), worst 14.3 ms; ticks needing more than 13 ms in the whole intro: 151 → 13.
- Left: recording costs about 3.4 ms per frame in that stretch, mostly in the Vulkan driver's command recording (barriers and render pass changes around frame-buffer reads); reducing it needs the draws grouped differently, a larger renderer change not made before the 0.9 release. The game tick itself (translated code and the VU1 programs) is about 4.7 ms.
- Version raised to **0.9.1** after this change (User request); the release archive is `out/release/ProjectStonehenge-0.9.1-win64.zip`.
- **Static C/C++ runtime in Release builds** (User decision, same day): the 0.9.1 release executable imported `MSVCP140.dll`, `VCRUNTIME140.dll` and `VCRUNTIME140_1.dll` (`dumpbin /dependents`), which a clean Windows does not have. `CMakeLists.txt` now sets `CMAKE_MSVC_RUNTIME_LIBRARY` (Release `/MT`; development builds keep `/MD`, `/MDd`) and `CMAKE_POLICY_DEFAULT_CMP0091` before the dependencies are fetched, so every library uses the same runtime. The release executable now imports only Windows system DLLs (KERNEL32, USER32, GDI32, WINMM, IMM32, ole32, OLEAUT32, VERSION, ADVAPI32, SETUPAPI, SHELL32); `vulkan-1.dll` is loaded at runtime by volk, as before. Size 7.56 → 7.90 MB. Release and development tests pass; the release executable started with the ISO and reached the first vsync wait with no warning or error in its log.
- Regression (`full.json`, run `rel09`, against this morning's `gpu_1022`): all 27 RAM checkpoints and all 6 hash logs identical; 75 of 77 screenshots identical, `m3_route` and `m4_route` screenshot 5300 differ only in the loading-fade column x = 409 (IR-1). Development and release tests pass.

## 2v. QUIT GAME in the main menu (2026-10-22)

User request: a QUIT GAME item after the existing ones in the main menu, built from the same components and style as the other items (like GRAPHICS in SCREEN SETTINGS). Port baseline (`../decisions/port-baseline.md`), `port-features.md` §9.

- Built: `src/game/quit_item.*` and `port:` hooks in `src/game/menu.cpp`: QUIT GAME is appended to column 0 (after OPTIONS) only while the menu tree is built or its cursors are stored (`QuitItemPatch`, the GRAPHICS mechanism); selections are mapped back to the game's ids (`MainMenuSelection`: the later columns' ids shift by one while the item is there). Selecting it fades the menu, the screen and the menu BGM out (0.5 s, port state 9) and then sets `port::Menu().quit`; the main loop ends the session as when the window is closed.
- Checked with card S and a pad script (before the validation-run rule below): the item is drawn as the sixth item (NEW GAME, LOAD, TUTORIAL, VS MODE, OPTIONS, QUIT GAME), typed in and highlighted like the others; the cursor wraps over it; the help line is empty on it (the help texts are glyph messages of the page's font block, which has no text for it); × on it at vblank 1480 ended the session cleanly at 1511 (`stopped after 1511 vblanks`, `clean exit`).
- First regression run: RAM changes outside the port area: (1) every menu sound counter one higher after the main menu, because the list widget types a column for as long as its longest item (`UpdateListWidget`) and QUIT GAME (9 characters) was longer than the visible items (8), so one more typing tick played. Fixed: items in the port area do not count toward the typing length (`port_menu::Contains`, `port:` line in `menu_widgets.cpp`); they are still typed in full (typing runs 3 characters past the longest item; NEW GAME, 8 characters, is always shown). (2) Leftovers of the main menu in later checkpoints (`s4_vs`): its menu tree in the freed work-heap object (one more item, the QUIT GAME text pointer 0x01FFFD40, shifted ids) and its draw packets in both packet buffers (0x004D1480, 0x008D1480). Presentation data nothing reads later, but not comparable with PCSX2.
- Therefore, as the SCREEN RATIO default: QUIT GAME is added only outside validation runs (`port::Boot().quit_item`; development runs with a pad script, an input log, a PCSX2 recording, `--memcard` or `--no-memcard` see the PS2 menu). The session log says which (`QUIT GAME in the main menu: on` / `off (validation run)`); a normal session logs `on`.
- Regression (`full.json`, run `quit3`, against `rel09`): all 27 RAM checkpoints and all 6 hash logs identical; 75 of 77 screenshots identical, the two route shots at 5300 differ only in the loading-fade column x = 409 (IR-1). Development tests pass.

## 2w. Save texts for a computer (2026-10-22)

User request: the save and load messages mention the console; reword them just enough for a computer ("saving, do not turn off the computer..."). Port baseline (`../decisions/port-baseline.md`), `port-features.md` §10.

- Search (`tools/research/msgscan.py` over every font block of DATA.CDP; glyph codes decoded from the disc: `../subsystems/sound.md` "Text engine"): no message contains "console". The references are "memory card (PS2)" (about 40 messages: save/load page DATA 0xB3, main menu help 0xB0, save menus 0xB1, 0xB2, 0x110, in-mission save 0x14D), the two-line warning "Do not remove memory card (PS2) / or switch off power" (saving, loading, formatting), and "(for PlayStation 2)" / "Do not turn power off" in the hard disk drive texts, which are unreachable in the release (HDD items hidden as on a PS2 without HDD) and stay unchanged.
- User choice: "memory card (PS2)" becomes "memory card"; "or switch off power" becomes "or switch off the computer".
- Built: `src/game/pc_texts.*`, called after `IndexMessages` in `LoadFontBlock` (`port:` line): every message just indexed is reworded in guest memory; a shorter one in place, a longer one into a port menu area slot (22-31; one per distinct text and end code) with its message table entry pointing there. The end code is kept (0xFFFF: the next message continues the text). Off in validation runs (`port::Boot().pc_texts`, the QUIT GAME rule); the startup log line names both.
- Tests: `tests/test_pc_texts.cpp` (messages built with the game's glyph codes: "(PS2)" dropped, the warning line moved and reworded with its end code, the HDD text unchanged, applying twice changes nothing more; off in validation runs). 90 test cases pass (development and release).
- Live check with the texts forced on for a scripted run (temporary local edit, reverted): main menu, LOAD, card S: the save/load page shows "Accessing memory card... / Do not remove memory card / or switch off the computer" in the game's font.
- Regression (`full.json`, run `texts2`, against `quit3`): all 27 RAM checkpoints and all 6 hash logs identical; 76 of 77 screenshots identical, `m4_route` 5300 the loading-fade column (IR-1). A first run (`texts1`) still used the forced build: restoring `main.cpp` with its old timestamp (`cp -p`) left it older than the object compiled from the temporary edit, so it was not recompiled; touched and rebuilt before `texts2`. Lesson: restore temporary edits without preserving the timestamp.

## 2x. Defaults: v-sync, FXAA and the internal resolution of the display (2026-10-22)

User request: when there is no saved data yet (no `settings.json`, no saves), start with v-sync on, FXAA and the internal resolution closest to the monitor resolution.

- Built (`src/platform/settings.cpp` `DefaultSettings`, `DefaultInternalScale`): v-sync on at every refresh rate (before: only at 60 / 120 Hz); FXAA; internal scale N with 448 x N lines closest to the display height, 1-8 (720p / 1080p 2x, 1440p 3x, 4K 5x, 8K clamped to 8x; unknown height 1x). The frame cap still takes the highest value at or below a refresh rate other than 60 / 120 Hz (it applies when the player turns v-sync off). These are the defaults wherever the port uses them: no `settings.json` yet (first start), a rejected file replaced with the defaults, missing keys of a file, and the GRAPHICS page's DEFAULT button. An existing `settings.json` keeps its values; saves do not influence the settings defaults.
- Validation runs (pad script, input log, PCSX2 recording, `--memcard`, `--no-memcard`; development builds only) keep 1x and no anti-aliasing (`SetValidationDefaults`), so their screenshots compare with the baselines. The startup log line names the default scale (`display: 3840x2160, 60 Hz (wider than 4:3); default internal scale 5x`, or `1x (validation run)`).
- Tests: `tests/test_settings.cpp` updated (v-sync on at every rate with the caps as before, FXAA, 1440p 3x, a file's missing keys on a 4K display 5x) and two new cases (the scale per display height; validation-run defaults). Development and release tests pass.
- Live check (fresh user-data folder, normal start, 3840 x 2160 at 60 Hz): `settings.json` written with `"vsync": true`, `"internal_scale": 5`, `"antialiasing": "fxaa"`; the renderer started at 5x.
- Regression (`full.json`, run `defaults1`, against `texts2`): 26 of 27 RAM checkpoints and all 6 hash logs identical; 76 of 77 screenshots identical (`m3_route` 5300: the loading-fade column, IR-1). `s1_cardE` differs in 3 bytes at 0x004121F5 and 0x00412214-15: the raw analog stick bytes of the live pad (that job has no pad script; 0x7C / 0x7B against 0x7F / 0x7E, the same in a rerun in this session), category `input` (`ramdiff-expected-s1.csv`).

## 2y. Sprites at internal scale > 1: native sample point (2026-10-22)

User report (0.9.1/0.9.2 release, 4K, INTERNAL RES. 7X): the "7" of "7X" on the GRAPHICS page drawn with a stray left stroke, like a "П".

- Cause: `gs.frag` flag 26 (2D sprites at N > 1 sample at the UV of their native pixel, `renderer.md` §1 fix 1) took the native sample point as floor(g), g = the host pixel's GS position. A native pixel *n* covers g in [n − ½, n + ½) (fix 9), so the host pixels of its left/top half sampled pixel *n* − 1: every sprite was shifted half a native pixel. The "7" is glyph (0, 90) of the menu font (T4 1024 × 1024, TBP 0x3D40, REPEAT): its left neighbour texel wraps to u = 1023, outside the decoded rectangle, and the clamp to the rectangle took its right edge, a stroke of the "4".
- Fix: sample point floor(g + ½). Same GRAPHICS page (pad script through OPTIONS → SCREEN SETTINGS → GRAPHICS, vblank 2400) at 7× against 1×, native screenshots: pixels over 16 from 3.47 % to 0.15 % (the rest is the value "7X" against "1X"); the glyph is a 7. 1× is unchanged (the flag is set only at N > 1).

## 2z. Explosions at internal scale > 1 and the sun flare's palette (2026-10-22)

User report (0.9.2, 4K, internal scale 4): explosions of hit aircraft drawn as black squares (fine when the game runs fast-forwarded), and a large square with speckled rings when looking at the sun. Reproduced from the User's input log (`--memcard` copy of the User's card, `--replay-input`, mission 1; not committed: `out/runs/user_1022.ac4i`, `out/cards/user`).

Explosions (black at N > 1, correct at 1×):
- The flight draws its work page at FBP 0x180 (512 × 128) every frame: CT32 sprites and strips, then the same bytes as CT16S (512 × 256, the page read as CT16S and written back), then CT32 with FBMSK 0xFFFF0000. The explosion debris strips sample it (TBP 0x3000, CT32). The CT16S pass moves bits between pixels; the scaled views, synchronised through native conversions and nearest upscaling, lost the result (black). With the page forced to 1× the debris is correct at 4×.
- Fix (`gs_vulkan.cpp` `GetTarget`, `DownscaleTarget`): the rule of `renderer.md` §1 row 7. A frame buffer drawn both in the 32-bit layout and in a 16-bit one is held at 1×, all its views; a view already scaled is rebuilt at 1× from its centre host pixels when the second layout appears (logged once: `frame buffer FBP 0x180 drawn in 32- and 16-bit layouts: kept at 1x`). At 4× the debris now matches 1× (vblanks 6400-6430). "Fine when fast-forwarded": the fast-forward shows only every few frames' last draws, so the debris was rarely on screen.

Sun flare (wrong at every scale; not a regression, the port never supported it):
- Per frame (dump at vblank 7800): the sun sprite is drawn depth-tested against the scene into FBP 0x120 (T4HL, ZTST GEQUAL); 225 points sample that area and add into one pixel of FBP 0x190 (the visible part of the sun); sprites spread it and scale the base palette (CBP 0x3F48) into a 16 × 16 CT32 palette at FBP 0x190; the flare (strip, T8H 128 × 128 at TBP 0x2000) is drawn with CLUT CBP 0x3200 = that palette. The port loaded CLUTs from its local-memory copy, which never holds what the GPU drew: the flare got stale bytes (rainbow rings, opaque square).
- Fix: `GsState::LoadClut` sees an 8-bit CT32 CLUT load from a frame buffer drawn earlier (and not overwritten by a transfer since) and gives the CLUT the identity (entry i = i), so the texture is decoded to palette indices and stays cacheable; the draw carries the CBP (`DrawCommand::clut_from_frame`). The backend copies the palette's 16 × 16 pixels from the newest 32-bit view at that page (after synchronising other views) into a 16 × 16 image right before the draw, and `gs.frag` (flag 29, binding 4) looks each texel up there before filtering (CSM1 index swizzle). The flare now shows the sun glow without the square at 1× and 4× (vblanks 7700-7950). Only 8-bit CT32 palettes take this path; a 4-bit or 16-bit palette from a drawn buffer is still read from local memory. `--cpu-texture-decode` uses the same path (the lookup is in the shader). A PCSX2 reference of the flare is still to be taken (`../pcsx2/procedures.md`).

Mission title card (User report: frame lines broken at the top left, 4K): the title image on the disc (DATA 0xBC for mission 2, a 544 × 192 T8 image) has no frame lines; the page draws the image, then the menu frame filter, which copies the frame onto itself with a zoom blur. In the User's recording the MISSION 01 card ("SITTING DUCK", vblanks 2330-2600) is clean at 4× at normal speed with this build.

Regression (`full.json`; run folders compared file by file, because `runmany.py --baseline` expects a `--save-baseline` folder):
- 1× (`clut1`) against `defaults1`: every RAM checkpoint, hash log and screenshot identical except the known ones: `m3_route` / `m4_route` screenshot 5300 in the loading-fade column x = 409 only (IR-1), `s1_cardE` the 3 live-pad bytes at 0x004121F5 and 0x00412214-15 (input class).
- 4× (`clut1_x4`) against `clut1`: every RAM checkpoint and hash log identical (`m3_route` has one vblank more at exit, identical at every common vblank). Held at 1× by the new rule: FBP 0x180 in every flight, FBP 0x1C0 in mission 3 (its 4× screenshots match 1× up to edge detail: 0.9 % and 1.5 % of pixels over 16).

## 2aa. In-engine cutscenes: ticks per frame with v-sync (2026-10-22)

User report: strong stutters remain in the in-engine cutscenes (no-save intro 0x23, mission 1 opening 0x25). Measured with a temporary per-frame probe in the main loop (tick time, GS processing, `DrawFrame`, pacing, ticks run; development build, window 1920 × 1080 on the 3840 × 2160 / 60 Hz display, internal scale 5, FXAA, v-sync on):
- Not CPU or GPU load: with v-sync off no frame of the opening needs more than 17.5 ms (tick about 1.7 ms, GS processing 0.65 ms, recording 0.5-0.8 ms on average), and 1× and 5× give the same picture with v-sync on.
- Cause: the ticks released per frame. With v-sync on, 450 of the opening's 1,552 frames after vblank 850 ran 0 or 2 ticks, in a repeating 1, 0, 1, 2 pattern (vblanks 1170-1215): the CPU is released at irregular times (`DrawFrame` blocks 2, 12, 20, 2 ms in turn, waiting for a swapchain image or a frame in flight), and `TickClock` turned that measured time into ticks. Four frames still took four refreshes, but the picture advanced 1, 0, 1, 2 ticks: a visible judder at 15 Hz.
- Fix (`sce::RefreshSnap`, `main.cpp`): with v-sync on, each frame counts as one refresh of the window's display (`SDL_GetCurrentDisplayMode`, exact rational rate, read again every 60 frames). The difference from the measured time is carried and paid out in whole refreshes once it passes one refresh (a refresh really missed), so the total follows the host clock and the game speed is unchanged (the 59.94 / 60 Hz difference still gives one frame without a tick about every 17 s). V-sync off is unchanged. New test `refresh snap: one refresh per frame despite jitter, missed refreshes paid out`.
- Result: opening 2 of 1,552 frames with other than one tick (was 450); no-save intro 9 of 1,840, all at two single slow frames.
- Those two frames (vblanks 1335 and 1874, 120-140 ms in `DrawFrame`) came only in the first run after the shader was rebuilt; three more runs had none. Neither target creation nor recording nor the fence wait took over 30 ms (temporary logs). HYPOTHESIS: the driver compiling the changed pipelines the first time each is used (it keeps them in its own cache afterwards), so a player sees such a hitch once per new effect after installing a new version. A pipeline cache of our own would need a new file in the user data folder: for the User to decide. Confirmed and fixed 2026-10-25 (§2ai).
- Regression (`full.json`, run `snap1`, against `clut1`): every RAM checkpoint, hash log and screenshot identical except `m4_route` 5300 in the loading-fade column x = 409 (IR-1).

## 2ab. Depth copy for the cloud pass; SPU2 reverb (2026-10-22)

User request after the review of unimplemented features: build the Z buffer read as a texture and the reverb.

### ZC. Z buffer drawn into another Z buffer
- What the game does (no-save intro first at vblank 1335, step capture at 1336; mission 12): a sprite reads the scene's Z buffer (ZBP 0xA8, TBP 0x1500, PSMZ24 texture) at half size (UV 0-512 × 0-448 over 256 × 224) into FBP 0x150 with a Z24 frame format; FBP 0x150 is then the Z buffer (ZTST GREATER, DATE) of small alpha strips drawn into the half-resolution buffer FBP 0x128 (mission 12: into the work page FBP 0x180). The port read the texels as 0, so those strips were never hidden behind nearer geometry.
- Built (`gs_vulkan.cpp` depth copy, `shaders/gs_depth_copy.frag`): a textured sprite whose texture is a Z format at a Z buffer the renderer knows, drawn into a frame buffer that is itself a known Z buffer, is a depth copy: the destination owner's depth image gets the source owner's depth image, sampled at the native texel under each pixel (nearest, the source's internal scale honoured) and moved between Z formats by 2^(bits difference). Colour is not written. Z buffers tested in a frame are now registered before its draws, so the copy works in the first frame. Target depth images are created with the sampled usage. A Z-format texture in any other draw is not drawn (logged once); the CPU and GPU decoders still give 0 for Z formats.
- Checked: Vulkan validation layer clean over the intro's first two cloud transitions; the copy runs every frame of them (source FBP 0 at 1×, destination FBP 0x128 / mission 12 FBP 0x180, same Z format). Screenshots every 8 vblanks over both intro transitions (vblanks 1300-1460, 1840-2000) and mission 12 at 2000 / 3000 are byte-identical with and without the copy: in these frames the strips are small (x 0-9, y 150-177 in the capture) and in front of the scene anyway. A PCSX2 comparison of a frame where they pass behind geometry is still to be taken.

### RV. Reverb
- What the game does (`SOUND.IRX` disassembled from its symbol table; `../subsystems/sound.md` corrected): command 6 `set_reverb_mode(core, mode)` = `sceSdSetEffectAttr` with mode | 0x100 (clear the work area) and depth 0 (0x5E0); command 7 `set_reverb_depth(core, l, r)` = `sceSdSetCoreAttr(EFFECT_ENABLE, 1)` and EVOL L/R (0x628); commands 8 / 9 `set_reverb_on` / `set_reverb_off(voice)` set or clear **the voice's** VMIXEL / VMIXER switch bits (0x6A4 / 0x75C; the voice is the signed byte at +5): the per-sound reverb flag (request +0x1C bit 1, `func_0021D0C8`), not a per-core switch. The game uses mode 2 (studio A) on core 1 at depth 10000 (menus) and mode 3 (studio B) on core 0 with an altitude-dependent depth (flight).
- Presets: libsd's table in the disc's `IRX/LIBSD.IRX` at 0x4224, ten modes of 0x44 bytes (32 registers in the PS1 layout, then 4 more bytes; mode 1 is the well-known room preset, mode 9 pipe). Read from the ISO at startup like other game data (the image's SHA-1 is verified, so the offset is fixed; the loader also checks room's first two registers).
- Built (`spu_mixer.*`, `iop_services.cpp`): one effect unit per core: input = the core's voices with the reverb bit (after their volume), the PS1 / SPU2 reverb formula (same-side and cross reflections, four combs, two all-pass stages) at 24 kHz with left and right on alternate samples, 39-tap half-band FIR down- and upsampling, work area in sound RAM from the core's ESA to EEA (cleared on a mode change), output × EVOL added to the core's mix. Echo / delay parameters (modes 7, 8) are not modelled (logged; the game does not use them). Presentation only: the voice status the game reads is unchanged.
- Checked: new tests `mixer: reverb only for voices sent to an enabled effect unit` and `mixer: reverb presets are found only at the libsd table`; audio dumps with and without the presets: menu sounds differ by 600-1,300 RMS where they play, the music (no reverb bit) is identical, a hit sound's tail decays (283 → 20 → 0.7 RMS per second), no clipping. A PCSX2 audio capture to compare levels is still to be taken.

Regression (`full.json`, run `rz1`, against `snap1`): every screenshot, RAM checkpoint and hash log identical except `m3_route` / `m4_route` 5300 (column x = 409, IR-1) and `s1_cardS`, the job without a pad script: the live controller's stick bytes 0x004121F4-F7 and 0x00412214-17 (input class). No warning in any session log (the Z-texture warning is gone).

## 2ac. Frame interpolation (2026-10-22)

User request: build the frame interpolation of ADR-002 (approved baseline). Design and limits: ADR-002 "Implementation (2026-10-22)"; feature: `port-features.md` §2.

- How (`platform/render/frame_interpolator.*`, `vu1/vif1.*`, `main.cpp`): while interpolation is active the VIF records every VU1 program run of a step (output range in the step's GIF stream, program, entry, TOP/ITOP, absolute VU qwords 0-63 and the TOP-relative input: fixed block and vertex records). Runs of the newest step are matched with the step before by key: model programs (A, B, C, E) and the glow program (D) by the guest address of their vertex records plus occurrence; terrain cells (T) by cell origin, vertex count and class. Each presented frame rebuilds the newest step's GIF stream with every matched run executed again on its recorded VU memory, the matrices blended at the clock's alpha (models: screen, clip and normal matrices; terrain: camera matrices and position; glow: node matrix); the rest of the stream (2D, HUD, effects the EE projects, unmatched runs) is the newest step's. That copy replaces the newest step in the renderer (the same GS register and local-memory changes); earlier steps of the same frame are processed without drawing.
- Guards: a run whose projected origin moves more than 160 pixels between the steps is not blended; a terrain cell that moves that far marks a camera cut (the step is shown unblended); a node that turns more than 25° (the propellers, 30-60° per step) has only its position blended (it stays on its parent with the newest orientation).
- Active only when it helps: the setting `presentation.frame_interpolation` (default on), more than 62 presented frames per second (v-sync: the display's refresh rate; otherwise the frame cap), not in validation runs (pad script, input log, PCSX2 recording, `--memcard`, `--no-memcard`: their screenshots compare with PCSX2) and not while fast-forwarding. At 60 Hz nothing changes.
- Found on the way: in flight the game uses the VIF double buffer (BASE 128, OFST 448: TOP alternates 128 / 576); the menus keep TOP 0 (`../subsystems/vu-programs.md` corrected).
- Checked: a rebuilt stream at alpha 1 is byte-identical to the step (temporary check, 301 flight steps of the User's recording, 168,502 runs, 0 differences), so the recorded VU memory is complete. 562 of 565 runs of a flight step are matched. Your recording at vblank 6440 (bomber after the hit) with alpha fixed at 0, 0.5 and 1: the bomber moves smoothly, the HUD is the newest step's, the propellers stay on the engines. No-save intro in a normal session (no input, empty user data, v-sync off, cap 144): no warning, 4,687 frames for 2,700 steps (2,689 with interpolation off), no visible artifacts in 12 screenshots; the first run after a rebuild dropped 83 ticks (pipeline compilation and screenshot readbacks), a second run none.
- Cost (mission 1, 1×, per frame): with a step 11.0 ms (tick 4.4, stream and GS processing 4.1, recording 2.5), without a step 5.7 ms: about 2 frames per step on this PC (120 Hz holds; 144 Hz about 130). A step's own GS pass is skipped when its blended copy replaces it.
- Not blended (ADR-002): 2D and HUD, effects and particles the EE projects itself (smoke, sparks, the sun and its flare: they follow the newest step), SMV movies.
- Regression (`full.json`, run `fi1`, against `rz1`; validation runs keep interpolation off): identical except the known `m3_route` / `m4_route` 5300 column and `s1_cardS` live stick bytes. Tests: `settings: frame interpolation`.

### FI-1. Hangar: flickering dark rectangles (User report, 0.9.3, v-sync off, no cap, 8×)
- Reproduced with interpolation on (mission 3 route, hangar at vblank 4450-4600, v-sync off, cap 360), not with it off, at 1× and 8× alike. The other marks in the report (the row of boxes after the red cross, MOBILITY and MSSL washed out by the windows, the blocky floor reflection) are in the PCSX2 picture too (`Slices/Slice4/s4_hangar_a.png`).
- Three causes, found by blending one program at a time (only B and C produced it), alpha forced to 0 and 1, and a byte comparison of the copy with the step it replaces (equal):
  1. In frames that caught up several steps (the hangar at 360 fps: up to 4), the earlier steps were processed without drawing, assuming the newest step redraws everything. The hangar keeps what earlier steps drew in its frame buffers: those parts went stale (the dark panels). Earlier steps are now always drawn; only the newest is replaced by its blended copy.
  2. Repeated meshes (the hangar's wall panels: one mesh, many places) were paired by their order in the step; when the order changed, a panel was blended halfway to its neighbour. Runs with the same key are now paired with the unused candidate whose screen matrix is closest.
  3. A node whose origin is behind the camera (large walls) failed the jump guard and stayed unblended while coplanar parts were blended (depth fighting). The guard now applies only when both origins project; the pair is chosen by the closest matrix anyway.
- Performance: the hangar has about 5,000 program runs per step and 7 ms of GS processing per step; with interpolation it dropped 119 ticks in 600 (259 frames). Steps with more than 2,000 runs are no longer blended (flight: 550-850), and a frame that catches up more than one step adds no blended copy. (The 2,000-run limit was removed again on 2026-10-23, §2ad: mission 1 flight steps have 2,200-2,600 runs, so it switched interpolation off there.) Hangar afterwards: 0 ticks dropped, 658 frames (706 with interpolation off); mission 1: every run with output matched, 3,468 frames for 2,500 steps, 0 dropped; no-save intro session: 4,414 frames for 2,700 steps, 0 dropped.

## 2ad. Frame interpolation speed; explosions at 7× (2026-10-23)

User report (0.9.3, 4K display, 8×, SMAA, v-sync off, cap 360): interpolation gives very low frame rates; and are explosions at 7× the same as at 1×? Runs from the User's mission 1 input log (`--mission 1 --replay-input`, recorded with `--mission 1`; a copy that switches to missiles with SELECT at vblank 6360 and fires every 30 vblanks from 6380, `out/fi2/fire_m.ac4i`, not committed). New development parameter `--frame-interpolation on|off` (`command-line.md`), new log lines: frames at normal speed and blended copies (main), GPU wait per frame (renderer).

### FI-2. Speed

Mission 1, vblanks 6300-7500 at normal speed, 8×, SMAA, fullscreen 3840 × 2160, v-sync off, cap 360, RX 9070 XT. CPU-bound throughout (GPU wait under 0.1 ms per frame with interpolation).

| Build | fps | blended frames | notes |
|---|---|---|---|
| interpolation off | 113-116 | - | |
| 0.9.3 changes of §2ac FI-1 (2,000-run limit) | 95-107 | 54 of 1,912 | mission 1 steps have 2,224-2,589 runs: almost never blended, but the runs were still recorded |
| limit lifted, otherwise unchanged | 56 | 954 of 1,129 | 172 frames had to catch up 2+ ticks |
| + run logs reused | 84 | 1,680 of 1,683 | |
| + blended copy built on 4 threads | 96.5 | 1,929 of 1,932 | build 1.49 → 0.70 ms |
| + repeated texture uploads skipped | 99 | 1,982 of 1,985 | GS processing 6.0 → 5.3 ms per frame |

Hangar (mission 3 route, vblanks 4400-5000, same settings): 80 fps with interpolation (795 of 801 frames blended), 163 without; 0 ticks dropped either way.

- **Run logs reused** (`sce_host.h` `gs_runs_pool`, `libgraph.cpp`, `main.cpp`). The VIF records about 4 MB of VU memory per flight step for the interpolator; each step got a new log grown from empty and freed afterwards. In the profile (`tools/research/sample_profile.py`) the vector growth and page faults (`_Insert_counted_range`, `RtlAllocateHeap`, `NtFreeVirtualMemory`) under `Vif1::RunProgram` were 7.5 % of all samples, more than the GS processing (5.4 %) and over three times the VU1 programs they record (2.2 %). Finished logs now go back to a pool with their capacity, as the GS step buffers already did.
- **Parallel build** (`frame_interpolator.cpp`). Every repeated run starts from its own recorded VU memory, so the runs are independent: up to 3 helper threads (when the CPU has more than 4 hardware threads) and the main thread each repeat a quarter of them into per-run buffers, which are then joined in stream order. The output is the same bytes as before. The marks are shifted with a binary search instead of a scan per mark.
- **Repeated uploads skipped** (`gs_memory.cpp` `WriteImage`). A packet of an image transfer that repeats the last packet written at the same place (BITBLTBUF, TRXPOS, TRXREG, start pixel and size) with the same bytes, while every block it wrote still has the generation it had after that write, would store bytes the blocks already hold, so it is skipped. Exact: block generations change on every write that changes a byte. Flight steps upload about 1 MB of textures (mostly T8) in about 670 packets; 73 % of the packets are now skipped; the rest are real changes (texture slots reused within a step). Each blended copy repeats its step's uploads, so this helps interpolation most.
- **2,000-run limit removed** (`frame_interpolator.cpp`). It was added for the hangar's dropped ticks (§2ac FI-1); with the changes above the hangar keeps up.
- Checks: guest RAM hash log identical at all 7,600 vblanks with interpolation on and off (8×, SMAA). Screenshots of 141 vblanks (6600-8000) identical byte for byte to the build before these changes at 1× and at 7× (interpolation off). Hangar at vblanks 4450-4600 with interpolation on: no dark panels (FI-1), only the expected small motion differences against interpolation off. `ctest`: passed.
- Remaining cost per blended frame: GS processing of the whole step (5.3 ms) and the Vulkan command recording (2.6 ms) on the main thread. Moving `SubmitGsSteps` and `Record` to a render thread (`renderer.md` §2.2) would be the next step; not done.

### EX-1. Explosions at 7× against 1×

- Native screenshots (the 7× image box-averaged to 512 × 448) of the same vblanks at 1× and 7×, AA off: missile run vblanks 6600-8000 every 10 (141 frames), the User's original recording 4500-8100, the mission 1 opening (mission id 0x25) 920-4000, the no-save intro (0x23) 300-3000. Mean absolute difference 0.0-2.1 levels per channel, at most 0.94 % of pixels differing by more than 48 (vblank 6630: falling UGBM bombs a few pixels in size, thinner at 7×).
- Missile hit on the TU-95 at vblank 7376 and the burning bomber at 7400: compared in crops of the 7× image against the 1× image enlarged 7×. The burst has the same size, place and colours; at 7× it is sharper. Fire and smoke match. The fix of §2z (work page FBP 0x180 kept at 1×) holds at 7×.
- Not covered: a kill explosion close to the camera (none in these runs).

## 2ae. Invalid guest reads; mission 6 crash (2026-10-23)

User report: a session (development build of §2ad, 8×, interpolation on) with many `guest invalid read` errors and a crash in mission 6. The session log (saved to `out/m6crash/`, not committed) has 821 invalid reads and ends with exception 0xC0000094 (integer divide by zero).

### IR-1. Light colours read as pointers (fixed)

- The reads come in bursts of fading values (e.g. 0x180A1B35, 0x170A1B35, … 0x02000205): colours being darkened from frame to frame and used as addresses. They also occur in the attract-mode demos (title screen left idle; `--memcard` with a copy of the User's card, `--fast-until 40000`): 221 reads, the first at the same addresses as in the User's session.
- A stack capture on the first reads (temporary, removed) gave `func_001483A0` (player aircraft, `aircraft_models.cpp`): light 2 of the model pass took its colour as `word (word A+0x70)[p]`, a pointer to a table. The original reads `lw a2, 0x70(A + 4p)` (0x001490DC, 0x00149604) and `lw a2, 0x78(A + 4p)` (0x00149238, 0x0014984C): the colours are words in A itself, after the two light vectors at A+0x50/A+0x60. The port read the colour, then used it as an address: light 2 got colour 0 (address outside RAM) or some unrelated RAM word. Both sites fixed; rendering.md corrected.
- Only the light-2 colour of the player aircraft's model passes changes (drawing only). The demo run now has 0 invalid reads; `ctest` passed.

### CR-1. Mission 6 crash (not reproduced)

- Crash in `func_00252C30` (radio chatter, `radio_chatter.cpp`): the period u16 at header +2 (header = word 0x004BA300) was 0. The original has a `break 7` for a zero divisor at 0x00252D74, so on the PS2 the same state would also stop the game: the header or its pointer was overwritten earlier.
- About 1.4 s before, one burst of invalid reads: 10 one-byte reads at 0xFA13E7A0…0xFFFB0999, then reads of 8 and 4 bytes from 0x02050008 up through every 64 KiB page to 0x03D40008, then 0x3CA00058. This looks like a loop that ran with a garbage count or pointer and walked from inside RAM to past its end, and probably overwrote RAM on the way (writes inside RAM are not logged). No invalid writes were logged.
- Not related to IR-1 (those are reads only). Mission 6 idle for 50,000 vblanks (`--mission 6`) did not reproduce it; it needs the User's flight. The log does not say which function made the reads; the next step is an input recording of a session that shows it (`--record-input`), or the caller in the invalid-access log line.

## 2af. Mission 6 report (2026-10-23)

User report from campaign mission 6 (mission id 5: `GetMissionId(0, 6)`, table 0x00384800), compared with PCSX2: (1) explosions differ at 8×; (2) no QUIT GAME after a save is loaded (the campaign menu, page 5); (3) after a landing, rearm and takeoff the game does not go back to the mission; (4) after the landing the camera keeps shaking with the aircraft standing still; (5) terrain textures broken at times (low over the city).

### TO-1. Takeoff after a rearm (fixed)

- `func_0017AC68` sub 0x1A: the original resets the counter +0x27C while aircraft +0x188 bit 0x20000000 (all three wheels on the ground, `player_flight.cpp`) is set and adds K = *0x0040EC14 (1/60) while it is clear (0x0017ADB4-0x0017ADEC: `beql` on the masked bit stores 0); above 5, i.e. 5 s in the air, it requests sub 0x1B (success: radio 0x11 "Altitude restrictions canceled", fade, sub 0xB). The translation had the two branches swapped: the counter grew only while rolling on the runway and was reset at lift-off, so in the air sub 0x1A stayed until its timer (0xE10 frames) ran out. Branches swapped back. Checked by the User in the game (2026-10-24) and in the replay of the User's recording: sub 0x1B and the return to the mission at vblank ~13300.

User recording 2026-10-24 (port, development build, 8×, interpolation on): mission 6 from the User's save (card unchanged since 2026-10-23 22:35, copy in `out/m6crash/card`), a kill, landing, rearm, takeoff, quit to the campaign menu; 14,301 vblanks, `out/m6rec/input.ac4i` (not committed). It replays in sync to the end (`--memcard` copy of the card, `--replay-input`).

### VB-1. Vibration and camera shake after the landing (fixed)

- User report: once landed the controller keeps vibrating (and, report of 2026-10-23, the camera keeps shaking: its strength is the pad vibration level, `func_00257360`).
- Temporary per-tick log of the vibration slots in the replay: from touchdown to the rearm menu a kind 0 slot (curve kind, limits *0x00410464 / *0x00410468 = 0.2 / 0.8) ran every tick with the value 1 at full strength (120).
- Kind 0 is requested by `UpdatePlayerAircraft` (`player_flight.cpp`) with the air-brake level P+0x24C, which rises to 1 at idle throttle. The original skips the request while P+0x188 bit 0x40000000 (a wheel on the ground) is set (0x0011FD7C-0x0011FD8C: `nor`, `and`, `beql` to the skip): the air-brake vibration runs only in the air. The translation requested it only on the ground. Fixed; after the fix both motors are 0 from touchdown (sub 0x16) on, and the takeoff roll (sub 0x1A) vibrates by speed as before.
- TO-1 and VB-1 are the same mistake: a bit test written `(~x & m) == 0` whose taken `beql` branch is the *skip*. Every other `(~x & m) == 0` site in `src/game` that acts on the bit (20 sites: `FlightPlayFrame`, `FlightSimStep`, `RunMissionEvents`, 0x0011A3D8, the landing subs 0x0017A1B8, 0x00126758, `UpdatePlayerSound` 0x001270B8, 0x00128090, `UpdateGroundUnit`, `DetectAIEvents`, ...) was compared with its branch in the disassembly: all match.

### EX-2. Explosions at 8×

- Mission 1 missile run of §2ad EX-1 at 8× against 1× (`--fast-until` past the screenshots so they land on the vblank asked for): 0.08-0.10 % of pixels differ by more than 48 at vblanks 7370-7400, the TU-95 hit included. The difference the User sees is specific to mission 6; not reproduced yet.
- Note: screenshots taken after `--fast-until` has ended can show an older step at 8× (the HUD clock 3 ticks behind): compare only screenshots inside the fast-forward range.

### EX-3. Explosion fire missing at every internal scale (fixed)

- User report with a PCSX2 picture: an aircraft destroyed in mission 6 explodes in an orange fireball in PCSX2; in the port only debris and black smoke. Replay of the User's recording at 1×: the same, so not a scaling problem (EX-2 compared 7× / 8× with 1×, never with PCSX2).
- The game spawns the explosion (kind 0x81 hit, 0xC1 kill: type 1 of the table *0x002B856C, with debris) and `func_001F1678` draws its 8 particles in two passes: smoke (colour 0x030105, ALPHA (Cs-Cd)·As+Cd, texture rows 64-127) and fire (colour 0x437DFF, additive, rows 0-63) from the 512 × 128 CT32 work page at TBP 0x3000 (FBP 0x180).
- The work page is drawn every frame by `func_001EEBE0` in GS context 2: glow strips, then a copy with RGBAQ 0x4000, then the page read as CT16S with region-repeat clamp U & 0x3F7 (each 32-bit word's low half copied into its high half: B := R, A := G), then R and G set to 0x80 with FBMSK 0xFFFF0000. Result: RGB (0x80, 0x80, 0), alpha = the glow shape.
- Port bug (`effect_draw.cpp`): the SCISSOR_2 values of these passes were translated as 0x7F01FF0000 / 0xFF01FF0000 (SCAY0 0x7F, SCAY1 0): an empty box, so the port drew nothing into the page and the fire sampled stale local memory (the city's textures; the smoke looked right because it is nearly black). The original builds them as `lui 0x7F; dsll 16; ori 0x1FF; dsll 16` = 0x007F000001FF0000 (0x001EED14, 0x001EFFB4, 0x001F0628). The same slip in the large-smoke overlay `func_0020D258` / heat haze `func_0020DE08` (0x01BF000000FF0000, 0x01BF000001FF0000: 0x0020D3D4, 0x0020DA34, 0x0020DFCC), which therefore never drew either. Six constants fixed. Found by reading the page back from the GPU (temporary): `used_height` 0, every draw rejected by its scissor.
- After the fix: the fireball at the hit and the kill (vblanks 6290-6440) and burning debris, as in the PCSX2 picture. A temporary log of every draw with an empty scissor over the whole 14,301-vblank replay found none left.
- Still to compare: a PCSX2 frame of the same explosion (the GS dump `gameplay_attacking_bombers_explosions_hud` has no close explosion, and its memory image holds no GPU-drawn page).

### QG-1. QUIT GAME in the campaign menu

- User clarification 2026-10-24: the menu without QUIT GAME is the campaign menu (START MISSION ... LOAD) reached after quitting a mission. QUIT GAME added as its last item, after LOAD (port-features §9). Checked with QUIT GAME forced on in a scripted run (temporary): the item is typed in with the others, the help line is empty on it and the next item's help is typed again, selecting it fades out and ends the session (31 vblanks after the press).
- Also fixed in both menus: after QUIT GAME, moving back to the item shown before it left the help line empty (`SetMessageBoxText(-1)` keeps the shown message id); the port line now resets it.

### Not reproduced yet

- (2) QUIT GAME (resolved by QG-1, kept for the record): User clarification: it is missing from the main menu (NEW GAME, CONTINUE, LOAD, TUTORIAL, VS MODE, OPTIONS) after coming back from a mission. Not reproduced (QUIT GAME forced on for these scripted runs with a copy of the User's card, temporary change removed): present after LOAD from the main menu; after `--mission 1`, QUIT MISSION, campaign menu, OPTIONS > TITLE, START; and the same with `--frame-interpolation on` (the menus get no blended copies). Every builder of the main menu tree (`MainMenuSetup`, the cursor stores) applies `QuitItemPatch`. Needs the User's route.
- (4) camera shake (resolved by VB-1, kept for the record): its strength is the pad vibration level (`func_00257360`: strong motor D_00411888, else weak D_00411880), so a vibration left running would also shake the camera. The vibration tick `func_00257890` matches the original instruction by instruction; the ground roll request (kind 3) scales with the ground speed P+0x130, which is clamped to exactly 0 at a standstill, and the ground model resets P+0x2DC/P+0x2E0 (kinds 8, 9) to 1. Needs the landing.
- (5) terrain: mission 6 flown straight (`--mission 6`), vblanks 1200-4200, 1× against 8×: at most 0.09 % of pixels differ, nothing broken. Needs the low flight over the city.

## 2ag. Second mission 6 crash (2026-10-24)

User report: in mission 6 an enemy subtitle turned black (normally red) a few seconds before the game crashed. No input recording of that session; the session log (development build with §2af, 8×) is saved in `out/m6crash2/` (not committed).

### CR-2. What the log shows

- 13.9 min into the mission (log 935.8 s): 15 one-byte accesses in one frame at addresses made of two halfwords (0x1F710101, 0x0D5A0028, ...): a byte loop following broken pointers. Then the black subtitle (the subtitle colour comes from the speaker class of the request, `func_00252B88`).
- 948.8 s: two-byte accesses at 0xFFFD1BBD, 0xFFFE0001, 0xFFFF0001, then accesses walking up through every 64 KiB page from 0x02000008 (the end of RAM) to 0x0C6D0000 (29,015 one-byte, 1,253 four-byte, 470 eight-byte); the log ends there (the game stopped responding or was closed). The one-byte addresses are floats plus 0x98 (0x45F19898 = 7731.0 + 0x98, 0x3F800098 = 1.0 + 0x98, 0xC33E0098 = -190.0 + 0x98) and strings plus small offsets: a loop reading pointers from a table and a byte at +0x98 from each, run past the table into float data and on past the end of RAM.
- That matches the per-unit loops over `Unit(i)` (table 0x002A1F70) with the count `W(W(0x00410640))`, e.g. `UpdateUnitStates` 0x0011BE30, which **writes** 0 to every `Unit(i) + 0x98`: with a garbage count it zeroes a byte at every word-as-pointer + 0x98 in RAM. This also explains the first crash (CR-1: the radio header 0x004BA300 overwritten after the same kind of walk) and the black subtitle (radio request data hit). Writes through `GRef` to an invalid address were logged as "read", which hid that these loops write.
- The unit count is only written at mission load (`flight_units.cpp`), so something else overwrites the count or the list pointer 0x00410640 first. Not found: the User's earlier recording continued from after the takeoff with neutral input (`out/m6rec/cont.ac4i`, 70,000 more vblanks) shows no invalid access (the aircraft is shot down within a minute and the replay loops); mission 6 idle (CR-1) neither.
- Audited on the way: `func_00252C30`'s ring store and its threshold selection match the original.

### Diagnostics added

- `GuestMemory::ReportInvalid` (`src/common/guest_memory.cpp`): the line now says `read`, `write` or `access (read or write)` (a `GRef` / `ps2ptr` reference, which the caller may write), and ends with `from` and the host return addresses (`ProjectStonehenge.exe+0x...`, up to 6, `platform::DescribeCallers` in `src/platform/win32/crash_win32.cpp`), symbolised with the PDB: `python tools/research/symbolize.py <exe> --log <session.log>` (the exact build that wrote the log). Still once per 64 KiB page. Test: `guest memory: an invalid page is logged once, with the kind of access and the host caller`.
- Next step: a session that shows it again, with this build and preferably `--record-input` (a copy of the saves folder taken before playing, so the replay starts from the same card).

## 2ah. Third mission 6 crash: the track stream runs into the radio header (2026-10-25)

User session with the §2ag build and `--record-input` (36,507 ticks; log, recording and the card copy in `out/m6crash3/`, not committed). The log has one invalid access, a two-byte access at 0xFFFD1DCF from `func_00251050` (`radio_chatter.cpp`), then the crash: integer divide by zero in `func_00252C30` (`rem = frame % period`, period read through the same garbage bank pointer) about 540 s into the mission.

### CR-3. Cause

- The replay (`--memcard <card copy> --replay-input out/m6crash3/input.ac4i`) reproduces the crash exactly.
- New research tool `tools/research/watch_guest.py`: runs the port under a minimal debugger and sets x64 hardware write breakpoints on guest addresses (host stack symbolised from the PDB, optional RAM dump at hit N). No port changes. Run with `python -P` (the local `tools/research/dis.py` shadows the standard `dis` module).
- Watching 0x004BA300 (the radio header copy: word 0 = bank header pointer) gives 2 writes at mission load (the header copy) and then, at the crash, 2 more from **`func_0024CDD0`** (`flight_sequences.cpp:801-802`): the position stores of the once-a-second track block.
- `func_0024CBE0` starts the track stream at 0x0043A300; `func_0024CDD0` appends one block per mission second (status +0x28 counts frames, the block is written when it is a multiple of 60): `{1, second}`, then a 4-byte record per tracked unit of kind 2/3 and a 16-byte record per other tracked unit, and a 0 terminator. The stream has no bound check (neither has the original); 0x80000 bytes after its start lie the radio header (0x004BA300) and the message pointer table (0x004BA570).
- RAM at the first write to 0x004BA200 (`--dump`): 540 blocks with 110 records in second 0 and 83-94 later, about 970 bytes per second, so the buffer was full after 9 minutes (mission 6's limit is 15 minutes, rules +0x14 = 54,000 frames).
- **PCSX2 (run 13 of `../pcsx2/sessions.md`)**: mission 6 to 1'54" before the end with no overrun; 7 units in second 0 (the player and units 44-49), 22-37 later; 227 KB used after 785 s. The port recorded exactly the units the original skips.
- **Cause: an inverted test in the translation.** The unit test at 0x0024CFE8 is `nor v0, zero, flags`, then `and` with 0x10000000 and `andi v0, v0, 0x2000`, both on `~flags`: a unit is skipped when bit 0x10000000 **or bit 0x2000** is clear. The translation skipped units with bit 0x2000 set (the old comment said "not 0x2000"). Applied to the PCSX2 state, the original's test gives the 34 units that the PS2 recorded in its last block.
- This explains all three mission 6 reports: the overrun wrote over the radio header and message pointers (black subtitle: a speaker class read from overwritten request data; CR-1: header overwritten; CR-2: the garbage pointers then fed other loops, with a wrong unit count, that wrote around RAM until the game hung).

### Fix and verification

- `src/game/flight_sequences.cpp` `func_0024CDD0`: `(~flags & 0x2000u) != 0` (skip without bit 0x2000), comment corrected. `ctest --preset dev` passes.
- The User's recording replayed with the fix to vblank 37,500 (past the old crash): no invalid access, no crash; 0x004BA300 written only at mission load. The units recorded in seconds 0, 1 and 30 are the same as in the PCSX2 session (other inputs); later they differ with the targets destroyed. About 280 bytes per second (250 KB for 15 minutes).
- Ghidra: plate comment on `func_0024CDD0` (logged).
- Lesson for translations: `nor` followed by several `and`/`andi` tests means every test is on the inverted flags.

## 3. Recommendations

1. Treat effect draw notes as orientation only. Translate packet code from the disassembly (already the rule in the `port-function` skill).
2. Before translating any function, check the appendix of `../game/random.md` for random reads. The translation must keep their order.
3. Rerun `tools/research/validate.py` after each research pass and before each commit of `coverage-functions.csv`.

## Samples

### First sample (seed 20261011)

| Address | Name | Subsystem | Group | Score | Comment |
|---|---|---|---|---|---|
| 0x00131DC8 | `func_00131DC8` | ai | understood | agree | ground vehicle move/turret AI incl. class-4 proximity hold and NextRandomFloat speed/offset |
| 0x0010D6A0 | `func_0010D6A0` | camera | understood | agree | view slot reset (0x440 bytes; two camera records +0xB0/+0x270); note terse |
| 0x00144DC8 | `func_00144DC8` | collision | understood | agree | segment vs static objects of one cell via 0x00143EF8 |
| 0x00105778 | `ApplyVibrationOptions` | core | understood | agree | vibration option bit 8 per port; CSV note empty (name only) |
| 0x00105178 | `func_00105178` | core | understood | minor | doc omitted the sign-extension word left in the matrix for negative scales (V-3) |
| 0x00100DC8 | `NextRandomFloat` | core | understood | agree | table 0x00411998 cursor 0x00411780 |
| 0x00100AC8 | `RequestSubState` | core | understood | agree | ctx req 2 + next sub |
| 0x0015E778 | `thunk_FUN_00155ed8` | effects | understood | agree | thunk to pool reset |
| 0x001E0700 | `func_001E0700` | effects | understood | agree | lowest-free bitmap allocator |
| 0x00155ED8 | `func_00155ED8` | effects | understood | agree | pool reset wrapper |
| 0x001558C8 | `func_001558C8` | effects | understood | agree | effect state packet (0x00155188) + store at +0x6249C |
| 0x0015E740 | `thunk_FUN_00156290` | effects | understood | agree | thunk to 256-entry pool reset |
| 0x001270B8 | `func_001270B8` | flight | understood | agree | player per-frame engine sound block 0x420610 + warning loops |
| 0x0023C8B8 | `func_0023C8B8` | frontend | understood | agree | save page message id by state and card status |
| 0x00239E10 | `func_00239E10` | frontend | understood | agree | cursor column x = base + n*0x140 |
| 0x00236F28 | `func_00236F28` | frontend | understood | agree | orbit 90 s period + easing |
| 0x00245B88 | `func_00245B88` | frontend | understood | agree | help text by state with typewriter reveal (HDD page context not checkable blind) |
| 0x00228EB8 | `func_00228EB8` | frontend | understood | minor | doc omitted the first stage black -> 0x765834 |
| 0x00235580 | `func_00235580` | frontend | understood | agree | tail call to hangar panel reset 0x00234078 |
| 0x0014B3F8 | `func_0014B3F8` | frontend | understood | major | doc said light/colour constants; code uploads camera world->screen/world->clip matrices + one vector (V-4) |
| 0x00105448 | `func_00105448` | frontend | understood | agree | save statistics clamps identical |
| 0x0025C410 | `func_0025C410` | hdd | understood | agree | partition create + sub-partitions (doc terse) |
| 0x00161890 | `func_00161890` | hud | understood | minor | doc omitted target marker and direction sprite |
| 0x0015FEE0 | `DrawHudSprite` | hud | understood | agree | sprite with 16:9 width 3/4 and half-texel offsets |
| 0x0010FB30 | `func_0010FB30` | mission | understood | major | doc said mission end kill bookkeeping; code is the phase-change unit activation/retire (V-5) |
| 0x00179998 | `func_00179998` | mission | understood | minor | doc omitted restore of other units' flags from 0x002B7828 |
| 0x0017BAF0 | `func_0017BAF0` | mission | understood | agree | 8-entry proximity queue |
| 0x00213A38 | `func_00213A38` | movie | understood | minor | doc did not say which register (TEX0_1) |
| 0x002507F0 | `func_002507F0` | radio | understood | agree | bounded table lookup |
| 0x001118C8 | `func_001118C8` | render | understood | agree | sky block init + GIF upload |
| 0x001152D8 | `func_001152D8` | render | understood | agree | cloud puffs up to 400 sorted |
| 0x00138C28 | `func_00138C28` | render | understood | minor | REF address is header+0x10 (0x00298AD0) |
| 0x00171460 | `func_00171460` | replay | understood | agree | segment vs terrain/objects for camera clamp |
| 0x0024B248 | `func_0024B248` | results | understood | agree | results text page |
| 0x00220BF0 | `func_00220BF0` | sequence | understood | agree | create/clear character set +0xE4 |
| 0x00219B48 | `func_00219B48` | sound | understood | agree | bank loader state reset base/base+0x30000 |
| 0x00218D58 | `func_00218D58` | sound | understood | agree | getter (bank count not verifiable blind) |
| 0x0021AAC8 | `func_0021AAC8` | sound | understood | minor | doc omitted the addition before the wrap |
| 0x0014F398 | `func_0014F398` | weapons | understood | agree | shell init radius 500 speed 510 |
| 0x0014BB40 | `UpdateProjectiles` | weapons | understood | agree | 7 pools in order |
| 0x001A5DA8 | `func_001A5DA8` | effects | partly | agree | plume sprite cluster with wind vector and table 0x002B91C0 |
| 0x001FF818 | `func_001FF818` | effects | partly | major | doc said trail parameters/colour table; code builds the frustum clip structure 0x002BB990 (V-6) |
| 0x00189B88 | `func_00189B88` | effects | partly | agree | record init with two NextRandomFloat reads |
| 0x002034A8 | `func_002034A8` | effects | partly | minor | labels: +0x624E0 is the clip matrix (not light) and +0x62494/98 near/far depths (not frame range) (V-6) |
| 0x001E88C0 | `func_001E88C0` | effects | partly | major | doc said plain afterburner sprite; code is a two-pass heat-haze distortion sampling the framebuffer with dest-alpha blend (V-7) |
| 0x001E81F0 | `func_001E81F0` | effects | partly | minor | afterburner draw is a heat-haze distortion (label) |
| 0x001EB728 | `func_001EB728` | effects | partly | agree | empty |
| 0x001DDAC0 | `func_001DDAC0` | effects | partly | agree | 12-entry trail ring every second frame |
| 0x001E24C8 | `func_001E24C8` | effects | partly | minor | LCG-placed sprite cluster agrees; doc omits ballistic motion and keyframe curves |
| 0x00202290 | `func_00202290` | effects | partly | agree | smoke trail ribbon pool B |
| 0x001CD6D8 | `func_001CD6D8` | effects | partly | minor | doc omits ground-plane intersection; puffs from table 0x002B91C0 agree |
| 0x00254340 | `func_00254340` | radio | partly | minor | note had contradictory queue number (3 vs 4); behaviour agrees |
| 0x00138350 | `func_00138350` | render | partly | agree | sea tiles with scrolling UV and fog alpha |
| 0x00171CD0 | `func_00171CD0` | replay | partly | minor | wrong offset constants in doc (style 8 too) |
| 0x00223B80 | `func_00223B80` | sequence | partly | minor | headline said texture/CLUT upload; body (strip mesh with ADC) agrees |
| 0x00259C40 | `func_00259C40` | hdd | dead | agree | getter +0x4380 |
| 0x002208E0 | `func_002208E0` | sequence | dead | agree | empty |
| 0x0021E200 | `func_0021E200` | sound | dead | agree | SetSoundPitch wrapper |
| 0x0024D7A8 | `func_0024D7A8` | unreached | dead | minor | note was empty |
| 0x0017C180 | `func_0017C180` | unreached | dead | minor | note was empty |

### Wider re-review (seed 20261012)

| Address | Name | Subsystem | Group | Score | Comment |
|---|---|---|---|---|---|
| 0x001F69A0 | `func_001F69A0` | effects | wide | minor | kind dispatch imprecise (kinds 1/3 -> 0x001F7EA8 |
| 0x0017F760 | `func_0017F760` | effects | wide | agree | scratchpad near/far range |
| 0x00185C48 | `func_00185C48` | effects | wide | minor | doc says up to 8 puffs; code draws 16 segments with a 16-entry jitter table; brightness from 0x001152A8 cloud function |
| 0x0015DDA8 | `func_0015DDA8` | effects | wide | agree | collect pass for 64-slot pool |
| 0x001859D8 | `func_001859D8` | effects | wide | agree | age = 0 |
| 0x00189BF8 | `func_00189BF8` | effects | wide | agree | 108-frame lifetime |
| 0x001CFAF8 | `func_001CFAF8` | effects | wide | agree | mission 7 colour switch |
| 0x001F3C08 | `func_001F3C08` | effects | wide | agree | matrices + near/far / 4 + size factor by view mode |
| 0x001B1BD8 | `func_001B1BD8` | effects | wide | agree | special object 0x15 fire/smoke column (doc thin) |
| 0x001D0BB0 | `func_001D0BB0` | effects | wide | agree | scratchpad parameters |
| 0x0015E5F0 | `func_0015E5F0` | effects | wide | agree | draw wrapper with depth sort |
| 0x001FF218 | `func_001FF218` | effects | wide | agree | allocator |
| 0x001FF020 | `func_001FF020` | effects | wide | agree | allocator |
| 0x00181BB0 | `func_00181BB0` | effects | wide | agree | spawn/refresh; random read in init 0x0017F6D8 documented |
| 0x00157728 | `func_00157728` | effects | wide | agree | update wrapper frees expired records |
| 0x00155FE8 | `func_00155FE8` | effects | wide | agree | reset wrapper |
| 0x00188A50 | `func_00188A50` | effects | wide | minor | two NextRandomFloat reads in the spawn not documented (V-8) |
| 0x0015E198 | `func_0015E198` | effects | wide | agree | draw loop over 8-slot pool |
| 0x0023D0E0 | `func_0023D0E0` | frontend | wide | agree | load slot into 0x002A0318 |
| 0x00230080 | `SetAircraftViewerAircraft` | frontend | wide | agree | viewer request gated by state 2/3 |
| 0x00234078 | `func_00234078` | frontend | wide | agree | hangar panel reset by category table 0x0039E128 |
| 0x002285D8 | `func_002285D8` | frontend | wide | agree | 6 hint icons with pulse colours |
| 0x00241708 | `func_00241708` | frontend | wide | agree | records text page |
| 0x00238380 | `func_00238380` | frontend | wide | agree | briefing movie sub-state |
| 0x002294F8 | `func_002294F8` | frontend | wide | agree | list widget frames (colour fade 0x00228EB8) |
| 0x00226F98 | `func_00226F98` | frontend | wide | agree | hint bar easing 0.88/0.9 |
| 0x002309C0 | `func_002309C0` | frontend | wide | minor | doc says leave to MenuGoTo; this function only enters state 2 with a fade |
| 0x002468B0 | `func_002468B0` | frontend | wide | agree | HDD install page draw |
| 0x0022E580 | `func_0022E580` | frontend | wide | agree | viewer instance allocation |
| 0x0023A208 | `func_0023A208` | frontend | wide | minor | s16 rect is lerped linearly by t; only the two floats use the eased value |
| 0x00238FE0 | `func_00238FE0` | frontend | wide | agree | movie page draw + send + swap |
| 0x002409D0 | `func_002409D0` | frontend | wide | agree | white/grey/black patch grid |
| 0x0023CF80 | `func_0023CF80` | frontend | wide | agree | store campaign block into slot |
| 0x00234A08 | `func_00234A08` | frontend | wide | agree | special weapon cycling among up to 3 owned options |
| 0x0010A898 | `FlightSceneEnter` | mission | wide | agree | subs 0/1 create scene + mission kind + double buffer |
| 0x00135C68 | `func_00135C68` | mission | wide | agree | running minimum vs line +0x1C |
| 0x0010B500 | `func_0010B500` | mission | wide | agree | mid-mission sequences by mission id and part |
| 0x0010B038 | `StoreFlightDataPointers` | mission | wide | minor | CSV note empty (name only); behaviour per subs 2-5 confirmed |
| 0x0017B740 | `func_0017B740` | mission | wide | minor | doc omits the libc rand() call per fired trigger (rand() % 1 via 0x00252FB8) |
| 0x0017BFB0 | `func_0017BFB0` | mission | wide | minor | requests (ORs the mask) rather than draws the tutorial hints |
| 0x0017E460 | `func_0017E460` | mission | wide | agree | rearm menu draw + special-weapon panel |
| 0x00176BA0 | `func_00176BA0` | mission | wide | agree | clear secondary lock |
| 0x0010AA50 | `CreateFlightSystems` | mission | wide | agree | sub 7 create subsystems + heap mark |
| 0x0010A3E0 | `func_0010A3E0` | mission | wide | minor | doc omits the fade level +0x1670 = 1.0 |
| 0x0010A130 | `func_0010A130` | mission | wide | agree | fade target/time + current level |
| 0x0010BCD8 | `func_0010BCD8` | mission | wide | agree | sub 0x1D frame without the sim step |
| 0x0017B890 | `func_0017B890` | mission | wide | agree | reset list fields |
| 0x001769C0 | `func_001769C0` | mission | wide | agree | drop stale locks |
| 0x0017EB30 | `func_0017EB30` | mission | wide | agree | menu item animation |
| 0x0017E1D0 | `func_0017E1D0` | mission | wide | agree | game-over menu reset/layout |

## 2ai. GS pipeline hitches on a new GPU driver (2026-10-25)

User report: on a new PC or graphics driver (often NVIDIA) the frame rate stutters from the hangar to the end of mission 1, with micro hitches down to about 5 fps, worst with several enemies and terrain in view; later sessions are smooth.

### PC-1. Pipelines compiled inside frames
- Code: no `VkPipelineCache` was used anywhere. The fixed pipelines (output pass, anti-aliasing, depth copy, conversion, texture decode, movie upscaling) are built at start, but the GS draw pipelines were built by `GsBackend::GetPipeline` on the main thread the first time a draw state (blend, colour write mask, depth test and write: `PipelineKey`) appeared. `gs.frag` is one large shader whose behaviour is chosen by push constants, so every new key makes the driver compile it again. With a warm driver cache this is fast (§2u: 14 pipelines in 2.4 ms); with a cold one (new GPU, new driver, new build of the shaders) it happens inside the frame.
- Measured with a temporary local edit, since reverted: the fragment module's SPIR-V generator word was set to a new value every session, so the driver treats it as a new shader. The AMD driver cache folder outside the repo was left alone. Development build, RX 9070 XT, driver 26.9.2, 1×, v-sync on, the User's mission 1 input log (`--mission 1 --replay-input out/runs/user_1022.ac4i --exit-at 8000`):

| Run | GS pipelines built in frames | compile time in frames | slowest | ticks dropped | fps |
|---|---|---|---|---|---|
| warm driver cache, before | 24 | a few ms (§2u) | – | 0 | 60.0 |
| cold, before | 24 | 7,897 ms | 913 ms | 439 | 56.6 |
| cold, with the warm-up | 0 (34 built at startup in 2,862 ms, before the ISO check ended) | 0 | – | 0 | 59.9 |

- Every `full.json` job with the cold shader (run `pk1`, 30 jobs in parallel, so each compile is slower): 3–24 pipelines per session, 2.6–10.0 s of compile time inside frames per session, 0.87–1.07 s for the slowest one. A frame that waits 0.9 s is the reported "5 fps" moment. The hangar and the first mission are where most new draw states first appear.
- Keys: 34 distinct keys over the 30 jobs (front end, VS mode, shop, hangar routes, opening, missions 1–18) and the mission 1 input log. Merged with the new `tools/research/pipeline_keys.py` from the exit lines of the session logs.

### Fix
- **Warm-up** (`gs_pipeline_keys.h`, `GsBackend::StartPipelineWarmUp`): the 34 keys are built at start by up to 4 threads (half the hardware threads). `GetPipeline` returns a pipeline that is already built. It waits only for a key that a warm-up thread is building at that moment, and builds a key missing from the table itself, as before. At exit the log lists:
  - the keys built during frames (`GS pipelines: N built during frames (…): keys`), so the table can be completed;
  - every key in use (`GS pipelines: N in use; keys: …`);
  - the warm-up time (`GS pipelines prepared at startup: N in … ms`).
- **Pipeline cache file** (`cache\pipelines.bin`; User approval 2026-10-25; ADR-005 §2 and `port-features.md` §6 updated). Every renderer pipeline is created through one `VkPipelineCache`, loaded at start and saved at exit when its size changed (temporary file, then rename). The file is used only when its header names this GPU and driver (vendor, device, `pipelineCacheUUID`). Otherwise, or when it is damaged or over 64 MiB, it is deleted and the session starts empty. With the cold shader the file was 2.6 MB; a second session loaded it and left it unchanged. On this PC the driver's own cache is warm for the released shaders, so the file's own benefit (sessions after a cleared or evicted driver cache) cannot be shown here: the User's NVIDIA check below covers it.
- Tests: `pipeline cache: header must name the same GPU and driver`, `pipeline cache: damaged headers are rejected`, `pipeline cache: file written whole and read back`.
- Regression (`full.json`, run `pc2` with the final build, against `pk1`: the same code with only the measuring log, pipelines built in frames). All 30 jobs exit cleanly with no warning. All RAM checkpoints and hash logs are identical, and 108 of 110 files are identical; `m3_route` / `m4_route` screenshot 5300 differ only in the loading-fade column x = 409 (IR-1). In every job the warm-up finished in 4–14 ms (warm driver cache), and no job built a pipeline during frames. Development tests pass.

### Render thread (User question, 2026-10-25)
Findings before it was built (built the same day after RT-1: §2aj):
- The GS data the renderer reads are self-contained copies of each step's GIF stream, and the game never reads GS state back. Processing and recording a frame on a second thread while the game runs the next tick would therefore not change game logic: ticks, input latching and the clock stay on the main thread, and validation runs stay deterministic.
- It would raise throughput at high refresh rates. Mission 1 at 1× would cost about max(tick 4.4 ms, GS 4.1 + recording 2.5 ms) per frame instead of their sum (§2ac).
- Costs: about one frame of extra latency, and synchronisation for settings changes, screenshots, `--dump-vram`, the overlay and the movie picture.
- It would not have fixed PC-1: a compile would have stalled the render thread, and so the picture.

### RT-1. Where the game tick's time goes (User request, 2026-10-25)
A temporary probe (since removed) timed each game tick: the whole tick, the VIF1 sends (`sceDmaSend` channel 1), the VIF processing inside them, and the VU1 programs inside that. It also timed a `memcpy` of the same bytes the VIF reads: the cost of the snapshot the game thread would still make if the VIF work moved to another thread. Setup: development build, RX 9070 XT PC, 1×, normal speed. Mission 1 = the User's input log, vblanks 1500–8000. Hangar = the `m3_route` pad script with card S, vblanks 4400–5000. Interpolation on = v-sync off, cap 360, `--frame-interpolation on`. Values are per tick:

| Scene | tick | VIF1 sends | VIF processing | of which VU1 programs | chain walk and the rest | snapshot copy (bytes) | game code (tick − VIF1 sends) |
|---|---|---|---|---|---|---|---|
| mission 1, interpolation off | 5.24 ms | 2.18 ms | 1.39 ms | 0.50 ms | 0.78 ms | 0.29 ms (1.4 MiB) | 3.06 ms |
| mission 1, interpolation on | 5.36 ms | 2.29 ms | 1.49 ms | 0.58 ms | 0.80 ms | 0.30 ms (1.4 MiB) | 3.07 ms |
| hangar, interpolation off | 8.15 ms | 6.28 ms | 4.47 ms | 1.99 ms | 1.82 ms | 0.65 ms (2.6 MiB) | 1.86 ms |
| hangar, interpolation on | 8.61 ms | 6.65 ms | 4.81 ms | 2.28 ms | 1.85 ms | 0.73 ms (2.6 MiB) | 1.96 ms |

- VIF processing = unpacking into VU memory, VIF commands, direct GIF data and the VU1 programs. With interpolation on, the VU1 time also includes the copy of VU memory into the run log.
- "Chain walk and the rest" also contains the probe's own clock reads, so the walk itself is somewhat cheaper.
- Renderer work per frame in mission 1 (session averages): GS processing 1.88 ms and recording 2.42 ms with interpolation off. The hangar's session averages (0.87 / 0.58 ms) include the menus before it, so they do not describe the hangar alone (§2ac measured about 7 ms of GS processing per hangar step).

What a render thread would gain (estimates from the table, interpolation off):

| Scene | today, one thread (tick + GS + recording) | group 1: GS processing and recording on a render thread | groups 1 + 2: VIF processing moved as well (game thread keeps walk and copy) |
|---|---|---|---|
| mission 1 | about 9.5 ms | max(game 5.2, render 4.3) ≈ 5.2 ms | max(game 4.1, render 5.7) ≈ 5.7 ms: no gain, the render thread becomes the slower side |
| hangar | about 8.2 ms + its renderer work | the game thread, 8.2 ms | game thread about 4.3 ms; render thread its renderer work + 4.5 ms |

- **Group 1** (GS processing, recording, interpolation build, present) gives most of the gain in flight. The game tick then sets the pace.
- **Group 2** (VIF and VU1 work) helps only scenes where the VIF dominates the tick, like the hangar (4.5 of 8.2 ms). In flight it only moves 1.4 ms from one thread to the other. If it is done, it belongs on its own worker (fed with the snapshot in send order), not on the render thread.
- At 60 Hz with v-sync every case already fits in 16.7 ms on this PC. The gain matters for high refresh rates with interpolation, and for slower CPUs.

### Pending: User check on NVIDIA
On the NVIDIA PC, with a new build:
1. Close the port.
2. Delete the driver's shader caches (`%LOCALAPPDATA%\NVIDIA\DXCache` and `%LOCALAPPDATA%\NVIDIA\GLCache`) and the port's `%LOCALAPPDATA%\ProjectStonehenge\cache\`.
3. Start the port and play from the hangar to the end of mission 1.
4. Send `%LOCALAPPDATA%\ProjectStonehenge\logs\session.log`.

Expected: `GS pipelines prepared at startup: 34 in … ms` early in the log, no `built during frames` line (or only a few keys to add to the table), and no stutter. A second session after deleting only the driver caches (keeping `cache\`) should also start without hitches.

## 2aj. Render thread (2026-10-25)

User request after RT-1 (§2ai): move the renderer's CPU work to a second thread without changing game logic. Design and rules: ADR-002 "Render thread".

### RT-2. Built and checked
- Built: `src/platform/render/render_thread.*` (`FrameJob`, `RenderThread`: one frame in flight); `main.cpp` builds the job while the game is parked and the render thread runs the old order of the frame (GS processing, interpolation, display state, movie picture, screenshot, `DrawFrame`); the renderer takes the window size from the job instead of asking SDL (`Renderer::SetWindowPixelSize`), and its movie upscaling factor is atomic (the main thread reads it for the decoder side path). Presentation settings changed at run time travel with the next job; a debug settings change and the shown overlays wait for the render thread.
- Game logic unchanged. Regression (`full.json`, run `rt1`, against `pc2`): all 30 jobs exit cleanly with no warning; every RAM checkpoint and hash log identical; 108 of 110 files identical, `m3_route` / `m4_route` screenshot 5300 differ only in the loading-fade column x = 409 (IR-1). Development tests pass (100 test cases).
- Speed (same build; the baseline is a temporary edit, reverted, in which `Submit` waited for the frame to be presented, which is the old serial order). RX 9070 XT PC, 1×, v-sync off, cap 360, frames at normal speed after `--fast-until` (mission 1: the User's input log, vblanks 1500–8000; hangar: `m3_route`, vblanks 4400–5000):

| Scene | serial | render thread | gain |
|---|---|---|---|
| mission 1, interpolation off | 197.7 fps | 245.0 fps | +24 % |
| mission 1, interpolation on | 164.9 fps | 201.0 fps | +22 % |
| hangar, interpolation off | 189.4 fps | 277.2 fps | +46 % |
| hangar, interpolation on | 103.6 fps | 153.2 fps | +48 % |

  No run dropped a tick. With interpolation on, mission 1 still blends 91 % of frames (19,755 of 21,797) and the hangar 99.5 %.
- Live session (no-save intro, normal start, `--fps-overlay`, so overlay frames ran serialised), with `settings.json` changed twice while running: fullscreen → windowed, internal scale 5 → 3, v-sync off; then back to fullscreen, 5×, v-sync on. Both changes were applied on the render thread (swapchain 1280 × 960 IMMEDIATE, then 3840 × 2160 FIFO; interpolation on at 360, then off). 5 + 2 ticks dropped at the two switches (device wait and new targets), clean exit.
- Vulkan validation layer (`--vk-validation`, mission 1 with interpolation and a screenshot at 1400): no validation message; the screenshot was written; clean exit.
- Latency: with the frame in flight the picture trails the game by at most one presented frame.
- Version raised to **0.9.5** after the pipeline warm-up and cache (§2ai) and the render thread (User request).

## 2ak. Engine sound missing in the external view (2026-10-26)

User report: the engine sound plays in the first-person views but not in the external (third-person) view.

### ES-1. View column read with the wrong stride
- `UpdatePlayerSound` 0x001270B8 stores byte 0 of the engine input (0x00420620) from the u8 view table 0x002A2538 (`sll v0,v0,2`, `lbu`, `sb` at 0x00127260–0x00127274): views 0 / 1 / 2 → columns 0 / 2 / 1 of the 9 × 3 layer volume tables (0x0033D448 …). The translation read a u32 at 0x002A2538 + 16 · view and stored a word. View 0 was right by chance; view 1 (cockpit) got column 4 (the next layer's column 1) and view 2 (external) column 100, past the table, so its layers were silent. The word store also cleared bytes 0x00420621–23, which the original leaves alone.
- Fix: `src/game/player_flight.cpp`, u8 load with a 4-byte stride and a byte store (also for subs 0x14 / 0x19 / 0x1B). Docs: `../subsystems/sound.md` "Engine and ambient sounds".
- Development build and tests pass (2026-10-26). Pending: a listen in all three views (mission 1); a RAM checkpoint in the external view should now match PCSX2 at 0x00420620 and 0x00423EB0.

## 2al. GPU work structure: counters, narrow barriers, partial frame-buffer copies (2026-10-26)

User report: very low frame rates in mission gameplay on NVIDIA GPUs. User approval (2026-10-26): exit-log counters of render passes, barriers and frame-buffer copies; narrower barriers; copy only the area a draw reads.

### GW-1. Counters
- `GsBackend::PassStats` per frame (Record and CopyPresent): render passes begun, pipeline barriers, frame-buffer copies and the host pixels they write, also by kind (frame as texture, field copy, DATE / shader-blend regions, GPU palettes, view syncs, conversions, anti-aliasing). Session averages at exit (`renderer: GPU work per frame on average: …`, `frame-buffer copies per frame by kind: …`); the last frame in the debug overlay's Performance panel.
- Mission 1, the User's input log, before the changes, 8x: **555 render passes, 2,296 barriers and 569 copies (186 Mpixels, ~745 MB) per frame**. 522 of the copies (151 Mpixels) were frames read as textures (glow and shadow chains, heat haze): each copied the whole source image (a 1x work target is 1024 rows tall) and was wrapped in four barriers that each waited for the whole GPU (`ALL_COMMANDS` → `ALL_COMMANDS`, all memory). A GPU with less bandwidth than the RX 9070 XT, or a driver that serialises such barriers harder, spends most of the frame there.

### GW-2. Changes (`gs_vulkan.cpp`, `gs_antialias.cpp`)
- **Narrow, batched barriers**: `Transitions` moves up to 4 images with one barrier whose scope is the stages and accesses of the old and new layouts (`UseOf`: colour attachment output, fragment tests, fragment/compute shaders, transfer). The paired transitions around each copy (source + destination before, destination + the draw's target after) are one barrier each.
- **Lazy restore**: a target read by a copy or a conversion keeps that layout until it is drawn into again (`begin_pass` transitions it); every target path already tracks and transitions from the current layout.
- **Partial frame-as-texture copies**: `read_box` gives the texels a draw can read (its vertices' UV box, through the perspective divide when every q > 0, with the bilinear margin, wrapped by CLAMP: repeat = whole texture, region repeat = MAX up to MIN | MAX, clamp / region clamp = the box inside the texture or region). Only that rectangle is copied (same position in `feedback_`); conversions (`Convert`) use the same box.
- **Copy reuse**: `feedback_copy_` records the source image, its `last_write` and the rectangle `feedback_` holds; a later draw that reads inside it while the source has not been drawn into skips the copy, its barriers and the render-pass break. Cleared at each Record, by conversions and when the scratch images are recreated.
- The field copy into `present_` stays a copy of the whole image.
- Fixed on the way (found by synchronization validation, present before the changes): a new target image's clears had no barrier before the copy or blit into it when a target grows or is downscaled (3 WRITE_AFTER_WRITE hazards).

### GW-3. Results (RX 9070 XT, development build, windowed, v-sync off, cap 360, no interpolation, no anti-aliasing; mission 1 input log, `--fast-until 1500 --exit-at 8000`, fps = frames presented at normal speed)

| Scale | Build | fps | render passes | barriers | copies (Mpixels) | recording ms | waiting for the GPU ms |
|---|---|---|---|---|---|---|---|
| 8x | before (2 runs) | 196.9 / 197.4 | 555.2 | 2,296.0 | 568.6 (186.2) | 2.89 / 2.56 | 2.55 / 2.53 |
| 8x | after (2 runs) | 312.6 / 317.6 | 56.0 | 175.8 | 69.5 (52.7) | 1.53 / 1.54 | 1.14 / 1.13 |
| 4x | before | 225.2 | 555.2 | 2,296.0 | 568.6 (156.6) | 2.60 | 2.06 |
| 4x | after | 339.9 | 56.0 | 175.8 | 69.4 (24.6) | 1.51 | 0.54 |
| 4x, FXAA | before | 224.9 | 557.1 | 2,303.0 | 570.6 (164.5) | – | 2.10 |
| 4x, FXAA | after | 339.9 | 57.9 | 180.8 | 71.4 (32.2) | – | 0.55 |
| 8x | after, final build (region repeat box) | 317.1 | 56.1 | 175.9 | 69.5 (52.5) | – | – |

- With frame interpolation on (`--frame-interpolation on`, same runs otherwise; about 92 % of the presented frames are blended copies):

| Scale | Build | fps | render passes | barriers | copies (Mpixels) | GS processing ms | recording ms | waiting for the GPU ms |
|---|---|---|---|---|---|---|---|---|
| 8x | before | 154.2 | 557.9 | 2,306.5 | 571.2 (186.9) | 1.93 | 2.62 | 1.55 |
| 8x | after | 245.6 | 54.9 | 172.7 | 68.1 (52.4) | 1.90 | 1.56 | 0.10 |
| 4x | before | 181.3 | 559.1 | 2,310.8 | 572.4 (157.6) | 1.92 | 2.97 | 0.21 |
| 4x | after | 249.4 | 55.0 | 172.8 | 68.2 (24.3) | 1.87 | 1.57 | 0.00 |

  +59 % at 8x, +38 % at 4x. After the change both scales run at about 250 fps with almost no wait for the GPU: the limit is now the render thread's CPU time per frame (GS processing about 1.9 ms, unchanged by this work, plus recording about 1.6 ms and the blend about 0.33 ms).
- Without interpolation (table above): +60 % at 8x, +51 % at 4x (4x after is close to the 360 cap). Frame-as-texture copies 521.6 → 22.5 per frame (150.9 → 17.4 Mpixels). Conversions (25 per frame, 12 Mpixels) did not shrink: their draws repeat-wrap or use STQ without a usable box.
- Pictures: 14 screenshots over the mission (vblanks 1600–7450, including the missile hit at 7370–7400) byte-identical before and after at 4x (native and scaled files) and at 1x.
- Vulkan validation with synchronization validation (`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1`): 0 messages at 4x to vblank 2600 and at 8x with SMAA to vblank 6300 (before: the 3 hazards above).
- Regression (`full.json`, run `al1`, against `rt1`; built before the last change, the region-repeat box): all 30 jobs exit cleanly with no warning; every RAM checkpoint identical; 108 of 110 files identical, `m3_route` / `m4_route` screenshot 5300 differ only in the loading-fade column x = 409 (IR-1). The final build's mission screenshots are again identical to the "before" build's. Development tests pass.
- Not changed, still open: field copy of the whole draw buffer (1 per frame, 16.8 Mpixels at 8x); conversions (25 per frame); the anti-aliasing copy of the whole target; view syncs (14 per frame, 3 barriers each); DATE / shader-blend copies could become fragment-shader interlock or rasterization-order attachment access (vendor support differs); no GPU timestamps yet (the "waiting for the GPU" figure is the only GPU-side measure).
- Pending: the User's NVIDIA check (session log of a mission: the new counter lines and fps).

## 2am. GS processing: one vertex buffer per draw list, pipeline reuse, blended copies replayed (2026-10-27)

User approval 2026-10-27 (items 1, 2 and 4 of the GS proposal). With the GPU work of §2al reduced, frame interpolation was limited by the render thread's CPU time: about 250 fps at 4x and 8x with almost no wait for the GPU.

### GP-1. Profile (CPU sampling, VS collector, 8x with interpolation, 15,150 frames drawn)
- Render thread busy almost all the time. Per frame: GS processing 1.74 ms (building draws 0.74, of which memory allocation about 0.2 and texture lookups 0.23; IMAGE uploads into the local-memory model 0.68, nearly all the swizzle loop; register writes and parsing 0.23), recording 1.53 ms (of which 0.26 the pipeline lookup per draw), the blended copy 0.25 ms.
- At 250 fps each game step is presented about 4 times, and every blended copy was processed in full (about 1.4 MB of GIF data, all its texture uploads and GPU decodes), although only vertex values differ between the copies of one step.
- Measured before building the replay (temporary probe): over 18,306 repeated copies (17 million run outputs), the repeated runs' output sizes stayed the same in 99.8 % of the copies; 415 runs changed size (clipping).

### GP-2. Changes
- **One vertex buffer per draw list** (`gs_state.h` `DrawList`): draws hold `first_vertex` / `vertex_count` into the list's vertices instead of a vector each (a primitive only ever goes to the newest draw, so each draw's vertices are contiguous). No allocation per draw; the buffers keep their capacity from frame to frame (`GsState::TakeDraws(DrawList&)` appends). `Record` reserves its host vertex buffer.
- **Pipeline reuse in Record**: consecutive draws with the same pipeline key skip `GetPipeline` (mutex, map lookup).
- **Blended copies replayed** (`Renderer::SubmitBlendedStep`, `GsState::ProcessGifTraced` / `ReplayVertices`, `FrameInterpolator::StepSerial`): the first blended copy of a step is processed with a trace: every kick (with its XYOFFSET), the kick each draw-list vertex came from and the kick of its colour (flat shading), the texel range of every textured sprite, line and point. A later copy of the same step (same step serial, nothing processed since) is parsed for vertex writes only: GIF tags, every register write other than RGBAQ / ST / UV / FOG / XYZ*, the kick kinds (ADC) and IMAGE data must be the traced bytes; the vertex registers are followed through the whole copy (a run's colour or texture coordinates carry into later packets), the texel ranges whose vertices changed must come out the same. Then the copy's draws are the traced draws with the new vertices, and the vertex registers and queue end as processing would leave them. The copy adds no texture work: the traced frame's textures stay held by its draws, and the GPU's copy of local memory is as its memory updates left it (repeating them would leave it the same; repeating a decode would also replace a held texture image). Otherwise the copy is processed again and traced.
- Development tests: `DrawList` in the GS tests; new test "a blended copy replayed from the traced one gives the draws of processing it" (positions and colours changed, a flat sprite taking the strip's colour across packets, a structural change and processing in between refused).

### GP-3. Correctness
- Temporary comparison in the running port (mission 1, 8x, interpolation on): every replayed copy was also processed in full from the same state and the draw lists compared field by field (texture contents by rectangles, format, TEXA, palette). 11,668 replayed copies: **0 vertex differences**. First version (runs only): 1.3 million vertex differences in 14,814 frames, because vertex values written by a run carry into later packets; fixed by following the vertex registers through the whole copy.
- 16 of the 11,668 copies differed in the state of their first 3 draws (drawn before the copy sets the registers they use, so their state comes from the processing before). A replayed copy has the state of the step's first copy, which starts where the previous step ended, as on the PS2; processing a copy again started it where the same step's previous copy ended. Not a replay error; the replay is the more faithful of the two.
- Interpolation off (the default for validation runs): 14 screenshots over the mission (vblanks 1600–7450) byte-identical to the §2al build at 4x (native and scaled files) and at 1x.
- Interpolation on: screenshots at vblanks 2800, 5200, 7400 against the build before the replay: 2800 identical, 5200 and 7400 differ in 0.6 % / 1.3 % of the bytes (the blend factor depends on presentation timing), no visible difference.
- Regression (`full.json`, run `gs1`, against `al1`): all 30 jobs exit cleanly with no warning; every RAM checkpoint and hash log identical; 108 of 110 files identical, `m3_route` / `m4_route` screenshot 5300 differ only in the loading-fade column x = 409 (IR-1). Development tests pass.
- Vulkan synchronization validation (4x, interpolation on, to vblank 2600): no message. Under the layer the port runs at about 3 fps, so no blended copy is drawn there; a replayed copy records only draws (no texture work), the same commands as any frame's draws.

### GP-4. Results (RX 9070 XT, development build, windowed, v-sync off, cap 360, no anti-aliasing; mission 1 input log, `--fast-until 1500 --exit-at 8000`)

| Case | Build | fps | GS processing ms | recording ms | GPU decodes | KiB uploaded | waiting for the GPU ms |
|---|---|---|---|---|---|---|---|
| 8x, interpolation | §2al | 245.6 | 1.90 | 1.56 | 69.5 | 781 | 0.10 |
| 8x, interpolation | + vertex buffer, pipeline reuse | 254.5 | 1.60 | 1.56 | 69.6 | 786 | 0.30 |
| 8x, interpolation | + replay | 261.8 | 0.75 | 1.50 | 17.2 | 201 | 0.93 |
| 4x, interpolation | §2al | 249.4 | 1.87 | 1.57 | 69.5 | 778 | 0.00 |
| 4x, interpolation | + replay | 320.6 | 0.67 | 1.35 | 13.9 | 162 | 0.15 |
| 8x | §2al | 317.1 | – | – | – | – | – |
| 8x | + all | 323.8 | 1.75 | 1.42 | 69.7 | 811 | 1.10 |
| 4x | §2al | 339.9 | – | 1.51 | – | – | 0.54 |
| 4x | + all | 342.9 | 1.73 | 1.41 | 69.7 | 811 | 0.52 |

- Blended copies: about 6,200 processed (one per game step) and 20,000–26,500 replayed per run (`renderer: blended copies: … processed, … reused from the copy before`).
- With interpolation: +7 % at 8x (now limited by the GPU again: 0.93 ms waiting per frame), +29 % at 4x. Without interpolation the GS work happens once per step, so the gain is small (+2 % at 8x; 4x is near the 360 cap).
- Not done: the IMAGE upload swizzle (0.68 ms per processed step; table-driven block rows); GS processing on its own thread (ADR-002 change, not approved).

## 2an. Terrain cells black up close: mirrored tiles flipped through a sprite (2026-10-27)

User report (mission 6, also other missions, present in earlier versions): whole terrain cells disappear (black, with stray pieces) when the camera is close; always the same cells after a restart. Reproduced from the User's input log of 2026-10-27 (boot → mission 6, 10,446 ticks; not committed: `out/terrain/input.ac4i`, card copy `out/terrain/card`, `--memcard <copy> --replay-input`): vblank 10030, a cell over the harbour drawn black with pieces of piers, at 1× and 8×, with the CPU and the GPU texture decoder alike.

### TT-1. Cause and fix
- All terrain cells sample one 512 × 512 T8 atlas (TBP 0x2900, mip levels at 0x2D00 / 0x2E00 / 0x2E40) whose 64 × 64 slots the game fills per bucket (`../subsystems/rendering.md`, terrain tile upload). A near cell whose tile is mirrored (attr flags 0x400 / 0x800) and close (d < 2 · cam+0x168 + 4096) does not get a plain upload: the tile goes as T8H to 0x3E00, a sprite (decal, context 2) draws it flipped into the alpha bytes of FBP 0x1E8 (block 0x3D00, FBMSK 0x00FFFFFF), and a local→local transfer (TRXDIR 2) copies 0x3D00 as T8H into the slot as T8.
- The port draws sprites only on the GPU; its local-memory model never got the flipped tile, so the transfer copied stale bytes into the slot (capture of the step at vblank 10029: tile (5,3) of the atlas, read by draws 91–180, filled by that copy at GIF offset 532944).
- Fix (`gs_state.cpp` `GsState::SpriteToMemory`): a sprite the GS draws as a plain texel copy is also drawn into local memory: textured with UV, no fog, blending or anti-aliasing, alpha test off or ALWAYS, no destination alpha test, depth test ALWAYS, nearest sampling without mip levels, decal or modulate by 128, a CT32 frame buffer (FBMSK and FBA applied), whole-pixel coordinates inside SCISSOR, a texture in local memory (not a frame buffer drawn earlier; the CLUT not drawn on the GPU). The texel of a pixel is at the pixel's own position (V from 63 to 0 over 64 rows flips the tile texel for texel, as PCSX2's software renderer does), wrapped by CLAMP. The GPU still draws the sprite. A blended copy whose such sprite's vertices changed is processed again rather than replayed (§2am).

### Results
- Vblank 10030: the cell shows the harbour water and piers continuing from its neighbours, at 1× and at 8× with interpolation; the other screenshots of the stretch (vblanks 9600–10000) are unchanged.
- Use and cost: the mission 6 replay draws 773 such sprites (2.9 Mpixels) over 9,644 frames, mission 1 339; GS processing per frame unchanged (1.76 ms at 8×).
- Mission 1 screenshots (14, vblanks 1600–7450, 1×) byte-identical. Regression (`full.json`, run `tf1`, against `gs1`): all 30 jobs exit cleanly, all 110 files identical. New test "a texel-copy sprite is drawn into local memory for a later local->local copy"; development tests pass.

### TT-2. Warning for other reads of GPU-only drawings (User approval 2026-10-27)
- `GsState` keeps the 8 KiB pages of local memory whose contents only the GPU holds (`gpu_pages_`, `NoteGpuDraw`): the frame buffer pages (FBW × scissor height, as `NoteFrameBuffer`) of every primitive that `SpriteToMemory` does not also draw, marked once per frame buffer and scissor, cleared by a host→local or local→local transfer into the page.
- A local→local transfer whose source touches such a page logs once `renderer: local->local transfer reads a frame buffer drawn on the GPU only (source block …, PSM …): it copies stale bytes`; a CLUT load from such a page (other than the palette drawn on the GPU, §2z) logs once `CLUT loaded from a frame buffer drawn on the GPU only …`. Counted in `GsState::GpuOnlyReads`. The picture is unchanged.
- Test: the texel-copy test also draws the sprite with blending (not a plain copy): the transfer that reads it back is counted; after an upload into the page it is not.
- Runs: the mission 6 replay (to vblank 10446), the mission 1 input log and all 30 jobs of `full.json` (run `sn1`, against `tf1`): no such warning. `sn1`: all jobs exit cleanly, 108 of 110 files identical, `m3_route` / `m4_route` screenshot 5300 differ only in the loading-fade column x = 409 (IR-1). Development tests pass.

## 2ao. GPU time per section: the pipeline switches of the flight passes (2026-10-30)

Player report (0.9.6 release, GTX 1050, 1920 × 1080 at 75 Hz, 2×, FXAA, v-sync, interpolation on): 41.5 fps, 14.25 ms per frame waiting for the GPU, 115 render passes, 320 barriers and 130 copies per frame; slow at 1× too. User request: GPU instrumentation that costs no noticeable performance, test runs, find the bottleneck. GRAPHICS page INTERPOLATION row: `port-features.md` §3.

### GT-1. Instrumentation (`src/platform/render/gpu_profiler.*`, `renderer.md` §3)
- Always: two timestamps per frame; exit log `renderer: GPU time per frame (timestamps): … ms on average over N frames with GS draws (… over all … presented), slowest … ms`.
- `--gpu-profile` (all builds): a bottom-of-pipe timestamp at the end of each section (texture work, each GS render pass, frame as texture, field copy, DATE / shader-blend region, GPU palette, view sync, conversion, depth copy, anti-aliasing, present copy, movie upscaling, output pass). A section's time is from the previous timestamp to its own, so the stall of the barrier in front of a copy counts for the copy. Results are read after the frame's fence (no wait). Exit log: the sections by time, and the draws by target (FBP, FBW, layout; passes and draw calls per frame).
- Draw-state counters (always, CPU side): `renderer: draw state per frame on average: … draw calls, … pipeline binds, … parameter updates, … texture binds`.
- Cost (mission 1 benchmark, 1×, RX 9070 XT, `out/bench/run.sh`): 341.5 fps without `--gpu-profile`, 340.6 fps with it; recording 1.43 → 1.52 ms per frame.

### GT-2. Where the GPU time goes (RX 9070 XT, development build, windowed, v-sync off, cap 360, no interpolation, no anti-aliasing; per frame with GS draws)

| Run | GPU total | draws | conversions | view syncs | texture work | frame as texture | rest |
|---|---|---|---|---|---|---|---|
| mission 1 log, 1× | 2.06 ms | 1.41 ms (56 passes) | 0.41 ms (11) | 0.08 ms (14.2) | 0.08 ms | 0.05 ms (22.5) | 0.03 ms |
| mission 1 log, 2× | 2.31 ms | 1.64 ms | 0.41 ms | 0.08 ms | 0.08 ms | 0.06 ms | 0.04 ms |
| mission 6 route (`out/bench/m6.sh`), 1× | 1.35 ms | 0.90 ms (36.7) | 0.24 ms (6.6) | 0.07 ms (9.6) | 0.08 ms | 0.04 ms (13.1) | 0.02 ms |

- 1× → 2× adds 0.25 ms: the GPU work is not bound by pixels. The copies and barriers of §2al are no longer the main cost (all copies together about 0.15 ms); conversions are the second cost (about 37 µs each).
- Draws by target (mission 1, 1×): the flight frame buffer (FBP 0x0, FBW 8) takes 1.25 of the 1.43 ms, with 1,206 draw calls in 17.7 passes per frame; every off-screen target (glow, shadow chains) 0.01–0.04 ms.
- Draw state (mission 1, 1×): 1,251 draw calls, **1,070 pipeline binds**, 1,247 parameter updates (push constants), 150 texture binds per frame, with only 36 pipelines in use.

### GT-3. The pipeline switches (temporary probe, removed)
- Of 2.89 M pipeline binds (mission 1 to vblank 4000), 2.72 M (94 %) alternate between two keys with the same blend: `0x0670101F` (RGB written, depth GEQUAL, no depth write) and `0x0CF0101F` (RGBA, depth GREATER, depth write). The colour mask changed in 95 % of the binds, the depth test in 94 %, the depth write in 94 %, the blend in 3 %. This is the game's draw order (a pass that writes depth, then one drawn over it with GEQUAL without depth write, alternating per object or terrain cell); the draws overlap, so they cannot be reordered.
- Timing experiment (wrong pictures, timing only): both keys drawn with `0x06F0101F` (RGBA, GEQUAL, no depth write; about the same pixel work): pipeline binds 1,070 → 72 per frame, **draws 1.43 → 0.34 ms, frame GPU time 2.08 → 1.01 ms**. With `0x0CF0101F` for both (fewer pixels pass): the same 0.35 ms. So the cost is the switch itself, about 1 µs of GPU time per switch on this GPU.
- Not measured on NVIDIA. GPUs and drivers of the GTX 1050's generation are known to handle pipeline state changes worse; with the player's session (115 passes per frame on average, and the hangar records about 4,000 draws per frame, §2am) the switches are the most likely cause of the 14 ms. To confirm: the player's log with `--gpu-profile` (GT-4).

### GT-4. Fix: VK_EXT_extended_dynamic_state, shared pipelines (ADR-009, User decision 2026-10-30)
- Hardware check (gpuinfo.org, Windows): no GPU usable with Windows 10/11 and a current driver lacks the extension (ADR-009). Required, no second path; a GPU without it is skipped at device selection, and the startup message asks for a driver from 2021 or later.
- Depth compare op and depth write are dynamic state, set only when they change; blended pipelines write A through the blend constant (1 writes As, 0 keeps Ad); unblended pipelines keep A in their mask (`gs_vulkan.cpp` `SplitKey`). The warm-up builds the reduced keys of `gs_pipeline_keys.h`, each once.
- Mission 1 benchmark, 1×, `--gpu-profile`: pipeline binds 1,069.9 → 69.5 per frame (2,115.7 depth state changes), pipelines in use 36 → 11 (11 prepared at startup in 373 ms); draws 1.43 → 1.33 ms, frame GPU time 2.08 → 1.99 ms; 342.7 fps (CPU-bound render thread, as before).
- Vulkan validation with synchronisation validation (`VK_KHRONOS_VALIDATION_VALIDATE_SYNC=1`), mission 1 at 4× with SMAA to vblank 2600: 0 messages.
- Regression (`tools/research/full.json`, 30 jobs) against baselines of the build without the fix (`out/exe_before`, same sources with the old `gs_vulkan.cpp`; baselines `out/runs/eds_base1`, `eds_base4`; runs `eds1`, `eds4`, not committed): all 30 jobs exit cleanly at 1× and 4×; **every RAM checkpoint identical**. Screenshots: at 1× 29 of 30 jobs identical, `m4_route` 5300 differs only in column x = 409 (400 pixels: the loading-fade column, IR-1); at 4× 28 of 30 identical, `opening` 1800 (known: host timing with `--fast-until`) and `s4_frontend` 11950, which differs only in the bottom row y = 447 by at most 1 (223 pixels). The build without the fix run again against its own baseline gives the same `s4_frontend` 11950 difference (row 447, 414 pixels, at most 1): host-timing flake, not the fix. So the pictures are unchanged by the fix.
- `ctest --preset dev`: passes.

### GT-5. Why the gain is small on this GPU (temporary probe, removed; pictures wrong, timing only)
Each alternating state frozen in turn (mission 1 to vblank 5000, draws' GPU time per frame with GS draws; 1.32 ms with nothing frozen):

| Frozen | draws |
|---|---|
| depth compare op | 1.32 ms |
| depth write | 0.98 ms |
| alpha blend constant | 1.32 ms |
| compare op + depth write | 0.97 ms |
| depth write + alpha constant | 0.97 ms |
| compare op + alpha constant | 1.32 ms |
| all three | 0.31 ms |

- On the RX 9070 XT any state change between draws costs about 0.6 µs, whether it is a pipeline switch or dynamic state; a change of the depth write costs about 0.35 ms per frame more. The 0.34 ms of GT-3 came from removing the alternation itself, which the game's draws need (the overlapping passes really use different depth and alpha states). So ADR-009 removes the pipeline switches but not this cost on AMD. On NVIDIA dynamic state is expected to be cheaper than a pipeline switch; not measured.

### GT-6. Pending
- Player check: the next build, `--gpu-profile`, 1×, interpolation and anti-aliasing off, one mission and some time in the hangar; send `logs\session.log` (lines `GPU time …`, `draw state …`).
- Next candidate: conversions (0.41 ms per frame in mission 1): cache while the source is unchanged.

## 2ap. Host memory leak scan (2026-11-01)

User request: build the development build and scan the code for memory leaks.

### ML-1. Code review
- Host allocations: the only raw `new` are guest RAM and the scratchpad (`common/guest_memory.cpp`), owned by `unique_ptr`. Every `std::thread` is joined.
- Vulkan, SDL and Win32 objects: every create has its destroy (`gs_vulkan.cpp`, `gs_antialias.cpp`, `movie_upscaler.cpp`, `renderer.cpp`, `vk_context.cpp` with the surface, `gpu_profiler.cpp`, audio stream, gamepads, window, ISO file handle). Per-frame lists (`retired_`, `retired_fb_`) are emptied when their frame comes round again.
- Bounded caches: CPU texture cache 256 MiB (`GsState::kTextureCacheBytes`, cleared above 4,096 entries), free GPU textures trimmed to `kFreeTextureBytes`, upload map cleared above 4,096 entries; the page maps (`fb_pages_`, `fb_written_bits_`, `zbuf_owner_`) are keyed by the 9-bit FBP (at most 512 entries).
- Guest memory: the work heap is the game's bump allocator with marks and rewinds (as on the PS2); the guest `malloc`/`free` (`sce/libc.cpp`) merges freed blocks.
- Found:
  1. `sce/libmpeg.cpp`: the game never calls `sceMpegDelete`; a decoder was closed only by a later `sceMpegCreate` at the same `mp`. `mp` is inside the 6 MiB movie work block, whose address depends on the work heap at movie start, so each address kept a `mpeg2dec_t` (with its picture buffers) for the rest of the session. Seen in the ML-3 run: three 6 MiB movie blocks, all from the same caller (image offset 0x16EDC8), at vblanks 1738 (SS01), 10438 and 14070, at 0x00CD4A40, 0x00CD4240 and 0x00CE9D80 (`--log-allocs`); which movies the last two are was not checked.
  2. `platform/win32/timer_win32.cpp`: the per-thread waitable timer was never closed, and `timeBeginPeriod(1)` had no `timeEndPeriod`. Both are released at process exit, so only untidy.

### ML-2. Fixes
- `sceMpegCreate` closes every earlier decoder before creating the new one: one SMV reader and one IPU, so one decoder is active at a time. Host memory only.
- Timer: a thread-local RAII holder closes the handle when its thread ends; a static object pairs `timeBeginPeriod(1)` with `timeEndPeriod(1)`.

### ML-3. Results
- Runtime, idle boot without a card (`--no-memcard --exit-at 14400`, `--vk-validation`, normal speed): private memory 400 MB, one step to about 600 MB on the title screen (the texture cache filling), then flat; handles and threads flat; no undestroyed-object messages from the validation layer at `vkDestroyDevice`.
- Without input, a boot with or without a card plays no movie (startup screen, then title and attract demos). The movie path needs input: card S (copy), `--pad-script 1200:start,1500:cross,1700:cross` then cross every 300 vblanks from 10400 (SS01, mission title, briefing, mission 1 flight), `--hash-log --log-allocs --fast-until 30000 --exit-at 30000`.
- Regression against the build without the fix (same sources with the HEAD `libmpeg.cpp` and `timer_win32.cpp`), that run and two boots (empty card; `--no-memcard`): **hash logs byte-identical at every vblank**, allocation logs identical, written cards identical. Both builds level off at 620–650 MB with flat handle and thread counts in flight; the saved decoder memory (estimated about 1.5 MB per movie) is below the noise of the texture cache.
- `ctest --preset dev`: passes.

## 2aq. Hangar aircraft tinted teal; device lost under the validation layer (2026-11-02)

Player report (0.9.5 log, then confirmed on 0.9.6): in the hangar every aircraft and the special weapons are teal, with panel lines and shading intact; the room is normal. RTX 5070 Ti, driver 617.42, 2560 × 1440 at 120 Hz, internal scale 6, SMAA, interpolation on, v-sync off, cap 120. Save imported from PCSX2 (slot 0: campaign cleared several times, mission 1 HARD, S-37A, everything owned, SCREEN RATIO 16:9; checksum valid).

### HT-1. The pass involved
- The viewer draws the aircraft and the special weapons with the aircraft light set (`CreateHangarObjects` 0x0022E450), which adds a light-map pass: T8 128 × 128 at TBP 0x3400, CLUT CBP 0x3F08, ALPHA_2 0x58 (`Cs·Ad + Cd`), TEST_2 depth GEQUAL, no depth write (`WriteLightSetPacket` 0x001370D8; PCSX2 hangar dump: 6,320 strips). The room uses the room light set without it. So the light map is added in proportion to the alpha the skin pass (T8 512 × 512 at 0x2D00, CLUT 0x3800, alpha written) left in the frame buffer; the report looks like the map added at full strength everywhere.
- In one frame the hangar uploads several textures over the same local memory: the room texture (T8 1024 × 512 at 0x2D00, blocks 0x2D00–0x34FF) covers the light map at 0x3400, then the skin and the light map are uploaded over it.

### HT-2. Not reproduced (RX 9070 XT, development build)
- Card S hangar route and the player's save (LOAD File 1, CONTINUE, Start on SS01, briefing, START MISSION: `--pad-script 700:start,1000:down,1060:cross,1400:cross,2300:cross,3200:up,3260:cross,3800:start,7100:start,8000:cross`), at 1× and at 6× with SMAA and interpolation; card S with interpolation, v-sync off, cap 120, captured from the screen (blended copies); card S patched to SCREEN RATIO 16:9: the aircraft in normal colours every time.
- Checked and ruled out on reading: anti-aliasing (flight views only, alpha kept), `MapBlend` for `Cs·Ad + Cd` (exact, unchanged since 0.9.5), the target format (R8G8B8A8_UNORM on every GPU), the barriers between GS-memory updates and decodes (compute read → transfer write → compute read), the palette buffer offsets (multiples of 1 KiB), the frame-as-texture and GPU-palette paths (§2z; their pages are not the hangar's).
- `--verify-texture-decode` on the player's route at 6× to vblank 10200: 173,575 GPU decodes equal to the CPU decode; GS processing 9.0 ms per frame with the check.
- Open: the GPU/driver (all validation is on AMD) is the remaining difference. To settle: a development build on the NVIDIA machine with `--cpu-texture-decode` (tint gone: GPU decode) and `--verify-texture-decode` (logs the first differing textures), and the GRAPHICS settings 1×, interpolation off, AA off.

### HT-3. Proposed safeguard (not built; needs the User's approval)
- A sampled decode check in all builds: the first GPU decode of each texture key, up to a fixed number per session, read back and compared with `DecodeOnCpu` (the `--verify-texture-decode` machinery, already compiled into every build). On the first difference: one log line with the texture and texel (as the development warning) and the rest of the session decoded on the CPU. Presentation only.

### HT-5. Player's tests B and C (0.9.7 development build, 2026-11-02)
- Player's machine: RTX 5070 Ti, driver 621.01, 2560 × 1440 at 120 Hz, fullscreen, v-sync on, internal scale 3, FXAA, interpolation on; S-37A in the hangar.
- B `--verify-texture-decode`: `texture decode check: 223756 GPU decodes equal to the CPU decode`. C `--cpu-texture-decode`: 0 GPU decodes, 66.4 CPU uploads per frame. The aircraft is teal in both F8 images (internal 3×), the room normal. **The texture decode is ruled out** on the player's GPU too; HT-3 would not help and is dropped.
- Same build, save and settings on the RX 9070 XT (`--internal-scale 3 --antialiasing fxaa --frame-interpolation on`, HT-2 route, vblank 9200): S-37A grey. Since the F8 image (before the output pass) is already teal, the difference is in the GS draws on the GPU (draw shader `gs.frag`, fixed-function blending, the frame-copy paths), or in the game state of the player's run, not in the presentation.
- Untested paths in the hangar's draws that the decode check does not cover: the GPU-drawn palette (flag 29, `clut_img_`), the shader blend and DATE copies (flags 22/28), `Cs·Ad` with source colour `Cs/128` up to ~2.0 into a UNORM target with dual-source blending (flag 18; Vulkan clamps the source to [0, 1] before blending, a driver that does not would differ).
- Next: an input log and a GS-memory dump from the player's machine (`--record-input --dump-vram`, F8 in the hangar), replayed here: a grey replay proves the GPU/driver, a teal one gives a local reproduction.

### HT-6. Reproduced and explained (2026-11-03)
- The User reproduced it on the RX 9070 XT with the player's save (identical to the zip) and `--record-input --dump-vram`: Start, LOAD file 1, second main-menu entry, right right cross, down cross, hangar at vblank ≈ 2260 (`blue_repro\`, outside the repo). The replay (`--memcard` copy + `--replay-input`) is teal at 1× and its GS memory at vblanks 2264 and 2442 is byte-identical to the User's run: deterministic, any GPU. HT-5's GPU suspicion is withdrawn.
- Against the HT-2 campaign route (grey), GS memory of the skin, light map and both palettes is identical, and the frames' A+D state (TEX0/TEX2/ALPHA) matches. The skin pass's vertex colours differ: mean RGB 31/51/50 (teal route) against 50/51/49 (campaign route). The light-map pass is the same.
- Cause: `InitLightSet` (0x00136AA0) clears the light matrix +0x50..+0x8F, then stores zero to +0x90 four times (the original code does so: four `sq` to the same register `v0`). The colour columns +0xA0..+0xC0 of the unused lights keep work-heap leftovers on the PS2 as in the port. Guest RAM on the teal route (`--dump-ram-at 0x00235AB8,200`, viewer at 0x00CD0170): aircraft light set +0x850, column 3 (+0xC0, at 0x00CD0A80) x = 0xFFFE0052 (leftover UTF-16 text). The VU has no NaN or infinity: an exponent of 255 is an ordinary number (±1.x · 2^128), and its weight (intensity w = rows' w × normal, 0) gives 0 × big = 0. The port's VU1 HLE (`vu1_programs.cpp`, program C lit colour `colour.Mul(inten)`) reads it as an IEEE NaN: NaN × 0 = NaN, `fmax(NaN, 0)` = 0, so red keeps only the ambient term (teal). On the campaign route the same slot holds a denormal (read as 0).
- Fix (User approval 2026-11-03; presentation only, VU1 output goes to the GS, not guest RAM): `Vu1State::F` (`vif1.h`), the one float read of VU memory for the VU1 programs, returns ±FLT_MAX for exponent 255, so 0 × it = 0 as on the VU. `InitLightSet` stays as the original (fixing the game's own missing clears would change guest RAM and leave the HLE wrong for other leftovers).
- Results (old = 0.9.7 build before the fix): the User's replay (vblanks 2264 S-37A, 2442 X-02) now in normal colours; campaign hangar route (vblank 9200) and `--mission 1` flight (vblanks 1500, 2500, 3500) screenshots byte-identical to the old build; `--hash-log` identical old/new on all three runs; `ctest --preset dev` passes. HYPOTHESIS: PCSX2's VU clamping gives the same result (the player reports a normal hangar in PCSX2; not checked on this route).

### HT-4. `VK_ERROR_DEVICE_LOST` with `--vk-validation`
- Once, card S route at 6× with SMAA, interpolation and `--screenshot-at 10861,11000,11400`: `vkAcquireNextImageKHR: VK_ERROR_DEVICE_LOST` at vblank ≈ 11000 (pre-mission menu); no validation message before it. Windows filed LiveKernelEvents 141 and 117 (GPU engine timeout, TDR) and 0x1B0 after the next wake from sleep, most likely this event.
- Not reproduced in two runs with the same options (with and without the screenshots). On reading: the acquire / submit / present path has no early return between acquire and submit, the swapchain is re-created after `vkDeviceWaitIdle`, OUT_OF_DATE / SUBOPTIMAL are handled. No cause found in the port's code; a rare GPU timeout under the layer (layer 1.4.363, driver Vulkan 1.4.355). If it recurs: record with AMD Radeon GPU Detective, or log `VK_EXT_device_fault` data at device loss (not built).

## 2ar. Integrated GPUs: defaults and single-source GS pipelines (2026-11-03)

User report: low performance on integrated GPUs (the User's laptop: Intel UHD Graphics, driver Vulkan 1.3.215, 1920 × 1080 at 60 Hz, 0.9.7 development build, first start).

### IG-1. The laptop session
- 2×, FXAA (the defaults): 10.8 fps (1,764 frames in 162.8 s, 5,092 ticks dropped). 1×, anti-aliasing off: 31.6 fps, GPU 24.9 ms per frame, 19.4 ms per frame waiting for the GPU; CPU side GS processing 3.3 ms, recording 2.5 ms. First start: 11 GS pipelines in 6,001 ms (no cache yet); slowest frame 316 ms.
- 4× the pixels (2×) give about 92 ms against 25 ms: about 3 ms fixed and 22 ms per-pixel work at 1×. The GPU is bound by pixels.

### IG-2. Where the per-pixel cost is (RX 9070 XT, `--mission 1 --pad-script 800:cross --gpu-profile --exit-at 4000`, AA off)
- 1×: GPU 1.95 ms; draws 1.30, conversions 0.40, frame as texture 0.05, the rest below 0.1 ms. 8×: GPU 5.76 ms; draws 4.78, conversions 0.43 (native size), frame as texture 0.24, present copy 0.09, DATE regions 0.05. About 85 % of the pixel-bound work is the GS draws (`gs.frag`, blending, depth); copies are already limited to the read box and barriers to the exact stages.
- Candidates (User decision 2026-11-03: A and C built, B and D dropped): A integrated-GPU defaults; C pipelines without dual-source blending; B specialised `gs.frag` variants (dropped: more pipelines would bring back the switches ADR-009 removed); D hardware bilinear filtering (dropped: not exact to the GS).

### IG-3. Changes
- A (User decision 2026-11-03): `GpuInfo::integrated` (`VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU`, `vk_context.cpp`); `SetDefaultsIntegratedGpu` (`settings.cpp`): `DefaultSettings` keeps internal scale 1 and anti-aliasing off, as in validation runs. The GPU is known only after the renderer starts, so `main.cpp` writes those two values into a `settings.json` written with the defaults in the same session (first start, rejected file) and applies them unless `--internal-scale` / `--antialiasing` set them; an existing file keeps the player's values. Log: `startup: integrated GPU: default internal scale 1x, anti-aliasing off`. The GRAPHICS page and debug overlay defaults follow (`DefaultSettings`). Not covered: a key missing from an existing file still takes the dedicated-GPU default (read before the GPU is known). Test: `tests/test_settings.cpp` "an integrated GPU keeps 1x and no anti-aliasing".
- C: `gs.frag` built a second time with `-DGS_SINGLE_SOURCE` (`gs_single_frag`, `cmake/Shaders.cmake` `ac04_add_shader_variant`): output 0 only. `CreatePipeline` gives it to every pipeline whose colour blend factors include no `SRC1` factor (blend off, `DST_ALPHA` for `Cs·Ad + Cd`, `ZERO`/`ONE`, the shader-blend draws written unblended). Same pipelines (11 in mission 1, 6 of them single-source), same binds; exit log `GS pipelines: N in use (M without dual-source blending)`. HYPOTHESIS: Intel GPUs before Gen12 run a dual-source pixel shader at SIMD8; to measure on the laptop.

### IG-4. Results (RX 9070 XT)
- Screenshots byte-identical to the builds before: the User's hangar replay (2264, 2442), the campaign hangar route (9200), `--mission 1` (1500, 2500, 3500). `--vk-validation`, mission 1 at 4× with SMAA to vblank 2600: no validation messages. GPU time, mission 1 at 1×: 2.03 ms (1.99 before; noise). `ctest --preset dev`: passes.
- A not run here (discrete GPU): to check on the laptop (delete `settings.json`, start, the log line and the file show 1× and `off`), then the `--gpu-profile` command of IG-2 against IG-1.

### IG-5. Laptop profile after A and C (User run 2026-11-03, `--mission 1 --pad-script 800:cross --internal-scale 1 --antialiasing off --gpu-profile --exit-at 4000`)
- A works: first start without `settings.json` logs `integrated GPU: default internal scale 1x, anti-aliasing off`; settings in effect 1× and `off`.
- 13.0 fps, 3,551 of 7,554 ticks dropped (the game runs at about half speed). The renderer executes the GS work of every tick: 1,615 drawn frames hold 2,590 draws each against 1,017 per tick on the RX 9070 XT (4.18 M against 4.04 M in total), so the laptop figures per drawn frame are about 2.5 ticks. GPU 71.65 ms per drawn frame, about 29 ms per tick (IG-1's 24.9 ms was another scene; C cannot be told apart from it).
- Per tick, laptop against RX 9070 XT (same run): conversions 16.9 ms against 0.41 ms (×41), draws 8.6 ms against 1.32 ms (×6.5), the rest about 3.5 ms. Conversions are the largest cost on the Intel GPU; the same 9.55 Mpixels per tick on both.
- Where the conversion pixels come from (`--dump-dma-at 2500`, scratch parser of the GIF stream): the sky work pages (FBP 0x120-0x148, FBW 1-2, drawn as CT32 and CT16S) are read back as 1024 × 1024 textures (TW = TH = 10) with REPEAT wrap (WMS = WMT 0) by sprites with UV 0.5-128.5 (one 128 × 128 page block; FBP 0x180: 512.5 × 128.5). `read_box` gives the whole texture for REPEAT, so each read converts 1,048,576 texels where about 17,000 are read.
- The conversions themselves are the CT16S reads of the same pages: REGION_REPEAT with MSKU 0x3F7, MSKV 0x3FF, FIX 0 (a texture shuffle) over UV 0.5-128.5 × 0.5-256.5. `read_box` gives MAX to (MSK | FIX) + 1 for REGION_REPEAT: 1016 × 1024 texels per read.

### IG-6. Change E (User approval 2026-11-03): convert only the texels the draw can read
- `read_box` REPEAT: the vertex box with the bilinear margin (−1, +2) when it lies inside one period; otherwise the whole texture as before. Used by the frame copies and conversions.
- `read_spans` (conversions only): for REPEAT and REGION_REPEAT the margin box is mapped through the wrap exactly as `gs.frag` `Wrap` does (`c & (size − 1)`, `(c & MSK) | FIX`), giving the set of texels the draw can fetch; it is kept as at most two ranges per axis (split at the widest gap). The margin texel −1 wraps to MSK | FIX (u 1015, v 1023): it becomes its own range instead of widening the box to the whole texture. `Convert` runs up to 4 rectangles (`ConvertCore` per rectangle). Texels outside them are never fetched, so the picture is unchanged.
- Results (RX 9070 XT, mission 1 at 1×): conversions 9.52 → 0.85 Mpixels per tick (19.9 → 46.1 dispatches including view syncs), GPU time of conversions 0.37 → 0.13 ms, frame as texture 2.08 → 1.76 Mpixels; GPU 1.80 ms per tick. Screenshots byte-identical to the build before E: mission 1 at 1× (vblanks 1000-3500, 6 shots) and at 4× with SMAA (internal and native), mission 7 at 2× (1500, 2500, 3500). `--vk-validation` mission 1 at 4× with SMAA to vblank 2600: no messages. `ctest --preset dev`: passes.
- Expected on the laptop: conversions about 1.5 ms per tick instead of 16.9 (proportional to the pixels). To measure: the `--gpu-profile` command of IG-5.

### IG-7. Laptop results with E (User runs 2026-11-03)
- Test 1 (IG-5 command): 59.0 fps, 1 tick dropped (before E: 13.0 fps, 3,551). GPU 13.30 ms per frame: draws 7.80, output pass 1.56, conversions 1.25 (was 16.9 per tick), view syncs 0.94, texture work 0.93, frame as texture 0.68; slowest 51.6 ms.
- Test 2 (screenshots at 1500 / 2500 / 3500): the laptop's new build matches the RX 9070 XT's within channel difference 7 (27-31 % of the pixels; Intel against AMD arithmetic, not E). The old build's shots were taken while it ran behind (files 1501, 2502), so they show other steps (difference up to 109).
- Test 3 (free play at the defaults, 12,010 ticks): 38.5 fps average, 774 ticks dropped, GPU 17.04 ms per frame (slowest 93 ms); 818 pipeline binds and 873 texture binds per frame (mission 1: 52 and 131). User: the frame rate falls to 10-20 fps whenever a detailed model (an aircraft) is on screen.
- The User's hangar recording (`blue_repro`) on the RX 9070 XT at 1×, `--gpu-profile`: GPU 0.56 ms per frame (draws 0.46 ms; 702 draws, 536 pipeline binds, 696 texture binds per frame). At mission 1's laptop/AMD ratio for draws (×6) that is about 3 ms, far from the 50-100 ms of a 10-20 fps frame: the cause is not yet known (laptop CPU — VU1 HLE and GS processing — or a per-bind cost of the Intel driver). Next: the same recording with `--gpu-profile` on the laptop, and the Performance window of the debug overlay in a slow scene.
- The User's Performance window in a slow scene (laptop, campaign hangar aircraft select, F-4E, 1×, debug overlay open; screenshot outside the repo): 10.9 fps (92 ms per frame, worst 206 ms), 4 steps in the frame (catch-up limit), 382 dropped. Renderer CPU per frame: GS processing 23.58 ms, recording 19.54 ms, so about 5.9 + 4.9 ms per tick; RX 9070 XT machine, same scene type (`blue_repro`, 1 tick per frame): 0.87 + 0.76 ms (about ×6.6). GPU work of that frame: 8 render passes, 80 barriers, 5 frame-buffer copies; 616 textures drawn, 522 GPU decodes (RX 9070 XT hangar average: 105 and 29 per frame).
- Reading: per tick about 23 ms of the 92 ms, more than the 16.7 ms a tick may take, so this is real cost, not only the catch-up spiral (4 ticks per frame multiply the work but also share the frame). The render thread accounts for about 43 of the 92 ms; the rest is the game thread (game code + VU1 HLE, not measured anywhere yet) or waiting for the GPU. Proposed next (needs the User's approval, code in `src/`): log the game thread's time per tick (game code, VU1 HLE) at exit and show it in the Performance window, then run the hangar on the laptop with `--gpu-profile`.

### IG-8. Game-thread timing (User approval 2026-11-03: allowed if it adds under 1 ms in release builds)
- Host time of each game tick (`game.RunTicks(1)`, the game thread's work with the VIF1 sends; the VU1 HLE runs inside `sceDmaSend` channel 1 and is timed apart in `DmaStats::vif1_ns`), and of the main thread's waits for the render thread (`Submit`, and the two `WaitIdle` calls while the overlay is shown). Log at exit, all builds: `game thread per tick at normal speed: … (VIF1 and VU1 HLE … of it), slowest …; main thread waiting for the render thread (presentation and v-sync included) … per frame`. Performance window: the frame's steps and the last frame's wait.
- Cost: about one VIF1 send per tick (check3: 5,078 sends in 5,449 steps), so 4 clock reads per tick and 2-4 per frame, well under 1 µs. Game behaviour unchanged (host time is only read, never handed to the game).
- Note for the overlay figures: while the Performance window is shown, the main thread waits for the render thread at the start of each frame (ImGui is not shared between threads), so the game thread and the render thread no longer overlap; check3's 92 ms is their sum.
- RX 9070 XT, mission 1 at 1× to vblank 3600: 3.01 ms per tick (VIF1 + VU1 HLE 0.83 ms), slowest 34.1 ms (loading); waiting for the render thread 13.66 ms per frame (mostly v-sync). `ctest --preset dev` passes; dev and release build.
- Laptop, the `blue_repro` replay (User run 2026-11-04, 19,142 ticks: the replay's 3,230, then idle pads): 59.6 fps, 19 ticks dropped; game thread 2.84 ms per tick (VIF1 + VU1 HLE 0.43 ms); 363 draws, 112 pipeline binds per frame, GPU 5.07 ms. Same path as on the RX 9070 XT (User: the replay reaches the hangar), but the hangar is only about vblanks 2260-3230 of it, so the session averages are mostly other screens. RX 9070 XT, same replay, `--fast-until 2200 --exit-at 3230` (hangar stretch only; S-37A at vblank 2264): 1,645 draws, 1,382 pipeline binds per frame; game thread 3.09 ms per tick (VIF1 + VU1 HLE 2.28 ms); GS processing 2.30 ms, recording 1.84 ms, GPU 1.24 ms per frame. Next: the same command on the laptop (without `--hash-log`, which adds about 2 ms per tick).
- Laptop, hangar stretch (User run 2026-11-04, same command, `--gpu-profile`): 38.5 fps, 39 ticks dropped, 1.34 ticks per drawn frame. Per tick, laptop against RX 9070 XT: game thread 8.66 against 3.09 ms (VIF1 + VU1 HLE 6.47 against 2.28, ×2.8); GS processing about 8.4 against 2.1 ms (×4); recording about 2.6 against 1.7 ms; GPU about 8.2 against 1.2 ms (draws 7.62 ms per frame; 2,446 draws, 2,059 pipeline binds, 2,429 texture binds per frame). Each part alone fits a 16.7 ms tick.
- What sets the frame rate: the render thread per frame = GS processing 11.22 + recording 3.45 + waiting for the GPU 10.80 = 25.5 ms, which is the 38.5 fps exactly. The wait is on the fence of the frame before last (2 frames in flight), so a frame takes about 2 frame periods (~51 ms) from submit to its fence although its timestamps add up to 11 ms: the GPU work does not overlap the CPU work as intended. Candidates: (a) each frame's submit waits for the swapchain image (`image_available`) at COLOR_ATTACHMENT_OUTPUT, and every GS pass writes colour attachments, so all GS draws of a frame wait for the presentation engine to free an image (FIFO, 3 images, Intel/DWM); (b) the iGPU runs slower in wall time than its timestamps show (shared power budget with the busy CPU). Next: the same run with v-sync off (`settings.json` `presentation.vsync` false); a wait that drops points to (a).
- Laptop, hangar stretch with v-sync off (User run 2026-11-04, IMMEDIATE present mode): waiting for the GPU 10.80 → 0.62 ms per frame; 771 drawn frames in 18.0 s (43 per second; v-sync on: 768 in 17.9 s), 49 ticks dropped (v-sync on: 39); GS processing 11.08, recording 3.52 ms per drawn frame; game thread 8.94 ms per tick (VIF1 + VU1 HLE 6.50). Both runs ran the game at about 95 % speed (1,030 ticks in 18 s); the 38.5 fps with v-sync on was presented frames carrying 1.34 ticks each.
- Reading: candidate (a) confirmed for the wait (it is the FIFO presentation: with v-sync on every frame's GS draws wait for a free swapchain image). It does not limit the game speed by itself: with v-sync off the render thread's CPU (about 14.6 ms per drawn frame, 10.9 ms per tick) and the game thread (8.9 ms per tick, 6.5 of it VU1 HLE) leave too little margin in a 16.7 ms tick, so ticks still group and some drop. Proposed: (1) acquire the swapchain image only for the final copy (GS draws submitted without the `image_available` wait), so v-sync on behaves as v-sync off here; (2) profile the CPU cost of GS processing and the VU1 HLE in the hangar (2,400 draws per frame) on the RX 9070 XT machine and reduce it. Both need the User's approval.

### IG-9. Hangar CPU work: baseline and profile (plan approved by the User 2026-11-04)
- Plan: profile first (Windows Performance Recorder, `wpr -start CPU`, the IG-8 hangar command, on the RX 9070 XT machine and on the laptop), then reduce the largest CPU costs of the VU1 HLE and GS processing one change at a time, every change bit-identical (screenshots byte-identical, `--hash-log` identical).
- Baseline (build before any change, kept with its PDB under `out/baseline_ig8/`): job file `out/ig8_cpu.json` (`tools/research/runmany.py`, results under `out/ig8_cpu_baseline/`, not committed): the User's hangar replay (vblanks 2264, 2442, hash log), the campaign hangar route on card S (9200, F-4E, hash log), `--mission 1` at 1× (1500 / 2500 / 3500, hash log) and at 4× with SMAA, `--mission 7` at 2× (hash log). All 5 jobs clean.
- Waiting for: the two traces (User runs, administrator prompt).
- Profile, RX 9070 XT (`wpr -start CPU`, the IG-8 hangar command with the baseline build; `xperf -a stack -butterfly` over the hangar stretch, 3.3-20.3 s; trace outside the repo). Inclusive CPU per tick (1,030 ticks; the threads' wait samples left out): game thread: VIF1 sends 2.68 ms (VU1 model program `RunModelStrip` 1.40, of it `Draw` 0.68 and `ClipTriangle` 0.27; `Vif1::Unpack` 0.45; VIF commands and the rest ~0.8), game code 0.63 ms. Render thread: `ProcessGif` 1.71 ms (`GsMemory::WriteImage` 0.81; `Kick`/`EmitPrimitive` 0.53, of it `DecodeTexture` 0.24), `GsBackend::Record` 2.0 ms (much of it in the AMD driver). The `DecodeTexture` block bookkeeping suspected in the plan is small.
- The uploads of one hangar step (`--dump-dma-at 2300`, scratch parser): 167 transfers, 1.85 MB, most of it at DBP 0x2D00: the 512 × 512 T8 skin three times (786 KB, whole-block path), the 1024 × 512 T8 room in two packets (524 KB, run path: the block path needed the transfer in one piece), smaller textures over the same memory. Each overwrites the previous one, so the repeat skip never applies and about 1.85 MB is swizzled again every step.
- Change A (`renderer.md` §2.9): the whole-block path keeps the blocks it assembled with the packet record and stores them again when the same bytes come back over changed blocks; it also takes packets that hold whole block rows (the room's halves). Results: the IG-9 suite (`out/ig8_cpu.json`, 18 screenshots, 4 hash logs) identical to the baseline; `ctest --preset dev` passes; dev and release build. Hangar stretch on the RX 9070 XT, two runs each: GS processing 2.35 / 2.36 → 1.82 / 1.78 ms per frame (−23 %); game thread unchanged (3.15 → 3.08 ms per tick, noise).
- Inlining: the development build (RelWithDebInfo) is compiled with CMake's default `/Ob1` (only functions marked `inline` are inlined), Release with `/Ob2`, so the profile and all laptop figures so far are of slower code than players run. A RelWithDebInfo tree configured with `/Ob2` outside the repo files (`out/build_ob2`, the dependency sources of `build/windows/_deps` reused, nothing fetched), hangar stretch, two runs each: game thread 3.09 → 2.47 ms per tick (VIF1 + VU1 HLE 2.28 → 1.71), GS processing ~1.79 → 1.45 ms, recording 1.90 → 1.76 ms per frame. The IG-9 suite with that build: identical to the baseline. Proposed to the User: `/Ob2` for development builds too (one line in the top `CMakeLists.txt`), so that measurements match the Release build.
- Change B (`vif1.cpp`): a command whose data words are all in the current block reads them in place (no copy into `data_`); the unpack fast path steps the CL/WL cycle counters instead of two divisions per element, and copies V4-32 elements as whole qwords. Results: the IG-9 suite identical to the baseline; `ctest --preset dev` passes. Hangar stretch (RX 9070 XT, `/Ob1`, A+B against A): VIF1 + VU1 HLE 2.27 → 2.14 ms per tick, game thread 3.08 → 2.95 ms. The VU1 model program itself (`RunModelStrip`, 1.4 ms per tick in the profile) is the larger game-thread cost left.
- Before the 0.9.7 package (User request 2026-11-08): `full.json` (30 jobs) with the last commit `975148a` built outside the repo files (`git archive` into `out/head_src`, dependency sources reused) as the reference, then the working tree (timing, E, A, B) against it: identical except the known `m3_route` / `m4_route` 5300 column x = 409 and `s1_cardS` (no pad script: it reads the live controller; reruns of the reference build differ from it the same way). `ctest --preset release` passes, `check_notices` clean, the Release executable starts (ISO verified, no warnings); `out/release/ProjectStonehenge-0.9.7-win64.zip`. The commit stamp (`AC04_GIT_COMMIT`) is taken at configure time: reconfigure after committing to stamp the release commit.
