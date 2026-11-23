// GRAPHICS view of the options page (graphics_page.h). Port code built from the game's widgets; the
// layout follows the BRIGHTNESS view (DrawBrightnessTexts 0x00240540, BrightnessUpdate 0x00240088):
// the same outer frames, title, typed texts (timer a), choice squares (timer c), pulsing box and
// crosshair. Options page object fields used: +0x230 fade, +0x240 crosshair, +0x27C panel (+0x27E
// parts, +0x2C8 / +0x2D0 timers a / c, +0x2D4 crosshair start, +0x2DC crosshair), +0x6E8 message
// box, +0x70C view, +0x710 state. The row and the edited values are kept on the host.
#include "game/graphics_page.h"

#include "common/ee_math.h"
#include "common/port_hooks.h"
#include "game/game.h"
#include "game/port_menu.h"

namespace ac04::game {

using guest::GRef;

namespace {

AC04_GUEST_VAR(u32, g_pMenuFade, 0x00410E88)

// Port menu area slots (port_menu.h; 0-3 are CONTROLLER SETUP's).
enum Slot : u32 {
    kSlotTitle = 4,
    kSlotRows = 5,     // 8 rows, one slot each: the name at +0, the value at +0x20 (2026-11-22)
    // 13-18 free
    kSlotRects = 19,
    kSlotItems = 20,
    kSlotItemText = 21,
};

constexpr u32 kItemCount = 6;  // column 0 of SCREEN SETTINGS on the PS2

struct Row {
    const char* name;
    s32 count;
    const char* values[8];
};

// Rows and their choices; square 0 is the leftmost.
constexpr s32 kRows = 8;
constexpr Row kRowTable[kRows] = {
    {"FULLSCREEN", 2, {"ON", "OFF"}},
    {"V-SYNC", 2, {"ON", "OFF"}},
    {"INTERPOLATION", 2, {"ON", "OFF"}},
    {"FRAME RATE CAP", 5, {"60 FPS", "120 FPS", "144 FPS", "240 FPS", "360 FPS"}},
    {"INTERNAL RES.", 8, {"1X", "2X", "3X", "4X", "5X", "6X", "7X", "8X"}},
    {"SCALING FILTER", 2, {"SHARP", "SMOOTH"}},
    {"ANTI-ALIASING", 3, {"OFF", "FXAA", "SMAA"}},
    {"TEXT", 2, {"ORIGINAL", "SHARP"}},  // presentation.text_filter (proposed 2026-11-22, issue #4)
};

// Name and value of row r in its slot (each string under 0x20 bytes).
u32 RowName(s32 r) { return port_menu::String(kSlotRows + static_cast<u32>(r), kRowTable[r].name); }
u32 RowValue(s32 r, const char* value) {
    return port_menu::StringAt(kSlotRows + static_cast<u32>(r), 0x20, value);
}
constexpr s32 kFrameCaps[5] = {60, 120, 144, 240, 360};

// Layout (12.4 screen units): title row as BRIGHTNESS, then one row per setting.
constexpr s32 kTitleY = 0x520;
constexpr s32 kFirstRowY = 0x6A0;
constexpr s32 kRowStep = 0x1C0;
constexpr s32 kNameX = 0x300;
constexpr s32 kValueX = 0xD00;
constexpr s32 kSquareX = 0x1500;
constexpr s32 kSquareStep = 0x140;
constexpr s32 kSquareDy = -0x20;   // squares sit 2 pixels above the value text, as BRIGHTNESS
constexpr s32 kNameDy = 0x20;      // the smaller name font, centred on the value line

s32 g_Row = 0;
port::GraphicsOptions g_Edit;
port::GraphicsOptions g_Entry;  // the values when the view opened (CANCEL restores them)

constexpr s32 kVsyncRow = 1;
constexpr s32 kCapRow = 3;
// The frame rate cap applies only with v-sync off: with v-sync on its row shows "V-SYNC", has no
// squares and is skipped by the cursor.
bool RowEnabled(const port::GraphicsOptions& g, s32 row) { return row != kCapRow || !g.vsync; }

// Every change is applied at once (not saved); ACCEPT saves, CANCEL goes back to g_Entry.
void Preview() {
    if (g_Edit != port::Menu().graphics) port::Menu().set_graphics = g_Edit;
}

s32 State(u32 obj) { return GRef<s32>(obj + 0x710); }
void SetState(u32 obj, s32 s) { GRef<s32>(obj + 0x710) = s; }

s32 Choice(const port::GraphicsOptions& g, s32 row) {
    switch (row) {
        case 0: return g.fullscreen ? 0 : 1;
        case 1: return g.vsync ? 0 : 1;
        case 2: return g.frame_interpolation ? 0 : 1;
        case 3:
            for (s32 i = 0; i < 5; ++i)
                if (kFrameCaps[i] == g.frame_cap) return i;
            return 4;
        case 4: return g.internal_scale < 1 ? 0 : (g.internal_scale > 8 ? 7 : g.internal_scale - 1);
        case 5: return g.smooth_filter ? 1 : 0;
        case 6: return g.antialiasing < 0 ? 0 : (g.antialiasing > 2 ? 2 : g.antialiasing);
        default: return g.sharp_text ? 1 : 0;
    }
}

void SetChoice(port::GraphicsOptions& g, s32 row, s32 c) {
    switch (row) {
        case 0: g.fullscreen = c == 0; break;
        case 1: g.vsync = c == 0; break;
        case 2: g.frame_interpolation = c == 0; break;
        case 3: g.frame_cap = kFrameCaps[c]; break;
        case 4: g.internal_scale = c + 1; break;
        case 5: g.smooth_filter = c == 1; break;
        case 6: g.antialiasing = c; break;
        default: g.sharp_text = c == 1; break;
    }
}

s32 RowY(s32 row) { return kFirstRowY + row * kRowStep; }
s32 SquareX(s32 c) { return kSquareX + c * kSquareStep; }

void MoveCursor(u32 obj) {
    const s32 x = SquareX(Choice(g_Edit, g_Row)) + 0x80;
    const s32 y = RowY(g_Row) + kSquareDy + 0x70;
    GRef<s16>(obj + 0x2D4) = static_cast<s16>(x);
    GRef<u16>(obj + 0x2D6) = static_cast<u16>(y);
    MoveCrosshairAuto(GRef<u32>(obj + 0x2DC), x, y);
}

// Panel outlines: the two outer frames of the BRIGHTNESS view (table 0x003AF3D0) and the rows'
// frame (centre x, centre y, width, height in 12.4 with the 0x6C00 / 0x7200 frame offset; colour;
// opening delay).
u32 PanelRects() {
    u32 w[12];
    for (u32 i = 0; i < 8; ++i) w[i] = GRef<u32>(0x003AF3D0 + i * 4);
    // The inner outline (centre (319.5, 197.5), 495 x 201) ends below the seventh row: one row
    // taller and half a row lower, so it still encloses every row with TEXT (2026-11-22).
    w[4] += 0x00E0u << 16;
    w[5] += 0x01C0u << 16;
    // One row (28 pixels) taller per row, centre moved down by half a row: 488 x 222 at (276, 186)
    // for seven rows, 488 x 250 at (276, 200) for eight (TEXT, 2026-11-22).
    w[8] = (0x7200u + 0x0C80u) << 16 | (0x6C00u + 0x1140u);  // centre (276, 200)
    w[9] = 0x0FA0u << 16 | 0x1E80u;                             // 488 x 250 (eight rows)
    w[10] = 0x00D1C1A9u;
    w[11] = 0x3DCCCCCDu;  // 0.1
    return port_menu::Words(kSlotRects, w, 12);
}

}  // namespace

GraphicsItemPatch::GraphicsItemPatch(u32 columns)
    : columns_(columns), count_(GRef<s16>(columns + 4)), items_(GRef<u32>(columns + 0x14)) {
    u32 items[kItemCount + 1];
    for (u32 i = 0; i < kItemCount; ++i) items[i] = GRef<u32>(items_ + i * 4);
    items[kItemCount] = port_menu::String(kSlotItemText, "GRAPHICS");
    GRef<s16>(columns + 4) = static_cast<s16>(kItemCount + 1);
    GRef<u32>(columns + 0x14) = port_menu::Words(kSlotItems, items, kItemCount + 1);
}

GraphicsItemPatch::~GraphicsItemPatch() {
    GRef<s16>(columns_ + 4) = count_;
    GRef<u32>(columns_ + 0x14) = items_;
}

s16 OptionsSelection(u32 tree) {
    const s16 id = GetMenuTreeSelection(tree);
    if (id == static_cast<s16>(kItemCount)) return kGraphicsItem;
    return id > static_cast<s16>(kItemCount) ? static_cast<s16>(id - 1) : id;
}

void EnterGraphicsView(u32 obj) {
    SetState(obj, 0);
    InitFade(obj + 0x230, 1);
    GRef<s32>(obj + 0x70C) = 3;
    g_Row = 0;
    g_Edit = port::Menu().graphics;
    g_Entry = g_Edit;
    InitPanel(obj + 0x27C, 3, PanelRects(), obj + 0x240);
    GRef<u16>(obj + 0x27E) |= 0xE;
    MoveCursor(obj);
}

void SetGraphicsHelp(u32 mb) {
    // As SetMessageBoxText, with two chosen lines.
    GRef<u32>(mb + 8) = 0;
    GRef<u32>(mb) = (GRef<u32>(mb) & 0xFFFFFFFDu) | 1;
    GRef<s16>(mb + 0xE) = 1;
    GRef<s16>(mb + 0x10) = 3;
    GRef<s16>(mb + 0xC) = 2;
    MeasureMessageBox(mb);
}

void GraphicsUpdate(u32 obj) {
    const u32 fade = g_pMenuFade();
    GRef<u16>(fade + 0x32) = 0;
    const u32 edge = Ctx()->pad_buttons[0][1];
    const u32 repeat = Ctx()->pad_buttons[0][2];
    UpdatePanel(obj + 0x27C);
    const s32 state = State(obj);
    if (state == 1) {
        const s32 count = kRowTable[g_Row].count;
        s32 c = Choice(g_Edit, g_Row);
        u16 arrows = 0x33;  // up, down, ACCEPT, CANCEL
        if (0 < c) arrows |= 4;
        if (c < count - 1) arrows |= 8;
        GRef<u16>(fade + 0x32) = arrows;
        if ((repeat & 3) != 0) {
            const s32 step = (repeat & 2) != 0 ? 1 : -1;
            s32 row = g_Row;
            do {
                row = (row + step + kRows) % kRows;
            } while (!RowEnabled(g_Edit, row));
            g_Row = row;
            MoveCursor(obj);
            PlayMenuSe(0, 4);
        } else if ((repeat & 0xC) != 0) {
            s32 next = c;
            if ((repeat & 4) != 0) next = c - 1;
            if ((repeat & 8) != 0) next += 1;
            if (next < 0) next = 0;
            else if (count - 1 < next) next = count - 1;
            if (next != c) {
                PlayMenuSe(0, 4);
                SetChoice(g_Edit, g_Row, next);
                MoveCursor(obj);
                Preview();
            }
        }
        if ((edge & 0x20) != 0) {
            SetPanelState(obj + 0x27C, 1);
            SetState(obj, 4);
            PlayMenuSe(0, 3);
            g_Edit = g_Entry;
            Preview();
            return;
        }
        if ((edge & 0x50) != 0) {
            SetPanelState(obj + 0x27C, 1);
            SetState(obj, 4);
            PlayMenuSe(0, 2);
            port::Menu().set_graphics = g_Edit;
            port::Menu().save_graphics = true;
            return;
        }
        if ((edge & 0x4000) != 0) {
            PlayMenuSe(0, 3);
            g_Edit = port::Menu().graphics_defaults;
            if (!RowEnabled(g_Edit, g_Row)) g_Row = kVsyncRow;
            MoveCursor(obj);
            Preview();
        }
    } else if (state == 0) {
        if ((GRef<u16>(obj + 0x27C) & 4) != 0) SetState(obj, 1);
    } else if (state == 4 && (GRef<u16>(obj + 0x27C) & 8) != 0) {
        SetOptionsView(obj, 0);
    }
}

namespace {

// Texts typed out by timer a, squares by timer c, then the pulsing boxes on the chosen squares
// (the current row's pulses), as DrawBrightnessTexts.
void DrawGraphicsTexts(u32 list, s32 index, u32 obj) {
    const u32 parts = GRef<u16>(obj + 0x27C);
    GRef<u16>(obj + 0x27C) = static_cast<u16>(parts & 0x7FFF);
    BeginMenuTextLayer(list, index);
    const f32 a = GRef<f32>(obj + 0x2C8);
    if (0.0f < a) {
        const f32 t = a * 90.0f;
        DrawMenuText(t, 0x240, kTitleY, 4, 0x004112C8);
        DrawMenuText(fpu::Sub(t, 1.0f), kNameX, kTitleY, 4, port_menu::String(kSlotTitle, "GRAPHICS >"));
        for (s32 r = 0; r < kRows; ++r) {
            const f32 tr = fpu::Sub(t, static_cast<f32>(4 + r * 4));
            const s32 y = RowY(r);
            DrawMenuText(tr, kNameX, y + kNameDy, 4, RowName(r));
            const char* value = RowEnabled(g_Edit, r) ? kRowTable[r].values[Choice(g_Edit, r)] : "V-SYNC";
            DrawMenuText(fpu::Sub(tr, 8.0f), kValueX, y, 3, RowValue(r, value));
        }
        if (a < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (6.0f < a && (parts & 0x100) != 0) {
            GRef<u16>(obj + 0x27C) &= 0xFEFF;
        }
    }
    const f32 c = GRef<f32>(obj + 0x2D0);
    if (0.0f < c) {
        const f32 t = c * 90.0f;
        for (s32 r = 0; r < kRows; ++r) {
            if (!RowEnabled(g_Edit, r)) continue;
            f32 m = fpu::Sub(t, static_cast<f32>(4 + r * 2));
            for (s32 k = 0; k < kRowTable[r].count; ++k) {
                DrawMenuText(m, SquareX(k), RowY(r) + kSquareDy, 8, 0x004112E0);
                m = fpu::Sub(m, 1.0f);
            }
        }
        const f32 left = fpu::Sub(t, 30.0f);
        if (left < 0.0f) {
            GRef<u16>(obj + 0x27C) |= 0x8000;
        } else if (5.0f < left && (parts & 0x400) != 0) {
            GRef<u16>(obj + 0x27C) = static_cast<u16>((GRef<u16>(obj + 0x27C) & 0xFBFF) | 0x800);
        }
    }
    EndMenuTextLayer();
    if ((GRef<u16>(obj + 0x27C) & 0x800) != 0 && GRef<u16>(obj + 0x240) == 0) {
        for (s32 r = 0; r < kRows; ++r) {
            if (!RowEnabled(g_Edit, r)) continue;
            const s32 frame_no = r == g_Row ? static_cast<s32>(Ctx()->frame_count) : 0;
            DrawPulseBox(list, index, 0x1067, SquareX(Choice(g_Edit, r)), RowY(r) + kSquareDy, 0x100, frame_no);
        }
    }
}

}  // namespace

void DrawGraphicsView(u32 obj) {
    DrawGraphicsTexts(obj, 0, obj);
    DrawPanel(obj, 1, obj + 0x27C);
}

}  // namespace ac04::game
