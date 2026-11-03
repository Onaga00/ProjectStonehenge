# Ace Combat 04 (SLUS_201.52) — native PC port

<!-- Maintainers: keep this file under 200 lines. Path-specific rules live in .claude/rules/,
     the step-by-step porting procedure in .claude/skills/port-function/. -->

## Goal

- Build a native PC (x86-64) version of Ace Combat 04: Shattered Skies (NTSC-U, `SLUS_201.52`) by translating the PS2 game code into C++.
- Game logic must behave exactly like the original. Instruction order, register use and control flow may change.
- This is NOT a matching decompilation. Never spend effort reproducing the original compiler's output.
- Reference behaviour = PCSX2 running the original disc. When unsure what "correct" is, check it in PCSX2.
- Three categories of behaviour:
  1. **Original behaviour**: game logic, simulation, rules and content reproduce the original exactly.
  2. **Modern-port baseline**: features intrinsic to a PC port, built into the port. They may change presentation only, never game logic or simulation results. Only features approved by the User and listed in `docs/decisions/port-baseline.md`.
  3. **Everything else** that departs from the original goes only into the optional modding/debug layer.
- **All original game code is kept and translated**, including dead, unreachable and debug code (e.g. scenes 1 and 3). In the default build it stays unreachable exactly as on the PS2. It may only be exposed through the optional debug/modding layer (e.g. a debug menu to explore cut content). Catalogue: `docs/port/dead-code.md`; keep it updated.
- This applies to **game code only** (0x001000D8–0x002616E7). Sony SDK and C runtime code is not translated: `src/sce/` implements what the game calls. SDK functions reached only from dead game code (e.g. the HDD path) get stubs that log the call and return an error.

## Platform (ADR-005, Accepted)

- Windows first; Linux is deferred until the port is complete. Do not choose libraries or APIs that would clearly be hard to make work on Linux later.
- OS-specific code stays in `src/platform/`, behind interfaces.
- Never rely on case-insensitive file lookup: match ISO file names exactly as stored (uppercase, e.g. `DATA.CDP`).
- User data (ADR-005): everything the port writes goes under `%LOCALAPPDATA%\ProjectStonehenge\`, resolved only in `src/platform/` (Linux equivalent added later). Nothing is written next to the executable.
  - `saves\`: a PCSX2 Folder Memory Card root (`_pcsx2_superblock` + one folder per PS2 directory; `docs/formats/folder-memcard.md`). Each save folder holds the raw files exactly as the PS2 writes them, so folders can be copied between the port and PCSX2 both ways.
  - `settings.json`: all port settings (keys, rules and defaults: `docs/port/port-features.md` §1). One settings struct with JSON load/save and change notification; every change applies at once, also when the file is edited while the port runs. Game code never reads settings: the port's own menu pages (GRAPHICS under SCREEN SETTINGS, the Modern control types on CONTROLLER SETUP) exchange values with the host through `src/common/port_hooks.h`. The game's own options (brightness, SCREEN RATIO, control type, …) stay in its save data, never in `settings.json`.
  - Settings rules (User decisions 2026-10-20, defaults changed 2026-10-22): defaults depend on the display the port starts on (fullscreen; v-sync on; FXAA; the internal scale whose 448 × N lines are closest to the display height; with a refresh rate other than 60/120 Hz the frame cap for v-sync off is the highest one ≤ that rate). Validation runs (development builds: pad script, input log, PCSX2 recording, `--memcard`, `--no-memcard`) keep 1× and no anti-aliasing; so does an integrated GPU (User decision 2026-11-03). A file that cannot be read, is not valid JSON, has another `version`, an unknown key or an invalid value is deleted and replaced with the defaults. A new key needs a default, read/write in `settings.cpp`, change notification, a test in `tests/test_settings.cpp` and a row in `port-features.md`; older files stay valid (missing key = default). Removing or renaming a key or changing a value's meaning needs a `version` bump. A setting the player sees goes on a port menu page or stays file-only; ask the User before adding a new page or row.
  - `controls.ini` (User approval 2026-10-31): the key, mouse and gamepad bindings, INI for hand editing (`docs/port/port-features.md` §1 "Controls file"). Starts with a guide to every input name. Written with explained defaults only when missing; an unreadable entry keeps its default and is logged; a file of another layout version (`Version = N` line) is deleted and replaced with the defaults, like an incompatible `settings.json` (no migrations, User rule 2026-10-31); edits apply at once. `settings.json` version 2 no longer holds bindings.
  - `cache\`: renderer caches the port can rebuild (`pipelines.bin`, the Vulkan pipeline cache; User approval 2026-10-25; `docs/port/port-features.md` §6). A file for another GPU or driver, or a damaged one, is deleted and rebuilt.
  - `logs\`: only the last session's log; older logs are deleted at start. It records errors and problems in the main modules (startup, ISO check, renderer, audio, file I/O, saves, IOP stand-ins), unimplemented `sce*` calls once each, and crashes (crash handler in `src/platform/`). Never per call or per frame. No game data in logs.
- Presentation (ADR-006, Accepted): render at the internal resolution (native × 1–8, `presentation.internal_scale`, approved 2026-10-19); a final output pass scales to the host screen, keeping 4:3 (pillarbox) or 16:9 while the game's own SCREEN RATIO option is on, with a `sharp` (sharp bilinear) or `smooth` (bilinear) filter. The full 448-line draw buffer is presented (ADR-006 §4). On a monitor wider than 4:3 the game's SCREEN RATIO starts at 16:9 when no save is loaded (User request 2026-10-20; validation runs keep the PS2 default).
- Movie upscaling (ADR-006 §5, approved baseline 2026-10-13): decoded movie pictures (640×448; 640×336 for SS) pass a separate, replaceable upscaling stage (AMD FSR 1 EASU, + RCAS if it helps) before the output pass, scaled by the internal-resolution factor: a passthrough at 1×. Game code and guest RAM unchanged; each picture still held exactly 2 game ticks.
- Simulation and presentation (ADR-002, Accepted): one game thread, fixed 59.94 Hz step; presentation on its own render thread (one frame in flight, only copies of the frame's GS data; 2026-10-25), decoupled and interpolated, v-sync on/off, frame cap 60/120/144/240 with 360 as the default upper limit (the cap applies only with v-sync off).
- Approved PC-port features: `docs/decisions/port-baseline.md`; as built (settings, GRAPHICS page, Modern control types, renderer caches, frame-rate overlay): `docs/port/port-features.md`.

## Architecture (ADR-001, ADR-003, ADR-004: Accepted 2026-10-11; ADR-007: Accepted 2026-10-13)

- Memory (ADR-001): guest memory, 32 MB RAM + scratchpad at 32-bit PS2 addresses; `ps2ptr<T>`, guest stack for escaping locals, `CallGuest` for indirect calls.
- Rendering (ADR-003): own Vulkan renderer for the AC04 GS subset at the packet level; VU1 programs as CPU HLE. Internal scale 1–8× (`docs/port/renderer.md` §1; anti-aliasing and anisotropic filtering study in its §1.8).
- Audio (ADR-004): HLE of SOUND.IRX/VAG.IRX at the SIF RPC boundary with our own SPU2-subset mixer.
- Translation strategy (ADR-007): hand translation by Claude in **vertical slices** (`docs/port/slices.md`); a recompiler is rejected. Correctness comes from PCSX2 save states, input recordings and function test vectors (`docs/port/validation-strategy.md`).
- VS LAN play (ADR-008, Accepted 2026-10-13): option A, lockstep input sharing over UDP, optional layer (category 3), slice S9 after S8. No game-code changes.

## Working rules

These replace any older per-phase stop conditions where they conflict.
- **Work autonomously on**:
  - decompilation and analysis;
  - renaming, and creating functions and data types in Ghidra;
  - research documents;
  - research tools in `tools/`;
  - importing files into the Ghidra project.
- **Ask first before**: writing any code for the port skeleton or in `src/`; any architecture decision (including marking an ADR `Accepted`); adding, fetching or downloading any dependency.
- **Always ask before destructive Ghidra operations**: clearing analysis, deleting functions or programs, re-importing a program that already exists.
- **PCSX2 questions**: give the User exact steps (breakpoints, registers, memory, what to do in the game) and continue with other work while waiting, unless the answer blocks you.
- **No git commits.** The User reviews and commits.
- **No sub-agents.** Do all work in the main session; never spawn sub-agents (User rule 2026-10-14).
- Log every Ghidra rename and comment in `docs/rename-log.csv`, and write findings to the docs as you go, not only at the end.

## Game files and local paths

- Absolute paths to the ISO, the extracted disc files and the Ghidra project are in `CLAUDE.local.md` (machine-specific, not committed).
- Treat every folder outside this repo as read-only.
- **The port reads all game data at runtime from the user's own original disc image** (ISO 9660, path from configuration). That includes the executable's initialised data (`.data`, `.rodata`, `.vutext`, `.vudata`): translate code, never copy tables into `src/`.
- The port accepts only the supported image and verifies its SHA-1 at startup (`docs/platform/executable.md`).
- The extracted-disc folder is for research tools (`tools/`) only. The port never reads it.
- Never copy or commit game data: the ISO, `SLUS_201.52`, `*.IRX`, `IOPRP23.IMG`, `*.CDP`, `*.TBL`, `*.SMV`, `*.VPK`, PCSX2 save states (`*.p2s`, they contain game memory; read them with `tools/p2s_read.py`), or anything extracted or converted from them (textures, models, audio, video, archives, tables). Generated output goes in `build/` or `out/` (both gitignored).
- Enforcement: `tools/hooks/pre-commit` (game-data names, files > 1 MiB outside `third_party/`, known format signatures). Enable per clone with `git config core.hooksPath tools/hooks`.

## Repo layout

| Path | Contents |
|---|---|
| `src/game/` | Game logic translated from EE code |
| `src/sce/` | PC implementations of the Sony SDK functions the game calls (`sce*`), same names and signatures |
| `src/platform/` | OS/backend layer: window, renderer, audio, input, file I/O, timing |
| `src/common/` | `types.h`, `ee_math.h` (EE float behaviour), `vu0_math.h` (VU0 macro-mode vector math) |
| `tools/` | Python 3 extractors and format parsers (research only) |
| `tools/hooks/` | Git hooks (`pre-commit` blocks game data) |
| `third_party/` | Only third-party code we modify (rules in `.claude/rules/third-party.md`); listed in `THIRD_PARTY_NOTICES.md` |
| `tests/` | Behaviour tests using values captured in PCSX2 |
| `docs/` | Index `docs/README.md` (start there); status `docs/STATUS.md`; `docs/game/` core structure, `docs/subsystems/` game systems, `docs/platform/` executable and SDK, `docs/port/` the port (features, command line, slices and their results, coverage, dead code, risks, validation), `docs/pcsx2/` pending checks (`procedures.md`, also the functions waiting for a PCSX2 answer) and past runs (`sessions.md`, one section per run) |
| `docs/formats/` | One file per reverse-engineered data format |
| `docs/decisions/` | Architecture decision records (`ADR-NNN-*.md`) and the proposed dependency list |
| `docs/symbols.csv` | `address,name,kind,status` for every symbol we name |

Folders and files in this table that don't exist yet are created when first needed.

## Build and test

- Build system: **CMake ≥ 3.24**. Language standard: **C++17**.
- Rendering: **Vulkan 1.2** minimum (ADR-003) with `VK_EXT_extended_dynamic_state` (ADR-009, Accepted 2026-10-30). Window, input and audio output: **SDL3**.
- Targets: Windows (MSVC) first. Linux (GCC, Clang) after the port is complete (ADR-005); macOS via MoltenVK is best effort.
- Dependencies are decided at the skeleton phase, within the platform requirements (ADR-005).
- Every third-party dependency needs the User's approval and a GPL-3.0 compatibility check before it is added. List with licenses: `docs/decisions/dependencies.md`.
- Third-party code rules (details: `.claude/rules/third-party.md`):
  - unmodified dependencies are fetched by CMake `FetchContent`, pinned to an exact version/commit with a checksum, never copied;
  - only code we modify is copied, into `third_party/<name>/`, with its original license, an `UPSTREAM.md`, and the original headers plus a "Modified by the ProjectStonehenge project" note;
  - never edit third-party code outside `third_party/`, never copy it into `src/`;
  - `THIRD_PARTY_NOTICES.md` is updated in the same change; `python tools/check_notices.py` must pass.
- When a new tool or project is used as a reference, add it to the README "Acknowledgements" in the same change.
- Architecture decisions: `docs/decisions/ADR-*.md`. Status `Proposed` means not decided: do not build on a proposed ADR without asking.
- Dependencies (approved 2026-10-11; AMD FidelityFX FSR 1 approved 2026-10-13; SMAA and NVIDIA FXAA 3.11 approved 2026-10-20): `docs/decisions/dependencies.md`, pinned in `cmake/Dependencies.cmake`. Any other dependency still needs the User's approval.
- Build and test (from a "Developer PowerShell for VS 2026", which puts the bundled CMake on PATH; first configure downloads the pinned dependencies):
  - `cmake --preset windows`
  - `cmake --build --preset dev` (RelWithDebInfo, development build), `--preset debug`, or `--preset release`
  - `ctest --preset dev` (or `debug` / `release`)
  - release archive: `cmake --build --preset release --target package_release` -> `out/release/ProjectStonehenge-<version>-win64.zip` (executable, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md` only; Release configuration only; stops on game-data names or signatures; `cmake/PackageRelease.cmake`)
  - executable: `build\windows\src\<Config>\ProjectStonehenge.exe`; parameters: `docs/port/command-line.md`
- Development build = every configuration except Release (`AC04_DEV_BUILD=1`): console window, debug overlay on by default (`\` opens it), F8 native screenshot, the development parameters of `docs/port/command-line.md` (grouped by purpose, with recipes); Vulkan validation only with `--vk-validation` (User decision 2026-10-16: the layer makes the hangar run at ~13 fps). Frame-rate overlay: `--fps-overlay` (all builds).
- After any dependency change: `python tools/check_notices.py`.

## Facts about SLUS_201.52

Full section table: `docs/platform/executable.md`.

- Fully stripped: no `.symtab`, `.mdebug` or `.comment`. No original names exist; every name in this project is ours.
- Sony SDK 2.3.0 (libmc 2.3.2). Compiler: GCC 2.9x `ee-gcc` with g++ (exact version is a hypothesis; not needed for the port).
- Game code is C++ written as "C with classes": no vtables, no `new`/`delete`, very few indirect calls. Details: `docs/platform/executable.md`.
- No `.lit8`: game math is single-precision `float`.
- `.text` 0x00100000–0x00297B33 (~1.6 MB) also contains statically linked Sony SDK, libc and libgcc code.
- `.vutext` 0x00297C00–0x0029C8FF, `.vudata` 0x004021F0–0x004036EF and the 17 `.DVP.overlay.*` sections are VU microcode/data, not R5900 code.
- `.data` 0x0029C980–0x004021E7 (~1.4 MB) is unexplored; likely contains game tables.
- `gp` = 0x004159F0 (from `.reginfo` and crt0), used for `.sdata`/`.sbss` access. Set in Ghidra as one register-context range over `.text`.
- Boot and main loop (`docs/game/boot.md`): `main` 0x001001F0; main loop 0x00100290–0x001003BC inside `main`; scene table 0x0029FDA0; game bump allocator 0x00101000.
- Architecture facts for the port: `docs/game/memory.md`, `docs/game/threads-timing.md` (single game thread, frame-locked 60 Hz NTSC), `docs/subsystems/rendering.md`, `docs/game/iop-interface.md`, `docs/game/random.md`.
- Ghidra memory blocks `registers.io`, `vu0.*`, `vu1.*`, `registers.gs`, `iop_ram`, `scratchpad`, `RAM(uncached*)` and `BIOS/*` are added by the Emotion Engine plugin, not by the ELF.

## Disc files

All "read" entries below mean: read from the ISO image at runtime, never from extracted files.

| File | Role | Port handling |
|---|---|---|
| `SLUS_201.52` | Main EE executable | Translate code into `src/game/`; load its initialised data sections from the ISO at runtime |
| `SYSTEM.CNF` | Boot config | Reference only |
| `IOPRP23.IMG` | Sony IOP kernel update image | Not used |
| `IRX/LIBSD, MCMAN, MCSERV, PADMAN, SIO2MAN, USBD` | Stock Sony IOP modules | Not used; replaced by `src/sce/` + `src/platform/`. Exception: libsd's reverb preset table is read from `IRX/LIBSD.IRX` at runtime (`docs/port/validation.md` §2ab) |
| `IRX/SOUND.IRX`, `IRX/VAG.IRX` | Namco sound drivers: SOUND = RPC 0x81000101 (SFX command batches), VAG = RPC 0x81000102 (6 streaming slots) (`docs/game/iop-interface.md`, `docs/subsystems/sound.md`) | Not used; their RPC interface is reimplemented in the audio backend (ADR-004) |
| `IRX/USBJOY.IRX` | USB joystick driver, RPC 0x80000210 | Not used; replaced by the input backend |
| `BIN/DATA.CDP+TBL`, `BIN/BD.CDP+TBL` | Packed archives: TBL = `{u32 sector, u32 size}[]`, CDP = sector-aligned entries, loaded by index (`docs/formats/CDP.md`). DATA = 360 `count+offsets` containers (missions, aircraft, menus, sound headers); BD = 13 raw SPU ADPCM banks | Read by index like the game (`tools/cdp_extract.py` is for research only) |
| `MIS/MIS01–18.SMV` | Briefing movies: MISnn belongs to campaign mission nn (confirmed from code) | The 18 MIS/*.SMV files are not mission data. They contain MPEG-2 video in a Namco chunked container, the same format as the cutscenes in SMV/ (which also carry audio). Read and demux from the ISO at runtime; decode MPEG-2 |
| `SMV/*.SMV` | INTRO, SS01–SS12 cutscenes, STAFF roll. Namco chunk container (0x800 header sector, then chunks `{next_size, video_bytes, audio_off, audio_bytes}`), MPEG-2 video (640×448, SS 640×336) plus 0x4000-byte PCM audio blocks (48 kHz s16 stereo, left/right in alternating 0x200-byte blocks, confirmed against a PCSX2 capture); `docs/formats/SMV.md` | Read and demux at runtime; decode MPEG-2; PCM goes to the SPU2 core-0 sound-data input through SOUND.IRX (`docs/subsystems/sound.md`) |
| `VAG/*.VPK` | Packs of VAG (Sony ADPCM) audio, streamed through VAG.IRX slots | Stream from the ISO at runtime; decode in the audio backend |

Statements marked "probably", "suggest" or "unverified" are hypotheses. Confirm them from code before building on them, then update this table.

## Working with Ghidra (MCP)

- Ghidra is the research database. When you name a function, global or struct, rename it in Ghidra AND add or update the row in `docs/symbols.csv`.
- Ghidra decompiler output is a draft. Check the disassembly whenever the function uses MMI instructions, 128-bit `lq`/`sq`, VU0 macro mode (`COP2`, `qmfc2`, `qmtc2`, `v*`) or COP0.
- If globals appear as `gp + offset` instead of named addresses, the `gp` register value is wrong: compare it with `.reginfo` before continuing.
- Never create functions or run disassembly inside `.vutext`, `.vudata` or `.DVP.overlay.*`.
- Destructive operations (clearing analysis, deleting functions or programs, re-importing an existing program) need the User's OK (see Working rules).
- Rename policy: rename in Ghidra only when the behaviour is confirmed. Otherwise add a `HYPOTHESIS:` plate comment and a `status=hypothesis` row in `docs/symbols.csv`. Log every rename and comment in `docs/rename-log.csv`.

## Naming

One convention for Ghidra, docs and C++ code. **No type prefixes** (no Hungarian: no `dw`, `w`, `p`, `a`, `fl`, `b`… in names).
- Game functions: PascalCase, verb first (`RequestScene`, `LoadBootData`); scene handlers `Scene<Name>`.
- SDK and libc functions: their original names, never changed (`sceGsSyncPath`, `sceCdRead`, `memcpy`).
- Globals: `g_` + PascalCase (`g_MainContext`, `g_MissionNo`). Constant tables: `k` + PascalCase (`kMissionIdTable`). Only exception: `g_p` for a pointer global whose pointee is also a named global (`g_pMainContext` → `g_MainContext`).
- Struct fields: lower snake_case (`next_scene`, `cur_buf`).
- Types (structs, enums): PascalCase (`MainContext`); enum values `kPascalCase`.
- Unnamed: `func_XXXXXXXX` (functions), `D_XXXXXXXX` (globals), `unk_XX` (struct fields at hex offset XX). Inside Ghidra the defaults `FUN_`/`DAT_` stay.
- When ghidra-mcp suggests or enforces a Hungarian prefix, override it (`strict_mode=false`) and rename auto-prefixed struct fields after creating a struct.
- Rename only when behaviour is confirmed.
- Put `// EE: 0x00XXXXXX` on the line above every translated function, with the original start address.
- Unconfirmed interpretations in code get a `// HYPOTHESIS:` comment.

## Workflow

- Translation runs slice by slice (ADR-007, `docs/port/slices.md`; order approved 2026-10-13). Per slice:
  1. plan: functions (`tools/research/slices.py --list <slice>`), renderer/audio/input/`sce` features, validation points;
     - **decompile first** (User rule 2026-10-15): before implementing any part of a slice, decompile and document everything the slice uses from the game files: every game function it reaches, the VU microcode programs it runs (disassembled and documented in `docs/subsystems/vu-programs.md`, `.claude/rules/vu-microcode.md`), and the data formats and tables it reads. Only then write port code for the slice;
  2. collect every PCSX2 request of the slice (save states, screenshots, recordings, test vectors) into **one** batch of exact steps in `docs/pcsx2/procedures.md`, early, and keep working while the User runs it;
  3. translate with the `port-function` skill; implement only the renderer, audio and `sce` features the slice needs;
  4. validate (`docs/port/validation-strategy.md`): tests pass, RAM checkpoint 0 unexplained ranges, screens match PCSX2;
  5. fix doc/Ghidra errors found on the way (logged);
  6. report (counts against `docs/port/coverage.md`, what is visible/playable, validation results, documented differences, problems) and wait for the User's go-ahead.
- To translate a function, follow the `port-function` skill.
- Order of work: the slice order in `docs/port/slices.md` (the skeleton priorities SDK → boot → main loop → file loading are done).
- No commits by Claude (see Working rules). Changes should still be grouped so the User can commit one subsystem or one source file at a time.
- Ask before large refactors, renaming files, or changing the repo layout above.
- Open research questions are tracked in `docs/open-questions.md`, pending PCSX2 checks in `docs/pcsx2/procedures.md`; update them when one is answered. A PCSX2 session's results go into a new "Run NN" section of `docs/pcsx2/sessions.md`; slice reports into `docs/port/slices.md` ("Results per slice"), validation records into `docs/port/validation.md`; `docs/STATUS.md` keeps only the summary. Add new docs to `docs/README.md`.
