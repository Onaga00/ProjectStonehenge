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
    bool sharp_text = true;   // presentation.text_filter "sharp"

    bool operator==(const GraphicsOptions& o) const {
        return fullscreen == o.fullscreen && vsync == o.vsync && frame_cap == o.frame_cap &&
               internal_scale == o.internal_scale && smooth_filter == o.smooth_filter &&
               antialiasing == o.antialiasing && frame_interpolation == o.frame_interpolation &&
               sharp_text == o.sharp_text;
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
