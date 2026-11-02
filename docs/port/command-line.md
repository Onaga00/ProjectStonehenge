# Command line of `ProjectStonehenge.exe`

Every parameter the executable accepts, grouped by purpose. Keep this file in sync with `src/platform/command_line.cpp` (`--help` prints the same list).

- Command-line values override `settings.json` **for the current session only**; they are never written back to the file. The keys of `settings.json`: `port-features.md` §1.
- Unknown parameters or a missing value stop the program with an error and the usage text (exit code 2).
- "Development builds" are every configuration except Release (`AC04_DEV_BUILD=1`, CMake presets `dev` and `debug`). Release builds reject those parameters with one error each and enable nothing; a parameter's value (e.g. the file of `--replay-input <file>`) is skipped, so it is not reported again as an unknown parameter (2026-10-22).
- In release builds (no console window) `--help` and `--version` show their text in a message box.

## Contents

- [All builds](#all-builds)
- [Development builds: disc image and memory card](#development-builds-disc-image-and-memory-card)
- [Development builds: where the game starts (debug layer)](#development-builds-where-the-game-starts-debug-layer)
- [Development builds: input](#development-builds-input)
- [Development builds: run control](#development-builds-run-control)
- [Development builds: screenshots, dumps and logs](#development-builds-screenshots-dumps-and-logs)
- [Development builds: renderer](#development-builds-renderer)
- [Keys in development builds](#keys-in-development-builds)
- [Recipes](#recipes)
- [Disc image at startup](#disc-image-at-startup)

## All builds

Accepted by every build, including release.

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--iso <path>` | path of the disc image (UTF-8) | A (used from B) | Uses this image instead of `paths.iso` in `settings.json` |
| `--user-data <dir>` | folder | A | Uses this folder for `settings.json`, `controls.ini`, `saves\` and `logs\` instead of `%LOCALAPPDATA%\ProjectStonehenge\`. Meant for testing; the folder is created if needed |
| `--internal-scale <n>` | factor 1–8 | 2026-10-19 | Internal rendering resolution for this session, as `presentation.internal_scale` (`renderer.md` §1). Presentation only: guest RAM is identical at every factor (`validation.md` IR-1) |
| `--antialiasing <m>` | `off`, `fxaa` or `smaa` | 2026-10-20 | Anti-aliasing of the flight views for this session, as `presentation.antialiasing` (`port-features.md` §2). Presentation only: guest RAM identical in every mode (`validation.md` §2n) |
| `--debug-overlay` | — | A (overlay in D) | Enables the debug overlay for this session, as if `debug.overlay_enabled` were true |
| `--no-debug-overlay` | — | A (overlay in D) | Disables the debug overlay for this session |
| `--fps-overlay` / `--no-fps-overlay` | — | 2026-10-20 | Shows (hides) the frame-rate overlay for this session, as `debug.fps_overlay`: presented frames per second and the mean frame time over the last half second, small white text with a shadow at the top right of the window. Also a checkbox in the debug overlay's Performance panel |
| `--gpu-profile` | — | 2026-10-30 | Writes the GPU time of each section of the frame to the log at exit (`renderer: GPU time per frame with GS draws by section …`: draws, conversions, view syncs, frame as texture, DATE / shader-blend regions, texture work, anti-aliasing, present copy, output pass …), from GPU timestamps at the end of each section (`renderer.md` §3). Without it the log still has the frame's total GPU time (two timestamps per frame). Costs about 0.1 ms of recording per frame and no measurable frame rate. All builds, so players can send a profile |
| `--verify-iso` | — | B | Recomputes the disc image SHA-1 instead of using the cached result (`<user data>\iso_cache.json`, keyed by path, size and modification time) |
| `--version` | — | A | Shows the version, commit and build type, then exits |
| `--help`, `-h`, `/?` | — | A | Shows the usage text, then exits |

## Development builds: disc image and memory card

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--allow-unverified-iso` | — | B, **development builds only** | Continues when the image fails the size/volume check or the SHA-1 check (and when `SLUS_201.52` differs). Logged as a warning; the window title shows `[UNVERIFIED DISC IMAGE]`. Release builds reject the parameter |
| `--memcard <dir>` | folder | S1, **development builds only** | Uses this PCSX2 folder memory card in slot 1 (read-only in S1) instead of `<user data>\saves\`. For validation runs with the User's cards; copy them under `out/` first |
| `--no-memcard` | — | S1, **development builds only** | No memory card in slot 1 (the "no card" messages of scene 6) |

## Development builds: where the game starts (debug layer)

Port-only starting points and run modes. Guest RAM then differs from a front-end route by design: use them for sweeps, renderer work, determinism and profiling, not for PCSX2 RAM comparisons.

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--mission <n>` | campaign mission 1–18 | S5, **development builds only** | Debug layer (category 3): when the startup scene (6) would request the no-save intro or enter the title, it starts the flight scene instead, the way the campaign path does (`ApplyCampaignSelection` 0x00226A98) with the game's own default selection: aircraft 2 (F-4E), colour 0, special weapon 0, difficulty 2 (NORMAL), mode 1, mission id `GetMissionId(0, n)` (mission 1 is mission 1 itself, not the 0x25 opening), flight-entry kind 1. The loading screen still waits for PRESS START (`--pad-script 800:cross` reaches the mission at vblank 834 with card S). No menu runs, so guest RAM differs from a front-end route: for port-only work (sweeps, renderer, determinism, profiling), not for PCSX2 RAM comparisons. Logged at start |
| `--mission-id <id>` | mission id 0–255 (0x.. accepted) | S5, **development builds only** | As `--mission` with a raw mission id, e.g. 0x23 (no-save intro: aircraft 0x11, entry kind 0), 0x24 (sequence before mission 18), 0x25 (opening before mission 1: aircraft 0x17), or a free-mission id. With `--mission` too, the id wins and the number sets `g_MissionNo` |
| `--mission-mode <m>` | `campaign` (default) or `free` | S5, **development builds only** | `free`: mode 2 and the mission id of free mission (`GetMissionId(2, n)`, as `AcceptMission` 0x0023A550) |
| `--freeze-sim` | — | S5 block A, **development builds only** | Debug layer (category 3): in scene 2 the sub-states 0xA–0xC (`FlightPlayFrame` 0x0010B150) and 0x13 (special sequence 0x0010BA38, the opening of mission id 0x25 = mission 1 with the default aircraft) run only their block A parts: random index (0xA–0xC), terrain/world update, `UpdateViewCameras`, `BuildFlightDrawLists`. The simulation step, the sequence step 0x0010C748 and the player/sound/skip passes are not run, so the world is drawn from the start camera. In sub 0x13 the original skip rule of `UpdateInEngineSequence` still works (a Start press, then 32 frames): the opening ends as at its last frame, so mission id 0x25 goes on to mission 1 proper (`validation.md` S5-A2). Guest RAM then differs from PCSX2 by design in the frozen sub-states; after the skip, what the opening would have left behind (random cursor, static draw state, sequence buffers) differs too |
| `--heap-fill <byte>` | 0–255 or 0x00–0xFF | S5, **development builds only** | Debug layer: `ResetWorkHeapToMark` fills the freed part of the work heap with this byte. Two runs with different bytes show which bytes the game has not written since the last scene change (leftovers, which PCSX2 has from its own history): `heapdiff.py --mask`. Guest RAM differs from a normal run only in those bytes unless the game reads unwritten memory (then the difference shows where) |

## Development builds: input

With any of these the live devices are ignored (a validation run reads only what it was given). Such runs also keep the game's own SCREEN RATIO default (`port-features.md` §2).

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--pad-script <v>:<button>[,...]` | vblank and button name (cross circle triangle square start select up down left right l1 r1 l2 r2 l3 r3) | S1, **development builds only** | Presses the button on pad 1 for 6 vblanks from vblank *v*. Since S5 P1 live devices are ignored when a script is given (pad 1 neutral, pad 2 not connected), so no real stick can leak into a validation run. For unattended validation runs (e.g. `--no-memcard --pad-script 320:cross` reaches the second card message) |
| `--record-input` | — | S5 P1, **development builds only** | Writes the pad state of every game tick to `<user data>\inputs\input.ac4i` (format in `src/platform/input_log.h`). The log is the run's whole external input |
| `--replay-input <file>` | path | S5 P1, **development builds only** | Takes both pads from an input log; live devices are ignored. A replay of a recorded run is identical to it (`validation.md` S5-P1) |
| `--replay-p2m2 <file>` | path | S5 P2, **development builds only** | Takes the pads from a PCSX2 input recording (`.p2m2` v1, `../formats/p2m2.md`; power-on recordings; pad 2 treated as not connected, HYPOTHESIS); live devices ignored |
| `--p2m2-offset <n>` | integer | S5 P2, **development builds only** | Game tick *t* reads recording frame *t* + *n*. Power-on recordings of PCSX2 2.8.2: **153** (`validation.md` S5-P2), valid until the first load whose length differs |

## Development builds: run control

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--fast-until <n>` | vblank number | S5, **development builds only** | Runs the game ticks back to back, without frame pacing, until vblank *n*, then continues at normal speed. The game gets the same ticks and pads as at normal speed (nothing in the game or the IOP stand-ins reads host time), so guest RAM and the `--hash-log` are identical to a normal run and RAM checkpoints stay valid. About every 100 ms one picture is shown (the last step's draws; the steps in between update GS registers and local memory only). The last 60 ticks before each `--screenshot-at` vblank and before *n* run one per frame with all draws, as at normal speed, so screenshots are identical to a normal run's (what a frame shows depends on how ticks are grouped into frames); batches also stop at `--exit-at`. Screenshots land exactly on the vblank asked for (at normal speed a frame that runs two ticks can shift one by a vblank). The sound plays chopped and its backlog is dropped when normal speed starts. Mission 1 route (vblank 11990) in about 25 s instead of 3 min 20 s; `--hash-log` adds about 2 ms per tick |
| `--exit-at <n>` | vblank number | S3, **development builds only** | Ends the session normally after vblank n (as closing the window), so `--dump-audio` and the other exit-time outputs are written. For validation runs |
| `--stress-host` | — | S5 P1, **development builds only** | Random 0–40 ms delays on the main thread each frame, so the catch-up batches vary: a run with it must give the same hash log as a calm run |

## Development builds: screenshots, dumps and logs

Every file lands in the user-data folder (`--user-data`); dumps contain game data and are never copied into the repo.

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--screenshot-at <n>[,...]` | vblank numbers | S1, **development builds only** | Writes a native screenshot (the pre-output-pass image, `port-features.md` §8.4.1) to `<user data>\screenshots\native_<vblank>.png` after each listed vblank. At an internal scale N > 1 the image as rendered goes to `internal_x<N>_<vblank>.png` and `native_<vblank>.png` is its N × N box average at the native size (so the PCSX2 comparisons apply unchanged). **F8** takes one at any time in development builds |
| `--dump-vram` | — | S5, **development builds only** | Every screenshot (`--screenshot-at`, F8) also writes the GS local memory (4 MiB, GS layout) to `<user data>\dumps\vram_<vblank>.bin`, for `tools/research/gsvram.py`. Contains game data |
| `--dump-ram-after-load` | — | B, **development builds only** | After loading `SLUS_201.52`, writes guest RAM (32 MiB) followed by the scratchpad (16 KiB) to `<user data>\dumps\ram_after_load.bin` for `tools/research/ramdiff.py`. The file contains game data: never copy it into the repo |
| `--dump-ram-at-first-scenestep` | — | E, **development builds only** | At the entry of the first `SceneStep` (0x00100B18, the PCSX2 breakpoint of `../pcsx2/procedures.md` §2), writes guest RAM (32 MiB) followed by the scratchpad (16 KiB) to `<user data>\dumps\ram_first_scenestep.bin`, for `tools/research/ramdiff.py --expected` (`validation.md` E-1). Runs once, on the game thread. The file contains game data: never copy it into the repo |
| `--dump-ram-at <addr>[,<hit>]` | hex guest address, optional hit number (default 1) | S1, **development builds only** | Writes guest RAM + scratchpad to `<user data>\dumps\ram_<ADDR>_<hit>.bin` when the game reaches a RAM checkpoint (`validation-strategy.md` §2.1). Checkpoints are the PCSX2 breakpoint addresses of a slice batch, reported by the translated code (`debug::OnGuestCheckpoint`): S1 has 0x00261378, 0x002613D0, 0x00260B54, 0x00260B78; S2a has 0x0022DF00 (title sub 6), 0x0022D748 (Start accepted), 0x0022D84C (menu tree update), 0x0022D880 (confirm in the menu); S4 has 0x00230790, 0x00238380, 0x00238440, 0x00230FC8, 0x00235AB8, 0x0023C708 and 0x00233F10 (`CommitHangarSelection` entry). The file contains game data |
| `--hash-log` | — | S5 P1, **development builds only** | Writes a hash of guest RAM after every vblank (plus one per 64 KiB block) to `<user data>\dumps\hashes.ac4h`; compare two runs with `tools/research/hashdiff.py` |
| `--log-allocs` | — | S5, **development builds only** | Writes one line per `WorkHeapAlloc` call (vblank, guest address, size, alignment, caller as an offset in the executable image) to `<user data>\dumps\allocs.txt`, for `tools/research/heapdiff.py`, which names the caller from the PDB and lists the differing bytes per allocation against a PCSX2 state |
| `--dump-dma-at <n>` | vblank number | S5, **development builds only** | Records the step that starts at vblank *n*: every word the VIF1 receives with its guest address (`<user data>\dumps\vif1_<n>.bin`, pairs of u32) and the GIF data of that step (`gif_<n>.bin`). Summary with `tools/research/dmacap.py <dumps dir> <n> [--vif] [--regs]`. Contains game data: never copy it into the repo |
| `--dump-audio` | — | S1, **development builds only** | Keeps everything the SPU mixer produced and writes it to `<user data>\dumps\audio.wav` (48 kHz stereo) at a normal exit. Contains game audio: never copy it into the repo |

## Development builds: renderer

| Parameter | Value | Since | Effect |
|---|---|---|---|
| `--vk-validation` | — | S4, **development builds only** | Enables the Vulkan validation layer when it is installed (off by default since 2026-10-16, User decision). The layer costs about a microsecond per Vulkan call: the hangar records ~4,000 draws per frame and drops from 60 to ~13 fps with it (`validation.md` S4-6) |
| `--cpu-texture-decode` | — | 2026-10-22, **development builds only** | Decodes GS textures on the CPU (the reference decoder, `GsState::DecodeTexture` / `DecodeOnCpu`) and uploads them, instead of decoding them on the GPU from its copy of GS local memory (`renderer.md` §2). Same picture, slower where textures change often (the no-save intro's terrain) |
| `--frame-interpolation <m>` | `on` or `off` | 2026-10-23, **development builds only** | Forces frame interpolation (`port-features.md` §2) on or off for this session, also in validation runs, which otherwise keep it off. It still runs only above 60 presented frames per second and not while fast-forwarding. For measuring its cost and checking its pictures on a replayed run (`validation.md` §2ad); guest RAM is the same either way. The session log ends with the frames presented at normal speed, how many had a blended copy, and the mean time to build one |
| `--verify-texture-decode` | — | 2026-10-22, **development builds only** | Decodes every GS texture on both the GPU and the CPU, reads the GPU result back and compares: the first 8 differing textures are logged with format, address and the first differing texel, and the session log ends with `texture decode check: N GPU decodes equal to the CPU decode` (or how many differ). Slow; for checking `gs_decode.comp` |
| `--play-gsdump <file>` | path of an uncompressed PCSX2 `.gs` dump | S5, **development builds only** | Draws the dump with the port's renderer instead of running the game: loads the dump's GS memory image, feeds its GIF transfers frame by frame, writes `<user data>\screenshots\gsdump_native.png` and PCSX2's own screenshot from the dump as `gsdump_pcsx2.png`, then exits. Checks the renderer on exactly the input PCSX2 drew (`../platform/gs-subset.md`). No ISO needed. Compressed dumps (`.zst`, `.xz`) must be unpacked first |
| `--movie-upscale <n>` | factor 2–4 | S3, **development builds only** | Runs the movie upscaling stage (ADR-006 §5, FSR 1 EASU) at factor n, with the game still drawn at 1×. A screenshot (`--screenshot-at`, F8) taken during a movie also writes the stage output as `screenshots\movie_x<n>_<vblank>.png`. For checking the stage; it changes nothing on screen |

## Keys in development builds

| Key | Effect |
|---|---|
| **F8** | native screenshot now (as `--screenshot-at`), into `<user data>\screenshots\` |
| `\` (`debug.overlay_toggle`) | opens or closes the debug overlay panels (`debug.overlay_enabled`, on by default in development builds; `port-features.md` §8) |

## Recipes

The disc image comes from `paths.iso` or `--iso`; the card folders under `out/cards/` are copies of the User's PCSX2 cards (`out/` is never committed).

```
REM Mission 1 gameplay from the loading screen (press Start at vblank 800), frame rate shown
ProjectStonehenge.exe --mission 1 --pad-script 800:cross --fps-overlay

REM The mission 1 opening (mission id 0x25) at 4x, screenshots, fast-forward to the first one
ProjectStonehenge.exe --mission-id 0x25 --mission 1 --pad-script 800:cross --internal-scale 4 --screenshot-at 1500,1700 --fast-until 1500

REM Replay a PCSX2 input recording over mission 4 with a RAM hash log, then exit
ProjectStonehenge.exe --mission 4 --replay-p2m2 out/runs/b3_combined.p2m2 --hash-log --fast-until 6000 --exit-at 6010

REM RAM checkpoint for a PCSX2 comparison (front-end route with card S)
ProjectStonehenge.exe --memcard out/cards/S --pad-script 1200:start,1450:cross --dump-ram-at 0x0022D748 --fast-until 2000 --exit-at 2000

REM Draw a PCSX2 GS dump with the port renderer (no ISO needed)
ProjectStonehenge.exe --play-gsdump D:\path\frame.gs
```

Many runs in parallel (regression suite, own user-data folder per job): `python -I tools/research/runmany.py tools/research/full.json --baseline out/runs/<baseline>` (`../../tools/research/README.md`).

## Disc image at startup (milestone B)

The image path comes from `--iso` or `paths.iso`. Startup checks, in order: the file opens; it is an ISO 9660 image; size 3,554,705,408 bytes and volume ids `PLAYSTATION` / `ACECOMBAT04`; SHA-1 of the whole image (about 9 s the first time, then cached); SHA-1 of `SLUS_201.52`. Any failure is logged and shows a message box (2026-10-20 layout: a headline, what happened, numbered steps under "What to do:", then the disc image, the supported disc and the settings file on their own lines) with two buttons: **Select ISO...** opens the system file picker; the chosen image is saved as `paths.iso` in `settings.json` and checked again; **Quit** ends the port (development builds: see `--allow-unverified-iso`). The other startup errors (data folder, SDL, window, renderer, game program, lost graphics device) use the same layout with the reason and the session log path. Closing the window during the SHA-1 check cancels it.
