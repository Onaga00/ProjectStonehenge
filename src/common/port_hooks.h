// Port options shown inside the translated menu pages (port-baseline features "Modern control
// types" and "GRAPHICS page", User approval 2026-10-20; docs/decisions/port-baseline.md). Game code
// never reads settings: the host copies the values here before each game tick, and a page that
// changes them leaves a request the host applies (and saves to settings.json) after the tick. The
// game thread and the host thread never run at the same time (ADR-002 lockstep), so no locking.
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "common/types.h"

namespace ac04::port {

struct GraphicsOptions {
    bool fullscreen = false;
    bool vsync = true;
    s32 frame_cap = 360;      // 60, 120, 144, 240 or 360 (used with v-sync off)
    s32 internal_scale = 1;   // 1-8
    bool smooth_filter = false;
    s32 antialiasing = 0;     // 0 off, 1 FXAA, 2 SMAA
    bool frame_interpolation = true;

    bool operator==(const GraphicsOptions& o) const {
        return fullscreen == o.fullscreen && vsync == o.vsync && frame_cap == o.frame_cap &&
               internal_scale == o.internal_scale && smooth_filter == o.smooth_filter &&
               antialiasing == o.antialiasing && frame_interpolation == o.frame_interpolation;
    }
    bool operator!=(const GraphicsOptions& o) const { return !(*this == o); }
};

struct MenuOptions {
    // Written by the host before each tick.
    std::array<bool, 2> modern_controls{};  // per player: Modern variant of the CONTROL TYPE
    GraphicsOptions graphics;
    GraphicsOptions graphics_defaults;      // the defaults on this display (the page's DEFAULT button)
    // Written by a page; taken (and cleared) by the host after the tick. The GRAPHICS page hands
    // over every change at once (applied, not saved); `save_graphics` asks for settings.json too.
    std::optional<std::array<bool, 2>> set_modern_controls;
    std::optional<GraphicsOptions> set_graphics;
    bool save_graphics = false;
    // QUIT GAME was chosen in the main menu or the campaign menu (game/quit_item.h): the host ends
    // the session as when the window is closed.
    bool quit = false;
};

MenuOptions& Menu();

// Values the game code reads once at boot, set by the host before the game thread starts.
struct BootOptions {
    // The game's SCREEN RATIO option starts at 16:9 instead of 4:3 (monitor wider than 4:3,
    // settings presentation.default_screen_ratio "monitor"); save data loaded later replaces it.
    bool wide_screen_ratio = false;
    // QUIT GAME is added to the main menu and the campaign menu (game/quit_item.h); off in validation
    // runs, so that their guest RAM stays comparable with PCSX2.
    bool quit_item = false;
    // The save, load and format messages are reworded for a computer (game/pc_texts.h); off in
    // validation runs for the same reason.
    bool pc_texts = false;
};

BootOptions& Boot();

// Input features of the port baseline (User approval 2026-10-31, docs/port/port-features.md §1).
struct InputHooks {
    // Set by the host before each tick from the latched pads (recorded in input logs): the
    // player's devices give □ no pressure, so the HUD's map button uses the cycle the original
    // has for a DualShock 2 in analog mode (radar, map 1/4, 1/2, whole map) instead of holding.
    std::array<bool, 2> digital_map_cycle{};
    // Set by the game while the tick reads the player's flight controls (ReadPlayerControls);
    // cleared by the host before each tick. The mouse steers only in flight.
    std::array<bool, 2> in_flight{};
};

InputHooks& Input();

// Presentation shape copied by the host before each game tick. Ultrawide is active only in flight
// with the game's 16:9 option on; frontend menus and movies keep their original 4:3 / 16:9 presentation.
struct DisplayOptions {
    int aspect_w = 4, aspect_h = 3;
    float projection_x_scale = 1.0f;  // extra horizontal world view, relative to 16:9
    float hud_x_scale = 1.0f;         // inverse, keeping the 2D overlay's 16:9 shape
    // Ultrawide is in effect this tick (projection_x_scale > 1), and its field-of-view basis.
    bool ultrawide = false;
    // true (presentation.ultrawide_4x3_fov): the flight, replay and in-engine sequence cameras keep
    // the 4:3 mode's vertical field of view and its camera distances, and only gain width (Hor+
    // relative to 4:3, like the PCSX2 0x0010C9F4 patch). false: Kimi's original behaviour, Hor+
    // relative to the game's own 16:9 mode (16:9 camera distances and the 640 x 360 sequence view).
    bool keep_4x3_fov = true;
    float window_aspect = 0.0f;  // window width / height while `ultrawide`
};

DisplayOptions& Display();

// `wide` is the game's SCREEN RATIO flag. `ultrawide` also includes the flight-scene and setting
// checks. At more than 1 % wider than 16:9 the actual window aspect is used, so 64:27, 43:18 and
// other ultrawide sizes all fill the window without stretching the world or HUD.
void SetDisplayOptions(bool wide, bool ultrawide, int window_width, int window_height,
                       bool keep_4x3_fov = true);

// Projection width for a flight camera whose original widths are `width_4x3` (SCREEN RATIO 4:3)
// and `width_16x9` (16:9) at the same projection height. Not ultrawide: `width_16x9` (the caller
// only asks in 16:9 mode). Ultrawide with keep_4x3_fov: width_4x3 x window_aspect / (4/3), so the
// vertical field of view is the 4:3 one whatever the 16:9 constant is. Otherwise Kimi's
// width_16x9 x window_aspect / (16/9).
float UltrawideProjectionWidth(float width_4x3, float width_16x9);

// Ultrawide with keep_4x3_fov: the view cameras use the 4:3 mode's camera distances (zoom) where
// the game has a separate 16:9 value (external view K 0x0040DAC8 instead of 0x0040DACC; carrier
// landing / takeoff camera K 0x0040DAC0 instead of 0x0040DAC4).
bool UltrawideUses4x3Distances();

// Ultrawide: the backend narrows every flight-overlay draw about its view centre by hud_x_scale
// (gs.vert), which keeps gauges and text at their 16:9 shape but would also pull world-anchored
// markers (target boxes, lock-on boxes, gun pipper, special-weapon reticle, flight-path marker)
// toward the centre, away from the objects they mark. The HUD code passes those markers' projected
// GS X (1/16-pixel units) through this to pre-expand it about `centre` by 1 / hud_x_scale, so after
// the narrowing they land on their true projected position; their size offsets are added
// afterwards and are still narrowed, so boxes keep their shape. Identity when not ultrawide.
s32 UltrawideWorldMarkerX(s32 x, s32 centre);

// Where each flight view's 2D overlay begins in the frame's DMA lists, for the optional
// anti-aliasing pass (presentation.antialiasing): BuildFlightDrawLists records the first tag of each
// view's overlay table in `built`; the main loop moves them to `sending` just before it sends the
// lists built in the previous step; the DMA walker marks the GIF data where it reaches such a tag.
// Host state only: guest memory is not touched.
struct RenderMarks {
    struct Point {
        u32 tag = 0;                    // guest address of the overlay table's first DMA tag
        s16 x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the view's rectangle (scissor), inclusive
    };
    std::vector<Point> built;
    std::vector<Point> sending;
};

RenderMarks& Render();

}  // namespace ac04::port
