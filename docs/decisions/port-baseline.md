# Modern-port baseline features

The port distinguishes three categories (CLAUDE.md, Goal):
1. **Original behaviour.** Game logic, simulation, rules and content reproduce the original exactly.
2. **Modern-port baseline.** Features intrinsic to a PC port, built into the port. They may change **presentation only**, never game logic or simulation results.
3. **Everything else** that departs from the original goes only into the optional modding layer.

This file lists category 2. A feature is added here only after the User approves it.

## Approved

| Feature | Details | Related | Date approved |
|---|---|---|---|
| Output scaled to the host's screen, internal rendering resolution native (4:3 preserved, pillarboxing; 16:9 when the game's own SCREEN RATIO option is on) | two filters, **sharp** (sharp bilinear) and **smooth** (bilinear), chosen in `settings.json`, default **sharp**; the full 448-line draw buffer is presented (ADR-006 §4) | ADR-006 | 2026-10-10, filters 2026-10-11 |
| Frame rate decoupled from the simulation, with interpolation | fixed 59.94 Hz step, interpolated presentation (interpolation built 2026-10-22: `../port/port-features.md` §2, setting `presentation.frame_interpolation`) | ADR-002 | 2026-10-10 |
| Frame cap | 60 / 120 / 144 / 240; **360 is the default upper limit** | ADR-002 | 2026-10-11 |
| V-sync on/off | presentation timing only | ADR-002 | 2026-10-11 |
| Window modes | **windowed** and **fullscreen** (a borderless window the size of the desktop; exclusive fullscreen not included). `settings.json` value `fullscreen` (`borderless`, the name used before 2026-10-20, is still read) | ADR-006 | 2026-10-11, naming 2026-10-20 |
| Settings in `settings.json` | `%LOCALAPPDATA%\ProjectStonehenge\settings.json`; settings struct + JSON load/save + change notification. Only port settings: the game's own options (brightness, screen ratio and position, captions, HUD unit, control type, vibration...) stay in the game's save data. A file that cannot be read, is not valid JSON, has another `version`, an unknown key or an invalid value is deleted and replaced with the defaults (User 2026-10-20) | ADR-005, ADR-006, `../port/port-features.md` | 2026-10-11, rejection rule 2026-10-20 |
| GRAPHICS page | last item of SCREEN SETTINGS, after HUD UNIT: the port's graphics settings (fullscreen, v-sync, frame cap, internal resolution, scaling filter), one row each with name, chosen value and choice squares, built from the game's own menu widgets as the BRIGHTNESS view. Every change applies at once; ACCEPT saves to `settings.json`, CANCEL restores the values the page opened with, the DEFAULT button sets the defaults for the display. With v-sync on the frame cap row is disabled ("V-SYNC", skipped) and the cap is not used. Game state unchanged | `../port/port-features.md` §3 | 2026-10-20 (immediate apply and the disabled cap row: User request the same day) |
| Anti-aliasing (FXAA / SMAA) | `presentation.antialiasing` `off` / `fxaa` (default since 2026-10-22) / `smaa`, GRAPHICS row ANTI-ALIASING, `--antialiasing`: FXAA 3.11 or SMAA 1x on each flight view's 3D, applied before the HUD is drawn so the HUD stays sharp; RGB only, guest RAM unchanged | `../port/port-features.md` §2, `../port/renderer.md` §1.8 | 2026-10-20 (User: "if SMAA and FXAA are compatible with GPL-3 you can proceed … add the HUD after the antialiasing pass") |
| Defaults from the start display | fullscreen; v-sync on; FXAA; the internal resolution closest to the display (448 × N lines closest to its height, 1–8: 1080p 2×, 1440p 3×, 4K 5×); with a refresh rate other than 60 / 120 Hz the frame cap (v-sync off) is the highest at or below it. Validation runs keep 1× and no anti-aliasing | `../port/port-features.md` §1 | 2026-10-20 (User request); v-sync always on, FXAA and the internal resolution 2026-10-22 (User request: "if there is no save data … set by default vSync, FXAA and the internal resolution closest to the monitor resolution") |
| SCREEN RATIO default from the monitor | on a monitor wider than 4:3 the game's own SCREEN RATIO option starts at 16:9 when no save data has been loaded (the port sets the option bit after the game's `InitOptionData`); a loaded save replaces it; setting `presentation.default_screen_ratio` (`monitor` / `game`); validation runs keep the PS2 default. **Note:** this presets a game option (category 2 by User request: the result is the game's own 16:9 mode, as if the player had chosen it) | `../port/port-features.md` §2 | 2026-10-20 (User request: "detect the aspect ratio of the monitor it was started on") |
| Modern control types | CONTROLLER SETUP's CONTROL TYPE offers NORMAL (MODERN), EASY (MODERN), NORMAL (CLASSIC), EASY (CLASSIC); the Classic ones are the game's NORMAL and EASY, the Modern ones the same types with L1 / L2 and R1 / R2 swapped by the port's input layer (live devices only). The save data keeps the Classic type; the Modern flag per player is a port setting (`input.modern_controls`), default on for both players, so the default is NORMAL (MODERN) (the game's own default type is NORMAL) | `../port/port-features.md` §4 | 2026-10-20 |
| QUIT GAME | the last item of the main menu's first column, after OPTIONS, and of the campaign menu's, after LOAD (2026-10-24), drawn by the game's own menu tree like the other items; selecting it fades the menu out like the game's own exits and ends the session as closing the window does. Help line empty on it. Not added in validation runs (their RAM stays comparable with PCSX2). Game state unchanged | `../port/port-features.md` §9 | 2026-10-22 (User request: "add in the game menu a QUIT GAME button after all the existing buttons"); campaign menu 2026-10-24 (User report: "when quitting the mission ... there is no QUIT button") |
| Save texts for a computer | the save, load and format messages reworded just enough for a computer: "memory card (PS2)" becomes "memory card", the warning "...or switch off power" becomes "...or switch off the computer"; changed in guest memory when a page's texts load, the disc unchanged; the unreachable hard disk drive texts unchanged; not applied in validation runs. Game state unchanged | `../port/port-features.md` §10 | 2026-10-22 (User request; wording chosen by the User: drop "(PS2)", "or switch off the computer") |
| Input rebinding | keyboard and host controllers (SDL3) mapped onto the exact PS2 pad bytes; bindings in `controls.ini` since 2026-10-31 (before: `settings.json`); the game's `ConvertPadInput` is unchanged. Since 2026-10-31 (User request) also the button pressures of pads that report them (DualShock 3, PS3-compatible DualShock 2 adapters, any device through `joyaxis:` bindings): the DualShock 2 pressure mode the game uses (`../subsystems/pad-input.md`, `../port/port-features.md` §1) | ADR-005 | 2026-10-11 |
| Controls file | `%LOCALAPPDATA%\ProjectStonehenge\controls.ini`: INI file of the key, mouse and gamepad bindings, keyboard entries named by action (they keep their action under the Modern types), gamepad entries by PS2 button; starts with a guide to every input name; written with explained defaults only when missing; an unreadable entry keeps its default and is logged; a file of another layout version (`Version = 1` line) is deleted and replaced with the defaults; edits apply at once. Keyboard defaults in ACE COMBAT 7's PC layout (User choice). `settings.json` version 2 (bindings removed; a version 1 file is replaced with the defaults, as every incompatible file). `../port/port-features.md` §1 | ADR-005 | 2026-10-31 |
| Mouse as a stick | the mouse steers roll, pitch, yaw or the camera (`controls.ini` [Mouse]: sensitivity, invert, return to centre), only in flight (a `// port:` flag in `ReadPlayerControls`; pointer captured only then, with the window focused and the overlay closed); buttons and wheel are inputs of keyboard actions; once per game tick. Presentation of input only: the game reads ordinary pad bytes. `../port/port-features.md` §1 | ADR-005 | 2026-10-31 |
| Map button without pressure | a player whose devices give □ no pressure gets the original analog-mode (pad type 4) map-button cycle (radar, map 1/4, 1/2, whole map) instead of holding: the original type-4 code runs (`// port:` in `UpdateMappedPadButtons`, `DrawHud`, `DrawRadarMap`), values the original holds; flag recorded in input logs. `../port/port-features.md` §1 | ADR-005 | 2026-10-31 |
| Audio on focus loss | setting `audio.on_focus_loss`: `keep_playing` (default) or `mute`; never pauses the simulation | ADR-005 | 2026-10-11 |
| Higher internal rendering resolution | factor 1–8 (`presentation.internal_scale`, default the closest to the display since 2026-10-22, before 1; `--internal-scale`); frame-sized buffers render at N×, the small shadow/glow targets stay 1×, 2D sprites keep their native texel grid, movies are drawn from the upscaling stage at N > 1; the output pass scales from N × the native size. Guest RAM identical at every factor (`../port/validation.md` IR-1) | `../port/renderer.md` §1, ADR-006 | 2026-10-19 (User request: test up to 8×) |
| Movie upscaling | decoded movie pictures (640×448; 640×336 for SS01–SS12) pass a separate, replaceable upscaling stage before the output pass, scaled by the internal-resolution factor (passthrough at 1× until higher internal resolutions are enabled); AMD FidelityFX FSR 1 EASU, plus RCAS sharpening if it improves the result; each picture is still held for exactly 2 game ticks; game code and guest RAM unchanged | ADR-006 §5 | 2026-10-13 |

Interpolation is feasible with the node matrices and the camera recorded per step (ADR-002 research findings). Effects and particles use their pool slot as identity and must snap when a slot is reused.

## Pending (User's decision)

None.

## Later

| Candidate | Reasoning | Risk |
|---|---|---|
| Audio output device selection and latency setting | the port uses the system default device until then (User 2026-10-11) | none |
| MSAA, anisotropic filtering, area downsample in the output pass | image quality | feasibility study 2026-10-20: `../port/renderer.md` §1.8 (recommended next: the area downsample; MSAA and anisotropic filtering not recommended now). FXAA / SMAA: built (Approved) |

## Modding layer only (category 3)

| Feature | Why not baseline |
|---|---|
| Widescreen (Hor+) | changes the projection and HUD layout, possibly culling and what the player sees. Not checked yet: whether any gameplay rule uses on-screen visibility (e.g. lock-on through the camera projection). Draw-phase randomness (sky, sun jitter) must run identically either way |
| Effects detail / "reduce effects" | effects read the random table inside the simulation step, so changing them changes gameplay randomness; must be labelled gameplay-altering (`../subsystems/effects.md`) |
| VS LAN play (ADR-008, option A, approved 2026-10-13) | not intrinsic to a PC port and adds input delay; lockstep input sharing over UDP on the local network, opt-in per session, original local split screen unchanged as the default; optional crop to the local player's view |
| Debug overlay and its tools | optional debug layer (`../port/port-features.md` §8); opens only when `debug.overlay_enabled` is true (default off; on by default in development builds); nothing changes unless it is opened |
| Frame-rate overlay | debug layer (User request 2026-10-20): FPS and frame time at the top right, `--fps-overlay` / `debug.fps_overlay` / the debug overlay's checkbox, off by default (`../port/port-features.md` §7) |

## Not features (follow from other decisions)

| Item | Note |
|---|---|
| Memory card mapping onto `saves\` | PCSX2 interoperability (ADR-005); port 2 reports empty |
| On-screen messages from the port (ISO check failure, save path) outside game rendering | needed for errors |
