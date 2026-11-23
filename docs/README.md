# Documentation index

Reference documentation for the Ace Combat 04 (SLUS_201.52) PC port. Project rules: `../CLAUDE.md`. Short status: `STATUS.md`.

Where to start:
- **Running or configuring the port:** `port/port-features.md` (settings, defaults, menus, overlays) and `port/command-line.md` (every parameter, development builds included, with recipes).
- **Translating game code:** `port/slices.md` (plan and results per slice), the subsystem notes under `subsystems/`, `game/` and `formats/`, and the `port-function` skill.
- **Checking against PCSX2:** `port/validation-strategy.md` (method), `port/validation.md` (every run), `pcsx2/procedures.md` (what the User still has to capture).

Reorganised 2026-10-20 (no content removed): the PCSX2 session files were merged into `pcsx2/sessions.md`, `port/pcsx2-pending.md` into `pcsx2/procedures.md`, `formats/SM-model.md` into `formats/MODEL.md`; the slice reports moved from `STATUS.md` to `port/slices.md` and the skeleton milestones to `port/skeleton-readiness.md`; the `settings.json` keys moved from `port/command-line.md` to `port/port-features.md`.

Reorganised again 2026-10-22 (no content removed; merged documents keep their sections, numbered ones with a prefix): `game/scenes.md` and `game/main-context.md` into `game/boot.md`; `subsystems/iop-sound.md` into `subsystems/sound.md`; `subsystems/mission-load.md` into `subsystems/mission-flow.md`; `platform/supported-version.md`, `platform/elf-sections.md` and `platform/toolchain.md` into `platform/executable.md`; `port/debug-overlay.md` into `port/port-features.md` (§8, its §4.1 = §8.4.1); `port/internal-resolution.md` and `port/gpu-work.md` into `port/renderer.md` (§1.x and §2.x); `port/s4-inventory.md` and `port/s5-plan.md` into `port/slices.md` (appendices A and B, §A.x and §B.x). References in the repo were updated; `rename-log.csv` and the Ghidra comments it records keep the old names.

## Top level

| File | Contents |
|---|---|
| `STATUS.md` | decompilation status, pending PCSX2 checks, User decisions, risks, readiness |
| `open-questions.md` | open research questions (and answered ones with pointers) |
| `symbols.csv` | `address,name,kind,status` for every named symbol |
| `rename-log.csv` | every Ghidra rename, comment and created function |

## `game/`: core game structure

| File | Contents |
|---|---|
| `game/boot.md` | crt0, `main` initialisation, main loop, static constructors; scene system (dispatch, the 8 scenes, request helpers, transition graph); `MainContext` layout and flags, game-state block `g_GameState` |
| `game/memory.md` | work-heap allocator, pointers and 32-bit dependencies, alignment, scratchpad |
| `game/threads-timing.md` | threads, interrupts, frame pacing, frame count vs. time |
| `game/file-loading.md` | file table, CD request queues, DATA/BD archive access |
| `game/iop-interface.md` | EE side of the SOUND, VAG and USBJOY RPCs |
| `game/random.md` | the five random sources and their users |
| `game/math.md` | vector and Euler-angle helpers, the MMI colour lerp |

## `subsystems/`: game systems

| File | Contents |
|---|---|
| `subsystems/frontend.md` | scene 4: title, menus, options, briefing, in-menu movies, loading screen |
| `subsystems/mission-flow.md` | scene 2: flight scene sub-states, mission setup, rules and event script, cutscenes, replay and demo, pause menus; "Mission load": exact creation and allocation order up to the first gameplay frame (slice 5 block A) |
| `subsystems/flight.md` | player aircraft: controls, flight model, landing gear, collision response |
| `subsystems/pad-input.md` | pad modes and mapping tables; which actions read DualShock 2 button pressures (throttle, yaw, map zoom) |
| `subsystems/ai.md` | enemy and friendly units |
| `subsystems/weapons.md` | projectiles, damage, terrain and unit collision |
| `subsystems/camera-hud.md` | cameras and the HUD |
| `subsystems/results-startup.md` | scene 5 results, scene 6 startup, scene 7 HDD error |
| `subsystems/save.md` | memory-card and HDD save layers |
| `subsystems/sound.md` | EE-side sound, radio chatter, subtitles, text engine, rumble; SOUND.IRX and VAG.IRX internals and command tables |
| `subsystems/rendering.md` | packet transport, flight frame structure, cameras, models, lighting, terrain, shadows, cutscene characters |
| `subsystems/sky.md` | sky dome, stars, sun/moon and lens flare, clouds, weather, full-screen post effects (S5b) |
| `subsystems/effects.md` | effect pools and special objects |
| `subsystems/vu-programs.md` | VU1/VU0 microcode blocks and their upload |
| `subsystems/vs-mode.md` | two-player VS mode: entry, split screen, rules, what the port needs |

## `formats/`: data formats

| File | Contents |
|---|---|
| `formats/CDP.md` | DATA/BD archives and the DATA index map |
| `formats/MISSION.md` | mission entries (DATA 1–100) |
| `formats/AIRCRAFT.md` | aircraft entries (DATA 101–172) |
| `formats/MODEL.md` | `SM` model format (with the slice S4 loader notes) |
| `formats/GIM.md` | Namco GIM textures |
| `formats/SMV.md` | movie container |
| `formats/SAVE.md` | memory-card save image |
| `formats/folder-memcard.md` | PCSX2 folder memory card layout used by the port |
| `formats/p2m2.md` | PCSX2 input recording file (not a game format; used for replay validation) |

## `platform/`: executable and SDK

| File | Contents |
|---|---|
| `platform/executable.md` | the supported disc image and its checksums; ELF section table; compiler, SDK version, `gp` |
| `platform/sdk-functions.md` | SDK and libc functions identified in the executable |
| `platform/gs-subset.md` | GS features used by the EE-side packet code |

## `decisions/`: decision records

| File | Contents |
|---|---|
| `decisions/ADR-001-memory-model.md` … `decisions/ADR-009-gpu-dynamic-depth-state.md` | architecture decisions (status in each file); ADR-007 translation strategy (Accepted), ADR-008 VS LAN play (Accepted, option A), ADR-009 `VK_EXT_extended_dynamic_state` required, GS draws share pipelines (Accepted 2026-10-30) |
| `decisions/port-baseline.md` | approved modern-port features and candidates |
| `decisions/dependencies.md` | third-party dependencies: approved, added, options |
| `decisions/naming-convention.md` | naming rules and the analysis behind them |

## `port/`: the port

Using and building on the port:

| File | Contents |
|---|---|
| `release/keybinds.md` | the players' guide to the default controls and how to change them in `controls.ini`; shipped as `keybinds.md` in the release archive (`cmake/PackageRelease.cmake`) |
| `port/port-features.md` | what the port adds and how it is built: settings (where they live, rules, display defaults, every `settings.json` key), presentation, the GRAPHICS page, Modern control types, the port-owned guest memory, renderer caches, the frame-rate overlay; §8 the debug overlay (features, dependency, rules) |
| `port/command-line.md` | every command-line parameter, grouped (all builds; development builds: disc and card, start points, input, run control, dumps, renderer), development-build keys, recipes, the disc check at start |
| `port/linux.md` | the Linux build and the AppImage (proposed, issue #13): POSIX platform files, compiler flags that match the MSVC build, the Ubuntu 22.04 build container, packaging, Steam Deck setup |
| `port/renderer.md` | renderer studies and work: §1 internal rendering resolution 1-8x (study, as-built table §1.7, anti-aliasing and anisotropic filtering feasibility §1.8); §2 what CPU work could move to the GPU, GPU texture decoding, generation-based texture cache and per-texture images (plan, step 1 measurement, as built 2026-10-22); §3 GPU time measurement (timestamps, `--gpu-profile`, 2026-10-30) |

Translation progress and planning:

| File | Contents |
|---|---|
| `port/slices.md` | translation slices: order, contents, features, validation points (ADR-007), the results per slice; appendix A the slice 4 inventory, appendix B the slice 5 plan (historical) |
| `port/coverage.md`, `port/coverage-functions.csv` | decompilation status per function and subsystem |
| `port/translated-functions.csv` | every translated function and its source file |
| `port/dead-code.md` | dead, debug and unreachable code |
| `port/risks.md` | port risks per subsystem |
| `port/skeleton-readiness.md` | functions and SDK calls the port skeleton needed, and the phase 3 milestone results (historical) |
| `port/s5-block*-inventory.csv` | slice 5 function inventories per block (historical; the plan is `port/slices.md` appendix B) |

Validation:

| File | Contents |
|---|---|
| `port/validation-strategy.md` | how slices are validated: RAM checkpoints, screenshots, input recordings, determinism checker, test vectors |
| `port/validation.md` | record of every validation run, newest last (§2m: 2026-10-20, settings and renderer) |
| `port/ramdiff-expected*.csv` | classified RAM differences against PCSX2 (all checkpoints, per slice) |

## `pcsx2/`: reference runs

| File | Contents |
|---|---|
| `pcsx2/procedures.md` | pending PCSX2 checks with exact steps, and the functions whose translation waits for a PCSX2 answer (User rule 2026-10-16) |
| `pcsx2/sessions.md` | results of every past PCSX2 session, one section per run (01-12), with an index of the answered checks |

Research tools: `../tools/research/README.md`.
