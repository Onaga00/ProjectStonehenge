# Pad input and pressure-sensitive buttons

Confirmed from code (`src/game/pad.cpp`) and from the tables in `.data` (read 2026-10-31 from `SLUS_201.52` with a one-off script; values below).

## Connection and modes

`UpdatePadConnection` 0x00101298 runs a libpad state machine per port (records 0x004121D8, 100 bytes each). A DualShock 2 (`scePadInfoMode` id 7) gets its actuators aligned (states 0x46–0x47) and is put into **pressure mode** with `scePadEnterPressMode` (states 0x48–0x4D); its reads then have type byte 0x79 and 12 pressure bytes. A pad of id 4 is switched to analog mode (0x28–0x2A). A change of the read type (e.g. the ANALOG button) restarts the machine.

## Mapping tables

`ConvertPadInput` 0x00101858 picks a table of 32 entries `{kind<<4 | type, source}` plus 4 dead-zone bytes at +0x40, by the read type:

| Read type | Pad type (`pad_state[0]`) | Table | Buttons L2 R2 L1 R1 △ ○ × □ and the d-pad |
|---|---|---|---|
| 0x79 (DualShock 2, pressure) | 5 | 0x0029FE50 | **pressure** (type 3, kind 0: 0..255 with dead zone) from the read's pressure bytes |
| 0x73 (analog) | 4 | 0x0029FE08 | digital (type 1: 0 or 255) |
| 0xFF (USB joystick, USBJOY.IRX) | 6 | 0x0029FE98 | pressure, as 0x79 (dead zone 0x10 instead of 0x30) |
| other (digital) | 0xFF | 0x0029FDC0 | digital |

Entries 16–27 are the sticks (full axis, negative half, positive half for LX, LY, RX, RY) in the analog, pressure and USB tables.

**Diagonals (checked 2026-10-31).** For pad types 4–6, `ConvertPadInput` first makes each stick round. It computes the radius `(int)sqrt(x² + y²)`, limits it to 0x7F and writes both axes back along the same direction (`src/game/pad.cpp`). Both sticks then give two independent axes; for the left stick these are roll and pitch, read separately by `ReadPlayerControls` (`player_flight.cpp`). A diagonal gives both axes in proportion, about 0.71 each at 45°.

A square corner gets the same rounding as a DualShock 2 stick pushed into its corner. The square corner comes from a keyboard diagonal (two keys), or from a pad whose stick reaches its corners.

`UpdateMappedPadButtons` 0x00101E28 then copies chosen values into the extended pad record (`PadExt`, main context +0x2D0 + 0x20 per port, values at +0x10..+0x1F) through a map: 0x0029FF60 for types 5 and 6, 0x0029FF40 for type 4, 0x0029FF20 otherwise. With map 0x0029FF60:

| Value | Source | Read by |
|---|---|---|
| +0x14 / +0x15 | L2 / R2 pressure | yaw (`player_flight.cpp`, `(R2 − L2) / 255`) |
| +0x16 / +0x17 | R1 / L1 pressure | throttle (`player_flight.cpp`, `(R1 − L1) / 255`) |
| +0x1D | □ pressure | HUD map zoom, analog only when the pad type is 5 or 6 (`hud.cpp`) |
| +0x10–+0x13, +0x18–+0x1B | stick halves | pitch, roll, camera |

The map 0x0029FF40 (analog pad) takes the same shoulder values, but its table gives them only 0 or 255; □ is a plain button there. △, ○, × and the d-pad are digital in every map: their pressures are computed but never stored. The value +0x1C (view zoom, `view_camera.cpp`, pad type 5/6 only) is fed by no entry of these maps in normal play (open question).

The mapped button bits (PadExt +0x0 held, +0x4 new presses, +0x8 repeats) follow the map's plain entries: under 0x0029FF60, × → 0, ○ → 1, SELECT → 2, R3 → 5, △ → 6 (a hold of 15 frames also gives bit 3), d-pad down → 4, d-pad up → 8. Under 0x0029FF40, □ also gives bit 9. CONTROLLER SETUP's button table (0x003AF5B8 glyphs, 0x003AF5F0 actions) names them:

| Button | Action |
|---|---|
| ○ | MISSILE |
| × | VULCAN |
| △ | CHANGE TARGET |
| □ | MAP |
| L1 / R1 | THROTTLE DOWN / UP |
| L2 / R2 | YAW LEFT / RIGHT |
| SELECT | CHANGE WEAPON |
| START | PAUSE |
| d-pad up (glyph m4) | LEVEL AIRCRAFT |
| d-pad down (glyph m5) | CHANGE VIEW |
| R3 | REAR VIEW |

**Map button by pad type** (`DrawHud`, `DrawRadarMap`, `hud.cpp`):

| Pad type | Campaign | Versus (split screen) |
|---|---|---|
| 4 (analog, no pressure) | each press (bit 9 in +0x4) steps HudState +0x72 through 0..3: radar, map with a window of 1/4, 1/2, then the whole map | 3 steps: speed readout, radar, map with a 1/2 window |
| 5 / 6 | the map shows while □ has a pressure, the window following it: up to 80 → 1/4, 89–184 → 1/2, above 192 → whole map, blended in between | pressure 0 → readout, 1–0x80 → radar, above → map |

The port reports type 5 and uses the type-4 cycle for players without a □ pressure (`../port/port-features.md` §1, "Map button without pressure").

So with a DualShock 2 in the original, **throttle (L1/R1), yaw (L2/R2) and the map zoom (□) are proportional to how hard the button is pressed**. The control type (NORMAL/EASY) and the port's Modern variant (L1↔L2, R1↔R2 swapped, `../port/port-features.md` §4) move them between the shoulder buttons.

## On the PC

The port answers `scePadInfoPressMode` = 1 and returns type 0x79 with the 12 pressures of `platform::PadState` (`src/sce/libpad.cpp`), so the game always runs the pressure tables. Which host devices deliver real pressures: `../port/port-features.md` §1 "Pressure-sensitive buttons".
