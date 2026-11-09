# Port features (as built)

What the port adds on top of the original game, how it is built and where it is configured. The list of approved features and their approval dates is `../decisions/port-baseline.md`; command-line parameters are in `command-line.md`. Nothing here changes game logic: guest RAM and the save data hold what the PS2 holds (checked by the regression suite, `validation.md` §2l and §2m). The one deliberate exception is the SCREEN RATIO default on a wide monitor (§2, User request 2026-10-20): it presets a game option before any save data is read, as if the player had chosen 16:9.

The debug overlay (formerly `debug-overlay.md`, "Proposal: debug overlay") is section 8; its sections keep their numbers with the prefix 8. (`debug-overlay.md` §4.1 is §8.4.1 here).

## 1. Settings

### Where settings live

| Data | Place | User |
|---|---|---|
| Port settings (presentation, Modern control types, audio, paths, debug) | `%LOCALAPPDATA%\ProjectStonehenge\settings.json` (or `--user-data`) | the port (`src/platform/settings.*`) |
| Key, mouse and gamepad bindings (2026-10-31; before: `settings.json` `input.bindings`) | `%LOCALAPPDATA%\ProjectStonehenge\controls.ini` (below, "Controls file") | the player; the port writes it only when it is missing (`src/platform/controls_file.*`) |
| The game's own options: brightness, screen ratio, screen position, captions, HUD unit, interlude, both players' control type, reverse, vibration and pilot view | the game's save data on the memory card (`saves\`, the campaign block 0x002A0318, 0x12C0 bytes, which includes the option bytes 0x002A15CC and the controller words 0x002A15D0), written by the game's SAVE page | the game |

A new memory card at first start: when the saves folder holds no card, the port creates a formatted 8 MB PCSX2 folder card there at once (`_pcsx2_superblock`, `../formats/folder-memcard.md` §9), so PCSX2 sees a formatted card even before the first save; the game itself creates its save file on the first SAVE ("No AC04 files found on memory card. Create save file?"). Checked 2026-10-20 (`validation.md` §2o): New Game, campaign menu, SAVE, File 1, then LOAD and CONTINUE in a new session; the files and the `_pcsx2_index` are those of a PCSX2-made save.

Checked end to end 2026-10-20 (`validation.md` §2m, ST-3): CONTROLLER SETUP changed to NORMAL (CLASSIC) with REVERSE on and saved through the campaign menu's SAVE page: `settings.json` got `modern_controls: [false, true]`, the card file changed in exactly two places (player 1's controller word 0x0008 → 0x000A and the checksum), and a new session that loads the card shows NORMAL (CLASSIC), REVERSE ON.

### `settings.json` rules (User decision 2026-10-20)

- Missing keys take their default; a missing file is written with the defaults at start. The defaults depend on the display the port starts on (below), so a missing key takes the default **for this display**.
- A file that cannot be read, is not valid JSON, has another `version` (current: 2), an unknown key or an invalid value is **rejected as a whole**: the port logs what was wrong, deletes the file and writes the defaults in its place. This also applies when the file is changed on disk while the port runs (it is re-read once per second; every change applies at once).
- The file holds only known keys. It is written when a menu page saves (GRAPHICS: ACCEPT; CONTROLLER SETUP: ACCEPT) and at exit for the windowed size; always the file values, so session-only command-line overrides are never saved.
- Adding a key: give it a default in `src/platform/settings.h`, read and write it in `settings.cpp` (with `Reader`), add it to the change notification (`SettingsStore::Replace`), test it in `tests/test_settings.cpp`, and list it below. A key added this way keeps older files valid (missing = default). Removing or renaming a key, or changing the meaning of a value, needs a `version` bump, which rejects older files (the User's rule above).
- Version 2 (2026-10-31): `input.bindings` moved to `controls.ini`. A version 1 file is incompatible like any other: deleted and replaced with the defaults (User rule 2026-10-31: no migration of incompatible files; test `settings: a file of version 1 is incompatible`).

### Defaults on the start display (User request 2026-10-20)

The port reads the desktop mode of the primary display (where the window opens) at start (`DisplayInfo`, logged as `display: WxH, N Hz`). `DefaultSettings(display)`:
- **Fullscreen** (borderless, the size of the desktop).
- **V-sync on** (since 2026-10-22, User request; before: only at 60 or 120 Hz). With a refresh rate other than 60 or 120 Hz the frame cap, which applies when v-sync is turned off, is the highest one at or below it (144 Hz → 144, 165 Hz → 144, 240 Hz → 240, 75 Hz → 60
- **FXAA** and the **internal resolution closest to the display** (since 2026-10-22, User request): N with 448 × N lines closest to the display height, 1–8 (720p and 1080p 2×, 1440p 3×, 4K 5×, unknown 1×; `DefaultInternalScale`). Validation runs (development builds: pad script, input log, PCSX2 recording, `--memcard`, `--no-memcard`) keep 1× and no anti-aliasing so their screenshots compare with the baselines; the startup log names the default scale.
- These are the defaults of a first start (no `settings.json` yet), of a rejected file replaced with the defaults and of a file's missing keys; an existing `settings.json` keeps its values. The saves do not influence them).
- Everything else as in the key table.

The GRAPHICS page's DEFAULT button sets these values too.

### `settings.json` keys

| Key | Values | Default |
|---|---|---|
| `version` | `2` (2026-10-31; before: 1) | `2` |
| `presentation.filter` | `sharp`, `smooth` | `sharp` |
| `presentation.vsync` | `true`, `false` | `true` |
| `presentation.frame_cap` | `60`, `120`, `144`, `240`, `360`; used only with v-sync off (with v-sync on the display paces the frames) | `360`, or the refresh-rate cap (above) |
| `presentation.window_mode` | `windowed`, `fullscreen` (borderless desktop-size window; `borderless`, the earlier name, is read as `fullscreen`) | `fullscreen` |
| `presentation.window_width` / `window_height` | windowed client size (saved at exit) | `1280` / `960` |
| `presentation.internal_scale` | `1`–`8` (internal rendering resolution; changes apply while running) | closest to the display (above); `1` in validation runs and on an integrated GPU (User decision 2026-11-03: applied when the file is written with the defaults, first start or a rejected file; `validation.md` §2ar) |
| `presentation.frame_interpolation` | `true`, `false`: above 60 presented frames per second the 3D is drawn between the last two game steps (§2) | `true` |
| `presentation.antialiasing` | `off`, `fxaa`, `smaa`: anti-aliasing of the flight views before their HUD (§2); `--antialiasing` for a session | `fxaa`; `off` in validation runs and on an integrated GPU (as `internal_scale`) |
| `presentation.default_screen_ratio` | `monitor` (the game's SCREEN RATIO starts at 16:9 on a monitor wider than 4:3, §2), `game` (the PS2 default, 4:3) | `monitor` |
| `paths.iso` | disc image path | empty |
| `audio.on_focus_loss` | `keep_playing`, `mute` | `keep_playing` |
| `input.modern_controls` | `[player 1, player 2]`, `true` / `false`: the Modern variant of the game's CONTROL TYPE (L1 / L2 and R1 / R2 swapped; set on CONTROLLER SETUP, §4) | `[true, true]` |
| `debug.overlay_enabled` | `true`, `false` | `true` in development builds, `false` in release |
| `debug.overlay_toggle` | a typed character, or `key:<SDL key name>` | `\` |
| `debug.fps_overlay` | `true`, `false`: the frame-rate overlay (§7) | `false` |

### Controls file (`controls.ini`, 2026-10-31, User request)

The bindings have their own file, meant to be edited with Notepad by players who don't know JSON.

**Format.** INI was chosen over the alternatives:
- YAML: indentation rules, and `no` or `Y` are read as true/false;
- TOML: values need quotes, and it would be a new dependency;
- JSON: quotes and commas everywhere.

Lines are `Name = input, input`, grouped in sections, with notes after `;` or `#`. Our own reader parses it (`src/platform/controls_file.cpp`), so there is no new dependency.

**Sections.**
- `[Keyboard]` names **actions** (`Throttle up = W`). Each action resolves to its PS2 control in the game's Classic NORMAL layout (CONTROLLER SETUP's table 0x003AF5B8 / 0x003AF5F0, `../subsystems/pad-input.md`).
  - Keys and mouse keep their action under every control type. With a Modern type the backend places them on the swapped control, so the Modern swap lands them back on their action (`InputBackend::Update`).
  - With EASY the yaw keys do nothing, like the pad's yaw buttons.
- `[Mouse]`: what the mouse steers (see "Mouse" below).
- `[Gamepad]` names the **PS2 buttons** (`Cross = South`). The pad is a DualShock 2 by position; the game's CONTROL TYPE and the Modern swap decide what each button does, as before.

**Names.** Matching ignores case, spaces and `-` (`D-Pad Up` = `dpad up`). An empty value unbinds the action. Accepted inputs:
- **keys:** SDL key names (`W`, `Left Ctrl`, `Keypad 8`, `Return` or `Enter`, `Escape` or `Esc`); `Comma`, `Period`, `Semicolon`, `Minus` for those keys;
- **mouse:** `Mouse Left/Right/Middle/Back/Forward`, `Mouse Wheel Up/Down`. Each wheel notch is one press of 2 ticks, then 1 tick released before the next;
- **gamepad positions:** `South East West North Back Start Guide`, `Left/Right Shoulder`, `Left/Right Trigger`, `Left/Right Stick Button`, `D-Pad Up/Down/Left/Right`, `Left/Right Stick Up/Down/Left/Right`;
- **raw gamepad inputs:** `Joy Axis 7` (whole axis, resting at its minimum), `Joy Axis 7+` / `-` (half axis), `Joy Button 4`;
- **the earlier technical syntax:** `key:`, `pad:`, `axis:`, `joyaxis:`, `joybutton:`, `mouse:`, `wheel:`.

**Shared controls.** Several actions can share one PS2 control:
- `Machine gun` and `Accept` are ×;
- `Change target` and `Back` are △;
- `Level aircraft` and `Menu up` are d-pad up; `Change view` and `Menu down` are d-pad down.

**File handling.**
- The file starts with a short guide: how to change a control and every input name (keys, mouse, gamepad, adapters, what the mouse can steer), before the first section (User request 2026-10-31).
- The port writes the file, with its defaults and explanations, only when it is missing (log: `controls.ini written with the default controls`). It never rewrites a compatible file, so a player's notes and layout stay.
- **Version.** The line `Version = 1` before the first section is the file's layout version (`kControlsVersion`). A file with another version or without the line is incompatible: deleted and replaced with the defaults, like an incompatible `settings.json` (User rule 2026-10-31; log: `controls.ini is not of version 1: deleted and replaced with the default controls`). The version goes up when an action or entry is removed or renamed or a value changes meaning.
- **In a compatible file, an entry the port can't read keeps its default and is logged** (`controls.ini line N: …`). The rest of the file still counts (User approval 2026-10-31).
- Edits apply while the port runs; the file is checked once a second.
- Delete the file to get the defaults back.

**Ports.** Port 1 has the keyboard, the mouse and the first gamepad; port 2 has the second gamepad.

**Tests:** `tests/test_controls_file.cpp`:
- the default file reads with no warning;
- the AC7-style defaults;
- the gamepad defaults equal those of earlier builds;
- player spellings;
- a player's file with typos, an unknown action and an unknown section;
- files of another version or without one are incompatible.

**Keyboard defaults: ACE COMBAT 7 style** (User choice 2026-10-31). They follow AC7's PC defaults as community lists give them; the lists are not official ([StrategyWiki](https://strategywiki.org/wiki/Ace_Combat_7:_Skies_Unknown/Controls), [DefKey](https://defkey.com/ace-combat-7-skies-unknown-pc-shortcuts), [Migman](http://www.migman.com/sw/ace-combat-7/_key-commands.php)). Every AC7 flight action has an AC04 counterpart:

| Action (AC04) | PS2 control (Classic NORMAL) | Default | Source |
|---|---|---|---|
| Throttle up / down | R1 / L1 | W / S | AC7 |
| Roll left / right | left stick X | A / D | AC7 |
| Pitch up / down | left stick Y (down / up) | 3 / 1 | AC7 |
| Yaw left / right | L2 / R2 | Q / E | AC7 |
| Machine gun (VULCAN) | × | Mouse Left, Left Ctrl | AC7 (mouse); Left Ctrl ours |
| Missile | ○ | Space, Mouse Right | AC7 |
| Change weapon | SELECT | Mouse Wheel Up/Down, 2 | AC7 (wheel); 2 ours |
| Change target | △ | R, Mouse Middle | ours (to check against AC7) |
| Map | □ | M | ours (to check) |
| Change view | d-pad down | V | ours (to check) |
| Rear view | R3 | C | ours (to check) |
| Level aircraft | d-pad up | X | ours (to check) |
| Look up/down/left/right | right stick | Keypad 8/2/4/6 | AC7 (numpad camera) |
| Pause | START | Escape | ours |
| Menu up/down/left/right, Accept, Back | d-pad, ×, △ | arrows, Return / Keypad Enter, Backspace | ours |

Players' guide to these defaults and to editing `controls.ini`: `docs/release/keybinds.md`, shipped in the release archive as `keybinds.md` (User request 2026-11-10). Update it together with the default file (`kDefaultText` in `src/platform/controls_file.cpp`).

The keys marked "ours" stay until the User checks them in AC7's key settings. The arrow keys are the d-pad, not flight controls: they move through menus, and in flight up levels the aircraft and down changes the view.

### Mouse (2026-10-31, User request)

The mouse acts as a stick for port 1. Its settings are in the `[Mouse]` section of `controls.ini`:

| Entry | Values | Default |
|---|---|---|
| `Left/right`, `Up/down` | what the axis steers: `Roll` (left stick X), `Pitch` (left stick Y), `Yaw` (L2 / R2, as pressures), `Camera` (right stick X or Y), `None` | `Roll`, `Pitch` |
| `Sensitivity` | 0.1–10 | 1.0 |
| `Invert up/down` | `yes` / `no`; no: mouse forward = stick forward (nose down) | `no` |
| `Return to centre` | `yes`: the deflection follows the mouse's speed; `no`: the stick stays where the mouse moved it (400 pixels from centre to full) | `yes` |
| `Hold to swap stick` (2026-11-10, User request) | input list, as a `[Keyboard]` action; empty = never. While one is held each mouse axis steers the other stick: `Roll`, `Pitch`, `Yaw` become `Camera` on the same axis, `Camera` becomes `Roll` (left/right) or `Pitch` (up/down) (`SwappedTarget`). On press and release the mouse stick restarts from the centre, so the stick it leaves centres at once. A file without the line gets the default | `Left Alt, Right Alt` |

With return to centre, each tick keeps 80 % of the deflection and adds the new motion. At sensitivity 1, 12 pixels per tick (720 per second) gives full deflection, and the stick centres about 0.2 s after the mouse stops.

**When it steers.** The mouse steers **only in flight**.
- The game marks the ticks that read the player's flight controls: a `// port:` line in `ReadPlayerControls` sets `port::Input().in_flight`.
- The host captures the pointer (SDL relative mouse mode) only in those ticks, and only with the window focused, the debug overlay closed and a live run (no pad script, input log or recording).
- Otherwise the pointer is free and the stick centred: in menus, while paused (the simulation step stops, so the flag drops), in other windows and with the overlay open.

**Other details.**
- The stick advances once per game tick (`InputBackend::StepTick`), not per presented frame, so the feel does not depend on the frame rate.
- The game makes the stick round as it does a DualShock 2's (`ConvertPadInput`), so diagonals behave as on the pad.
- The mouse drives PS2 **stick bytes**, not game actions: `Roll`/`Pitch` are the left stick, `Camera` the right stick. What a stick does in the game follows its CONTROL TYPE, as for a gamepad.
- Mouse buttons and the wheel are ordinary inputs of `[Keyboard]` actions. They are ignored while the overlay has the keyboard. The side buttons (`Mouse Back` = X1, `Mouse Forward` = X2) can be bound like any other input and have no action by default (User request 2026-11-10; the file's guide says so).
- Input logs record the resulting pad bytes, so replays stay exact.
- **Tests:** `tests/test_input.cpp` (stick with and without return to centre, invert and sensitivity, wheel presses, swapped targets); `tests/test_controls_file.cpp` (swap default Alt, side buttons unbound by default, side buttons and the swap entry in a player's file).

### Map button without pressure (2026-10-31, User request)

On the PS2 the map button (□) works in two ways (`../subsystems/pad-input.md`):
- **pressure mode** (DualShock 2, pad type 5): the map shows while □ is held, zoomed by how hard it is pressed;
- **analog mode** (type 4): each press steps HudState +0x72: radar → map 1/4 → map 1/2 → whole map → radar. In versus: speed readout → radar → map.

The port always reports pressure mode. Until now, a device that gives □ no pressure (keyboard, Xbox and similar pads) could therefore only hold the map at full zoom.

**What changed.** When the player's devices give □ no pressure (no DualShock 3 or pressure adapter on □; `InputBackend::Update` → `PadState::map_cycle`), the game takes the original type-4 path. This is the original 4-step cycle the User chose. Two `// port:` changes do it:
- `UpdateMappedPadButtons`: □ also sets mapped bit 9 (0x200). That is the bit the type-4 map 0x0029FF40 gives □ (entry 7 = 9). The type-5 map 0x0029FF60 gives □ only its value (0x8D), and none of its entries sets bit 9.
- `DrawHud` and `DrawRadarMap`: their `mode == 4` tests also accept type 5 when the cycle is on (`MapCycle`).

**Effect.**
- The original code runs unchanged, and HudState +0x72 only holds values the original holds.
- Throttle and yaw stay analog.
- With a DualShock 3, □ keeps the original hold-and-press behaviour.
- The overlay's Input panel shows which mode is active.

**How the flag travels.**
- It reaches the game through `port_hooks.h` (`InputHooks::digital_map_cycle`), set from the latched pads before each tick.
- Input logs record it in bit 1 of each pad's `connected` byte. Logs of earlier builds replay with the cycle off.
- Pad scripts and PCSX2 recordings never set it.

### Pressure-sensitive buttons (2026-10-31, User request)

The game puts a DualShock 2 into pressure mode and reads how hard L1/R1 (throttle), L2/R2 (yaw) and □ (map zoom) are pressed (`../subsystems/pad-input.md`). The port always reports a DualShock 2 in pressure mode; each control's pressure comes from its bindings (`src/platform/input.cpp`):

- a key or a plain button: 0 or 255; a trigger or stick axis (`axis:`): its travel 0–255 (pressed above half travel);
- a gamepad button of a gamepad whose SDL driver reports its pressure: that pressure, with the device's own digital bit (a light press counts as pressed, as on a DualShock 2). Found automatically when the gamepad is opened (log: `port N: gamepad '…', pressure-sensitive buttons (12)`): SDL's HIDAPI PS3 drivers report △ ○ × □, L1, R1 and the d-pad as joystick axes 6–15, L2/R2 as the trigger axes (`SDL_hidapi_ps3.c` of the pinned SDL 3.4.16);
- `Joy Axis` bindings (`joyaxis:`): any other device that reports a pressure as an axis;
- with several sources the highest level counts, any press sets the bit.

| Controller | Pressure on the PC |
|---|---|
| DualShock 3 / Sixaxis | all 12 buttons, automatically, with Sony's `sixaxis.sys` or DsHidMini in its **SXS** mode (the port sets SDL's hint `SDL_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER`, off by default in SDL on Windows; the environment variable of that name still overrides it). DsHidMini's other modes: XInput/DS4Windows give triggers only; SDF/GPJ are plain HID devices, usable with `joyaxis:` bindings if SDL has a gamepad mapping for them |
| DualShock 2 | through a USB adapter only. Adapters that present themselves as a PS3 controller (they answer SDL's third-party PS3 check) give all 12 pressures automatically; others only if they report pressures as axes (then `joyaxis:` bindings), most send digital buttons |
| Xbox, DualShock 4, DualSense, Switch Pro and similar | the two triggers only (as before): with the Modern control type the throttle is on the triggers and analog |
| Keyboard | 0 or 255 |

Devices without an SDL gamepad mapping are not opened (as before); a mapping can be added with SDL's `SDL_GAMECONTROLLERCONFIG` environment variable. The debug overlay's Input panel (§8.7) shows each port's device, its pressures and the pressures the game read. HIDAPI is part of the SDL3 archive, not a separate dependency (`../decisions/dependencies.md`). Tests: `tests/test_input.cpp`.

Vibration (2026-10-22, User request): the values the game sends with `scePadSetActDirect` (mapped by its `scePadSetActAlign` table: the DualShock 2's small motor on/off, the large motor 0–255) go to the gamepad of that port as SDL rumble (large motor = low-frequency rumble, small = high-frequency at full strength), sent when they change and refreshed every 500 ms while running. The game's VIBRATION option (CONTROLLER SETUP, save data) switches it, as on the PS2; no `settings.json` key. Runs fed from `--pad-script` or a recording keep the pads still. The guest rumble state is untouched (`risks.md`, rumble row). Code: `src/sce/libpad.cpp`, `InputBackend::SetVibration`.

## 2. Presentation

| Feature | Built as | Setting |
|---|---|---|
| Output scaling | the native (or N×) image is scaled to the window, 4:3 pillarboxed or 16:9 while the game's SCREEN RATIO option is on (main context flag 0x10000); filters sharp / smooth | `presentation.filter` |
| Window | windowed (size remembered) or fullscreen (a borderless window the size of the desktop) | `presentation.window_mode` |
| V-sync, frame cap | presentation timing only; the simulation steps at 59.94 Hz (ADR-002). V-sync on = FIFO present mode and no frame cap; off = MAILBOX (or IMMEDIATE) and the cap | `presentation.vsync`, `presentation.frame_cap` |
| Internal resolution 1–8× | frame-sized targets at N×, small shadow/glow targets at 1×, 2D sprites on their native texel grid, sprite edges on native pixel borders (`renderer.md` §1.7) | `presentation.internal_scale`, `--internal-scale` |
| Movie upscaling | FSR 1 EASU at the internal-resolution factor (ADR-006 §5) | follows the internal resolution; `--movie-upscale` |
| Anti-aliasing (2026-10-20) | FXAA 3.11 (one pass, quality preset 29, luma from green) or SMAA 1x (three passes, preset HIGH, luma edges) over each flight view's rectangle of the frame buffer, **after the view's 3D and before its HUD**, so HUD lines, text, radio window, menus and the screen fade stay sharp (User request). How the point is found: `BuildFlightDrawLists` records the first DMA tag of each view's 2D overlay table (`port::Render()`, host state only); the DMA walker marks the GIF data where it reaches that tag (`SourceMark::kAntiAlias`); `GsState` puts an `aa_point` entry into the draw list there; the backend copies the target, runs the passes with the view's rectangle as scissor and copies the rectangle back (`src/platform/render/gs/gs_antialias.cpp`). RGB only: the frame buffer's alpha (GS alpha, read by later draws) is kept. Works at every internal scale (it runs on the scaled target). Only scene 2 (flight, in-engine cutscenes) has the point; menus with 3D (hangar, aircraft viewer) are not anti-aliased. **The SMV movies (INTRO, SS01–SS12, STAFF, the MIS briefing movies) are never anti-aliased** (User rule 2026-10-20): they get only the FSR 1 upscaling stage; checked with SS01 at 1× and 2×, SMAA on and off give identical frames (`validation.md` §2n AA-4). Third-party code: `../decisions/dependencies.md` | `presentation.antialiasing`, `--antialiasing` |
| Frame interpolation (2026-10-22) | above 60 presented frames per second (a 120/144 Hz display with v-sync, or v-sync off with a higher cap), each frame shows the 3D world between the last two game steps: the VU1 program runs of the newest step are repeated with their matrices blended (ADR-002; `validation.md` §2ac). The HUD, 2D, effects the EE projects itself and movies show the newest step. A frame that has to catch up more than one step is shown unblended. Off in validation runs (unless `--frame-interpolation on`) and while fast-forwarding; at 60 Hz nothing changes. Cost (2026-10-23, `validation.md` §2ad): mission 1 at 8× with SMAA on a 4K display, v-sync off, cap 360: 99 fps, 0 ticks dropped; the hangar 80 fps. Since 2026-10-27 the later blended copies of a step reuse its first copy's draws with their new vertex values, without GS processing or texture work again (`validation.md` §2am) | `presentation.frame_interpolation` (GRAPHICS page INTERPOLATION row since 2026-10-30), `--frame-interpolation` |
| SCREEN RATIO default | on a monitor wider than 4:3 (aspect above 4:3 by more than 2.5 %: 16:9, 16:10, 21:9) the game's SCREEN RATIO option starts at 16:9: the port sets bit 0 of the option byte 0x002A15CC right after `InitOptionData` at boot (`src/game/boot.cpp`, `port::Boot()`), before `ApplyOptionSettings`. Save data loaded later replaces the byte, so a player's saved choice wins; with no save the game shows its own 16:9 mode (anamorphic, all of the game's 16:9 layouts). Validation runs keep the PS2 default (4:3): any pad script, input log, PCSX2 recording, `--memcard` or `--no-memcard` (logged as `SCREEN RATIO default: 4:3 (validation run: PS2 default)`) | `presentation.default_screen_ratio` |

All of them except the SCREEN RATIO default (applied once at boot) can be changed while the port runs (GRAPHICS page, the debug overlay's Graphics panel (§8.7, development builds) or `settings.json`), and every change applies at once. Checked 2026-10-20 (`validation.md` §2m, ST-1 and ST-2): eight combinations of window mode, v-sync, cap, scale and filter switched during mission 4 gameplay at normal speed. A change of the internal scale rebuilds the render targets: at 6× and 8× this costs one hitch of 0.3–0.6 s (the simulation drops those ticks, as after any host stall); the other settings switch within a frame.

## 3. GRAPHICS page

The last item of OPTIONS → SCREEN SETTINGS, after HUD UNIT (and after INTERLUDE once that is unlocked). Code: `src/game/graphics_page.*`, hooks marked `port:` in `src/game/options_page.cpp`.

- Opens like SCREEN POSITION and BRIGHTNESS (state 6 → view 3 of the options page) and uses the BRIGHTNESS view's widgets: its outer frames, a typed title "GRAPHICS >", the choice-square glyph, the pulsing box on the chosen square, the crosshair and the help box.
- Rows (name, chosen value, squares): FULLSCREEN ON / OFF, V-SYNC ON / OFF, FRAME RATE CAP 60–360 FPS, INTERNAL RES. 1X–8X, SCALING FILTER SHARP / SMOOTH, ANTI-ALIASING OFF / FXAA / SMAA (2026-10-20; the panel is one row taller). INTERPOLATION ON / OFF (`presentation.frame_interpolation`) sits between V-SYNC and FRAME RATE CAP (User request 2026-10-30, after a player's report that interpolation on a 75 Hz display costs more than it gives on a weak GPU); the rows' frame is one row taller again, 488 × 222 centred at (276, 186).
- With V-SYNC ON the frame rate cap does not apply: its row shows "V-SYNC" instead of a value, has no squares and the cursor skips it (User request 2026-10-20).
- Up/down choose the row, left/right the value (clamped, the < > arrows dim at the ends). **Every change applies at once** (User request 2026-10-20): the page hands each new value to the host, which applies it without saving. ACCEPT (× or ○) saves the values to `settings.json`; CANCEL (△) restores the values the page opened with; □ sets the defaults for this display (§1) and applies them. The help box shows the options page's own messages 1 and 3 ("Use Left Analog Stick to adjust", "×: ACCEPT / △: CANCEL / □: DEFAULT").
- The game's menu tables are not changed: the column-0 definition gets the GRAPHICS item only while the tree is built or its cursors are stored (`GraphicsItemPatch`), and the item ids the page reports are mapped back to the game's (`OptionsSelection`).
- The page reads and writes the settings through `src/common/port_hooks.h` (copied before each tick, applied by the host after it): game code never reads settings.

## 4. Modern control types

CONTROLLER SETUP's CONTROL TYPE row has four squares: NORMAL (MODERN), EASY (MODERN), NORMAL (CLASSIC), EASY (CLASSIC). The two Classic squares are the original ones, at their original places; the Modern ones are to their left.

- Classic = the game's NORMAL and EASY. Modern = the same type with **L1 ↔ L2 and R1 ↔ R2** (buttons and pressures) swapped by the port before the game reads the pad (`platform::SwapShoulderButtons`, applied in `TickInputFeed::Latch` to live devices only: pad scripts, input logs and PCSX2 recordings already hold what the game reads, and input logs record the swapped state).
- The save data keeps the Classic type: moving over the four squares changes the game's type bit exactly as the original two squares do, so guest RAM and the memory card hold only values the original can hold. The Modern flag is a port setting per player (`input.modern_controls`), handed to the host on ACCEPT and re-applied at every start.
- Applied on ACCEPT, not at once: the page is the game's own, which keeps every change in a copy until ACCEPT (original behaviour), and the Modern flag belongs to the same CONTROL TYPE choice.
- Default: Modern on for both players. The game's own default type is NORMAL (controller word 8 from `func_00105728`), so the port's default is NORMAL (MODERN) for players 1 and 2 without touching game data.
- With Modern on, the page's button table shows the swapped rows (L1 YAW LEFT, L2 THROTTLE DOWN, R1 YAW RIGHT, R2 THROTTLE UP; with EASY the yaw rows show "------", which are then L1 / R1 instead of L2 / R2).
- Code: `src/game/controller_page.cpp` (lines marked "port:"), `src/platform/input_log.*`, `src/main.cpp`.

## 5. Port-owned guest memory

The game draws menu texts and items from guest addresses, so the strings and tables of §3 and §4 live in a port-owned area above the initial guest stack pointer, **0x01FFF800–0x01FFFFFF** (`src/game/port_menu.h`, 32 slots of 64 bytes). The game never reads or writes it (the stack starts at 0x01FFED60 and grows down); it is written only while CONTROLLER SETUP, the options page or the main menu (§9, slots 20-21) is open, or a page's texts are loaded (§10, slots 22-31). RAM checkpoints taken on those pages show it as a port-only range.

## 6. Renderer caches (performance, no visible change)

| Cache | Key | Bound |
|---|---|---|
| Decoded textures (`GsState::DecodeTexture`, 2026-10-22) | texture registers, palette with TEXA and the mip registers, texel range; one entry per key, valid while every 256-byte GS block it read keeps its generation (no content hash any more, `renderer.md` §2) | 4096 entries and 256 MiB of texture images (`GsState::kTextureCacheBytes`); emptied when either is reached. A texture whose memory changed is decoded again and replaces its entry; the CPU decoder (`--cpu-texture-decode`) redoes only the changed blocks when at most half changed |
| Texture images (`GsBackend::TexImage`, 2026-10-22, replaces the atlas) | `DecodedTexture::serial` (one per decode) | one RGBA8 image per decoded texture (size rounded up to 32), kept while the decoded-texture cache or a draw holds the texture; then it waits in a free pool (128 MiB) and is reused for a texture of the same size once the frames in flight are done. Decoded on the GPU from the GPU copy of GS local memory (`gs_decode.comp`); with `--cpu-texture-decode` uploaded whole |
| GPU copy of GS local memory (`GsBackend::gs_memory_`, 2026-10-22) | — | 4 MiB, device local. Only the 256-byte blocks written since the last update are copied, just before the first decode that reads one of them (`TextureOp`) |
| Per-frame lists of the Vulkan backend (texture work, uploads, vertices) | reused from frame to frame | grow to the largest frame |
| GS draw pipelines (`GsBackend::pipelines_`; warm-up 2026-10-25) | `PipelineKey`: blend enable, Vulkan blend factors and op, colour write mask, depth compare op, depth write | the session. The 34 keys the game is known to use (`gs_pipeline_keys.h`, collected with `tools/research/pipeline_keys.py`) are built at startup by up to 4 warm-up threads (half the hardware threads), so a new driver compiles them before the first 3D frame and not inside frames; a key missing from the table is still built when first drawn, and the exit log lists it (`GS pipelines: N built during frames (…): keys`) to be added to the table (`validation.md` §2ai) |
| Pipeline cache file (`cache\pipelines.bin`, 2026-10-25, User approval) | the driver's own (`vkGetPipelineCacheData`) | every pipeline of the renderer (GS, anti-aliasing, depth copy, conversion, texture decode, movie upscaling, output pass) is created through one `VkPipelineCache`, loaded at start and saved at exit when its size changed (temporary file, then rename). Used only when its header names the same GPU and driver (vendor, device, `pipelineCacheUUID`); otherwise, or when damaged or over 64 MiB, the file is deleted and the session starts empty (logged once). About 2.6 MB with all 34 GS pipelines on an RX 9070 XT. Also used in validation runs: it changes no output |

Before 2026-10-20 the decoded-texture cache had only the entry bound: with the terrain atlas rebuilt every frame it held up to about 6 GB before emptying. Until the residency (also 2026-10-20) every texture a frame drew was copied to the GPU again each frame: about 3 MB per frame in the mission 1 opening, 108 KiB in mission 4; now 18 KiB and 1 KiB (`validation.md` §2m, ST-4). Until 2026-10-22 decoded textures were packed into one atlas, which restarted when a frame's new textures did not fit: in mission 4 combat about every 12 game ticks, with 5.3 MiB uploaded per frame (`renderer.md` §2.5). The renderer logs its average cost per frame at exit (`renderer: N frames drawn; per frame on average: GS processing … ms, recording … ms, … textures drawn, … decoded on the GPU, … uploaded from the CPU, … GPU memory updates, … KiB uploaded`), the structure of its GPU work (`renderer: GPU work per frame on average: … render passes, … pipeline barriers, … frame-buffer copies (… Mpixels)` and the copies by kind, 2026-10-26, `validation.md` §2al), with frame interpolation the blended copies processed and reused (`renderer: blended copies: … processed, … reused from the copy before`, 2026-10-27, §2am); a warning, once per session, when the game reads back through local memory something only the GPU drew (`renderer: local->local transfer reads a frame buffer drawn on the GPU only …` or `CLUT loaded from a frame buffer drawn on the GPU only …`, 2026-10-27, §2an TT-2) and shows the last frame's values in the debug overlay's Performance panel.

## 7. Frame-rate overlay (debug layer)

Presented frames per second and the mean frame time over the last half second, two lines of small white text with a one-pixel shadow at the top right of the window (no window frame or background, so it hides as little of the picture as possible; in fullscreen 4:3 it sits in the pillarbox). Drawn by the debug overlay's ImGui context after the output pass, so it never enters the game image, screenshots or the guest. On with `--fps-overlay` (session), `debug.fps_overlay` (saved) or the "FPS overlay" checkbox in the debug overlay's Performance panel (session); works in release builds too, where it creates only the overlay context, not the debug panels. Code: `src/platform/debug/overlay.cpp` (`DrawFps`).

## 8. Debug overlay (proposal and implementation)

Status: **first version implemented (skeleton milestone D, 2026-10-12)**, see §8.7. The rest of this section is the approved proposal. Part of the optional debug layer (CLAUDE.md, category 3). Nothing changes in the default experience unless the overlay is opened, and features that can change simulation results are marked.

### 8.1 Opening and closing

- **Toggle key: `\`**, matched by the **typed character**, not the physical key: the overlay listens to SDL3 text input (`SDL_EVENT_TEXT_INPUT`) and toggles when the text is `"\"`. This works on any layout: on an Italian keyboard `\` is the key left of `1`; on US/UK it is next to Enter/Shift.
  - Layouts that produce `\` only with AltGr still work, because the produced character is what counts.
  - Text input must stay enabled while the game runs; the game itself reads only pad state, so this has no side effect.
- **Rebindable** in `settings.json`: `"debug": {"overlay_toggle": "\\"}` (a character) or a key name (`"F12"`) for players who prefer a scancode.
- While the overlay has keyboard focus, keys are **not** forwarded to the game's pad mapping (no accidental inputs). Gamepad input still goes to the game unless "capture pad" is ticked.

**Proposal: also require a settings flag.** `"debug": {"overlay_enabled": false}` by default. With it off, `\` does nothing and the overlay code is never initialised (no ImGui context, no GPU resources). Reasons: a stray `\` (e.g. while typing a pilot name in the game's own name entry, which uses the pad, or alt-tabbing) cannot open it; players who never touch the debug layer get exactly the default experience; support logs record whether it was enabled. The flag is also settable by a command-line switch (`--debug-overlay`) for development.

### 8.2 Look

- **Square UI, no rounded corners anywhere.** Dear ImGui style: `WindowRounding`, `ChildRounding`, `FrameRounding`, `PopupRounding`, `ScrollbarRounding`, `GrabRounding`, `TabRounding` all **0**; also `WindowBorderSize = 1`, no anti-aliased rounded fill. Applied once at context creation.
- Drawn by the **output pass, after the 4:3 game image** (ADR-006), directly into the swapchain image at host resolution. It never draws into guest render targets, so GS state, framebuffer contents and any GS-dump comparison are unaffected.
- Semi-transparent panels over the game; a "game in a window" mode that shrinks the 4:3 image into a panel is a later option.

### 8.3 Dependency

| | |
|---|---|
| Library | **Dear ImGui** (Omar Cornut and contributors) |
| License | **MIT**: compatible with GPL-3.0 (`../decisions/dependencies.md`) |
| Backends | `imgui_impl_sdl3` + `imgui_impl_vulkan`, both in the upstream repo, so no third-party glue |
| Integration | FetchContent pinned to a release tag with checksum, unmodified (no `third_party/` copy). Memory editor widget: write our own (the popular `imgui_club` memory editor is MIT too, but ours must show PS2 addresses and symbol names anyway) |
| Build | always compiled (one binary), initialised only when `overlay_enabled` is true |

Alternatives: Nuklear (MIT, no maintained Vulkan backend, fewer widgets), RmlUi (MIT, HTML/CSS, aimed at game UI, heavy). Recommendation: **Dear ImGui**. Added to `../decisions/dependencies.md` as proposed.

### 8.4 Features

Effort: S ≤ 2 days, M 3–6 days, L > 1 week (each assumes the subsystem it reads already exists). "Sim" = can it change simulation results: **no** (read-only, cannot), **guarded** (changes only presentation or is safe by construction, rules given), **yes** (changes game state on purpose; such actions mark the session as "modified" in the log and in the overlay title bar).

#### 8.4.1 Read-only

| Feature | What it shows | Source | Effort | Sim |
|---|---|---|---|---|
| Performance and frame timing | presented FPS, simulation steps per presented frame, catch-up count and dropped time (ADR-002 accumulator), CPU time per phase (game step, draw-list build, renderer, present), GPU time per bucket | platform timers, Vulkan timestamp queries | S | no |
| Scene inspector | current scene / sub-state (`ctx` scene fields, scene-2 sub-state 0x0029F38E), frame counters (`frame_count`, `frame_count_unpaused`, `scene_frame`), mission id (`game[0x27]`), `RequestScene`/`RequestSubState` log with frame numbers | guest memory + a hook in the translated `RequestScene`/`RequestSubState` | S | no |
| Memory viewer | hex/float/u16/u32 view at PS2 addresses (RAM, scratchpad), labels from `docs/symbols.csv` loaded at start, go-to by name | `GuestMemory` | M | no |
| Watch list | user-chosen globals and `g_MainContext` fields (typed from the struct definitions shared with `static_assert`s), change highlighting | guest memory + struct metadata | M | no |
| Work heap meter | used / free of the game bump allocator (0x00101000) and the work heap (98.4 % used in mission 1, `../game/memory.md`), high-water mark, allocation list by caller address | allocator hook | S | no |
| Random sources | the five sources (`../game/random.md`): table cursor and reads this frame (with caller address), libc `rand` state and call count, VU0 R, effect LCG words, sound LFSR | guest memory + counters in the translated helpers | S | no |
| File / CD queue | pending CD requests (ticket, DATA/BD index, sector, size, age vs. the 3600-frame timeout), ISO read throughput | `sceCd*` shim | S | no |
| Sound | SOUND.IRX voices (state, ADSR phase, pitch, volume), VAG slots 0–5 (stream position, buffer fill), RPC log (command batches, replies) | audio backend (ADR-004) | M | no |
| Input viewer | host input → PS2 pad bytes (`scePadRead` buffer), analog/pressure values, `ConvertPadInput` result | input backend + guest memory | S | no |
| Renderer statistics | per ordering-table bucket: packets, primitives, GS state changes, draw calls, time; VU1 program used | renderer | M | no |
| Framebuffer / GS VRAM viewer | render targets (main, shadow 0x2500, glow, small targets FBP 0x120–0x1C0), uploaded textures with CLUT, by page/PSM | renderer RT cache | M | no |
| Live session log | the session log (ADR-005) with filters (module, level) | log sink | S | no |
| *Extra:* game TTY | output of the game's own `printf` calls (shipping debug prints, e.g. `func_00250C30`, the movie-stream "CH0/CH2" warning, the MTXT version warning), invisible on a retail PS2 | `printf` in the libc shim | S | no |
| *Extra:* `sce*` call counters | per SDK function: calls this frame/total, unimplemented hits | `src/sce/` | S | no |
| *Extra:* object inspector | flight units (pointer table 0x002A1F70: position, speed, HP, AI state), projectile and effect pools with slot occupancy | guest memory + struct metadata | M | no |
| *Extra:* save image inspector | the save image (`../formats/SAVE.md`) decoded field by field, before/after a save, checksum check | `saves\` + format code | M | no |
| *Extra:* determinism checker | hash of guest RAM regions after each simulation step, written to a file; two runs (or port vs. replay) compared step by step to find the first divergence | `GuestMemory` | M | no |
| *Extra:* native screenshot | saves the native 512×448 (or N×) frame before the output pass, for comparison with PCSX2 screenshots; output goes to `%LOCALAPPDATA%\ProjectStonehenge\screenshots\`, never the repo | renderer | S | no |

#### 8.4.2 Control

| Feature | What it does | Effort | Sim |
|---|---|---|---|
| Pause and single-step | pauses the **simulation step and the audio clock together**; "step" runs exactly one 59.94 Hz step (one PS2 frame). Presentation keeps running so the overlay stays usable | S | **guarded**: no step is skipped or repeated, so results equal an uninterrupted run. Wall-clock-dependent paths (none found: the game counts frames) would be the only risk |
| Toggle render buckets | hide a bucket / table (terrain, effects, HUD, sky…) in the renderer | S | **guarded**: only the GPU replay skips it. The draw-list build still runs (it consumes `rand` and the random table and changes state, `../subsystems/effects.md`, ADR-002). Never implemented by skipping game draw functions |
| Jump to scene | calls the translated `RequestScene(n, sub)` at the start of the next step, including the dead **scenes 1 and 3** (`SceneMenuEntry`, `SceneMovie`) to explore cut content (`dead-code.md`) | M | **yes**: changes game state; scene 3 needs `ret_scene` set first (dead `SetReturnScene` 0x00100B08) |
| RAM diff against a PCSX2 save state | loads `eeMemory.bin` from a `.p2s` (uncompressed, as `tools/p2s_read.py`) and diffs it with guest RAM, region by region, with symbol names; only reads the state file the user picks | M | no (read-only) |
| *Extra:* memory poke | edit a value in the memory viewer / watch list | S | **yes** |
| *Extra:* breakpoint on guest write | stop the simulation when a watched guest address is written (debug builds have write-watch, ADR-001) | M | **guarded** (pauses only) |
| *Extra:* random-read trace | log every `NextRandomFloat` read of one frame with caller address, to compare with a PCSX2 trace | S | no |

#### 8.4.3 Later

| Feature | Notes | Effort | Sim |
|---|---|---|---|
| Save states | snapshot of guest RAM + scratchpad + all `src/sce/`/platform service state (CD queue, audio voices, VAG streams, pad, timers). Straightforward with guest memory (ADR-001); the service state is the work | L | **yes** on load |
| Mission select | sets the mission id and enters the mission through the game's own menu path (`MissionSelectAction` 0x0023AC70) | M | **yes** |
| Free camera | a **presentation-side** camera: the renderer replays the step's draw lists with its own view matrix (ADR-002 records the per-node world matrices). It never writes the game camera, so lock-on and HUD projection are unchanged | L | **guarded** |
| *Extra:* input recording/playback | record pad bytes per step and play them back (with the determinism checker, a regression test) | M | **yes** on playback (replaces input) |
| *Extra:* GS frame capture | write the frame's GIF/VIF packets in PCSX2 `.gs` dump format, to compare with PCSX2 GS dumps in the same tools | M | no |

### 8.5 Rules

- Part of the debug layer: compiled in, **inactive** unless `overlay_enabled` is true and the overlay is opened.
- Reading never mutates guest memory or service state (no read side effects: e.g. reading the random cursor must not call `NextRandomFloat`).
- Every "yes" action is logged and marks the session as modified.
- No game data leaves the process: no dumps into the repo, screenshots and traces go to the user data folder.

### 8.6 User decisions

1. Approve Dear ImGui as a dependency (`../decisions/dependencies.md`).
2. Require `overlay_enabled` in `settings.json` (proposed: **yes**, default false).
3. Which features go in the first version (proposed: the S items of 4.1, pause/step, bucket toggles, memory viewer, RAM diff).

### 8.7 Implemented (milestone D)

Code: `src/platform/debug/overlay.{h,cpp}`; hook for scene requests: `src/common/debug_hooks.{h,cpp}` (called by the translated `RequestScene`/`RequestSubState`, records only). Dear ImGui v1.92.9b (`../decisions/dependencies.md`).

- **Enable:** `settings.json` `"debug": {"overlay_enabled": true}` (default on in development builds, off in release), or `--debug-overlay` / `--no-debug-overlay` for one session. With it off, no ImGui context or GPU resource is created. Changing the setting while the port runs creates or destroys the overlay.
- **Toggle:** the typed character in `"overlay_toggle"` (default `"\"`, matched on SDL text input, so any keyboard layout works), or `"key:<SDL scancode name>"` (e.g. `"key:F12"`).
- **Input:** while an overlay window has keyboard focus, the keyboard is not mapped to the game's pad; gamepads still reach the game.
- **Look:** square style (every rounding 0, border 1), drawn in the output pass after the game image at host resolution. `imgui.ini` is not written (`IniFilename = nullptr`).
- **Graphics panel** (2026-10-31, User request; "Sim: no", presentation only): every presentation setting that applies while the port runs: window mode, v-sync, frame cap (disabled with v-sync on), frame interpolation, internal resolution, scaling filter, anti-aliasing. The internal resolution is chosen only as a multiplier (1×–8×); each choice shows the render size it gives (presented draw buffer × N, e.g. 640 × 448 at 2× = 1280 × 896) and a line reports the renderer's current render size. No resolution or windowed size can be typed or picked directly (User request 2026-11-09); `presentation.window_width`/`window_height` stay file-only. Each change applies at once for the session, as on the GRAPHICS page (§3); "Save to settings.json" writes the values into the file's own values (session-only command-line values are not saved), "Defaults for this display" sets the §1 defaults (the windowed size and the SCREEN RATIO default stay). Not in the panel: `presentation.default_screen_ratio`, which applies only at boot. The overlay hands the change to the host after it builds the frame (`DebugOverlay::TakePresentationRequest`); `src/main.cpp` applies it like the GRAPHICS page. A GRAPHICS page that is open keeps the values it opened with.
- **Input panel** (2026-10-31, read-only): per port the host device, whether it reports pressures, the button bits and sticks, whether the map button steps or holds, the 12 pressures from the host, the game's pad type (5 = DualShock 2 pressure mode) and the pressures the game last read (libpad port record +0x40).
- **Panels** (all read-only, "Sim: no"):
  - *Performance:* presented fps and frame-time graph (240 frames), the "FPS overlay (top right)" checkbox, the renderer's CPU time of the last frame (GS processing, recording) and its texture statistics (textures drawn, GPU decodes, CPU uploads, GPU memory updates, KiB uploaded, texture images held and their MiB; §6), its GPU work of the last frame (render passes, pipeline barriers, frame-buffer copies and their Mpixels), steps this frame, total steps, dropped steps (catch-up limit 4), interpolation alpha, DMA intake per step (GIF/VIF1 qwords, sends, bad chains), last DISPFB/DISPLAY.
  - *Scene:* scene and name, sub-state, previous/next scene, `req`, flags, `frame_count`/`scene_frame` (and unpaused), mission id, and the last 64 `RequestScene`/`RequestSubState` calls with step, `frame_count` and requesting scene.
  - *Memory:* 32 rows of 16 bytes at a PS2 address, as bytes+ASCII, u32 or f32; go to a hex address or an exact symbol name; each row labelled with the symbol it belongs to. Only RAM and scratchpad are read (never hardware registers). Symbols: `docs/symbols.csv` of the source tree in development builds, or an optional `symbols.csv` next to the executable (read only).
  - *Session log:* the last 400 log lines with a text filter; errors and warnings coloured.
- **Frame-rate overlay** (2026-10-20): FPS and mean frame time over the last half second, small text at the top right of the window, drawn without a window or background (`DrawFps`). On with `--fps-overlay`, `debug.fps_overlay` or the Performance panel's checkbox; it also works with the panels disabled (release builds), then only the ImGui context is created (§7).
- **Not yet** (later items of §8.4): pause/single-step, bucket toggles, RAM diff against a `.p2s`, the other read-only panels.

#### 8.7.1 Changes after the hands-on test (2026-10-22, `validation.md` §2q)
- **Toggle after a text field:** `imgui_impl_sdl3` stops SDL text input when a text field loses focus, and the toggle character arrives as text input, so after typing in the Memory address or log filter field `\` no longer worked. Text input is switched on again after every ImGui frame (`DebugOverlay::EndFrame`).
- **UI scale:** the scalable default font and every size are scaled with the window height (1 up to 1080 lines, 2 at 2160, quarter steps), also the frame-rate overlay; it follows window size changes.
- **Default layout** (1080-line units): Debug top left, Performance below it, Scene and Memory side by side at the top, Session log under Performance; nothing overlaps at 16:9.
- **Keyboard on opening:** opening the panels focuses none of them, so the keyboard keeps driving the game until a panel is clicked.
- Scene: the request table fills the window, the request column (with its argument) gets most of the width, the header row stays visible; mission id in hex and decimal.
- Memory: the row of the address or symbol looked up is highlighted; the label column fits at the default size.
- Session log: the filter ignores case.
- Main window title "Debug" (was "AC04 debug").

## 9. QUIT GAME (main menu, campaign menu)

The last item of the main menu's first column, after OPTIONS (User request 2026-10-22), and of the campaign menu's first column, after LOAD (User request 2026-10-24: the menu after a mission). Code: `src/game/quit_item.*`, hooks marked `port:` in `src/game/menu.cpp` and `src/game/campaign_menu_page.cpp`; one in `src/game/menu_widgets.cpp`.

- Drawn by the game's own menu tree: the column-0 definition gets the item only while the tree is built or its cursors are stored (`QuitItemPatch`, as GRAPHICS in §3), and the item ids are mapped back to the game's (`MainMenuSelection`). It is typed in, highlighted and wrapped over like the other items. Items the port adds do not lengthen a column's typing (`UpdateListWidget`), so its duration and typing sounds stay the game's.
- Selecting it plays the select sound and fades the menu, the screen and the menu BGM out (0.5 s, port state 9); then the host ends the session as when the window is closed (`port::Menu().quit`).
- The help line is empty on it: the game's help texts are glyph messages of the page's font block, which has none for it.
- Not added in validation runs (development runs with a pad script, an input log, a PCSX2 recording, `--memcard` or `--no-memcard`; `port::Boot().quit_item`), the rule of the SCREEN RATIO default (§2): with the item, the main menu's tree and draw packets left in RAM differ from the PS2's (`validation.md` §2v). The session log says `QUIT GAME in the main menu: on` or `off (validation run)`.

## 10. Save texts for a computer

The save, load and format messages are reworded just enough for a computer (User request 2026-10-22; wording chosen by the User). Code: `src/game/pc_texts.*`, one `port:` line in `LoadFontBlock` (`src/game/message_window.cpp`).

- "memory card (PS2)" becomes "memory card" (about 40 messages: "Accessing memory card...", "Insert a memory card", "Insufficient space on memory card", ...); the warning shown while saving, loading and formatting reads "Do not remove memory card / or switch off the computer" (was "... (PS2) / or switch off power").
- Done in guest memory right after a page's messages are indexed: a message that gets shorter is changed in place, a longer one is written into the port menu area (§5, slots 22-31) and its message table entry points there; the disc data is not changed. Messages use the game's glyph codes (`../subsystems/sound.md` "Text engine").
- Not changed: the hard disk drive texts ("(for PlayStation 2)", "Do not turn power off"), unreachable in the release.
- Not applied in validation runs, the rule of §9 (the startup log line "QUIT GAME in the main menu and save texts for a computer: on / off").
