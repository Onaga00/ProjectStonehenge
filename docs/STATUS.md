# Project status

Ace Combat 04 (SLUS_201.52) PC port. Phase 3 (port skeleton) done (milestones A–E); architecture decisions ADR-001–009 accepted. Phase 4, translation in vertical slices (ADR-007), started 2026-10-13 with S1 (section 7). Details: `README.md` (index).

## 1. Decompilation status

Game code 0x001000D8–0x002616E7: **2,884 functions**, every one classified. Unknown: **0**.

| Status | Functions | Share |
|---|---|---|
| understood | 2,612 | 91 % |
| partly | 0 | 0 % |
| dead (not reachable from `main`) | 272 | 9 % |
| unknown | 0 | 0 % |

Names: 670 symbols in `symbols.csv` (388 confirmed game names, 25 game hypotheses).

Validation (2026-10-11, `port/validation.md`): names, totals, classification and dead-code reachability checked by `tools/research/validate.py`; blind re-review of 60 sampled functions.

### Per subsystem

| Subsystem | Functions | Understood | Dead |
|---|---|---|---|
| effects | 585 | 576 (98 %) | 9 (2 %) |
| frontend | 467 | 455 (97 %) | 12 (3 %) |
| render | 214 | 193 (90 %) | 21 (10 %) |
| core | 211 | 196 (93 %) | 15 (7 %) |
| sound | 202 | 149 (74 %) | 53 (26 %) |
| mission | 164 | 164 (100 %) | 0 |
| weapons | 116 | 116 (100 %) | 0 |
| sequence | 108 | 82 (76 %) | 26 (24 %) |
| radio | 98 | 94 (96 %) | 4 (4 %) |
| hdd | 96 | 73 (76 %) | 23 (24 %) |
| hud | 88 | 88 (100 %) | 0 |
| ai | 81 | 81 (100 %) | 0 |
| unreached | 78 | 0 | 78 (100 %) |
| replay | 72 | 72 (100 %) | 0 |
| movie | 57 | 48 (84 %) | 9 (16 %) |
| results | 40 | 40 (100 %) | 0 |
| camera | 35 | 32 (91 %) | 3 (9 %) |
| text | 33 | 30 (91 %) | 3 (9 %) |
| flight | 32 | 32 (100 %) | 0 |
| collision | 30 | 30 (100 %) | 0 |
| startup | 23 | 22 (96 %) | 1 (4 %) |
| save | 21 | 18 (86 %) | 3 (14 %) |
| file | 13 | 1 (8 %) | 12 (92 %) |
| input | 12 | 12 (100 %) | 0 |
| aircraft-data | 7 | 7 (100 %) | 0 |
| boot | 1 | 1 (100 %) | 0 |

The whole HDD subsystem never runs (its init has no caller), although only 23 of its functions are call-graph dead.

### Depth of the notes

Every reachable function has a behaviour note. Game logic (flight, AI, weapons, collision, mission scripting, HUD, results, radio selection, replay recording) is documented down to its rules, constants and random reads. Large draw routines (effect draws, terrain and model packets, the cutscene character engine, movie upload) are documented at interface level: inputs, outputs, random reads, and confirmation that they write no game state; packet details are read from the code at translation time. Details and the list of functions that use double precision: `port/coverage.md`.

## 2. PCSX2 checks still needed

Exact steps for each: `pcsx2/procedures.md` (same numbers).

| # | Check | Purpose | Blocks |
|---|---|---|---|
| S5-B | block B batch: recordings of mission 4 (free flight, firing run, to the end) and mission 3 from the S5-A start states, 10 test-vector entry/exit pairs, 2 GS dumps of effect frames, HUD screenshots (incl. 16:9), a sound capture | reference data for block B (flight, AI, rules, weapons, effects, HUD, sound) | validation of block B only |
| 1 | VU0 skinning test vectors for part types 0 and 3 (bp 0x002250E8, 0x00225140) during the no-save intro or the post-mission-18 sequence | inputs/outputs of the remaining two skinning paths | verification of decision 14 only |

Answered checks, run by run: `pcsx2/sessions.md` ("Index of answered checks").

Also before release: a folder memory card round trip with PCSX2 (needs the port's save layer, `formats/folder-memcard.md`).

## 3. Decisions

### Decided by the User (2026-10-11)

| Decision | Choice | Record |
|---|---|---|
| Memory model | B: guest memory, 32-bit PS2 addresses | ADR-001 Accepted |
| Threading and timing | A: fixed 59.94 Hz step, decoupled interpolated presentation | ADR-002 Accepted |
| Rendering | C: own Vulkan renderer for the AC04 GS subset (Vulkan 1.2 with `VK_EXT_extended_dynamic_state`) | ADR-003, ADR-009 Accepted |
| Audio | B: HLE at the RPC boundary with own mixer | ADR-004 Accepted |
| User data | everything under `%LOCALAPPDATA%\ProjectStonehenge\` (`saves\` folder card, `settings.json`, `logs\` last session only, crash handler) | ADR-005 (revised) |
| Scaling filter | two filters, `sharp` and `smooth`, in `settings.json`; settings struct + JSON + change notification, no menu yet | ADR-006 §3 |
| Baseline features | approved: output scaling with both filters, decoupled frame rate with interpolation, frame cap 60/120/144/240 (360 default upper limit), v-sync on/off, windowed and borderless, `settings.json`. Widescreen: modding layer. MSAA: later | `decisions/port-baseline.md` |

### Decided by the User (2026-10-11, phase 3 start)

All previously open decisions approved as recommended:

| # | Decision | Choice |
|---|---|---|
| 7 | Interlace handling | present the full 448-line draw buffer (ADR-006 §4) |
| 8a | Input rebinding | approved baseline feature |
| 8b | Audio on focus loss | approved as a setting, default keep playing |
| 8c | Audio device selection | later; system default device |
| 9 | Dependencies | approved: SDL3, Vulkan-Headers, volk, VMA, glslang, doctest, nlohmann/json, Dear ImGui (license re-checked per version when added) |
| 10 | MPEG-2 decoder | libmpeg2 (not needed before the movie phase) |
| 11 | Skeleton | approved, phase 3 |
| 12 | Memory card port 2 | reports empty |
| 13 | Ghidra struct field names | later |
| 14 | VU0 skinning | C++ rewrite (later phase) |
| 15 | Debug overlay | requires `debug.overlay_enabled` (default off; on by default in development builds) |
| 16 | Default scaling filter | sharp |
| 17 | Higher internal resolution | approved 2026-10-19, factor 1–8 (`decisions/port-baseline.md`) |
| — | ISO SHA-1 mismatch | refuse in release builds; development builds may continue with `--allow-unverified-iso` (logged) |

### Decided by the User (2026-10-13)

| Decision | Choice | Record |
|---|---|---|
| Translation strategy | A: hand translation in vertical slices; B (recompiler) rejected | ADR-007 Accepted |
| Validation | per slice RAM checkpoint + screenshots; gameplay: PCSX2 input recordings replayed in the port with checkpoint frames and the determinism checker; risky functions: entry/exit test vectors | `port/validation-strategy.md` |
| Movie upscaling | approved baseline: separate replaceable stage before the output pass, internal-resolution factor (passthrough at 1×), FSR 1 EASU (+RCAS if better), both video sizes, 2-tick hold unchanged; picture handed over by the decoder side path (option b, 2026-10-14), stage built in S3 | ADR-006 §5, `decisions/port-baseline.md` |
| FSR 1 dependency | approved; v1.0.2, MIT (license checked), FetchContent with SHA-256 | `decisions/dependencies.md` |
| EE FPU rounding | round to nearest, as PCSX2 runs AC04 | `port/validation.md` E-2 |
| Slice order | approved as proposed, plus S9 LAN play | `port/slices.md` |
| VS LAN play | option A: lockstep input sharing, optional layer (category 3), after S8 | ADR-008 Accepted |
| Floating-point compiler flags | approved: explicit no-FMA-contraction flags (MSVC `/fp:precise`, GCC/Clang `-ffp-contract=off`), added when gameplay translation starts | `port/risks.md` |
| README tagline ("static recompilation") | to be reworked later by the User | — |

### Decided by the User (2026-10-25)

| Decision | Choice | Record |
|---|---|---|
| GS pipeline hitches on a new GPU driver | fixed: the 34 known GS pipelines are built at startup on warm-up threads, and a Vulkan pipeline cache file is kept in the new `cache\` user-data folder (approved); open: the User's check on an NVIDIA PC with cleared driver caches | `port/validation.md` §2ai, ADR-005 §2 |
| Renderer CPU work on a second thread | built after the measurement (RT-1): GS processing, interpolation, recording and present run on a render thread beside the game ticks, one frame in flight; game logic unchanged (regression identical), +22–48 % frames per second at v-sync off; the VIF/VU1 work stays on the game thread | `port/validation.md` §2ai, §2aj; ADR-002 "Render thread" |

### Decided by the User (2026-10-31)

| Decision | Choice | Record |
|---|---|---|
| Pressure-sensitive buttons | DualShock 3 (sixaxis.sys / DsHidMini SXS) and PS3-compatible DualShock 2 adapters give the game its pressures; built, waiting for the User's hardware check | `port/port-features.md` §1, `subsystems/pad-input.md` |
| Controls | bindings in a new `controls.ini` (INI, actions for the keyboard), AC7-style keyboard defaults, mouse as a stick in flight, the original analog-mode map cycle for pads without □ pressure; `settings.json` version 2 (older files replaced with the defaults); built, waiting for the User's live check and the AC7 keys marked "ours" | `port/port-features.md` §1, `decisions/port-baseline.md` |

### Open for the User

| Question | Blocks |
|---|---|
| Ghidra struct field names (A by hand / B scripts) | nothing; later |

## 4. Top risks

| Risk | Doc |
|---|---|
| Five random sources must match bit for bit, including call order inside a frame | `game/random.md` |
| Work heap 98.4 % full in mission 1 and allocations unchecked: struct sizes and heap size must be exact | `game/memory.md` |
| Effects run in the simulation step and read the random table: they cannot be skipped or reduced | `subsystems/effects.md` |
| Draw-list build changes state and consumes `rand`: must run exactly once per step | `decisions/ADR-002-threading.md` |
| About 233 packet-writing functions and 5 VU1 programs; the GS dumps add trilinear mipmapping, CT24, Z24 sampled as a texture and destination-alpha blending to the renderer's required subset | `platform/gs-subset.md` |
| VU0 micro-mode skinning for cutscenes needs an interpreter or exact rewrite | `subsystems/vu-programs.md` |
| Flight damping and a few sound, sequence and clipping paths use double-precision soft-float; libm `sinf`/`cosf` must be bit-exact | `port/coverage.md` |
| Projectile and effect slot rules must stay exact; replays refer to slot numbers | `subsystems/weapons.md` |
| Byte-identical saves need the exact image build, checksum and write order | `formats/SAVE.md` |
| In-menu movies are paced by timer 0 in H-blanks (one loop iteration = 2 fields, measured); SMV PCM layout unconfirmed | `formats/SMV.md` |
| The ghidra-mcp plugin re-adds type prefixes to struct fields; `save_program` can hang | `decisions/naming-convention.md` |

All risks with mitigations: `port/risks.md`.

## 5. Skeleton (phase 3)

Done 2026-10-11 to 2026-10-12 (milestones A-E). Readiness study and milestone results: `port/skeleton-readiness.md` ("Phase 3 outcome").

## 6. Phase 4: translation slices (ADR-007)

Proposed order (`port/slices.md`, assignment by `tools/research/slices.py`, all 2,884 game functions assigned):

| Slice | Content | Functions |
|---|---|---|
| S0 | skeleton (done) | 244 |
| S1 | scene 6 startup (**done 2026-10-14**, accepted by the User) | 87 |
| S2a / S2b | front end: title and main menu (**validated 2026-10-14**) / options, records, save/load (**validated 2026-10-14**) | 86 / 151 |
| S3 | movies (libmpeg2, SMV audio, FSR 1 stage) (**translated 2026-10-14, validated 2026-10-15**) | 86 |
| S4 | front end: mission title, briefing, campaign menu, hangar, mission select (**translated 2026-10-16**, validation in progress: title card, briefing, campaign menu, hangar with the 3D aircraft and mission select run; PCSX2 runs A-C compared 2026-10-16: VU0 macro mode now rounds to nearest (the 1/16-pixel map sprite differences are gone), `CommitHangarSelection` byte-exact against the run B states, VS mission select 0 unexplained; hangar performance 5 fps -> 60 fps (validation layer now opt-in with `--vk-validation`; S4-6); with a frame-aligned pad script every S4 checkpoint has 0 unexplained ranges (S4-7); the effect drawers and glow pass of the menu object, not reached in S4, are left to S5f) | 253 |
| S5a–S5j | scene 2: load, static world, flight, weapons + effect sim, AI + rules, effect drawing, HUD, sound, cutscenes, replay/carrier/menus | 291 / 157 / 65 / 294 / 250 / 208 / 86 / 49 / 102 / 98 |
| S6 | VS mode | 3 + two-player paths of shared code |
| S7 | scene 5 results, post-mission menu | 46 |
| S8 | remaining code (dead, HDD, scene 7) (**translated 2026-10-19**) | 328 |
| S9 | VS LAN play (ADR-008 A) | no game code |


Status: order approved 2026-10-13. Done: S1-S5 and S7 translated (validation of S5 blocks B/C and S7 waits for the PCSX2 batches), S8 translated 2026-10-19. Skipped by the User for now: S6, S9.

Per-slice reports (S1 to S8, the internal-resolution work of 2026-10-19 and the port features of 2026-10-20): `port/slices.md` "Results per slice". Port features as built: `port/port-features.md`.
