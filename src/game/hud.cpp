// HUD (slice S5g): UpdateHud (simulation step) and DrawHud (draw lists, per player), with the HUD
// sprite, text and digit packet builders (docs/subsystems/camera-hud.md "HUD"). Packets are built
// in the current double-buffer half exactly as on the PS2.
//
// GIF conventions used by the builders: sprites are PRIM 0x156 + UV + XYZ2 + UV + XYZ2 (tag
// 0x5400000000008000 | count, registers 0x53530), XYZ2 carries Z = 0xFFFFFF; in the 16:9 layout
// (ctx flag 0x10000, mode bit 0x100) widths are 3/4 and texel coordinates are inset by 8 instead
// of offset by 8.
#include "game/game.h"

#include <cstring>

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "common/vu0_math.h"
#include "game/flight_common.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

bool Wide() { return (Ctx()->flags & 0x10000) != 0; }

constexpr u64 kZFar = 0x00FFFFFF00000000ull;  // XYZ2 Z = 0xFFFFFF

// Texel coordinate of a font/sprite cell edge (u or v in 1/16 texels): +8 normally, -8 masked to
// 0xFF8 in the 16:9 layout.
s32 TexelEdge(s32 t16, bool wide) {
    return wide ? ((t16 - 8) & 0xFF8) : static_cast<s16>(t16 + 8);
}

// Closes a sprite strip opened with func_00104250: GIF tag with count / 5 sprites, DMA size in
// the VIF word, then the "cnt" tag via func_001042A0. Returns the quadword count.
u32 CloseSpriteStrip(u32 p, u32 count) {
    Put64(p, 0, (static_cast<u64>((count / 5u) | 0x8000u)) | 0x5400000000000000ull);
    const u32 qw = (count & 1u) != 0 ? (count >> 1) + 1 : count >> 1;
    GRef<u32>(p - 4) = (qw + 1) | 0x50000000u;
    func_001042A0(qw + 1);
    return qw;
}

}  // namespace

// EE: 0x0015F2B0
// RGBAQ register packet (colour r, g, b, a; Q = 0) into bucket `index`.
s32 func_0015F2B0(u32 list, s32 index, s8 r, s8 g, s8 b, s8 a) {
    const u32 p = AllocBucketPacket(list, index, 2, 0, 0x50000002);
    Put64(p, 1, 1);
    Put64(p, 0, 0x1000400000008001ull);
    GRef<s32>(p + 0x1C) = a;
    GRef<s32>(p + 0x10) = r;
    GRef<s32>(p + 0x14) = g;
    GRef<s32>(p + 0x18) = b;
    return 0;
}

// EE: 0x0015F350
// ALPHA_1 register packet by blend mode: 0 -> 0x44, 1 -> 0x48, 2 -> FIX 0x80 / 0x68, 3 -> 0x58,
// others FIX 0x80 / 0x64.
s32 func_0015F350(u32 list, s32 index, s32 mode) {
    const u32 p = AllocBucketPacket(list, index, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008001ull);
    u64 v;
    switch (mode) {
        case 0: v = 0x44; break;
        case 1: v = 0x48; break;
        case 2: v = 0x8000000068ull; break;
        case 3: v = 0x58; break;
        default: v = 0x8000000064ull; break;
    }
    Put64(p, 2, v);
    Put64(p, 3, 0x42);
    return 0;
}

// EE: 0x0015F420
// TEST_1 (alpha test on: 0x70000, else 0x30000) and FRAME_1 from the draw environment packet
// (ctx +0xE0) with bit 32 cleared when `fbmask` is set, else set.
s32 func_0015F420(u32 list, s32 index, s32 test, s32 fbmask) {
    const u32 p = AllocBucketPacket(list, index, 3, 0, 0x50000003);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008002ull);
    Put64(p, 2, test == 0 ? 0x30000u : 0x70000u);
    Put64(p, 3, 0x47);
    const u64 env = GRef<u64>(CtxAddr() + 0xE0);
    Put64(p, 4, fbmask == 0 ? (env | 0x100000000ull) : (env & 0xFFFFFFFEFFFFFFFFull));
    Put64(p, 5, 0x4E);
    return 0;
}

// EE: 0x0015F4F0
// Register 0x14 packet: v << 5 | v << 6.
s32 func_0015F4F0(u32 list, s32 index, s64 v) {
    const u32 p = AllocBucketPacket(list, index, 2, 0, 0x50000002);
    Put64(p, 0, 0x1000400000008001ull);
    Put64(p, 1, 0xE);
    Put64(p, 2, static_cast<u64>(v) << 5 | static_cast<u64>(v) << 6);
    Put64(p, 3, 0x14);
    return 0;
}

// EE: 0x0015F560
// Full-screen mission image: calls the scene's prepared packet (scene +0x1694) from bucket 0,
// then TEXFLUSH and a register reset packet.
void func_0015F560(u32 list) {
    func_00104338(list, 0, W(Scene() + 0x1694), 0);
    const u32 p = AllocBucketPacket(list, 0, 4, 0, 0x50000004);
    Put64(p, 0, 0x1000000000008003ull);
    Put64(p, 1, 0xE);
    Put64(p, 3, 0x3F);
    Put64(p, 2, 0);
    Put64(p, 4, 0x2004B00621412500ull);
    Put64(p, 5, 6);
    Put64(p, 7, 8);
    Put64(p, 6, 0);
}

// EE: 0x0015F610
// Draws at most min(n, 32) characters of `str` with 0x0015F6A0 (copied to a local buffer).
void func_0015F610(u32 list, s32 index, u32 str, u16 x, u16 y, s32 mode, s32 n) {
    const s32 lim = n < 0x20 ? n : 0x20;
    guest::GuestFrame fr(0x40);
    const u32 buf = fr.Address(0);
    if (lim > 0) {
        s32 i = 0;
        u8 c = GRef<u8>(str);
        GRef<u8>(buf) = c;
        while (c != 0) {
            ++i;
            if (!(i < lim)) break;
            c = GRef<u8>(str + static_cast<u32>(i));
            GRef<u8>(buf + static_cast<u32>(i)) = c;
        }
    }
    GRef<u8>(buf + static_cast<u32>(lim)) = 0;
    func_0015F6A0(list, index, buf, x, y, mode);  // t1 passes through
}

// EE: 0x0015F6A0
// HUD text: one sprite per printable ASCII character (font by mode & 0xFF: 7 small, 10 large,
// others medium; glyph cells at 0x002B6918 / 0x002B6810 / 0x002B6890, 2 bytes per character from
// 0x20); tab and space advance, control and high characters are skipped; 'I' is drawn narrower.
// Returns the quadword count of the strip.
u32 func_0015F6A0(u32 list, s32 index, u32 str, u16 x, u16 y, s32 mode) {
    const bool wide169 = Wide();
    const u32 m = wide169 ? static_cast<u32>(mode) | 0x100u : static_cast<u32>(mode);
    u32 table;
    s32 sw, tw, th, back, narrow, adv;
    if ((m & 0xFF) == 7) {
        sw = wide169 ? 100 : 0x80;
        tw = 0x80;
        th = 0x80;
        table = 0x002B6918;
        back = 0x30;
        narrow = 0x20;
        adv = sw + 0x10;
    } else if ((m & 0xFF) == 10) {
        table = 0x002B6810;
        th = 0x80;
        tw = 0xB0;
        if (wide169) {
            sw = 0x88;
            back = 0x30;
            narrow = 0x30;
            adv = 0x88;
        } else {
            sw = 0xB0;
            back = 0x40;
            narrow = 0x40;
            adv = 0xB0;
        }
    } else {
        table = 0x002B6890;
        th = 0x90;
        tw = 0x90;
        if (wide169) {
            sw = 0x70;
            back = 0x20;
            narrow = 0x20;
            adv = 0x80;
        } else {
            sw = 0x90;
            back = 0x30;
            narrow = 0x30;
            adv = 0xA0;
        }
    }
    const bool inset = (m & 0x100u) != 0;
    u32 cx = x, cy = y;
    if (inset) {
        cx = (cx - 0x10) & 0xFFFF;
        cy = (cy - 0x10) & 0xFFFF;
    }
    const u32 p = func_00104250(list, index, 0, 0x50000000);
    Put64(p, 1, 0x53530);
    u32 count = 0;
    u32 o = p + 0x10;
    const u64 y0 = static_cast<u64>(cy) << 16;
    const u64 y1 = (static_cast<u64>(cy) + static_cast<u64>(th)) << 16;
    for (u32 s = str;; ++s) {
        const u8 c = GRef<u8>(s);
        if (c == 0) break;
        if (c == 9) {
            cx = (cx + static_cast<u32>(adv)) & 0xFFFF;
            continue;
        }
        if (c < 0x20 || (c & 0x80) != 0) continue;
        if (c == 0x20) {
            cx = (cx + 0x40) & 0xFFFF;
            continue;
        }
        const u32 k = static_cast<u8>(c - 0x20);
        const s32 u = TexelEdge(GRef<u8>(table + 2 * k) << 4, inset);
        const s32 v = TexelEdge(GRef<u8>(table + 2 * k + 1) << 4, inset);
        if (k == 0x29) cx = (cx - static_cast<u32>(back)) & 0xFFFF;
        GRef<u64>(o) = 0x156;
        GRef<u64>(o + 8) = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(v) << 16))) |
                           static_cast<u64>(static_cast<s64>(static_cast<s16>(u)));
        GRef<u64>(o + 0x10) = cx | y0 | kZFar;
        GRef<u64>(o + 0x18) = static_cast<u64>(static_cast<u32>(u + tw)) |
                              (static_cast<u64>(static_cast<u32>(v + th)) << 16);
        GRef<u64>(o + 0x20) = (static_cast<u64>(cx) + static_cast<u64>(sw)) | y1 | kZFar;
        o += 0x28;
        count += 5;
        cx = (cx + static_cast<u32>(k == 0x29 ? adv - narrow : adv)) & 0xFFFF;
    }
    return CloseSpriteStrip(p, count);
}

// EE: 0x0015FA18
// HUD number: `digits` digits of value (bit 0x80: zero-padded, else leading zeros dropped but at
// least one digit), right to left from x + (digits - 1) x advance, font by mode & 0xFF (8, 9, 10,
// 12, others; glyph tables 0x002B69E0 / C8 / B0 / 98 / F8). Returns digits x advance.
s32 func_0015FA18(u32 list, s32 index, s32 value, u16 x, u16 y, s32 mode, s32 digits) {
    u32 nd = static_cast<u32>(digits) & 0xFF;
    const bool pad = (nd & 0x80u) != 0;
    if (pad) nd &= 0x7F;
    const bool wide169 = Wide();
    const u32 m = wide169 ? static_cast<u32>(mode) | 0x100u : static_cast<u32>(mode);
    s32 sw, sz;
    u32 table;
    switch (m & 0xFF) {
        case 8: sw = wide169 ? 0x60 : 0x80; table = 0x002B69E0; sz = 0x80; break;
        case 9: sw = wide169 ? 0x6C : 0x90; table = 0x002B69C8; sz = 0x90; break;
        case 10: sw = wide169 ? 0x78 : 0xA0; table = 0x002B69B0; sz = 0xA0; break;
        case 12: sw = wide169 ? 0x90 : 0xC0; table = 0x002B6998; sz = 0xC0; break;
        default: sw = wide169 ? 0x54 : 0x70; table = 0x002B69F8; sz = 0x70; break;
    }
    const s32 adv = sw + 0x20;
    const bool inset = (m & 0x100u) != 0;
    u32 cx = x, cy = y;
    if (inset) {
        cx = (cx - 0x10) & 0xFFFF;
        cy = (cy - 0x10) & 0xFFFF;
        sw += 0x10;
        sz += 0x10;
    }
    const s32 total = static_cast<s32>(nd) * adv;
    cx = (cx + static_cast<u32>(total - adv)) & 0xFFFF;
    const u32 p = func_00104250(list, index, 0, 0x50000000);
    Put64(p, 1, 0x53530);
    u32 count = 0;
    if (nd != 0) {
        const u64 y0 = static_cast<u64>(cy) << 16;
        const u64 y1 = static_cast<u64>(static_cast<u32>(cy + static_cast<u32>(sz))) << 16;
        u32 o = p + 0x10;
        s32 v = value;
        u32 digit = static_cast<u32>(v % 10) & 0xFF;
        for (u32 i = 0;;) {
            v = v / 10;
            GRef<u64>(o) = 0x156;
            const s32 u = TexelEdge(GRef<u8>(table + 2 * digit) << 4, inset);
            const s32 t = TexelEdge(GRef<u8>(table + 2 * digit + 1) << 4, inset);
            GRef<u64>(o + 8) = static_cast<u64>(static_cast<s64>(static_cast<s32>(static_cast<u32>(t) << 16))) |
                               static_cast<u64>(static_cast<s64>(static_cast<s16>(u)));
            GRef<u64>(o + 0x10) = cx | y0 | kZFar;
            GRef<u64>(o + 0x18) = static_cast<u64>(static_cast<u32>(u + sz)) |
                                  (static_cast<u64>(static_cast<u32>(t + sz)) << 16);
            GRef<u64>(o + 0x20) = static_cast<u64>(static_cast<u32>(cx + static_cast<u32>(sw))) | y1 | kZFar;
            o += 0x28;
            count += 5;
            cx = (cx - static_cast<u32>(adv)) & 0xFFFF;
            if (!(++i < nd)) break;
            digit = static_cast<u32>(v % 10) & 0xFF;
            if (!pad && v == 0) break;
        }
    }
    CloseSpriteStrip(p, count);
    return total;
}

// EE: 0x0015FE20
// Line from (x0, y0) to (x1, y1) in bucket 0 (PRIM line, white, Z 0xFFFFFF).
s32 func_0015FE20(u32 list, s16 x0, s16 y0, s16 x1, s16 y1) {
    const u32 p = AllocBucketPacket(list, 0, 3, 0, 0x50000003);
    Put64(p, 1, 5);
    Put64(p, 0, 0x1041400000008002ull);
    GRef<s32>(p + 0x10) = x0;
    GRef<s32>(p + 0x14) = y0;
    GRef<u32>(p + 0x18) = 0xFFFFFF;
    GRef<u32>(p + 0x1C) = 0;
    GRef<s32>(p + 0x20) = x1;
    GRef<s32>(p + 0x24) = y1;
    GRef<u32>(p + 0x28) = 0xFFFFFF;
    GRef<u32>(p + 0x2C) = 0;
    return 0;
}

// EE: 0x0015FEE0
// DrawHudSprite: texture cell (u, v, w, h) at (x, y) in bucket `index`; in the 16:9 layout (or
// flag 0x100) the cell is inset and enlarged by 16 and the screen width is 3/4 of w.
s32 DrawHudSprite(u32 list, s32 index, u16 x, u16 y, u16 u, u16 v, u16 w, u16 h, u32 flags) {
    u32 sx = x, sy = y, su = u, sv = v, tw = w, sh = h;
    u32 dw = tw;
    if (Wide()) {
        flags |= 0x100;
        const s32 t = static_cast<s32>(tw) * 3;
        dw = static_cast<u32>((t < 0 ? t + 3 : t) >> 2) & 0xFFFF;
    }
    if ((flags & 0x100u) != 0) {
        sx = (sx - 0x10) & 0xFFFF;
        sy = (sy - 0x10) & 0xFFFF;
        dw = (dw + 0x10) & 0xFFFF;
        tw = (tw + 0x10) & 0xFFFF;
        sh = (sh + 0x10) & 0xFFFF;
        su = (su - 8) & 0xFFF;
        sv = (sv - 8) & 0xFFF;
    } else {
        su = (su + 8) & 0xFFFF;
        sv = (sv + 8) & 0xFFFF;
    }
    const u32 p = AllocBucketPacket(list, index, 4, 0, 0x50000004);
    Put64(p, 1, 0x53530);
    Put64(p, 0, 0x5400000000008001ull);
    Put64(p, 3, su | static_cast<u64>(sv) << 16);
    Put64(p, 2, 0x156);
    Put64(p, 4, sx | static_cast<u64>(sy) << 16 | kZFar);
    Put64(p, 5, (static_cast<u64>(su) + tw) | (static_cast<u64>(sv) + sh) << 16);
    Put64(p, 6, (static_cast<u64>(sx) + dw) | (static_cast<u64>(sy) + sh) << 16 | kZFar);
    Put64(p, 7, 0);
    return 0;
}

// EE: 0x00160098
// HUD sprite without the inset option: texel edges +8, screen width 3/4 of w in the 16:9 layout.
s32 func_00160098(u32 list, s32 index, u16 x, u16 y, u16 u, u16 v, u16 w, u16 h) {
    const u32 tw = w;
    u32 dw = tw;
    if (Wide()) {
        const s32 t = static_cast<s32>(tw) * 3;
        dw = static_cast<u32>((t < 0 ? t + 3 : t) >> 2);
    }
    const u32 su = (static_cast<u32>(u) + 8) & 0xFFFF, sv = (static_cast<u32>(v) + 8) & 0xFFFF;
    const u32 p = AllocBucketPacket(list, index, 4, 0, 0x50000004);
    Put64(p, 1, 0x53530);
    Put64(p, 0, 0x5400000000008001ull);
    Put64(p, 3, su | static_cast<u64>(sv) << 16);
    Put64(p, 2, 0x156);
    Put64(p, 4, static_cast<u64>(x) | static_cast<u64>(y) << 16 | kZFar);
    Put64(p, 5, (static_cast<u64>(su) + tw) | (static_cast<u64>(sv) + h) << 16);
    Put64(p, 6, (static_cast<u64>(x) + dw) | (static_cast<u64>(y) + h) << 16 | kZFar);
    Put64(p, 7, 0);
    return 0;
}

namespace {

// HUD frame globals (DrawHud): projected point 0x00420670 (x, y, z, w), colour 0x00420680..83,
// screen centre 0x00420684 / 0x00420686, size 0x00420688 / 0x0042068A, edges 0x0042068C (left),
// 0x0042068E (top), 0x00420690 (right), 0x00420692 (bottom), nose 0x00420694 / 0x00420696.
constexpr u32 kHud = 0x00420670;
u16 HudU16(u32 off) { return GRef<u16>(kHud + off); }

// Speed for the HUD: |velocity| in the unit of option 0x002A15CC bit 2 (metric: x C(a) x 3600 x
// 60 / 1000 x C(b); else / C(c) between, then x C(d)).
f32 HudSpeed(u32 unit, u32 metric_a, u32 metric_b, u32 imp_a, u32 imp_b, u32 imp_c) {
    if ((GRef<u8>(0x002A15CC) & 4) != 0) {
        const f32 v = GetVectorLength(unit + 0x20);
        return v * F(metric_a) * 3600.0f * 60.0f / 1000.0f * F(metric_b);
    }
    const f32 v = GetVectorLength(unit + 0x20);
    return v * F(imp_a) / F(imp_b) * 3600.0f * 60.0f / 1000.0f * F(imp_c);
}

s32 DigitCount(s32 v, s32 max_digits) {
    if (max_digits >= 5 && v >= 10000) return 5;
    if (v >= 1000) return 4;
    if (v >= 100) return 3;
    return v >= 10 ? 2 : 1;
}

double D64(u32 a) {
    const u64 b = GRef<u64>(a);
    double d;
    std::memcpy(&d, &b, sizeof d);
    return d;
}

}  // namespace

// EE: 0x00160200
// Look-around reference lines (view 0): when the camera angles differ from the aircraft's by at
// least the double C 0x00404BC0 on x or y (wrapped by C 0x0040E7FC..0x0040E810), the 7-point
// line 0x00404B40 (translated by 0x00404BB0) is drawn in the aircraft frame through the camera's
// view-screen matrix, fading out below screen y 0x21C (alpha = HUD alpha x (0x21C - w) / 80).
// fpucheck: 6 adds
void func_00160200(u32 cam, u32 list, u32 unit) {
    guest::GuestFrame fr(0x160);
    const u32 pts = fr.Address(0), tv = fr.Address(0x70), m1 = fr.Address(0x80), m2 = fr.Address(0xC0),
              vs = fr.Address(0x100), out = fr.Address(0x140);
    for (u32 o = 0; o < 0x70; o += 4) GRef<u32>(pts + o) = W(0x00404B40 + o);
    GRef<u64>(tv) = GRef<u64>(0x00404BB0);
    GRef<u64>(tv + 8) = GRef<u64>(0x00404BB8);
    f32 dx = fpu::Sub(F(cam + 0x10), F(unit + 0x10));
    const f32 hi = F(0x0040E7FC), lo = F(0x0040E804);
    if (hi < dx) dx = fpu::Sub(dx, F(0x0040E800));
    if (dx < lo) dx = fpu::Add(dx, F(0x0040E808));
    f32 dy = fpu::Sub(F(cam + 0x14), F(unit + 0x14));
    if (hi < dy) dy = fpu::Sub(dy, F(0x0040E80C));
    if (dy < lo) dy = fpu::Add(dy, F(0x0040E810));
    const double k = D64(0x00404BC0);
    double ax = static_cast<double>(dx);
    if (ax < 0.0) ax = 0.0 - ax;
    if (ax < k) {
        double ay = static_cast<double>(dy);
        if (ay < 0.0) ay = 0.0 - ay;
        if (ay < k) return;
    }
    sceVu0UnitMatrix(m1);
    func_00102D28(m1, m1, unit + 0x10);
    sceVu0UnitMatrix(m2);
    func_00102D28(m2, m2, cam + 0x10);
    sceVu0InversMatrix(m2, m2);
    sceVu0TransMatrix(m2, m2, tv);
    sceVu0ViewScreenMatrix(vs, F(0x0040E818), F(cam + 0x180), F(cam + 0x184), F(cam + 0x178), F(cam + 0x17C),
                           F(cam + 0x190), F(cam + 0x194), F(cam + 0x16C), F(cam + 0x170) * F(0x0040E814));
    sceVu0MulMatrix(m2, vs, m2);
    sceVu0MulMatrix(m1, m2, m1);
    const u32 p = AllocBucketPacket(list, 2, 0xF, 0, 0x5000000F);
    Put64(p, 0, 0x2025400000008007ull);
    Put64(p, 1, 0x51);
    u32 o = p + 0x20;
    for (u32 i = 0; i < 7; ++i) {
        sceVu0RotTransPers(out, m1, pts + 16 * i, 1);
        GRef<u32>(o - 0x10) = GRef<u8>(kHud + 0x10);
        GRef<u32>(o - 0xC) = GRef<u8>(kHud + 0x11);
        GRef<u32>(o - 8) = GRef<u8>(kHud + 0x12);
        const s32 amax = GRef<u8>(kHud + 0x13);
        s32 a = amax * (0x21C - GRef<s32>(out + 0xC)) / 0x50;
        if (!(-1 < a)) a = 0;
        if (amax < a) a = amax;
        GRef<s32>(o - 4) = a;
        GRef<u32>(o) = W(out);
        GRef<u32>(o + 8) = 0xFFFFFD;
        GRef<u32>(o + 4) = W(out + 4);
        GRef<u32>(o + 0xC) = 0;
        o += 0x20;
    }
    func_0015F2B0(list, 2, GRef<s8>(kHud + 0x10), GRef<s8>(kHud + 0x11), GRef<s8>(kHud + 0x12), GRef<s8>(kHud + 0x13));
}

// EE: 0x00160598
// Flight-path marker: the velocity direction projected (mode 1) into 0x00420670; drawn when it is
// inside the 0x7000..0x9000 x 0x7200..0x8E00 window and in front.
void func_00160598(u32 cam, u32 list, u32 unit) {
    guest::GuestFrame fr(0x10);
    sceVu0AddVector(fr.Address(0), unit + 0x20, unit);  // result unused
    sceVu0RotTransPers(kHud, cam + 0xE0, unit + 0x20, 1);
    const s32 x = GRef<s32>(kHud);
    if (static_cast<u32>(x - 0x7000) >= 0x2001) return;
    const s32 y = GRef<s32>(kHud + 4);
    if (y < 0x7200 || 0x8E00 < y) return;
    if (GRef<s32>(kHud + 0xC) <= 0) return;
    const u32 sx = static_cast<u32>(x - (Wide() ? 0x90 : 0xC0)) & 0xFFFF;
    DrawHudSprite(list, 2, static_cast<u16>(sx), static_cast<u16>((HudU16(4) - 0x78) & 0xFFFF), 0x8C0, 0x9C0,
                  0x180, 0xF0, 0x100);
}

// EE: 0x00160698
// Nose marker at the projected nose position 0x00420694.
void func_00160698(u32 list) {
    const u32 x = static_cast<u32>(HudU16(0x24) - (Wide() ? 0x90 : 0xC0)) & 0xFFFF;
    DrawHudSprite(list, 2, static_cast<u16>(x), HudU16(0x26), 0x8C0, 0x900, 0x180, 0x80, 0x100);
}

// EE: 0x00160710
// Two-player speed and altitude readouts at the lower left of the player's screen.
void func_00160710(u32 list, u32 unit) {
    const s32 dx_speed = Wide() ? 0x2B0 : 0x390;
    const s32 dx_alt = Wide() ? 0x1D0 : 0x260;
    const s32 speed = fpu::FloatToInt(HudSpeed(unit, 0x0040E81C, 0x0040E820, 0x0040E824, 0x0040E828, 0x0040E82C));
    u32 sx = (HudU16(0x1C) + 0x100u) & 0xFFFF;
    u32 sy = (HudU16(0x22) - 0x5C0u) & 0xFFFF;
    DrawHudSprite(list, 2, static_cast<u16>(sx), static_cast<u16>(sy), 0x700, 0x600, 0x340, 0x100, 0x100);
    func_0015FA18(list, 2, speed, static_cast<u16>(sx + static_cast<u32>(dx_speed)), static_cast<u16>(sy + 0x30), 0x109,
                  DigitCount(speed, 4));
    f32 a;
    if ((GRef<u8>(0x002A15CC) & 4) != 0)
        a = -F(unit + 4) * F(0x0040E830);
    else
        a = -F(unit + 4) * F(0x0040E834) / F(0x0040E838);
    const s32 alt = fpu::FloatToInt(a);
    sx = (HudU16(0x1C) + 0x100u) & 0xFFFF;
    sy = (HudU16(0x22) - 0x480u) & 0xFFFF;
    DrawHudSprite(list, 2, static_cast<u16>(sx), static_cast<u16>(sy), 0xA60, 0x600, 0x210, 0x100, 0x100);
    func_0015FA18(list, 2, alt, static_cast<u16>(sx + static_cast<u32>(dx_alt)), static_cast<u16>(sy + 0x30), 0x109,
                  DigitCount(alt, 5));
}

// EE: 0x00160E78
// Third-person speed readout (left of the frame centre).
void func_00160E78(u32 list, u32 unit) {
    const u32 x0 = static_cast<u32>(HudU16(0x14) - (Wide() ? 0x5D0 : 0x7C0));
    const u32 sx = x0 - 0x340, sy = HudU16(0x16) - 0x3C0u;
    DrawHudSprite(list, 2, static_cast<u16>(sx), static_cast<u16>(sy), 0x700, 0x600, 0x340, 0x100, 0);
    const f32 v = HudSpeed(unit, 0x0040E850, 0x0040E854, 0x0040E858, 0x0040E85C, 0x0040E860);
    func_0015FA18(list, 1, fpu::FloatToInt(v), static_cast<u16>(sx), static_cast<u16>(sy + 0x180), 9, 4);
}

namespace {
s8 HudR() { return GRef<s8>(kHud + 0x10); }
s8 HudG() { return GRef<s8>(kHud + 0x11); }
s8 HudB() { return GRef<s8>(kHud + 0x12); }
s8 HudA() { return GRef<s8>(kHud + 0x13); }
void HudColour(u32 list, s32 index) { func_0015F2B0(list, index, HudR(), HudG(), HudB(), HudA()); }
}  // namespace

// EE: 0x001609D8
// Speed tape left of the nose (layout table 0x002B72F8, 16:9 0x002B7308): single player also
// draws the speed box and value; a vertical line with ticks every 256 units of y (one per 10 speed
// units, scrolled by the fraction), labels every 10 on the visible ticks (the top and bottom ones
// faded by the fraction).
// fpucheck: 1 add
void func_001609D8(u32 list, u32 unit) {
    const u32 tab = Wide() ? 0x002B7308u : 0x002B72F8u;
    const auto T = [tab](u32 i) { return static_cast<u32>(GRef<u16>(tab + 2 * i)); };
    u32 x = static_cast<u32>(HudU16(0x24) - (Wide() ? 0x630 : 0x840));
    const u32 y = HudU16(0x26) - 0x190u;
    const f32 speed = HudSpeed(unit, 0x0040E83C, 0x0040E840, 0x0040E844, 0x0040E848, 0x0040E84C);
    const u32 ybox = y + 0x160;
    if (Players() == 1) {
        DrawHudSprite(list, 2, static_cast<u16>(x - T(0)), static_cast<u16>(y), 0x700, 0x600, 0x340, 0x100, 0x100);
        func_0015FA18(list, 2, fpu::FloatToInt(speed), static_cast<u16>(x - T(4)), static_cast<u16>(ybox), 0x109, 4);
    }
    DrawHudSprite(list, 2, static_cast<u16>(T(2) + x), static_cast<u16>(ybox), 0xE60, 0xF20, 0x80, 0x80, 0x100);
    x = static_cast<u32>(HudU16(0x24) - (Wide() ? 0x570 : 0x740));
    const u32 ny = HudU16(0x26);
    const u32 top = ny - 0x740;
    func_0015FE20(list, static_cast<s16>(x), static_cast<s16>(top), static_cast<s16>(x), static_cast<s16>(ny + 0x750));
    const s32 tens = fpu::FloatToInt(speed / 10.0f);
    const s32 frac = fpu::FloatToInt(fpu::Sub(speed, fpu::IntToFloat(tens) * 10.0f) / 10.0f * 16.0f * 16.0f);
    u32 ty = ny - 0x700 + static_cast<u32>(frac);
    if (!(static_cast<s32>(ty) < static_cast<s32>(ny + 0x740))) return;
    s32 label = tens + 7;
    for (s32 i = 0;;) {
        func_0015FE20(list, static_cast<s16>(x - T(2)), static_cast<s16>(ty), static_cast<s16>(x), static_cast<s16>(ty));
        if ((i < 5 || static_cast<u32>(i - 8) < 3) && label % 10 == 0) {
            if (i == 4 || i == 10)
                func_0015F2B0(list, 2, HudR(), HudG(), HudB(),
                              static_cast<s8>(static_cast<s32>(static_cast<u32>(GRef<u8>(kHud + 0x13) * (0xFF - frac)) << 16) >> 24));
            if (i == 8)
                func_0015F2B0(list, 2, HudR(), HudG(), HudB(),
                              static_cast<s8>(static_cast<s32>(static_cast<u32>(GRef<u8>(kHud + 0x13) * frac) << 16) >> 24));
            func_0015FA18(list, 2, (tens - (i - 7)) * 10, static_cast<u16>(x - T(6)), static_cast<u16>(ty - 0x40), 0x107, 4);
            if (i == 4 || i == 8 || i == 10) HudColour(list, 2);
        }
        ++i;
        ty += 0x100;
        --label;
        if (!(i < 0xE)) break;
        if (!(static_cast<s32>(ty) < static_cast<s32>(top + 0xE80))) break;
    }
}

// EE: 0x00160FF8
// Throttle ladder right of the nose: a vertical line with 5 ticks, the throttle marker (state
// +0x240 x 3456 up from the bottom) in a lighter colour, and the afterburner sprite when the
// throttle is at least C 0x0040E864 with +0x244 > 0 (not for aircraft types 5 and 12).
void func_00160FF8(u32 list, u32 unit, u32 state) {
    const u32 x = static_cast<u32>(HudU16(0x24) - (Wide() ? 0x528 : 0x6E0));
    const u32 ny = HudU16(0x26);
    const u32 bottom = ny + 0x6D0, top = ny - 0x6C0;
    func_0015FE20(list, static_cast<s16>(x), static_cast<s16>(top), static_cast<s16>(x), static_cast<s16>(bottom));
    for (u32 k = 0; k < 5; ++k) {
        const s16 ty = static_cast<s16>(top + k * 0x360);
        func_0015FE20(list, static_cast<s16>(x + 0x60), ty, static_cast<s16>(x), ty);
    }
    const s32 v = fpu::FloatToInt(F(state + 0x240) * 3456.0f);
    const u32 off = static_cast<u32>(0xD80 - v);
    func_0015F2B0(list, 0, static_cast<s8>(GRef<u8>(kHud + 0x10) + 0x40), static_cast<s8>(GRef<u8>(kHud + 0x11) + 0x40),
                  static_cast<s8>(GRef<u8>(kHud + 0x12) + 0x40), HudA());
    func_0015FE20(list, static_cast<s16>(x + 0x40), static_cast<s16>(top + off + 0x10), static_cast<s16>(x + 0x40),
                  static_cast<s16>(bottom));
    HudColour(list, 0);
    const u8 type = GRef<u8>(unit + 0x69);
    if (type == 5 || type == 0xC) return;
    if (!(0.0f < F(state + 0x244))) return;
    if (!(F(0x0040E864) <= F(state + 0x240))) return;
    DrawHudSprite(list, 2, static_cast<u16>(x), static_cast<u16>(top - 0x100), 0x460, 0x9E0, 0x1A0, 0xC0, 0x100);
}

// EE: 0x00161720
// Third-person altitude readout (right of the frame centre); up to 5 digits.
void func_00161720(u32 list, u32 unit) {
    const u32 x = static_cast<u32>(HudU16(0x14) + (Wide() ? 0x5D0 : 0x7C0));
    const u32 y = HudU16(0x16) - 0x3C0u;
    DrawHudSprite(list, 2, static_cast<u16>(x), static_cast<u16>(y), 0xA60, 0x600, 0x210, 0x100, 0);
    f32 a;
    if ((GRef<u8>(0x002A15CC) & 4) != 0)
        a = -F(unit + 4) * F(0x0040E878);
    else
        a = -F(unit + 4) * F(0x0040E87C) / F(0x0040E880);
    s32 nd;
    if (F(0x0040E884) <= a)
        nd = 5;
    else if (1000.0f <= a)
        nd = 4;
    else if (100.0f <= a)
        nd = 3;
    else
        nd = 10.0f <= a ? 2 : 1;
    func_0015FA18(list, 2, fpu::FloatToInt(a), static_cast<u16>(x + 0xC0), static_cast<u16>(y + 0x180), 9, nd);
}

// EE: 0x00161250
// Altitude tape right of the nose (layout 0x002B7318, 16:9 0x002B7328): marker, single-player
// altitude box and value, a vertical line with ticks every 256 y per 50 altitude units (scrolled by
// the fraction), labels every 500 on the visible ticks (top and bottom faded).
// fpucheck: 1 add
void func_00161250(u32 list, u32 unit) {
    const u32 tab = Wide() ? 0x002B7328u : 0x002B7318u;
    const auto T = [tab](u32 i) { return static_cast<u32>(GRef<u16>(tab + 2 * i)); };
    u32 x = static_cast<u32>(HudU16(0x24) + (Wide() ? 0x640 : 0x850));
    const u32 y0 = HudU16(0x26) - 0x190u;
    f32 alt;
    if ((GRef<u8>(0x002A15CC) & 4) != 0)
        alt = -F(unit + 4) * F(0x0040E868);
    else
        alt = -F(unit + 4) * F(0x0040E86C) / F(0x0040E870);
    const u32 ybox = y0 + 0x160;
    DrawHudSprite(list, 2, static_cast<u16>(x - T(0)), static_cast<u16>(ybox), 0xF00, 0xF20, 0x80, 0x80, 0x100);
    if (Players() == 1) {
        DrawHudSprite(list, 2, static_cast<u16>(x), static_cast<u16>(y0), 0xA60, 0x600, 0x210, 0x100, 0x100);
        s32 nd;
        if (F(0x0040E874) <= alt)
            nd = 5;
        else if (1000.0f <= alt)
            nd = 4;
        else if (100.0f <= alt)
            nd = 3;
        else
            nd = 10.0f <= alt ? 2 : 1;
        func_0015FA18(list, 2, fpu::FloatToInt(alt), static_cast<u16>(x), static_cast<u16>(ybox), 0x109, nd);
    }
    x = static_cast<u32>(HudU16(0x24) + (Wide() ? 0x570 : 0x740));
    const u32 ny = HudU16(0x26);
    const u32 top = ny - 0x740;
    func_0015FE20(list, static_cast<s16>(x), static_cast<s16>(top), static_cast<s16>(x), static_cast<s16>(ny + 0x750));
    const s32 q = fpu::FloatToInt(alt / 50.0f);
    const s32 frac = fpu::FloatToInt(fpu::Sub(alt, fpu::IntToFloat(q) * 50.0f) / 50.0f * 16.0f * 16.0f);
    u32 ty = ny - 0x700 + static_cast<u32>(frac);
    if (!(static_cast<s32>(ty) < static_cast<s32>(ny + 0x740))) return;
    s32 acc = frac;
    s32 label = q + 7;
    for (s32 i = 0;;) {
        func_0015FE20(list, static_cast<s16>(T(2) + x), static_cast<s16>(ty), static_cast<s16>(x), static_cast<s16>(ty));
        if ((i < 5 || static_cast<u32>(i - 8) < 3) && label % 10 == 0) {
            const s32 v = label * 50;
            const s32 nd = v >= 10000 ? 5 : v >= 1000 ? 4 : v >= 100 ? 3 : v >= 10 ? 2 : 1;
            if (i == 4 || i == 10)
                func_0015F2B0(list, 2, HudR(), HudG(), HudB(),
                              static_cast<s8>(static_cast<s32>(static_cast<u32>(GRef<u8>(kHud + 0x13) * (0xFF - frac)) << 16) >> 24));
            if (i == 8)
                func_0015F2B0(list, 2, HudR(), HudG(), HudB(),
                              static_cast<s8>(static_cast<s32>(static_cast<u32>(GRef<u8>(kHud + 0x13) * frac) << 16) >> 24));
            func_0015FA18(list, 2, v, static_cast<u16>(T(4) + x), static_cast<u16>(ty - 0x40), 0x107, nd);
            if (i == 4 || i == 8 || i == 10) HudColour(list, 2);
        }
        ++i;
        --label;
        acc += 0x100;
        if (!(i < 0xE)) break;
        ty = top + 0x40 + static_cast<u32>(acc);
        if (!(static_cast<s32>(ty) < static_cast<s32>(top + 0xE80))) break;
    }
}

// EE: 0x00161890
// Right ladder: a bracket line; for the player in control (+0x54 == 0) the fuel level (+0x1B4 of
// its state, x 0xD80 / 0x8000), the target distance marker (proximity table, clamped) and, with
// the gun-lock flag (+0x188 bit 0x100), the in-range sprite when the angle to the target is inside
// C 0x0040E8A0..0x0040E8A4 on both axes (wrapped by C 0x0040E888..0x0040E89C).
// fpucheck: 4 adds
void func_00161890(u32 list, u32 unit) {
    const u32 x = static_cast<u32>(HudU16(0x24) + (Wide() ? 0x528 : 0x6E0));
    const u32 ny = HudU16(0x26);
    const s16 top = static_cast<s16>(ny - 0x6C0), bottom = static_cast<s16>(ny + 0x6D0);
    const u32 low = ny + 0x6C0;
    func_0015FE20(list, static_cast<s16>(x), top, static_cast<s16>(x), bottom);
    func_0015FE20(list, static_cast<s16>(x - 0x60), top, static_cast<s16>(x), top);
    func_0015FE20(list, static_cast<s16>(x - 0x60), static_cast<s16>(low), static_cast<s16>(x), static_cast<s16>(low));
    if (GRef<u8>(unit + 0x54) != 0) return;
    const u32 st = W(unit + 0x44);
    const s32 lvl = (fpu::FloatToInt(F(st + 0x1B4)) * 0xD80) >> 15;
    func_0015FE20(list, static_cast<s16>(x - 0x10), static_cast<s16>(low - static_cast<u32>(lvl)), static_cast<s16>(x - 0x10),
                  bottom);
    const u8 tgt = GRef<u8>(unit + 0x62);
    if (tgt != 0xFF) {
        const u32 prox = W(0x00410648 + 4u * GRef<u8>(unit + 0x55));
        s32 v = (fpu::FloatToInt(F(prox + 12u * tgt + 4)) * 0xD80) >> 15;
        if (!(v < 0xD81)) v = 0xD80;
        DrawHudSprite(list, 2, static_cast<u16>(x - 0x80), static_cast<u16>(ny + 0x660 - static_cast<u32>(v)), 0xE60, 0xF20,
                      0x80, 0x80, 0x100);
    }
    if ((W(st + 0x188) & 0x100u) == 0) return;
    guest::GuestFrame fr(0x10);
    const u32 d = fr.Address(0);
    sceVu0SubVector(d, Unit(GRef<u8>(unit + 0x62)) + 0x10, unit + 0x10);
    f32 dx = F(d), dy = F(d + 4);
    const f32 hi = F(0x0040E888), lo = F(0x0040E890);
    if (hi < dx) {
        dx = fpu::Sub(dx, F(0x0040E88C));
        GRef<f32>(d) = dx;
    }
    if (dx < lo) {
        dx = fpu::Add(dx, F(0x0040E894));
        GRef<f32>(d) = dx;
    }
    if (hi < dy) {
        dy = fpu::Sub(dy, F(0x0040E898));
        GRef<f32>(d + 4) = dy;
    }
    if (dy < lo) {
        dy = fpu::Add(dy, F(0x0040E89C));
        GRef<f32>(d + 4) = dy;
    }
    const f32 a = F(0x0040E8A0), b = F(0x0040E8A4);
    if (!(a < dx) || !(dx < b) || !(a < dy) || !(dy < b)) return;
    DrawHudSprite(list, 2, static_cast<u16>(x - 0x280), static_cast<u16>(ny + 0x700), 0x620, 0x9E0, 0x280, 0xC0, 0x100);
}

// EE: 0x00161BB0
// Heading tape above the nose: 5 ticks 11.25 degrees apart scrolled by the heading (+0x14 x 180 /
// C 0x0040E8A8), cardinal letters at 0/90/180/270 and two-letter labels at 45/135/225/315
// (C 0x0040E8AC), then the baseline.
// fpucheck: 4 adds
void func_00161BB0(u32 list, u32 unit) {
    s32 base = 0;
    f32 h = F(unit + 0x14) * 180.0f / F(0x0040E8A8);
    while (5.625f < h) {
        ++base;
        h = fpu::Sub(h, 11.25f);
    }
    while (h < -5.625f) {
        h = fpu::Add(h, 11.25f);
        --base;
    }
    h = -h;
    const auto letter = [list](u32 x, u32 y, u16 u) {
        DrawHudSprite(list, 2, static_cast<u16>(x), static_cast<u16>(y), u, 0x440, 0x80, 0x80, 0x100);
    };
    for (s32 i = -2; i < 3; ++i) {
        const s32 v = fpu::FloatToInt(fpu::Add(h / 11.25f, fpu::IntToFloat(i)) * 416.0f);
        const s16 o = static_cast<s16>(v);
        if (static_cast<u16>(o + 0x340) >= 0x681) continue;
        const s16 sx = static_cast<s16>(static_cast<u16>(o) + HudU16(0x24));
        const u32 ny = HudU16(0x26);
        func_0015FE20(list, sx, static_cast<s16>(ny - 0x840), sx, static_cast<s16>(ny + 0xF800));
        f32 hdg = fpu::IntToFloat(base + i) * 11.25f;
        const u32 y = (HudU16(0x26) - 0x8E0u) & 0xFFFF;
        if (hdg < 0.0f) hdg = fpu::Add(hdg, 360.0f);
        const u32 x1 = static_cast<u32>(sx - 0x40), x2a = static_cast<u32>(sx - 0x90), x2b = static_cast<u32>(sx + 0x10);
        if (hdg == 0.0f) {
            letter(x1, y, 0x640);
        } else if (hdg == 90.0f) {
            letter(x1, y, 0x780);
        } else if (hdg == 180.0f) {
            letter(x1, y, 0x6E0);
        } else if (hdg == 270.0f) {
            letter(x1, y, 0x820);
        } else if (hdg == 45.0f) {
            letter(x2a, y, 0x640);
            letter(x2b, y, 0x780);
        } else if (hdg == 135.0f) {
            letter(x2a, y, 0x6E0);
            letter(x2b, y, 0x780);
        } else if (hdg == 225.0f) {
            letter(x2a, y, 0x6E0);
            letter(x2b, y, 0x820);
        } else if (hdg == F(0x0040E8AC)) {
            letter(x2a, y, 0x640);
            letter(x2b, y, 0x820);
        }
    }
    const s16 by = static_cast<s16>(HudU16(0x26) + 0xF800);
    func_0015FE20(list, static_cast<s16>(HudU16(0x24) - 0x340), by, static_cast<s16>(HudU16(0x24) + 0x350), by);
}

// EE: 0x00162030
// Pitch ladder: 5 bars 5 degrees apart around the pitch (+0x10 x 180 / C 0x0040E8B0, wrapped into
// +-2.5 with the base angle), placed by tan(pitch) x focal (cam +0x168 / +0x164 x height +0x18E)
// and rolled by +0x18. Bars below the threshold D_004109E8 are dashed (a UV-textured strip of 4
// sprites plus 2 end ticks), positive bars get end ticks downward, every bar its two halves, and
// both ends the angle label (|base - 5i| folded to 0..90). Horizontal scale C 0x0040E8B8 (16:9:
// C 0x0040E8B4).
// fpucheck: 42 adds
void func_00162030(u32 cam, u32 list, u32 unit) {
    const f32 roll = F(unit + 0x18);
    f32 base = 0.0f;
    f32 p = F(unit + 0x10) * 180.0f / F(0x0040E8B0);
    while (2.5f <= p) {
        p = fpu::Sub(p, 5.0f);
        base = fpu::Add(base, 5.0f);
    }
    while (p < -2.5f) {
        p = fpu::Add(p, 5.0f);
        base = fpu::Sub(base, 5.0f);
    }
    const bool wide169 = Wide();
    const s32 lab_dx = wide169 ? 0x60 : 0x90;
    const f32 hs = wide169 ? F(0x0040E8B4) : F(0x0040E8B8);
    const auto nx = [] { return static_cast<s32>(HudU16(0x24)); };
    const auto ny = [] { return static_cast<s32>(HudU16(0x26)); };
    const auto I = [](f32 v) { return fpu::FloatToInt(v); };
    const auto line = [list](s32 x0, s32 y0, s32 x1, s32 y1) {
        func_0015FE20(list, static_cast<s16>(x0), static_cast<s16>(y0), static_cast<s16>(x1), static_cast<s16>(y1));
    };
    for (s32 i = -2; i < 3; ++i) {
        const f32 fi = fpu::IntToFloat(i);
        const f32 a = fpu::Add(fi * 5.0f, p) * F(0x0040E8BC) / 180.0f;
        const f32 t = ee::sinf(a);
        f32 r = t / ee::cosf(a);
        r = r * F(cam + 0x168) / F(cam + 0x164) * fpu::IntToFloat(GRef<s16>(cam + 0x18E));
        const f32 sr = ee::sinf(roll);
        const f32 cr = ee::cosf(roll);
        const f32 ang = fpu::Add(-fi * 5.0f, base);
        const f32 f26 = r * sr * 16.0f, f27 = r * cr * 16.0f;
        const f32 f22 = sr * 80.0f * 16.0f, f20 = cr * 80.0f * 16.0f;
        const f32 s18 = sr * 24.0f * 16.0f, f31 = cr * 24.0f * 16.0f;
        if (ang < F(0x004109E8)) {
            const f32 rm = fpu::Sub(r, 8.0f);
            const f32 f29 = rm * cr * 16.0f, f28 = rm * sr * 16.0f;
            const u32 q = AllocBucketPacket(list, 2, 9, 0, 0x50000009);
            Put64(q, 0, 0x40E8C00000008002ull);
            Put64(q, 1, 0x5353);
            const s32 t4 = I(fpu::Add(f26, f20) * hs);
            const s32 t5 = I(fpu::Sub(f27, f22));
            const s32 a2 = I(fpu::Add(f26, f31) * hs);
            const s32 a0 = I(fpu::Sub(f27, s18));
            const s32 s0 = I(fpu::Sub(f26, f20) * hs);
            const s32 s1 = I(fpu::Add(f27, f22));
            const s32 b2 = I(fpu::Sub(f26, f31) * hs);
            const s32 t1 = I(fpu::Add(f27, s18));
            const auto uv = [](u32 o, u32 u) {
                GRef<u32>(o) = u;
                GRef<u32>(o + 4) = 0x908;
                GRef<u32>(o + 8) = 0;
                GRef<u32>(o + 0xC) = 0;
            };
            const auto xyz = [](u32 o, s32 dx, s32 dy) {
                GRef<s32>(o) = static_cast<s32>(HudU16(0x24)) + static_cast<s16>(dx);
                GRef<s32>(o + 4) = static_cast<s32>(HudU16(0x26)) + static_cast<s16>(dy);
                GRef<u32>(o + 8) = 0xFFFFFD;
                GRef<u32>(o + 0xC) = 0;
            };
            uv(q + 0x10, 0xA80);
            xyz(q + 0x20, t4, t5);
            uv(q + 0x30, 0xE00);
            xyz(q + 0x40, a2, a0);
            uv(q + 0x50, 0xA80);
            xyz(q + 0x60, s0, s1);
            uv(q + 0x70, 0xE10);
            xyz(q + 0x80, b2, t1);
            line(nx() + t4, ny() + t5, nx() + I(fpu::Add(f28, f20) * hs), ny() + I(fpu::Sub(f29, f22)));
            line(nx() + s0, ny() + s1, nx() + I(fpu::Sub(f28, f20) * hs), ny() + I(fpu::Add(f29, f22)));
        } else {
            f32 f25, f21, f23, f24;
            if (0.0f < ang) {
                f24 = fpu::Sub(f27, f22);
                f23 = fpu::Add(f26, f20);
                const f32 rp = fpu::Add(r, 8.0f);
                f25 = fpu::Sub(f26, f20);
                const f32 f28 = rp * sr * 16.0f, f29 = rp * cr * 16.0f;
                f21 = fpu::Add(f27, f22);
                const s32 v1 = I(fpu::Add(f28, f20) * hs);
                const s32 v0 = I(fpu::Sub(f29, f22));
                line(nx() + I(f23 * hs), ny() + I(f24), nx() + v1, ny() + v0);
                const s32 w0 = I(fpu::Sub(f28, f20) * hs);
                const s32 w1 = I(fpu::Add(f29, f22));
                line(nx() + I(f25 * hs), ny() + I(f21), nx() + w0, ny() + w1);
            } else {
                f25 = fpu::Sub(f26, f20);
                f21 = fpu::Add(f27, f22);
                f23 = fpu::Add(f26, f20);
                f24 = fpu::Sub(f27, f22);
            }
            const s32 e0 = I(fpu::Add(f26, f31) * hs);
            const s32 e1 = I(fpu::Sub(f27, s18));
            line(nx() + I(f23 * hs), ny() + I(f24), nx() + e0, ny() + e1);
            const s32 g0 = I(fpu::Sub(f26, f31) * hs);
            const s32 g1 = I(fpu::Add(f27, s18));
            line(nx() + I(f25 * hs), ny() + I(f21), nx() + g0, ny() + g1);
        }
        const f32 len = fpu::Add(80.0f, 14.0f);
        s32 deg = I(fpu::Sub(base, fi * 5.0f));
        if (!(-1 < deg)) deg = -deg;
        if (!(-0x5B < deg)) deg += 0xB4;
        if (!(deg < 0x5B)) deg = 0xB4 - deg;
        const f32 lc = len * cr * 16.0f, ls = len * sr * 16.0f;
        const s32 ax = I(fpu::Add(f26, lc) * hs), ay = I(fpu::Sub(f27, ls));
        func_0015FA18(list, 2, deg, static_cast<u16>(nx() + (ax - lab_dx)), static_cast<u16>(ny() + (ay - 0x40)), 0x107, 2);
        const f32 bxf = fpu::Sub(f26, lc), byf = fpu::Add(f27, ls);
        const s32 bx = I(bxf * hs), by = I(byf);
        func_0015FA18(list, 2, deg, static_cast<u16>(nx() + (bx - lab_dx)), static_cast<u16>(ny() + (by - 0x40)), 0x107, 2);
    }
}

// EE: 0x00162958
// Score panel (bucket 1): label, the player's score (+0x5C, 4 digits zero-padded), and the rank
// letters for the score thresholds of the mission rules (+0x38 / +0x3C / +0x40 / +0x18; campaign and
// free mission only, unless rules flag 0x400). Missions 0x18-0x19 show rules +0x44, two-player
// mode the target score (game +0x2E), no rank the rules +0x18; tutorial, mission 0x20 or rules
// flags 3 show 4 dashes. Layouts 0x00404BD8 / 16:9 0x00404BC8.
void func_00162958(u32 list, u32 unit) {
    const u32 tab = Wide() ? 0x00404BC8u : 0x00404BD8u;
    const auto L = [tab](u32 i) { return static_cast<u32>(GRef<u16>(tab + 2 * i)); };
    u32 y, x;
    if (Players() == 2) {
        y = HudU16(0x1E) + 0x2C0u;
        x = HudU16(0x1C) + 0x100u;
    } else {
        y = HudU16(0x1E) + 0x180u;
        x = HudU16(0x1C) + 0x140u;
    }
    DrawHudSprite(list, 1, static_cast<u16>(x), static_cast<u16>(y), 0, 0x600, 0x220, 0x100, 0);
    const u32 vy = y + 0x30;
    func_0015FA18(list, 1, GRef<u16>(unit + 0x5C), static_cast<u16>(L(0) + x), static_cast<u16>(vy), 10, 0x84);
    DrawHudSprite(list, 1, static_cast<u16>(L(1) + x), static_cast<u16>(vy), 0xA70, 0x380, 0xA0, 0xA0, 0);
    const u32 R = Rules();
    s32 rank = -1;
    const u8 kind = GRef<u8>(Scene() + 0x1653);
    if ((kind < 2 || static_cast<s8>(kind) == 3) && (~W(R + 0x10) & 0x400u) != 0) {
        const u32 sc = GRef<u16>(unit + 0x5C);
        if (!(sc < W(R + 0x38)))
            rank = 0;
        else if (!(sc < W(R + 0x3C)))
            rank = 1;
        else if (!(sc < W(R + 0x40)))
            rank = 2;
        else if (!(sc < W(R + 0x18)))
            rank = 3;
    }
    if (Game(0x29) == 3 || Game(0x27) == 0x20)
        rank = -2;
    else if (rank == -1 && (W(R + 0x10) & 3u) != 0)
        rank = -2;
    if (static_cast<u32>(Game(0x27) - 0x18) < 2) {
        func_0015FA18(list, 1, GRef<s32>(R + 0x44), static_cast<u16>(L(2) + x), static_cast<u16>(vy), 10, 0x84);
        return;
    }
    if (GRef<s8>(Scene() + 0x1653) == 2) {
        func_0015FA18(list, 1, GRef<u16>(g_GameState().addr + 0x2E), static_cast<u16>(L(2) + x), static_cast<u16>(vy), 10, 0x84);
        return;
    }
    if (rank == -1) {
        func_0015FA18(list, 1, GRef<s32>(R + 0x18), static_cast<u16>(L(2) + x), static_cast<u16>(vy), 10, 0x84);
        return;
    }
    if (rank == -2) {
        const u32 step = Wide() ? 0x90u : 0xC0u;
        u32 dx = x + static_cast<u32>(static_cast<s32>(static_cast<s16>(L(2))));
        for (u32 k = 0; k < 4; ++k) {
            DrawHudSprite(list, 0, static_cast<u16>(dx), static_cast<u16>(y + 0x40), 0xB40, 0x380, 0xA0, 0xA0, 0);
            dx += step;
        }
        return;
    }
    u16 u;
    switch (rank) {
        case 0: u = 0x240; break;
        case 2: u = 0xC0; break;
        case 3: u = 0x180; break;
        default: u = 0; break;
    }
    DrawHudSprite(list, 1, static_cast<u16>(L(2) + x), static_cast<u16>(vy), 0x300, 0xE00, 0xA0, 0xA0, 0);
    DrawHudSprite(list, 1, static_cast<u16>(L(3) + x), static_cast<u16>(vy), u, 0xE00, 0xA0, 0xA0, 0);
    DrawHudSprite(list, 1, static_cast<u16>(L(4) + x), static_cast<u16>(vy), 0x3C0, 0xE00, 0x90, 0xA0, 0);
}

// EE: 0x00162D98
// Target panel (bucket 1): label and, with a target, its hit points (+0x60, 1-3 digits) and the
// percent sign after them (layout 0x004109F0 / 16:9 0x004109F8).
void func_00162D98(u32 list, u32 unit) {
    const u32 tab = Wide() ? 0x004109F8u : 0x004109F0u;
    u32 y, x;
    if (Players() == 2) {
        y = HudU16(0x1E) + 0x400u;
        x = HudU16(0x1C) + 0x100u;
    } else {
        y = HudU16(0x1E) + 0x2C0u;
        x = HudU16(0x1C) + 0x140u;
    }
    DrawHudSprite(list, 1, static_cast<u16>(x), static_cast<u16>(y), 0x240, 0x600, 0x220, 0x100, 0);
    const u8 tgt = GRef<u8>(unit + 0x62);
    if (tgt == 0xFF) return;
    const s16 hp = GRef<s16>(Unit(tgt) + 0x60);
    const s32 nd = hp >= 100 ? 3 : hp >= 10 ? 2 : 1;
    const u32 vy = (y + 0x30) & 0xFFFF;
    func_0015FA18(list, 1, hp, static_cast<u16>(GRef<u16>(tab) + x), static_cast<u16>(vy), 10, nd);
    const s32 w = GRef<s16>(tab + 2);
    DrawHudSprite(list, 1, static_cast<u16>(GRef<u16>(tab) + x + static_cast<u32>(w * nd)), static_cast<u16>(vy), 0xDD0, 0x380,
                  0x200, 0xA0, 0);
}

namespace {
u32 HudState() { return W(0x004117D0); }  // current player's HUD state (DrawHud / UpdateHud)
}  // namespace

// EE: 0x00162F48
// Kill message for player p: unit index into the 32-entry ring (write index +0x41), age 0.
void func_00162F48(s32 player, u8 unit) {
    const u32 h = W(0x00410AA0 + 4u * static_cast<u32>(player));
    GRef<u8>(h + GRef<u8>(h + 0x41)) = unit;
    GRef<u8>(h + GRef<u8>(h + 0x41) + 0x20) = 0;
    const u8 w = static_cast<u8>(GRef<u8>(h + 0x41) + 1);
    GRef<u8>(h + 0x41) = w;
    if (w == 0x20) GRef<u8>(h + 0x41) = 0;
}

// EE: 0x00162F98
// Kill-message ageing: the oldest entry is dropped after 120 frames; the entries age by one per
// frame from the oldest while the previous one is at least 16 frames old; with more than 4 waiting
// the oldest is also dropped when the fourth one ages.
void func_00162F98() {
    const u32 h = HudState();
    const u8 r0 = GRef<u8>(h + 0x40);
    if (r0 == GRef<u8>(h + 0x41)) return;
    const u32 ages = h + 0x20;
    s32 n = static_cast<s32>(GRef<u8>(h + 0x41)) - static_cast<s32>(r0);
    if (!(-1 < n)) n += 0x20;
    if (!(GRef<u8>(ages + r0) < 0x79)) {
        const u8 r1 = static_cast<u8>(r0 + 1);
        GRef<u8>(h + 0x40) = r1;
        if (r1 == 0x20) GRef<u8>(h + 0x40) = 0;
    }
    u32 k = GRef<u8>(HudState() + 0x40);
    if (n <= 0) return;
    if (k == 0x20) k = 0;
    u8 a = static_cast<u8>(GRef<u8>(ages + k) + 1);
    GRef<u8>(ages + k) = a;
    if (a < 0x10) return;
    for (s32 i = 0;;) {
        if (i == 3 && !(n < 5)) {
            const u8 r = static_cast<u8>(GRef<u8>(h + 0x40) + 1);
            GRef<u8>(h + 0x40) = r;
            if (r == 0x20) GRef<u8>(h + 0x40) = 0;
        }
        ++i;
        if (!(i < n)) return;
        ++k;
        if (k == 0x20) k = 0;
        a = static_cast<u8>(GRef<u8>(ages + k) + 1);
        GRef<u8>(ages + k) = a;
        if (a < 0x10) return;
    }
}

// EE: 0x001630A0
// Kill messages (bucket 1): up to 4 entries from the oldest, each the unit's score value revealed
// digit by digit (+0x60: hundreds at age 1, tens at age 2, then all), and in single player the
// "DESTROYED"-style text 0x00410A00 from age 4 and the unit name typed in one character per frame
// (players 0x00410A18, others by type: 0x002B7008, ids >= 0x60 0x002B7240).
void func_001630A0(u32 list) {
    const u32 h0 = HudState();
    const u8 r0 = GRef<u8>(h0 + 0x40);
    if (r0 == GRef<u8>(h0 + 0x41)) return;
    s32 n = static_cast<s32>(GRef<u8>(h0 + 0x41)) - static_cast<s32>(r0);
    if (!(-1 < n)) n += 0x20;
    if (!(n < 5)) n = 4;
    HudColour(list, 1);
    u32 k = GRef<u8>(HudState() + 0x40);
    if (n <= 0) return;
    if (k == 0x20) k = 0;
    if (GRef<u8>(HudState() + k + 0x20) == 0) return;
    for (s32 i = 0;;) {
        const u32 h = HudState();
        const u32 u = Unit(GRef<u8>(h + k));
        u32 x, y;
        if (Players() == 2) {
            x = HudU16(0x20) - 0x2C0u;
            y = HudU16(0x1E) + static_cast<u32>(i * 0xC0 + 0x2C0);
        } else {
            x = HudU16(0x1C) + 0x140u;
            y = HudU16(0x1E) + static_cast<u32>(i * 0xC0 + 0x400);
        }
        const s16 hp = GRef<s16>(u + 0x60);
        const u8 age = GRef<u8>(h + k + 0x20);
        const u16 sy = static_cast<u16>(y);
        if (hp >= 100 && age == 1)
            func_0015FA18(list, 1, hp / 100, static_cast<u16>(x), sy, 0x107, 1);
        else if (age == 2)
            func_0015FA18(list, 1, hp / 10, static_cast<u16>(x), sy, 0x107, 2);
        else
            func_0015FA18(list, 1, hp, static_cast<u16>(x), sy, 0x107, 3);
        if (Players() == 1) {
            if (GRef<u8>(HudState() + k + 0x20) >= 4)
                func_0015F6A0(list, 1, 0x00410A00, static_cast<u16>(HudU16(0x1C) + 0x2F0u), sy, 0x107);
            const s32 chars = static_cast<s32>(GRef<u8>(HudState() + k + 0x20)) - 4;
            if (chars > 0) {
                u32 str;
                if (GRef<u8>(u + 0x55) < 2)
                    str = W(0x00410A18 + 4u * GRef<u8>(u + 0x55));
                else if (GRef<u8>(u + 0x65) >= 0x60)
                    str = W(0x002B7240 + 4u * (GRef<u8>(u + 0x65) - 0x60u));
                else
                    str = W(0x002B7008 + 4u * GRef<u8>(u + 0x65));
                func_0015F610(list, 1, str, static_cast<u16>(HudU16(0x1C) + 0x380u), sy, 0x107, chars);
            }
        }
        ++i;
        if (!(i < n)) return;
        ++k;
        if (k == 0x20) k = 0;
        if (GRef<u8>(HudState() + k + 0x20) == 0) return;
    }
}

// EE: 0x001633C8
// Mission timer (bucket 1): scripted segments count status +0x38 (blinking below 20 s), timed
// missions (rules flag 0x100) show the elapsed status +0x24, others the remaining rules +0x14 -
// status +0x24 (blinking below 10 s); minutes, seconds and hundredths with separators. Layout
// 0x00404BF8 / 16:9 0x00404BE8.
// fpucheck: 3 adds (unsigned converts)
void func_001633C8(u32 list) {
    const u32 tab = Wide() ? 0x00404BE8u : 0x00404BF8u;
    const auto L = [tab](u32 i) { return static_cast<u32>(GRef<u16>(tab + 2 * i)); };
    const u32 S = Status(), R = Rules();
    bool blink = false;
    f32 t;
    if ((W(S + 0x20) & 0x10000000u) != 0) {
        t = fpu::UnsignedToFloat(W(S + 0x38)) * F(0x0040E8C0);
        if (0.0f < t && t < 20.0f) blink = true;
    } else if ((W(R + 0x10) & 0x100u) != 0) {
        t = fpu::UnsignedToFloat(W(S + 0x24)) * F(0x0040E8C4);
    } else {
        t = fpu::UnsignedToFloat(W(R + 0x14) - W(S + 0x24)) * F(0x0040E8C8);
        if (t < 0.0f) t = 0.0f;
        if (0.0f < t && t < 10.0f) blink = true;
    }
    const u32 secs = fpu::FixUnsSfSi(t);
    const u32 mins = (secs / 60u) & 0xFF, sec = (secs % 60u) & 0xFF;
    const u32 cs = (fpu::FixUnsSfSi(t * 100.0f) % 100u) & 0xFF;
    const u32 y = HudU16(0x1E) + 0x180u;
    u32 x;
    if (Players() == 2)
        x = HudU16(0x1C) + 0x100u;
    else
        x = HudU16(0x20) - static_cast<u32>(GRef<s16>(tab) + 0x140);
    DrawHudSprite(list, 1, static_cast<u16>(x), static_cast<u16>(y), 0x480, 0x600, 0x250, 0x100, 0);
    if (blink) {
        const u32 f = W(Scene() + 0x165C);
        const u32 a = (f & 0x10u) != 0 ? (GRef<u8>(kHud + 0x13) * (0x10 - (f & 0xF))) >> 4
                                       : (GRef<u8>(kHud + 0x13) * (f & 0xF)) >> 4;
        func_0015F2B0(list, 1, HudR(), HudG(), HudB(), static_cast<s8>(a));
    }
    const u16 y2 = static_cast<u16>(y + 0x30);
    func_0015FA18(list, 1, static_cast<s32>(mins), static_cast<u16>(L(1) + x), y2, 10, 0x82);
    DrawHudSprite(list, 1, static_cast<u16>(L(2) + x), y2, 0x930, 0x380, 0x40, 0x40, 0);
    func_0015FA18(list, 1, static_cast<s32>(sec), static_cast<u16>(L(3) + x), y2, 10, 0x82);
    DrawHudSprite(list, 1, static_cast<u16>(L(4) + x), y2, 0x9D0, 0x380, 0x80, 0x40, 0);
    func_0015FA18(list, 1, static_cast<s32>(cs), static_cast<u16>(L(5) + x), y2, 10, 0x82);
    if (blink) HudColour(list, 1);
}

// EE: 0x00163818
// Weapon panel (bucket 1, lower right): gun rounds (+0x158, highlighted while non-zero), missiles
// (+0x15A, highlighted when the missile is selected) and the special weapon (icon cell from
// 0x002B6F90 by type +0x173, count +0x15B, highlighted when selected: +0x14C bit 0x8000). Layout
// 0x00404C18 / 16:9 0x00404C08 (the right offset 3/4 in 16:9).
void func_00163818(u32 list, u32 st) {
    const u32 tab = Wide() ? 0x00404C08u : 0x00404C18u;
    const auto L = [tab](u32 i) { return static_cast<u32>(GRef<u16>(tab + 2 * i)); };
    const bool two = Players() == 2;
    const s32 d = two ? -0x700 : -0x7C0;
    u32 y = HudU16(0x22) - (two ? 0x5C0u : 0x9C0u);
    const u32 x = Wide() ? HudU16(0x20) + static_cast<u32>((d * 3) >> 2) : HudU16(0x20) + static_cast<u32>(d);
    const auto hl = [&] { DrawHudSprite(list, 1, static_cast<u16>(x), static_cast<u16>(y), 0, 0x720, 0x2E0, 0x100, 0); };
    if (GRef<u16>(st + 0x158) != 0) hl();
    DrawHudSprite(list, 1, static_cast<u16>(L(3) + x), static_cast<u16>(y + 0x40), 0x300, 0x720, 0x1A0, 0x80, 0);
    func_0015FA18(list, 1, GRef<u16>(st + 0x158), static_cast<u16>(L(0) + x), static_cast<u16>(y + 0x30), 10, 0x84);
    y = HudU16(0x22) - (two ? 0x480u : 0x880u);
    DrawHudSprite(list, 1, static_cast<u16>(L(4) + x), static_cast<u16>(y + 0x40), 0x4C0, 0x720, 0x230, 0x80, 0);
    if ((W(st + 0x14C) & 0x8000u) == 0 && GRef<u8>(st + 0x15A) != 0) hl();
    u16 y2 = static_cast<u16>(y + 0x30);
    DrawHudSprite(list, 1, static_cast<u16>(L(0) + x), y2, 0x840, 0x380, 0xA0, 0xA0, 0);
    func_0015FA18(list, 1, GRef<u8>(st + 0x15A), static_cast<u16>(L(1) + x), y2, 10, 0x83);
    y = HudU16(0x22) - (two ? 0x340u : 0x740u);
    const u32 e = 0x002B6F90 + 8u * static_cast<u32>(static_cast<s32>(GRef<s8>(st + 0x173)));
    const s32 xo = GRef<s16>(e + 4) < 0x1E1 ? static_cast<s16>(L(3)) : static_cast<s16>(L(4));
    DrawHudSprite(list, 1, static_cast<u16>(static_cast<u32>(xo) + x), static_cast<u16>(y + 0x40), GRef<u16>(e), GRef<u16>(e + 2),
                  GRef<u16>(e + 4), GRef<u16>(e + 6), 0);
    if ((W(st + 0x14C) & 0x8000u) != 0 && GRef<u8>(st + 0x15B) != 0) hl();
    y2 = static_cast<u16>(y + 0x30);
    DrawHudSprite(list, 1, static_cast<u16>(L(0) + x), y2, 0x840, 0x380, 0xA0, 0xA0, 0);
    DrawHudSprite(list, 1, static_cast<u16>(L(1) + x), y2, 0x840, 0x380, 0xA0, 0xA0, 0);
    func_0015FA18(list, 1, GRef<u8>(st + 0x15B), static_cast<u16>(L(2) + x), y2, 10, 0x82);
}

// EE: 0x00163C08
// Aircraft status (bucket 1): damage percent ((max - hp) x 100 / max, max from 0x0011E878 by type)
// with label; single player also draws the aircraft silhouette (scene packet per player +0x1764,
// tinted green to red by the damage, blinking above 90%) and the weapon reload bars: the special
// weapon's slots (layout 0x002B6A18 / 30 / 40 + type x 0x40 by its kind +0x172, timers +0x166,
// 300-frame reload) or the two missile rails (0x002B6A10, timers +0x15E, 240-frame reload).
void func_00163C08(u32 list, u32 unit) {
    const u32 tab = Wide() ? 0x00410A28u : 0x00410A20u;
    const bool two = Players() == 2;
    const s32 d = two ? -0x700 : -0x7C0;
    const u32 y = HudU16(0x22) - (two ? 0x200u : 0x600u);
    u32 x = Wide() ? HudU16(0x20) + static_cast<u32>((d * 3) >> 2) : HudU16(0x20) + static_cast<u32>(d);
    const s32 maxhp = func_0011E878(GRef<u8>(unit + 0x69));
    const s32 dmg = static_cast<s16>((maxhp - GRef<s16>(unit + 0x5E)) * 100 / maxhp);
    DrawHudSprite(list, 1, static_cast<u16>(x), static_cast<u16>(y), 0xC80, 0x600, 0x2E0, 0x100, 0);
    func_0015FA18(list, 1, dmg, static_cast<u16>(GRef<u16>(tab) + x), static_cast<u16>(y + 0x30), 10, 0x83);
    DrawHudSprite(list, 1, static_cast<u16>(GRef<u16>(tab + 2) + x), static_cast<u16>(y + 0x30), 0x780, 0x380, 0xA0, 0xA0, 0);
    if (two) return;
    func_00104338(list, 0, W(Scene() + 0x40u * GRef<u8>(unit + 0x55) + 0x1764), 0);
    u32 p = AllocBucketPacket(list, 1, 3, 0, 0x50000003);
    Put64(p, 0, 0x1000400000008002ull);
    Put64(p, 1, 0xE);
    Put64(p, 2, 0x2004B605D9412590ull);
    Put64(p, 3, 6);
    Put64(p, 4, 0x000000BC000BC00Aull);
    Put64(p, 5, 8);
    s32 g = 0x80 - (dmg << 7) / 100;
    if (!(-1 < g)) g = 0;
    s32 r = (dmg << 7) / 0x28;
    if (!(r < 0x81)) r = 0x80;
    s32 b = 0x40 - (dmg << 6) / 0x28;
    if (!(-1 < b)) b = 0;
    func_0015F2B0(list, 1, static_cast<s8>(r), static_cast<s8>(g), static_cast<s8>(b), HudA());
    x = HudU16(0x20) - (Wide() ? 0x4B0u : 0x640u);
    const u32 sy0 = HudU16(0x22) - 0x4C0u;
    if (dmg < 0x5A || (W(Scene() + 0x165C) & 2u) != 0)
        func_00160098(list, 1, static_cast<u16>(x), static_cast<u16>(sy0), 0, 0, 0x300, 0x300);
    const u32 st = W(unit + 0x44);
    func_0015F2B0(list, 1, static_cast<s8>(r), static_cast<s8>(g), static_cast<s8>(b), static_cast<s8>(GRef<u8>(kHud + 0x13) >> 1));
    p = AllocBucketPacket(list, 1, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008001ull);
    const u32 type = GRef<u8>(unit + 0x69);
    const auto ox = [](u32 e) {
        s32 v = GRef<s16>(e);
        if (Wide()) {
            const s32 t = v * 3;
            v = (t < 0 ? t + 3 : t) >> 2;
        }
        return v;
    };
    if ((W(st + 0x14C) & 0x8000u) != 0) {
        const s8 kind = GRef<s8>(st + 0x172);
        u32 tbl;
        u16 u, v;
        if (kind == 0) {
            Put64(p, 2, 0x0000015C300BC18Aull);
            tbl = 0x002B6A18 + type * 0x40;
            u = 0x180;
            v = 0x300;
        } else if (kind == 1) {
            Put64(p, 2, 0x000001FC5805C00Aull);
            tbl = 0x002B6A30 + type * 0x40;
            u = 0;
            v = 0x580;
        } else {
            Put64(p, 2, 0x000001FC580BC18Aull);
            tbl = 0x002B6A40 + type * 0x40;
            u = 0x180;
            v = 0x580;
        }
        Put64(p, 3, 8);
        const s32 n = GRef<s8>(st + 0x173) == 8 ? 2 : GRef<s8>(st + 0x175);
        for (s32 i = 0; i < n; ++i) {
            const s16 t = GRef<s8>(st + 0x173) == 8 ? GRef<s16>(st + 0x166) : GRef<s16>(st + 0x166 + 2u * static_cast<u32>(i));
            const s32 h = (300 - t) / 0x3C * 0x270 / 5;
            const u32 e = tbl + 4u * static_cast<u32>(i);
            const u16 sx = static_cast<u16>(x + static_cast<u32>(ox(e)));
            const u16 sy = static_cast<u16>(sy0 - GRef<u16>(e + 2));
            func_00160098(list, 1, sx, sy, u, v, 0x180, static_cast<u16>(h));
            func_00160098(list, 1, sx, sy, u, v, 0x180, 0x280);
        }
    } else {
        Put64(p, 3, 8);
        Put64(p, 2, 0x0000015C3005C00Aull);
        const u32 tbl = 0x002B6A10 + type * 0x40;
        for (u32 i = 0; i < 2; ++i) {
            const s16 t = GRef<s16>(st + 0x15E + 2 * i);
            const u16 h = static_cast<u16>(((0xF0 - t) / 0x3C * 0xA0) & 0xFFE0);
            const u32 e = tbl + 4 * i;
            const u16 sx = static_cast<u16>(x + static_cast<u32>(ox(e)));
            const u16 sy = static_cast<u16>(sy0 - GRef<u16>(e + 2));
            func_00160098(list, 1, sx, sy, 0, 0x300, 0x180, h);
            func_00160098(list, 1, sx, sy, 0, 0x300, 0x180, 0x280);
        }
    }
    p = AllocBucketPacket(list, 1, 3, 0, 0x50000003);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008002ull);
    Put64(p, 2, 0x2004B00621412500ull);
    Put64(p, 3, 6);
    Put64(p, 5, 8);
    Put64(p, 4, 0);
    HudColour(list, 1);
}

// EE: 0x0014C7A0
// Gun lead time: distance / ((speed + C 0x0040E534) x 60), capped at C 0x0040E538.
// fpucheck: 1 add
f32 func_0014C7A0(f32 dist, u32 vel) {
    const f32 s = fpu::Add(GetVectorLength(vel), F(0x0040E534)) * 60.0f;
    const f32 cap = F(0x0040E538);
    if (s * cap < dist) return cap;
    return dist / s;
}

// EE: 0x0014C800
// Gun impact point after lead time t: pos + vel x 60t + the muzzle velocity along the nose
// (C 0x0040E53C x 60t, turned by rot) + 24.5 t^2 drop; w = 1. (The stack vector's w is not set in
// the original; it only reaches out.w, which is overwritten.)
// fpucheck: 1 add
f32 func_0014C800(f32 t, u32 out, u32 pos, u32 rot, u32 vel) {
    sceVu0ScaleVector(out, vel, t * 60.0f);
    guest::GuestFrame fr(0x50);
    const u32 m = fr.Address(0), v = fr.Address(0x40);
    GRef<u32>(v) = 0;
    GRef<u32>(v + 4) = 0;
    GRef<f32>(v + 8) = t * F(0x0040E53C) * 60.0f;
    GRef<u32>(v + 0xC) = 0;  // uninitialised in the original (only reaches out.w)
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, rot);
    sceVu0ApplyMatrix(v, m, v);
    sceVu0AddVector(out, out, v);
    GRef<f32>(out + 4) = fpu::Add(F(out + 4), t * 24.5f * t);
    sceVu0AddVector(out, out, pos);
    GRef<f32>(out + 0xC) = 1.0f;
    return t;
}

// EE: 0x00164440
// Gun pipper (bucket 2): for a lockable target (+0x4C bits 0x4000 and 0x8000, not type 0xB2)
// within 4000, the predicted impact point (0x0014C800 after the lead time 0x0014C7A0) is projected
// and offset by the target's own motion over that time; inside the 0x7000..0x9000 x
// 0x7200..0x8E00 window it draws the two-sprite reticle and the range arc (a fan of up to 9
// segments from tables 0x00404C28 / 50 / 78 (16:9 A8) / D8, the last one interpolated by the
// distance above 500).
// fpucheck: 2 adds
void func_00164440(u32 cam, u32 list, u32 unit) {
    const u8 t = GRef<u8>(unit + 0x62);
    if (t == 0xFF) return;
    const u32 tu = Unit(t);
    const u32 f4c = W(tu + 0x4C);
    if ((~f4c & 0x4000u) != 0) return;
    const u32 prox = 0x00410648 + 4u * GRef<u8>(unit + 0x55);
    const f32 dist = F(W(prox) + 12u * t + 4);
    if (4000.0f < dist) return;
    if ((~f4c & 0x8000u) != 0) return;
    if (GRef<u8>(tu + 0x69) == 0xB2) return;
    const u32 m = cam + 0xE0;
    guest::GuestFrame fr(0x40);
    const u32 a = fr.Address(0), b = fr.Address(0x10), P = fr.Address(0x20), Q = fr.Address(0x30);
    const f32 lead = func_0014C7A0(dist, unit + 0x20);
    sceVu0ScaleVector(a, tu + 0x20, lead * 60.0f);
    sceVu0AddVector(a, a, tu);
    sceVu0RotTransPers(P, m, a, 1);
    sceVu0RotTransPers(Q, m, tu, 1);
    GRef<s32>(Q) = GRef<s32>(Q) - GRef<s32>(P);
    GRef<s32>(Q + 4) = GRef<s32>(Q + 4) - GRef<s32>(P + 4);
    func_0014C800(lead, b, unit, unit + 0x10, unit + 0x20);
    sceVu0RotTransPers(P, m, b, 1);
    const s32 px = GRef<s32>(P) + GRef<s32>(Q), py = GRef<s32>(P + 4) + GRef<s32>(Q + 4);
    GRef<s32>(P) = px;
    GRef<s32>(P + 4) = py;
    if (GRef<s32>(P + 8) < 0) return;
    if (px < 0x7000 || 0x9000 < px) return;
    if (py < 0x7200 || 0x8E00 < py) return;
    const u32 X = Wide() ? 0x00404CA8u : 0x00404C78u;
    const u32 U = 0x00404C28, V = 0x00404C50, Y = 0x00404CD8;
    const auto at = [](u32 tab, s32 i) { return GRef<s32>(tab + 4u * static_cast<u32>(i)); };
    u32 p = AllocBucketPacket(list, 2, 5, 0, 0x50000005);
    Put64(p, 1, 0x5353);
    Put64(p, 0, 0x40AB400000008001ull);
    const auto quad = [](u32 o, s32 a0, s32 a1, u32 a2) {
        GRef<s32>(o) = a0;
        GRef<s32>(o + 4) = a1;
        GRef<u32>(o + 8) = a2;
        GRef<u32>(o + 0xC) = 0;
    };
    quad(p + 0x10, 0x10, 0x920, 0);
    quad(p + 0x20, px + at(X, 9) * 16, py + at(Y, 9) * 16, 0xFFFFFD);
    quad(p + 0x30, 0x250, 0xB60, 0);
    quad(p + 0x40, px + at(X, 10) * 16, py + at(Y, 10) * 16, 0xFFFFFD);
    f32 f = fpu::Sub(F(W(prox) + 12u * GRef<u8>(unit + 0x62) + 4), 500.0f) * 8.0f / 4000.0f;
    if (f < 0.0f) f = 0.0f;
    if (8.0f < f) f = 8.0f;
    const s32 n = fpu::FloatToInt(f);
    const u32 qwc = static_cast<u32>((n + 3) * 2 + 1);
    f = fpu::Sub(f, fpu::IntToFloat(n));
    p = AllocBucketPacket(list, 2, qwc, 0, qwc | 0x50000000);
    Put64(p, 1, 0x53);
    Put64(p, 0, static_cast<u64>(static_cast<u32>(n + 3) | 0x8000u) | (0x82ABull << 46));
    quad(p + 0x10, 0x360, 0xA00, 0);
    quad(p + 0x20, px, py, 0xFFFFFD);
    u32 o = p + 0x30;
    s32 k = 0;
    for (; k < n + 1; ++k) {
        quad(o, at(U, k) * 16 + 0x280, at(V, k) * 16 + 0x920, 0);
        quad(o + 0x10, px + at(X, k) * 16, py + at(Y, k) * 16, 0xFFFFFD);
        o += 0x20;
    }
    const s32 j = k - 1;
    const auto lerp = [&](u32 tab) {
        return fpu::FloatToInt(fpu::IntToFloat((at(tab, k) - at(tab, j)) * 16) * f);
    };
    quad(o, lerp(U) + at(U, j) * 16 + 0x280, lerp(V) + at(V, j) * 16 + 0x920, 0);
    quad(o + 0x10, px + lerp(X) + at(X, j) * 16, py + lerp(Y) + at(Y, j) * 16, 0xFFFFFD);
}

// EE: 0x00164BD8
// Multi-lock boxes (bucket 0, red): for each lock slot (+0x152 slots, targets +0x154) a box at the
// locked target (+0x188 bit 0x100 << i) or, while the lock is still sliding in (bit 0x1000 << i),
// at the slot's own direction (+0x194 / +0x1A4 angles relative to the aircraft, at the camera
// distance); not for special weapon type 3. Cell by target class (+0x54 == 2) and by the special
// weapon being selected.
void func_00164BD8(u32 cam, u32 list, u32 unit) {
    const u32 st = W(unit + 0x44);
    if ((W(st + 0x14C) & 0x8000u) != 0 && GRef<s8>(st + 0x173) == 3) return;
    const s32 hw = Wide() ? 0x60 : 0x80;
    guest::GuestFrame fr(0x60);
    const u32 P = fr.Address(0), v = fr.Address(0x10), m = fr.Address(0x20);
    for (u32 i = 0; i < GRef<u8>(st + 0x152); ++i) {
        u32 u;
        if ((W(st + 0x188) & (0x1000u << i)) != 0) {
            u = Unit(GRef<u8>(st + 0x154 + i));
            GRef<f32>(v) = 0.0f;
            GRef<u32>(v + 8) = W(cam + 0x168);
            GRef<f32>(v + 0xC) = 1.0f;
            GRef<f32>(v + 4) = 0.0f;
            sceVu0UnitMatrix(m);
            sceVu0RotMatrixX(m, m, F(st + 0x194 + 4 * i));
            sceVu0RotMatrixY(m, m, F(st + 0x1A4 + 4 * i));
            sceVu0RotMatrixX(m, m, F(unit + 0x10));
            sceVu0RotMatrixY(m, m, F(unit + 0x14));
            sceVu0TransMatrix(m, m, cam);
            sceVu0MulMatrix(m, cam + 0xE0, m);
            sceVu0RotTransPers(P, m, v, 1);
        } else if ((W(st + 0x188) & (0x100u << i)) != 0) {
            u = Unit(GRef<u8>(st + 0x154 + i));
            sceVu0RotTransPers(P, cam + 0xE0, u, 1);
        } else {
            continue;
        }
        if (GRef<s32>(P + 8) < 0) continue;
        if (static_cast<u32>(GRef<s32>(P) - 0x7000) >= 0x2001) continue;
        if (static_cast<u32>(GRef<s32>(P + 4) - 0x7200) >= 0x1C01) continue;
        const s32 cv = GRef<u8>(u + 0x54) == 2 ? 0xC20 : 0xB00;
        const s32 cu = (W(st + 0x14C) & 0x8000u) != 0 ? 0xE60 : 0xD40;
        const u32 p = AllocBucketPacket(list, 0, 6, 0, 0x50000006);
        Put64(p, 0, 0x50AB400000008001ull);
        Put64(p, 1, 0x53531);
        GRef<u32>(p + 0x10) = 0xFF;
        GRef<u32>(p + 0x1C) = 0x60;
        GRef<u32>(p + 0x14) = 0;
        GRef<u32>(p + 0x18) = 0;
        GRef<s32>(p + 0x20) = cu;
        GRef<s32>(p + 0x24) = cv;
        GRef<u32>(p + 0x28) = 0;
        GRef<u32>(p + 0x2C) = 0;
        GRef<s32>(p + 0x30) = GRef<s32>(P) - hw;
        GRef<u32>(p + 0x38) = 0xFFFFFD;
        GRef<u32>(p + 0x3C) = 0;
        GRef<s32>(p + 0x34) = GRef<s32>(P + 4) - 0x90;
        GRef<s32>(p + 0x40) = cu + 0x100;
        GRef<s32>(p + 0x44) = cv + 0x100;
        GRef<u32>(p + 0x48) = 0;
        GRef<u32>(p + 0x4C) = 0;
        GRef<s32>(p + 0x50) = GRef<s32>(P) + hw;
        GRef<u32>(p + 0x5C) = 0;
        GRef<u32>(p + 0x58) = 0xFFFFFD;
        GRef<s32>(p + 0x54) = GRef<s32>(P + 4) + 0x90;
    }
}

namespace {

// One textured dash of the bomb-fall line (bucket 2): UV (0, 0xFE0) .. (0x200, 0xFE0).
void FallDash(u32 list, u64 tag, s32 x0, s32 y0, s32 x1, s32 y1) {
    const u32 p = AllocBucketPacket(list, 2, 5, 0, 0x50000005);
    Put64(p, 0, tag);
    Put64(p, 1, 0x5353);
    GRef<u32>(p + 0x10) = 0;
    GRef<u32>(p + 0x14) = 0xFE0;
    GRef<u32>(p + 0x18) = 0;
    GRef<u32>(p + 0x1C) = 0;
    GRef<s32>(p + 0x20) = x0;
    GRef<u32>(p + 0x28) = 0xFFFFFD;
    GRef<s32>(p + 0x24) = y0;
    GRef<u32>(p + 0x2C) = 0;
    GRef<u32>(p + 0x30) = 0x200;
    GRef<u32>(p + 0x34) = 0xFE0;
    GRef<u32>(p + 0x38) = 0;
    GRef<u32>(p + 0x3C) = 0;
    GRef<s32>(p + 0x40) = x1;
    GRef<u32>(p + 0x4C) = 0;
    GRef<s32>(p + 0x44) = y1;
    GRef<u32>(p + 0x48) = 0xFFFFFD;
}

// Two-corner sprite (bucket 2) centred at (x, y) with half sizes hw, hh and cell (u0, v0)-(u1, v1).
void CentredSprite(u32 list, s32 x, s32 y, s32 hw, s32 hh, s32 u0, s32 v0, s32 u1, s32 v1) {
    const u32 p = AllocBucketPacket(list, 2, 5, 0, 0x50000005);
    Put64(p, 1, 0x5353);
    Put64(p, 0, 0x40AB400000008001ull);
    GRef<s32>(p + 0x10) = u0;
    GRef<s32>(p + 0x14) = v0;
    GRef<u32>(p + 0x18) = 0;
    GRef<u32>(p + 0x1C) = 0;
    GRef<s32>(p + 0x20) = x - hw;
    GRef<u32>(p + 0x28) = 0xFFFFFD;
    GRef<u32>(p + 0x2C) = 0;
    GRef<s32>(p + 0x24) = y - hh;
    GRef<s32>(p + 0x30) = u1;
    GRef<s32>(p + 0x34) = v1;
    GRef<u32>(p + 0x38) = 0;
    GRef<u32>(p + 0x3C) = 0;
    GRef<s32>(p + 0x40) = x + hw;
    GRef<u32>(p + 0x48) = 0xFFFFFD;
    GRef<u32>(p + 0x4C) = 0;
    GRef<s32>(p + 0x44) = y + hh;
}

// 4 dashes from (x, y) in steps of (dx, dy); dashes with any end beyond 0xFFF0 (unsigned) are
// skipped. Returns the end point.
void FallLine(u32 list, u64 tag, u32& x, u32& y, s32 dx, s32 dy) {
    for (u32 k = 0; k < 4; ++k) {
        const u32 nx = x + static_cast<u32>(dx), ny = y + static_cast<u32>(dy);
        if (!(0xFFF0u < x) && !(0xFFF0u < nx) && !(0xFFF0u < y) && !(0xFFF0u < ny))
            FallDash(list, tag, static_cast<s32>(x), static_cast<s32>(y), static_cast<s32>(nx), static_cast<s32>(ny));
        x = nx;
        y = ny;
    }
}

}  // namespace

// EE: 0x00165310
// Bomb reticle: the fall line from the impact point P toward the flight-path marker in 4 dashes
// and the impact box (16:9 half width 0xC9, else 0x107).
void func_00165310(u32 list, u32 P) {
    const u32 px = W(P);
    if (0x10000u < px) return;
    const s32 py = GRef<s32>(P + 4);
    if (py < 0 || 0x10000 < py) return;
    if (GRef<s32>(P + 8) < 0) return;
    const s32 dx = static_cast<s32>(W(kHud) - px) >> 2;
    const s32 dy = static_cast<s32>(W(kHud + 4) - static_cast<u32>(py)) >> 2;
    const s32 hw = Wide() ? 0xC9 : 0x107;
    u32 x = px, y = static_cast<u32>(py);
    FallLine(list, 0x40A8C00000008001ull, x, y, dx, dy);
    if (0xFFF0u < W(P) || 0xFFF0u < W(P + 4)) return;
    CentredSprite(list, GRef<s32>(P), GRef<s32>(P + 4), hw, 0x130, 8, 0xB78, 0x258, 0xDC8);
}

// EE: 0x001655E8
// Cluster-bomb reticle: fall lines from the flight-path marker to the first impact point P and on
// to the second R, each ending in a marker (16:9 half width 0x62, else 0x7E).
void func_001655E8(u32 list, u32 P, u32 R) {
    u32 px = W(P);
    if (0xFFF0u < px) return;
    s32 py = GRef<s32>(P + 4);
    if (py < 0 || 0xFFF0 < py) return;
    if (GRef<s32>(P + 8) < 0) return;
    if (0xFFF0u < W(R)) return;
    if (GRef<s32>(R + 4) < 0 || 0xFFF0 < GRef<s32>(R + 4)) return;
    if (GRef<s32>(R + 8) < 0) return;
    const s32 hw = Wide() ? 0x62 : 0x7E;
    for (s32 pass = 0;;) {
        u32 x, y;
        s32 dx, dy;
        if (pass == 0) {
            x = W(kHud);
            y = W(kHud + 4);
            dx = static_cast<s32>(px - x) >> 2;
            dy = static_cast<s32>(static_cast<u32>(py) - y) >> 2;
        } else {
            dx = static_cast<s32>(W(R) - px) >> 2;
            dy = static_cast<s32>(W(R + 4) - static_cast<u32>(py)) >> 2;
            x = W(P);
            y = W(P + 4);
        }
        FallLine(list, 0x40A8C00000008001ull, x, y, dx, dy);
        if (0xFFF0u < x || 0xFFF0u < y) return;
        ++pass;
        CentredSprite(list, static_cast<s32>(x), static_cast<s32>(y), hw, 0x90, 0xD38, 0xC18, 0xE48, 0xD28);
        if (!(pass < 2)) return;
        px = W(P);
        py = GRef<s32>(P + 4);
    }
}

// EE: 0x00165970
// Area-weapon impact marker at P (16:9 half width 0x95, else 0xC2).
void func_00165970(u32 list, u32 P) {
    const u32 px = W(P);
    if (0x10000u < px) return;
    const s32 py = GRef<s32>(P + 4);
    if (py < 0 || 0x10000 < py) return;
    if (GRef<s32>(P + 8) < 0) return;
    const s32 hw = Wide() ? 0x95 : 0xC2;
    if (0xFFF0u < px || 0xFFF0u < static_cast<u32>(py)) return;
    CentredSprite(list, GRef<s32>(P), GRef<s32>(P + 4), hw, 0xE0, 0x278, 0xAF8, 0x428, 0xCA8);
}

// EE: 0x00164F00
// Special-weapon / bomb reticle: the impact point of the selected weapon (bombs: 0x00154198 release
// velocity and 0x00153FD8 fall over the target's or the terrain's height; type 3 also follows the
// lock-on slide or the locked target; type 6 adds a second point 128 frames of motion ahead) is
// projected into the state +0x90 (+0xA0) and drawn by 0x00165310 / 0x001655E8 / 0x00165970 (type
// 8). Nothing for types 9-14 or when the flight-path marker is off screen.
void func_00164F00(u32 cam, u32 list, u32 unit) {
    const u32 fx = W(kHud);
    if (0x10000u < fx) return;
    const s32 fy = GRef<s32>(kHud + 4);
    if (fy < 0 || 0x10000 < fy) return;
    if (GRef<s32>(kHud + 0xC) < 0) return;
    const u32 st = W(unit + 0x44);
    const u8 type = GRef<u8>(st + 0x173);
    if (static_cast<u32>(type - 0xC) < 3) return;
    const s8 t = static_cast<s8>(type);
    if (t == 10 || t == 11 || t == 9) return;
    guest::GuestFrame fr(0x70);
    const u32 hit = fr.Address(0), rel = fr.Address(0x10), v = fr.Address(0x20), m = fr.Address(0x30);
    const auto height = [unit] {
        const u8 tg = GRef<u8>(unit + 0x62);
        return tg == 0xFF ? func_0013E338(Terrain(), unit) : F(Unit(tg) + 4);
    };
    s32 mode = 0;
    if (t == 3) {
        const u32 f188 = W(st + 0x188);
        if ((f188 & 0x1000u) != 0) {
            GRef<f32>(v) = 0.0f;
            GRef<u32>(v + 8) = W(cam + 0x168);
            GRef<f32>(v + 0xC) = 1.0f;
            GRef<f32>(v + 4) = 0.0f;
            sceVu0UnitMatrix(m);
            sceVu0RotMatrixX(m, m, F(st + 0x194));
            sceVu0RotMatrixY(m, m, F(st + 0x1A4));
            sceVu0RotMatrixX(m, m, F(unit + 0x10));
            sceVu0RotMatrixY(m, m, F(unit + 0x14));
            sceVu0TransMatrix(m, m, cam);
            sceVu0MulMatrix(m, cam + 0xE0, m);
            sceVu0RotTransPers(st + 0x90, m, v, 1);
        } else if ((f188 & 0x100u) != 0) {
            sceVu0RotTransPers(st + 0x90, cam + 0xE0, Unit(GRef<u8>(st + 0x154)), 1);
        } else {
            func_00154198(rel, unit + 0x10, unit + 0x20, 3);
            const f32 h = height();
            if (func_00153FD8(h, hit, unit, rel) < 0) return;
            sceVu0RotTransPers(st + 0x90, cam + 0xE0, hit, 1);
        }
    } else if (t == 6) {
        mode = 1;
        const f32 h = height();
        func_00154198(rel, unit + 0x10, unit + 0x20, GRef<s8>(st + 0x173));
        func_00153FD8(h, hit, unit, rel);
        sceVu0RotTransPers(st + 0x90, cam + 0xE0, hit, 1);
        sceVu0ScaleVector(v, unit + 0x20, 128.0f);
        sceVu0AddVector(v, v, unit);
        func_00153FD8(h, hit, v, rel);
        sceVu0RotTransPers(st + 0xA0, cam + 0xE0, hit, 1);
    } else {
        mode = t == 8 ? 2 : 0;
        func_00154198(rel, unit + 0x10, unit + 0x20, t);
        const f32 h = height();
        if (func_00153FD8(h, hit, unit, rel) < 0) return;
        sceVu0RotTransPers(st + 0x90, cam + 0xE0, hit, 1);
    }
    if (mode == 0)
        func_00165310(list, st + 0x90);
    else if (mode == 1)
        func_001655E8(list, st + 0x90, st + 0xA0);
    else
        func_00165970(list, st + 0x90);
}

// EE: 0x00165AB8
// Target markers (bucket 1, alpha test on): every live, shown unit (+0x4C bits 0x10000000,
// 0x01000000, 0x4000; display class +0x64 not 0 / 0x14; not type 0xB2) within 32768 (the current
// target only from 1536 on) gets a two-layer box at its projected centre (or at its surface toward
// the camera when far), coloured by side (friendly blue, enemy green, red when it targets this
// player, blinking for the current target) and faded by distance; cell by class and the lock state
// of the player (+0x188). The current target also gets its name, the player tag in two-player
// mode and the distance; flagged units (+0x48 bit 31) a marker, friendly units their callsign.
// Not drawn while the state flag 0x08000000 is set.
// fpucheck: 2 adds
void func_00165AB8(u32 cam, u32 list, u32 unit) {
    const u32 st = W(unit + 0x44);
    if ((W(st + 0x188) & 0x08000000u) != 0) return;
    const u32 team = GRef<u8>(unit + 0x48) & 3u;
    const s32 hw = Wide() ? 0x60 : 0x80;
    const u32 me = GRef<u8>(unit + 0x55);
    func_0015F420(list, 1, 1, 0);
    guest::GuestFrame fr(0x20);
    const u32 P = fr.Address(0), A = fr.Address(0x10);
    const auto dist = [me](u32 i) { return F(W(0x00410648 + 4 * me) + 12 * i + 4); };
    for (u32 i = 0; static_cast<s32>(i) < GRef<s32>(UnitList()); ++i) {
        const u32 u = Unit(static_cast<s32>(i));
        if (u == unit) continue;
        const u32 nf = ~W(u + 0x4C);
        if ((nf & 0x10000000u) != 0 || (nf & 0x01000000u) != 0 || (nf & 0x4000u) != 0) continue;
        if (32768.0f < dist(i)) continue;
        const u8 cls = GRef<u8>(u + 0x64);
        if (cls == 0 || cls == 0x14) continue;
        if (GRef<u8>(u + 0x69) == 0xB2) continue;
        if (i == GRef<u8>(UnitList() + 0x88) && dist(i) < 1536.0f) continue;
        const f32 d = dist(i);
        const u32 alpha = d < 8192.0f ? 0x40u : d < 16384.0f ? 0x20u : 0x10u;
        if (dist(i) <= fpu::Add(F(u + 0x70), F(u + 0x70))) {
            CopyQ(A, u);
        } else {
            sceVu0SubVector(A, cam, u);
            sceVu0Normalize(A, A);
            sceVu0ScaleVector(A, A, F(u + 0x70));
            sceVu0AddVector(A, u, A);
            GRef<f32>(A + 0xC) = 1.0f;
        }
        sceVu0RotTransPers(P, cam + 0xE0, A, 1);
        if (static_cast<u32>(GRef<s32>(P) - 0x7000) >= 0x2001) continue;
        if (static_cast<u32>(GRef<s32>(P + 4) - 0x7200) >= 0x1C01) continue;
        if (GRef<s32>(P + 8) < 0 || GRef<s32>(P + 0xC) < 0) continue;
        if (dist(i) <= fpu::Add(F(u + 0x70), F(u + 0x70))) GRef<u32>(P + 8) = 0xFFFFFC;
        const u32 f188 = W(st + 0x188);
        u32 cell = 0x360;
        if ((W(u + 0x48) & 3u) != team) {
            if ((cls & 2) != 0) {
                if ((f188 & 0x04000000u) != 0 || (cls & 4) != 0)
                    cell = 0x240;
                else
                    cell = (~f188 & 0x20000u) != 0 ? 0x240 : 0x120;
            } else {
                if ((f188 & 0x04000000u) != 0 || (cls & 4) != 0)
                    cell = 0x360;
                else
                    cell = (~f188 & 0x10000u) == 0 ? 0 : 0x360;
            }
        }
        const bool targets_me = (me == 0 && (W(u + 0x4C) & 1u) != 0) || (me == 1 && (W(u + 0x4C) & 2u) != 0);
        const bool blink_off = i == GRef<u8>(unit + 0x62) && (Ctx()->frame_count & 4u) != 0;
        const bool same = (W(u + 0x48) & 3u) == team;
        const s32 px = GRef<s32>(P), py = GRef<s32>(P + 4), pz = GRef<s32>(P + 8);
        for (u32 layer = 0; layer < 2; ++layer) {
            const u32 p = AllocBucketPacket(list, 1, 6, 0, 0x50000006);
            Put64(p, 1, 0x53531);
            Put64(p, 0, 0x50AB400000008001ull);
            u32 r, g, b;
            if (targets_me) {
                r = 0x80, g = 0, b = 0;
            } else if (blink_off) {
                r = 0, g = 0, b = 0;
            } else if (same) {
                r = layer == 0 ? 0x20u : 0u, g = 0x20, b = 0x80;
            } else {
                r = 0, g = 0x80, b = 0x40;
            }
            GRef<u32>(p + 0x10) = r;
            GRef<u32>(p + 0x14) = g;
            GRef<u32>(p + 0x18) = b;
            GRef<u32>(p + 0x1C) = alpha;
            GRef<u32>(p + 0x20) = cell + 0x8C0;
            GRef<u32>(p + 0x24) = layer == 0 ? 0xB00u : 0xC20u;
            GRef<u32>(p + 0x28) = 0;
            GRef<u32>(p + 0x2C) = 0;
            GRef<s32>(p + 0x30) = px - hw;
            GRef<s32>(p + 0x34) = py - 0x80;
            GRef<s32>(p + 0x38) = layer == 0 ? pz : 0xFFFFFC;
            GRef<u32>(p + 0x3C) = 0;
            GRef<u32>(p + 0x40) = cell + 0x9C0;
            GRef<u32>(p + 0x44) = layer == 0 ? 0xC00u : 0xD20u;
            GRef<u32>(p + 0x48) = 0;
            GRef<u32>(p + 0x4C) = 0;
            GRef<s32>(p + 0x50) = px + hw;
            GRef<s32>(p + 0x54) = py + 0x80;
            GRef<s32>(p + 0x58) = layer == 0 ? pz : 0xFFFFFC;
            GRef<u32>(p + 0x5C) = 0;
        }
        const u16 tx = static_cast<u16>(static_cast<u32>(GRef<u16>(P)) + static_cast<u32>(hw) + 0x10);
        const u16 ty = static_cast<u16>(GRef<u16>(P + 4) - 0x100u);
        const u16 ty2 = static_cast<u16>(GRef<u16>(P + 4) - 0x70u);
        if (i == GRef<u8>(unit + 0x62) && GRef<u8>(unit + 0x54) == 0) {
            func_0015F2B0(list, 1, 0, -0x80, 0x40, -0x80);
            if (GRef<s8>(Scene() + 0x1653) == 2) {
                const u8 tg = GRef<u8>(unit + 0x62);
                if (tg == 0)
                    func_0015F6A0(list, 1, 0x00410A08, tx, ty, 0x107);
                else if (tg == 1)
                    func_0015F6A0(list, 1, 0x00410A10, tx, ty, 0x107);
            }
            const u8 id = GRef<u8>(u + 0x65);
            if (id != 0) func_0015F6A0(list, 1, W(0x002B7008 + 4u * id), tx, ty, 0x107);
            if (GRef<u8>(u + 0x65) >= 0x60) func_0015F6A0(list, 1, W(0x002B7240 + 4u * (GRef<u8>(u + 0x65) - 0x60u)), tx, ty2, 0x107);
            const f32 div = (GRef<u8>(0x002A15CC) & 4) != 0 ? 5.0f : F(0x0040E8CC);
            func_0015FA18(list, 1, static_cast<s32>(fpu::FixUnsSfSi(dist(i) / div)), tx,
                          static_cast<u16>(GRef<u16>(P + 4) + 0x80u), 0x107, 4);
        }
        if (GRef<s32>(u + 0x48) < 0) {
            if (i == GRef<u8>(unit + 0x62) && (Ctx()->frame_count & 4u) != 0)
                func_0015F2B0(list, 1, 0, 0, 0, -0x80);
            else
                func_0015F2B0(list, 1, -0x80, 0, 0, -0x80);
            DrawHudSprite(list, 1, static_cast<u16>(GRef<u16>(P) - static_cast<u32>(hw) - 0x1B0u), ty, 0x680, 0x900, 0x1A0, 0x80,
                          0x100);
        }
        if ((W(u + 0x48) & 3u) == team) {
            func_0015F2B0(list, 1, 0, 0x20, -0x80, -0x80);
            DrawHudSprite(list, 1, static_cast<u16>(GRef<u16>(P) - static_cast<u32>(hw) - 0x200u),
                          static_cast<u16>(GRef<u16>(P + 4) - 0xF0u), 0xBE0, 0x860, 0x1F0, 0x70, 0x100);
            if (GRef<u8>(u + 0x65) >= 0x60) {
                func_0015F6A0(list, 1, W(0x002B7008 + 4u * GRef<u8>(u + 0x65)), tx, ty, 0x107);
                func_0015F6A0(list, 1, W(0x002B7240 + 4u * (GRef<u8>(u + 0x65) - 0x60u)), tx, ty2, 0x107);
            }
        }
    }
    func_0015F420(list, 1, 0, 0);
}

// EE: 0x00166628
// Target direction arrow (bucket 0): the target's direction in the camera frame; once it is at
// least C 0x0040E8D4 off the view axis, a triangle at the frame centre points toward it, pushed out
// by the angle (120 + half the angle in degrees, scaled by the view's x / y scale +0x180 / +0x184).
// With `fade` its alpha grows from 0 at distance 16384 (2^-14 per unit).
// fpucheck: 5 adds
void func_00166628(u32 cam, u32 list, u32 target, s32 fade) {
    guest::GuestFrame fr(0x50);
    const u32 d = fr.Address(0), m = fr.Address(0x10);
    sceVu0SubVector(d, target, cam);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, cam + 0x10);
    sceVu0TransposeMatrix(m, m);
    sceVu0ApplyMatrix(d, m, d);
    s32 alpha = GRef<u8>(kHud + 0x13);
    if (fade != 0) {
        const s32 amax = GRef<u8>(kHud + 0x13);
        alpha = fpu::FloatToInt(fpu::IntToFloat(amax) * fpu::Sub(GetVectorLength(d), 16384.0f) * 6.1035156e-05f);
        if (amax < alpha)
            alpha = amax;
        else if (!(-1 < alpha))
            alpha = 0;
    }
    const f32 dx = F(d), dy = F(d + 4), dz = F(d + 8);
    const f32 h = fpu::Sqrt(fpu::Add(dx * dx, dy * dy));
    f32 yaw, pitch;
    if (h == 0.0f) {
        yaw = 0.0f;
        pitch = dz < 0.0f ? F(0x0040E8D0) : 0.0f;
    } else {
        yaw = ee::atan2f(dx, dy);
        pitch = ee::atan2f(h, dz);
    }
    if (pitch < F(0x0040E8D4)) return;
    const f32 half = pitch * 180.0f / F(0x0040E8D8) * 0.5f;
    const f32 a1 = fpu::Add(yaw, F(0x0040E8DC)), a2 = fpu::Sub(yaw, F(0x0040E8DC));
    const f32 r0 = fpu::Add(half, 120.0f);
    const f32 sx = F(cam + 0x180), sy = F(cam + 0x184);
    const s32 x0 = fpu::FloatToInt(ee::sinf(yaw) * r0 * sx * 16.0f);
    const s32 y0 = fpu::FloatToInt(ee::cosf(yaw) * r0 * sy * 16.0f);
    const s32 x1 = fpu::FloatToInt(ee::sinf(a1) * 120.0f * sx * 16.0f);
    const s32 y1 = fpu::FloatToInt(ee::cosf(a1) * 120.0f * sy * 16.0f);
    const s32 x2 = fpu::FloatToInt(ee::sinf(a2) * 120.0f * sx * 16.0f);
    const s32 y2 = fpu::FloatToInt(ee::cosf(a2) * 120.0f * sy * 16.0f);
    func_0015F2B0(list, 0, HudR(), HudG(), HudB(), static_cast<s8>(alpha));
    const u32 p = AllocBucketPacket(list, 0, 4, 0, 0x50000004);
    Put64(p, 1, 0x555);
    Put64(p, 0, 0x3061C00000008001ull);
    const s32 cx = HudU16(0x14), cy = HudU16(0x16);
    const s32 xs[3] = {x0, x1, x2}, ys[3] = {y0, y1, y2};
    for (u32 k = 0; k < 3; ++k) {
        const u32 o = p + 0x10 + 0x10 * k;
        GRef<s32>(o) = xs[k] + cx;
        GRef<u32>(o + 8) = 0xFFFFFD;
        GRef<u32>(o + 0xC) = 0;
        GRef<s32>(o + 4) = ys[k] + cy;
    }
    HudColour(list, 0);
}

// EE: 0x00166588
// Target direction arrow for the current target; in two-player mode against a non-player target
// in missions 0x14, 0x1F and 0x28 it fades in with distance.
void func_00166588(u32 cam, u32 list, u32 unit) {
    const u8 t = GRef<u8>(unit + 0x62);
    if (t == 0xFF) return;
    const u32 tu = Unit(t);
    s32 fade = 0;
    if (GRef<s8>(Scene() + 0x1653) == 2 && GRef<u8>(tu + 0x54) == 0) {
        const u8 m = Game(0x27);
        if (m == 0x1F || m == 0x14 || m == 0x28) fade = 1;
    }
    func_00166628(cam, list, tu, fade);
}

// EE: 0x0017B1E0
// Landing radio hint k for player 0 (state +0x28C bits): hints 6/7 and 9/10 clear each other; a
// hint not given yet is played (0x00253000) and marked.
void func_0017B1E0(s32 k) {
    const u32 st = W(Unit(0) + 0x44);
    u32 clear = 0xFFFFFFFFu;
    if (k == 6)
        clear = 0xFFFFFF7Fu;
    else if (k == 7)
        clear = 0xFFFFFFBFu;
    else if (k == 9)
        clear = 0xFFFFFBFFu;
    else if (k == 10)
        clear = 0xFFFFFDFFu;
    if (clear != 0xFFFFFFFFu) GRef<u32>(st + 0x28C) = W(st + 0x28C) & clear;
    if (((GRef<s32>(st + 0x28C) >> (k & 0x1F)) & 1) != 0) return;
    func_00253000(k);
    GRef<u32>(st + 0x28C) = W(st + 0x28C) | (1u << (k & 0x1F));
}

namespace {

// XYZ2 word at (cx + dx, cy + dy) with Z 0xFFFFFF (the sums are 32-bit, sign-extended).
u64 GuideXy(s32 dx, s32 dy) {
    const s64 x = static_cast<s32>(static_cast<u32>(HudU16(0x24)) + static_cast<u32>(dx));
    const s64 y = static_cast<s32>(static_cast<u32>(HudU16(0x26)) + static_cast<u32>(dy));
    return static_cast<u64>(x) | (static_cast<u64>(y) << 16) | kZFar;
}

}  // namespace

// EE: 0x00166A10
// Landing guidance (sub-state 0x15): the runway point (rules +0x00, offset; carrier missions
// status flag 0x02000000 use the carrier offsets and approach angles C 0x0040E8E0..0x0040E8E8) is
// taken into the approach frame; the heading error by bearing sectors (C 0x0040E8F0..0x0040E928)
// triggers the turn hints 6 / 7 while far behind. With the nose on screen and the aircraft on the
// approach (frame z < -256, bearing and heading inside C 0x0040E92C / 0x0040E930) the ILS cross is
// drawn (localizer and glide lines offset by the frame x / y clamped to +-0x600, ticks every 0x300,
// end arrows). Past the runway (z > 0) and low, hint 0xD; then the arrow to the runway point; before
// it, the distance hints 3/4/5 and in the last 400-500 the speed / glide / alignment hints 8-11, and
// with the nose on screen the distance readout.
// fpucheck: verified 16 (the original shares the corr + C 0x0040E910 add between two paths)
void func_00166A10(u32 cam, u32 list, u32 unit, s32 nose_on) {
    const u32 st = W(unit + 0x44);
    guest::GuestFrame fr(0x80);
    const u32 T = fr.Address(0), ang = fr.Address(0x10), m = fr.Address(0x20), r = fr.Address(0x60), q = fr.Address(0x70);
    ZeroQ(ang);
    CopyQ(T, Rules());
    if ((StatusFlags() & 0x02000000u) != 0) {
        GRef<f32>(T) = fpu::Add(F(T), 61.0f);
        GRef<f32>(T + 4) = fpu::Add(F(T + 4), -8.0f);
        GRef<f32>(T + 8) = fpu::Add(F(T + 8), F(0x0040E8E0));
        GRef<u32>(ang) = W(0x0040E8E4);
        GRef<u32>(ang + 4) = W(0x0040E8E8);
    } else {
        GRef<f32>(T) = fpu::Add(F(T), 0.0f);
        GRef<f32>(T + 4) = fpu::Add(F(T + 4), -8.0f);
        GRef<f32>(T + 8) = fpu::Add(F(T + 8), 2816.0f);
        GRef<f32>(T + 0xC) = 1.0f;
        GRef<u32>(ang) = W(0x0040E8EC);
    }
    const f32 dist = func_00104F10(unit, T);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, ang);
    sceVu0InversMatrix(m, m);
    sceVu0SubVector(r, unit, T);
    sceVu0ApplyMatrix(r, m, r);
    const f32 dyaw = fpu::Sub(F(unit + 0x14), F(ang + 4));
    const f32 bearing = ee::atan2f(-F(r), -F(r + 8));
    f32 corr;
    const auto side = [&](f32 k) {
        if (k < bearing) {
            corr = fpu::Sub(dyaw, F(0x0040E908));
            if (corr < F(0x0040E90C)) corr = fpu::Add(corr, F(0x0040E910));
        } else if (bearing < F(0x0040E914)) {
            corr = fpu::Add(dyaw, F(0x0040E918));
            if (F(0x0040E91C) < corr) corr = fpu::Sub(corr, F(0x0040E920));
        } else {
            corr = 0.0f;
        }
    };
    if (F(0x0040E8F0) < bearing) {
        if (bearing < F(0x0040E8F4)) {
            corr = fpu::Sub(dyaw, bearing);
            if (F(0x0040E8F8) < corr) corr = fpu::Sub(corr, F(0x0040E8FC));
            if (corr < F(0x0040E900)) corr = fpu::Add(corr, F(0x0040E910));
        } else {
            side(F(0x0040E8F4));
        }
    } else {
        side(F(0x0040E904));
    }
    CopyQ(q, r);
    if (F(q + 8) < -256.0f) {
        if (corr < 0.0f && corr < F(0x0040E924)) func_0017B1E0(6);
        if (0.0f < corr && F(0x0040E928) < corr) func_0017B1E0(7);
    }
    if (nose_on == 1 && GRef<s8>(st + 0x149) != 2 && F(q + 8) < -256.0f && dyaw < F(0x0040E92C) && F(0x0040E930) < dyaw &&
        bearing < F(0x0040E92C) && F(0x0040E930) < bearing) {
        func_0015F2B0(list, 0, 0, -0x80, 0x40, 0x60);
        s32 sx = fpu::FloatToInt(-F(q));
        s32 sy = fpu::FloatToInt(-F(q + 4));
        if (!(sx < 0x601)) sx = 0x600;
        if (!(sy < 0x601)) sy = 0x600;
        if (!(-0x601 < sx)) sx = -0x600;
        if (!(-0x601 < sy)) sy = -0x600;
        u32 p = AllocBucketPacket(list, 0, 0x10, 0, 0x50000010);
        Put64(p, 1, 0x0555555555555550ull);
        Put64(p, 0, 0xF400000000008002ull);
        const s32 a[14][2] = {{sx, -0x600}, {sx, 0x600}, {sx + 0x40, -0x300}, {sx + 0x80, -0x300}, {sx - 0x40, -0x300},
                              {sx - 0x80, -0x300}, {sx + 0x40, 0}, {sx + 0x80, 0}, {sx - 0x40, 0}, {sx - 0x80, 0},
                              {sx + 0x40, 0x300}, {sx + 0x80, 0x300}, {sx - 0x40, 0x300}, {sx - 0x80, 0x300}};
        const s32 b[14][2] = {{-0x600, sy}, {0x600, sy}, {-0x300, sy + 0x40}, {-0x300, sy + 0x80}, {-0x300, sy - 0x40},
                              {-0x300, sy - 0x80}, {0, sy + 0x40}, {0, sy + 0x80}, {0, sy - 0x40}, {0, sy - 0x80},
                              {0x300, sy + 0x40}, {0x300, sy + 0x80}, {0x300, sy - 0x40}, {0x300, sy - 0x80}};
        u32 o = p + 0x10;
        GRef<u64>(o) = 0x41;
        o += 8;
        for (const auto& v : a) {
            GRef<u64>(o) = GuideXy(v[0], v[1]);
            o += 8;
        }
        GRef<u64>(o) = 0x41;
        o += 8;
        for (const auto& v : b) {
            GRef<u64>(o) = GuideXy(v[0], v[1]);
            o += 8;
        }
        p = AllocBucketPacket(list, 0, 9, 0, 0x50000009);
        Put64(p, 1, 0x5550);
        Put64(p, 0, 0x4400000000008004ull);
        const s32 c[4][3][2] = {{{sx - 0x40, -0x620}, {sx, -0x600}, {sx + 0x40, -0x620}},
                                {{sx - 0x40, 0x620}, {sx, 0x600}, {sx + 0x40, 0x620}},
                                {{-0x620, sy - 0x40}, {-0x600, sy}, {-0x620, sy + 0x40}},
                                {{0x620, sy - 0x40}, {0x600, sy}, {0x620, sy + 0x40}}};
        o = p + 0x10;
        for (const auto& tri : c) {
            GRef<u64>(o) = 0x42;
            o += 8;
            for (const auto& v : tri) {
                GRef<u64>(o) = GuideXy(v[0], v[1]);
                o += 8;
            }
        }
    }
    if (0.0f < F(q + 8) && F(q + 4) < -512.0f) func_0017B1E0(0xD);
    func_00166628(cam, list, T, 0);
    if (!(F(q + 8) < 0.0f)) return;
    f32 d = dist / F(0x0040E934);
    if (d < F(0x0040E938) && F(0x0040E93C) < d)
        func_0017B1E0(3);
    else if (d < 2000.0f && F(0x0040E940) < d)
        func_0017B1E0(4);
    else if (d < 1000.0f && 900.0f < d)
        func_0017B1E0(5);
    if (d < 500.0f && 400.0f < d) {
        if (F(0x0040E944) < func_00104FA8(unit + 0x20)) {
            func_0017B1E0(8);
        } else {
            const f32 g = fpu::Sub(F(Rules() + 4), 240.0f);
            const f32 y = F(unit + 4);
            if (y < fpu::Sub(g, 512.0f))
                func_0017B1E0(9);
            else if (fpu::Add(g, 120.0f) < y)
                func_0017B1E0(10);
            else if (F(0x0040E948) < bearing && bearing < F(0x0040E94C) && F(0x0040E950) < dyaw && dyaw < F(0x0040E954))
                func_0017B1E0(0xB);
        }
    }
    if (nose_on != 1) return;
    if ((GRef<u8>(0x002A15CC) & 4) != 0) d = d * F(0x0040E958);
    s32 nd;
    if (F(0x0040E95C) <= d)
        nd = 5;
    else if (1000.0f <= d)
        nd = 4;
    else if (100.0f <= d)
        nd = 3;
    else
        nd = 10.0f <= d ? 2 : 1;
    func_0015F6A0(list, 1, 0x00410A30, static_cast<u16>(HudU16(0x24) - 0x280u), static_cast<u16>(HudU16(0x26) + 0x800u), 0x108);
    func_0015FA18(list, 1, fpu::FloatToInt(d), static_cast<u16>(HudU16(0x24) + 0x80u), static_cast<u16>(HudU16(0x26) + 0x800u),
                  0x108, nd);
}

namespace {

// XYZ2 doubleword from signed 32-bit x, y (sign-extended as the EE's addiu/dsll leave them) with
// Z 0xFFFFFF.
u64 XyS(s32 x, s32 y) {
    return static_cast<u64>(static_cast<s64>(x)) | static_cast<u64>(static_cast<s64>(y) << 16) | kZFar;
}

// A+D packet (bucket index) writing TEST_1 (0x47).
void HudTest(u32 list, s32 index, u64 test) {
    const u32 p = AllocBucketPacket(list, index, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008001ull);
    Put64(p, 2, test);
    Put64(p, 3, 0x47);
}

}  // namespace

// EE: 0x00167758
// Mission-kind-1 progress panel (bucket 2, top centre): a dark box, then two bars from x =
// centre - 0x400 (rules +0x44 = full width 0x800): the mission value 0x0017BDD0(status +0x24) in
// black and the unit's +0x5C in colour (0xEC,0x6B,0x76), which brightens to white with scene
// +0x1654 (capped at 8) during sub-state 10. The box is then drawn into the alpha channel
// (TEST 0x3C000), outlined (TEST 0x30000, 4 lines), and the panel label sprite is drawn twice.
void func_00167758(u32 list, u32 unit) {
    const u32 cx = HudU16(0x14), cy = HudU16(0x16);
    const s32 left = static_cast<s32>(cx - 0x440u), top = static_cast<s32>(cy - 0xC80u);
    func_0015F350(list, 2, 1);
    u32 p = AllocBucketPacket(list, 2, 3, 0, 0x50000003);
    Put64(p, 1, 0x5510);
    Put64(p, 0, 0x4400000000008001ull);
    Put64(p, 3, 0x180000000ull);
    Put64(p, 2, 0x46);
    Put64(p, 5, XyS(static_cast<s32>(cx + 0x440u), static_cast<s32>(cy - 0xB60u)));
    Put64(p, 4, XyS(left, top));
    const s32 bx = static_cast<s32>(HudU16(0x14) - 0x400u);
    const u32 full = W(Rules() + 0x44);
    const u32 w0 = (static_cast<u32>(func_0017BDD0(GRef<s32>(Status() + 0x24))) << 11) / full;
    const u32 w1 = (static_cast<u32>(GRef<u16>(unit + 0x5C)) << 11) / full;
    func_0015F350(list, 2, 0);
    func_0015F420(list, 2, 0, 0);
    p = AllocBucketPacket(list, 2, 5, 0, 0x50000005);
    Put64(p, 1, 0x05515510);
    Put64(p, 0, 0x7400000000008001ull);
    const s32 ymid = static_cast<s32>(cy - 0xBB0u);
    Put64(p, 2, 0x46);
    Put64(p, 3, 0x140000000ull);
    Put64(p, 4, XyS(bx, static_cast<s32>(cy - 0xBD0u)));
    Put64(p, 5, XyS(static_cast<s32>(static_cast<u32>(bx) + w0), ymid));
    Put64(p, 6, 0x140766BECull);
    Put64(p, 7, XyS(bx, ymid));
    Put64(p, 8, XyS(static_cast<s32>(static_cast<u32>(bx) + w1), static_cast<s32>(cy - 0xB90u)));
    Put64(p, 9, 0);
    HudTest(list, 2, 0x3C000);
    s32 k = 8;
    if (Ctx()->sub == 10) {
        k = GRef<s32>(Scene() + 0x1654);
        if (!(k < 9)) k = 8;
    }
    const s64 r = (k * 0x13) / 8 + 0xEC, g = (k * 0x94) / 8 + 0x6B, b = (k * 0x89) / 8 + 0x76;
    p = AllocBucketPacket(list, 2, 3, 0, 0x50000003);
    Put64(p, 0, 0x4400000000008001ull);
    Put64(p, 1, 0x5510);
    Put64(p, 2, 0x46);
    Put64(p, 3, static_cast<u64>(r) | (static_cast<u64>(b << 16) | static_cast<u64>(g << 8)) | 0x100000000ull | 0x40000000ull);
    const s32 right = static_cast<s32>(static_cast<u32>(left) + 0x880u), bottom = static_cast<s32>(static_cast<u32>(top) + 0x120u);
    Put64(p, 4, XyS(left, top));
    Put64(p, 5, XyS(right, bottom));
    HudTest(list, 2, 0x30000);
    func_0015F2B0(list, 2, 0, 0, 0, 0x40);
    p = AllocBucketPacket(list, 2, 9, 0, 0x50000009);
    Put64(p, 1, 0x55);
    Put64(p, 0, 0x2020C00000008004ull);
    const s32 l2 = static_cast<s32>(static_cast<u32>(left) + 0x10u), t2 = static_cast<s32>(static_cast<u32>(top) + 0x10u);
    const s32 v[8][2] = {{l2, top}, {right, top}, {l2, bottom}, {right, bottom},
                         {right, t2}, {right, bottom}, {left, t2}, {left, bottom}};
    u32 o = p + 0x10;
    for (const auto& e : v) {
        GRef<s32>(o) = e[0];
        GRef<s32>(o + 4) = e[1];
        GRef<u32>(o + 8) = 0xFFFFFF;
        GRef<u32>(o + 0xC) = 0;
        o += 0x10;
    }
    const u16 sx = static_cast<u16>(static_cast<u32>(left) + 0x50u), sy = static_cast<u16>(static_cast<u32>(top) + 0x50u);
    func_0015F2B0(list, 2, 0, 0, 0, -0x80);
    DrawHudSprite(list, 2, sx, sy, 0x520, 0x970, 0x140, 0x50, 0);
    func_0015F2B0(list, 2, 0, 0, 0, -0x80);
    DrawHudSprite(list, 2, sx, sy, 0x520, 0x970, 0x140, 0x50, 0);
    func_0015F350(list, 2, 1);
    HudColour(list, 2);
}

// EE: 0x00167D68
// Versus mode (kind 2): while this player's +0x5C is above the other player's, a blinking red
// "leading" sprite (alpha 3 x the triangle wave of scene +0x165C) is drawn at the top right.
void func_00167D68(u32 list, u32 unit) {
    const u32 other = GRef<u8>(unit + 0x55) == 0 ? Unit(1) : Unit(0);
    if (!(GRef<u16>(other + 0x5C) < GRef<u16>(unit + 0x5C))) return;
    const u32 y = HudU16(0x1E) + (Wide() ? 0x120u : 0x180u);
    const u32 c = W(Scene() + 0x165C);
    u32 t = c & 0x1F;
    if ((c & 0x20u) != 0) t = 0x20 - t;
    func_0015F350(list, 2, 1);
    func_0015F2B0(list, 2, -1, 0, 0, static_cast<s8>(t * 3));
    DrawHudSprite(list, 2, static_cast<u16>(HudU16(0x20) - 0x390u), static_cast<u16>(y), 0x690, 0xD30, 0x2B0, 0xE0, 0);
    HudColour(list, 2);
}

// EE: 0x00167EC0
// Radar runway markers (bucket 3, green): the 4 runways of table 0x002B7358 (or the single carrier
// entry 0x002B7388 when status flag 0x04 is set), each {x, z, heading}, as a 4-vertex shape from
// 0x002B7338 scaled by 4000 and turned by the heading, relative to the radar centre pos, rotated
// by (s, c), scaled by w / h over range and placed at (sx, sy + h / 4).
// fpucheck: 8 adds
void func_00167EC0(f32 range, f32 s, f32 c, u32 list, u32 pos, s32 sx, s32 sy, s32 w, s32 h) {
    u32 tab;
    s32 n;
    if ((StatusFlags() & 4u) != 0) {
        tab = 0x002B7388;
        n = 1;
    } else {
        tab = 0x002B7358;
        n = 4;
    }
    func_0015F2B0(list, 3, 0, -0x80, 0x40, 0x60);
    const f32 fw = fpu::IntToFloat(w), fh = fpu::IntToFloat(h);
    const s32 yo = (h < 0 ? h + 3 : h) >> 2;
    for (s32 i = 0; i < n; ++i) {
        const u32 p = AllocBucketPacket(list, 3, 5, 0, 0x50000005);
        Put64(p, 1, 5);
        Put64(p, 0, 0x1021400000008004ull);
        const u32 e = tab + 12u * static_cast<u32>(i);
        const f32 sn = ee::sinf(F(e + 8));
        const f32 cs = ee::cosf(F(e + 8));
        const f32 dx = fpu::Sub(F(e), F(pos));
        const f32 dz = fpu::Sub(F(e + 4), F(pos + 8));
        u32 o = p + 0x10;
        for (u32 k = 0; k < 4; ++k) {
            const f32 tx = F(0x002B7338 + 8 * k), tz = F(0x002B7338 + 8 * k + 4);
            const f32 z = fpu::Add(fpu::Add(-sn * tx, cs * tz) * 4000.0f, dz);
            const f32 x = fpu::Add(fpu::Add(cs * tx, sn * tz) * 4000.0f, dx);
            GRef<u32>(o + 8) = 0xFFFFFE;
            GRef<u32>(o + 0xC) = 0;
            const f32 px = fpu::Sub(c * x, s * z) * fw / range;
            const f32 py = -fpu::Add(s * x, c * z) * fh / range;
            GRef<s32>(o) = fpu::FloatToInt(px) + sx;
            GRef<s32>(o + 4) = fpu::FloatToInt(py) + yo + sy;
            o += 0x10;
        }
    }
}

namespace {

// (a - b) * (c - d) / (e - f) + g with the EE's 32-bit mult / div.
s32 Lerp32(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g) {
    const s32 m = static_cast<s32>(static_cast<u32>(a - b) * static_cast<u32>(c - d));
    return m / (e - f) + g;
}

// Clips the radar line (x0, y0)-(x1, y1) to +-w / +-h, end 0 then end 1, x before y (EE integer
// interpolation). False when the line lies entirely beyond one edge.
bool ClipRadarLine(s32& x0, s32& y0, s32& x1, s32& y1, s32 w, s32 h) {
    if (w < x0 && w < x1) return false;
    if (x0 < -w && x1 < -w) return false;
    if (h < y0 && h < y1) return false;
    if (y0 < -h && y1 < -h) return false;
    if (w < x0) {
        if (x0 != x1) y0 = Lerp32(y0, y1, w, x1, x0, x1, y1);
        x0 = w;
    } else if (x0 < -w) {
        if (x0 != x1) y0 = Lerp32(y0, y1, -w, x1, x0, x1, y1);
        x0 = -w;
    }
    if (h < y0) {
        if (y0 != y1) x0 = Lerp32(x0, x1, h, y1, y0, y1, x1);
        y0 = h;
    } else if (y0 < -h) {
        if (y0 != y1) x0 = Lerp32(x0, x1, -h, y1, y0, y1, x1);
        y0 = -h;
    }
    if (w < x1) {
        if (x0 != x1) y1 = Lerp32(y1, y0, w, x0, x1, x0, y0);
        x1 = w;
    } else if (x1 < -w) {
        if (x0 != x1) y1 = Lerp32(y1, y0, -w, x0, x1, x0, y0);
        x1 = -w;
    }
    if (h < y1) {
        if (y0 != y1) x1 = Lerp32(x1, x0, h, y0, y1, y0, x0);
        y1 = h;
    } else if (y1 < -h) {
        if (y0 != y1) x1 = Lerp32(x1, x0, -h, y0, y1, y0, x0);
        y1 = -h;
    }
    return true;
}

}  // namespace

// EE: 0x00168168
// Radar runway outlines (bucket 3, grey): the 4 segments of table 0x002B7398 ({x0, z0, x1, z1})
// relative to the radar centre pos, rotated by (s, c) and scaled by w / h over range (y offset
// h / 4), clipped to +-w / +-h and drawn as lines at (sx, sy). A segment entirely outside one
// edge ends the whole function (the remaining segments are not drawn).
// fpucheck: 6 adds
void func_00168168(f32 range, f32 s, f32 c, u32 list, u32 pos, s32 sx, s32 sy, s32 w, s32 h) {
    const s32 yo = (h < 0 ? h + 3 : h) >> 2;
    const f32 fw = fpu::IntToFloat(w), fh = fpu::IntToFloat(h);
    for (u32 i = 0; i < 4; ++i) {
        const u32 e = 0x002B7398 + 0x10 * i;
        const f32 X[2] = {fpu::Sub(F(e), F(pos)), fpu::Sub(F(e + 8), F(pos))};
        const f32 Z[2] = {fpu::Sub(F(e + 4), F(pos + 8)), fpu::Sub(F(e + 0xC), F(pos + 8))};
        s32 px[2], py[2];
        for (u32 k = 0; k < 2; ++k) {
            const f32 a = fpu::Sub(c * X[k], s * Z[k]) * fw / range;
            const f32 b = -fpu::Add(s * X[k], c * Z[k]) * fh / range;
            px[k] = fpu::FloatToInt(a);
            py[k] = fpu::FloatToInt(b) + yo;
        }
        s32 x0 = px[0], y0 = py[0], x1 = px[1], y1 = py[1];
        if (!ClipRadarLine(x0, y0, x1, y1, w, h)) return;
        func_0015F2B0(list, 3, 0x40, 0x40, 0x40, 0x60);
        const u32 p = AllocBucketPacket(list, 3, 3, 0, 0x50000003);
        Put64(p, 1, 0x55);
        Put64(p, 0, 0x2020C00000008001ull);
        GRef<s32>(p + 0x10) = x0 + sx;
        GRef<s32>(p + 0x14) = y0 + sy;
        GRef<u32>(p + 0x18) = 0xFFFFFE;
        GRef<u32>(p + 0x1C) = 0;
        GRef<s32>(p + 0x20) = x1 + sx;
        GRef<s32>(p + 0x24) = y1 + sy;
        GRef<u32>(p + 0x2C) = 0;
        GRef<u32>(p + 0x28) = 0xFFFFFE;
    }
}

// EE: 0x00168620
// Radar approach line (bucket 3, blue): the segment x = 14464 from z = C 0x0040E960 to
// C 0x0040E964, relative to the radar centre pos, rotated by (s, c), scaled by w / h over range
// (y offset h / 4), clipped to +-w / +-h and drawn at (sx, sy).
// fpucheck: 5 adds
void func_00168620(f32 range, f32 s, f32 c, u32 list, u32 pos, s32 sx, s32 sy, s32 w, s32 h) {
    const s32 yo = (h < 0 ? h + 3 : h) >> 2;
    const f32 fw = fpu::IntToFloat(w), fh = fpu::IntToFloat(h);
    const f32 x = fpu::Sub(14464.0f, F(pos));
    const f32 Z[2] = {fpu::Sub(F(0x0040E960), F(pos + 8)), fpu::Sub(F(0x0040E964), F(pos + 8))};
    s32 px[2], py[2];
    for (u32 k = 0; k < 2; ++k) {
        const f32 a = fpu::Sub(c * x, s * Z[k]) * fw / range;
        const f32 b = -fpu::Add(s * x, c * Z[k]) * fh / range;
        px[k] = fpu::FloatToInt(a);
        py[k] = fpu::FloatToInt(b) + yo;
    }
    s32 x0 = px[0], y0 = py[0], x1 = px[1], y1 = py[1];
    if (!ClipRadarLine(x0, y0, x1, y1, w, h)) return;
    func_0015F2B0(list, 3, 0, 0x3C, 0x78, 0x60);
    const u32 p = AllocBucketPacket(list, 3, 3, 0, 0x50000003);
    Put64(p, 0, 0x2020C00000008001ull);
    Put64(p, 1, 0x55);
    GRef<s32>(p + 0x10) = x0 + sx;
    GRef<s32>(p + 0x14) = y0 + sy;
    GRef<u32>(p + 0x18) = 0xFFFFFE;
    GRef<u32>(p + 0x1C) = 0;
    GRef<s32>(p + 0x20) = x1 + sx;
    GRef<s32>(p + 0x24) = y1 + sy;
    GRef<u32>(p + 0x28) = 0xFFFFFE;
    GRef<u32>(p + 0x2C) = 0;
}

// EE: 0x00168A08
// Radar ring (bucket 3, gouraud strip): 17 points of the unit circle table 0x00404D08 (x scaled
// by 0.75 in 16:9) at radii outer / inner around (cx, cy); blue-green (0, a x 16 / 255,
// a x 8 / 255, a) with alpha a_outer on the outer edge and a_inner on the inner edge.
void func_00168A08(f32 outer, f32 inner, u32 list, s32 cx, s32 cy, s32 a_outer, s32 a_inner) {
    const s32 co[4] = {0, (a_outer << 4) / 0xFF, (a_outer << 3) / 0xFF, a_outer};
    const s32 ci[4] = {0, (a_inner << 4) / 0xFF, (a_inner << 3) / 0xFF, a_inner};
    func_0015F350(list, 3, 1);
    const u32 p = AllocBucketPacket(list, 3, 0x45, 0, 0x50000045);
    Put64(p, 1, 0x51);
    Put64(p, 0, 0x2026400000008022ull);
    const f32 k = Wide() ? 0.75f : 1.0f;
    u32 o = p + 0x10;
    for (u32 i = 0; i < 17; ++i) {
        const f32 tx = F(0x00404D08 + 8 * i) * k, ty = F(0x00404D08 + 8 * i + 4);
        for (u32 j = 0; j < 4; ++j) GRef<s32>(o + 4 * j) = co[j];
        GRef<s32>(o + 0x10) = cx + fpu::FloatToInt(outer * tx);
        GRef<s32>(o + 0x14) = cy + fpu::FloatToInt(outer * ty);
        GRef<u32>(o + 0x18) = 0xFFFFFF;
        GRef<u32>(o + 0x1C) = 0;
        for (u32 j = 0; j < 4; ++j) GRef<s32>(o + 0x20 + 4 * j) = ci[j];
        GRef<s32>(o + 0x30) = cx + fpu::FloatToInt(inner * tx);
        GRef<s32>(o + 0x34) = cy + fpu::FloatToInt(inner * ty);
        GRef<u32>(o + 0x38) = 0xFFFFFF;
        GRef<u32>(o + 0x3C) = 0;
        o += 0x40;
    }
}

// EE: 0x00168CD0
// Radar sweep pulse: a bright ring expanding with the low byte of the scene clock +0x165C
// (t = byte / 255, half width C 0x0040E968 x radius) over the radar disc of radius r, built from
// 0x00168A08 rings; once t passes C 0x0040E970 the ring fades against the rim at
// C 0x0040E96C x r.
// fpucheck: 5 adds
void func_00168CD0(f32 r, u32 list, s32 cx, s32 cy) {
    const f32 t = fpu::UnsignedToFloat(W(Scene() + 0x165C) & 0xFFu) / 255.0f;
    const f32 hw = F(0x0040E968);
    f32 r0 = r * fpu::Sub(t, hw);
    if (r0 < 0.0f) r0 = 0.0f;
    const f32 rim = F(0x0040E96C);
    const f32 rm = r * t, r1 = r * fpu::Add(t, hw), rr = r * rim;
    func_0015F350(list, 3, 1);
    if (0.0f < r0) func_00168A08(r0, 0.0f, list, cx, cy, 0xFF, 0xFF);
    if (t < F(0x0040E970)) {
        func_00168A08(rm, r0, list, cx, cy, 0, 0xFF);
        func_00168A08(r1, rm, list, cx, cy, 0xFF, 0);
        func_00168A08(rr, r1, list, cx, cy, 0xFF, 0xFF);
        func_00168A08(r, rr, list, cx, cy, 0, 0xFF);
    } else if (t < rim) {
        const s32 k = fpu::FloatToInt(fpu::Sub(rim, t) * 255.0f / hw);
        func_00168A08(rm, r0, list, cx, cy, 0, 0xFF);
        func_00168A08(rr, rm, list, cx, cy, k, 0);
        func_00168A08(r, rr, list, cx, cy, 0, k);
    } else {
        const s32 k = fpu::FloatToInt(fpu::Sub(t, rim) * 255.0f / hw);
        func_00168A08(rr, r0, list, cx, cy, k, 0xFF);
        func_00168A08(r, rr, list, cx, cy, 0, k);
    }
}

namespace {

// Radar frame (4 corner sprites, bucket 3): corner cells at u0 / u1 (v 0x9C8..0xAE8), inset d.
void RadarFrame(u32 list, s32 l, s32 t, s32 r, s32 b, s32 d, u32 u0, u32 u1) {
    const u32 p = AllocBucketPacket(list, 3, 0xB, 0, 0x5000000B);
    Put64(p, 0, 0x1400000000008001ull);
    Put64(p, 1, 0);
    Put64(p, 2, 0x156);
    Put64(p, 3, 0);
    Put64(p, 4, 0x4400000000008004ull);
    Put64(p, 5, 0x5353);
    const auto uv = [](s32 u, u32 v) { return static_cast<u64>(static_cast<s64>(u)) | (static_cast<u64>(v) << 16); };
    const s32 du0 = d + static_cast<s32>(u0), du1 = static_cast<s32>(u1) - d;
    Put64(p, 6, uv(static_cast<s32>(u0), 0x9C8));
    Put64(p, 7, XyS(l, t));
    Put64(p, 8, uv(du0, 0xA48));
    Put64(p, 9, XyS(l + d, t + 0x80));
    Put64(p, 10, uv(du1, 0x9C8));
    Put64(p, 11, XyS(r - d, t));
    Put64(p, 12, uv(static_cast<s32>(u1), 0xA48));
    Put64(p, 13, XyS(r, t + 0x80));
    Put64(p, 14, uv(static_cast<s32>(u0), 0xA68));
    Put64(p, 15, XyS(l, b - 0x80));
    Put64(p, 16, uv(du0, 0xAE8));
    Put64(p, 17, XyS(l + d, b));
    Put64(p, 18, uv(du1, 0xA68));
    Put64(p, 19, XyS(r - d, b - 0x80));
    Put64(p, 20, uv(static_cast<s32>(u1), 0xAE8));
    Put64(p, 21, XyS(r, b));
}

// Line packet (bucket 3) through n vertices of 32-bit (x, y) with Z 0xFFFFFE; tag with nloop.
void RadarLines(u32 list, u64 tag, const s32 (*v)[2], u32 n) {
    const u32 qw = n + 1;
    const u32 p = AllocBucketPacket(list, 3, qw, 0, 0x50000000u | qw);
    Put64(p, 1, 0x55);
    Put64(p, 0, tag);
    u32 o = p + 0x10;
    for (u32 i = 0; i < n; ++i) {
        GRef<s32>(o) = v[i][0];
        GRef<s32>(o + 4) = v[i][1];
        GRef<u32>(o + 8) = 0xFFFFFE;
        GRef<u32>(o + 0xC) = 0;
        o += 0x10;
    }
}

// Small sprite (bucket 3) from (x - 0xC, y - 0xC) to (x + 0xC, y + 0xC), Z 0xFFFFFF.
void RadarDot(u32 list, s32 x, s32 y) {
    const u32 p = AllocBucketPacket(list, 3, 3, 0, 0x50000003);
    Put64(p, 0, 0x2023400000008001ull);
    Put64(p, 1, 0x55);
    GRef<s32>(p + 0x10) = x - 0xC;
    GRef<s32>(p + 0x14) = y - 0xC;
    GRef<u32>(p + 0x18) = 0xFFFFFF;
    GRef<u32>(p + 0x1C) = 0;
    GRef<s32>(p + 0x20) = x + 0xC;
    GRef<s32>(p + 0x24) = y + 0xC;
    GRef<u32>(p + 0x28) = 0xFFFFFF;
    GRef<u32>(p + 0x2C) = 0;
}

}  // namespace

// EE: 0x00169010
// Radar (bucket 3, lower left; layout 0x00404D90 / 16:9 0x00404DA8 {half width, frame, tick
// inset, corner inset 1, corner inset 2}, centre (left edge + half width + 0x180 / 0x100 in
// two-player, bottom - 0x5C0 / 0x500), half height 0x400). The range (12288 / 18432 / 32768) comes
// from the distance of the locked target, or else of the nearest radar-visible enemy. World
// positions relative to the unit, turned by its heading, are scaled by half width / range in x and
// by 0x00410A38 / range in y (+0x100). Draws, under its own scissor: the dark backing and frame,
// crosshair, range ticks and view cone; mission 16 sweep pulses at the units of list +0x80
// (0x00168CD0); every shown unit as an icon (player white, by side and threat colour, heading cell
// for aircraft, marker for flagged units; alpha by distance for class bit 8); missiles and
// specials as dots (blinking, unless the state flag 0x08000000 or status flag 0x10000000 is set);
// the mission area border (rules +0x00 +-32768 with status flag 0x10000000, else +-rules +0x20)
// and, in some missions, the line z = rules +0x1C as a dashed sprite; the approach line (status
// flag 0x01000000) and mission 15's runways; then the HUD-coloured frame.
// fpucheck: verified 28 (the projection lambda holds 12 of them)
void DrawRadar(u32 cam, u32 list, u32 unit) {
    const u32 L = Wide() ? 0x00404DA8u : 0x00404D90u;
    const auto lay = [L](u32 i) { return GRef<s32>(L + 4 * i); };
    const s32 sz = lay(0);
    s32 X, Y;
    if (Players() == 2) {
        X = sz + 0x100;
        Y = static_cast<s32>(HudU16(0x22) - 0x500u);
    } else {
        X = sz + 0x180;
        Y = static_cast<s32>(HudU16(0x22) - 0x5C0u);
    }
    X = static_cast<s32>(HudU16(0x1C) + static_cast<u32>(X));
    const u32 team = W(unit + 0x48) & 3u;
    const u32 me = GRef<u8>(unit + 0x55);
    const u32 prox = W(0x00410648 + 4u * me);
    f32 nearest = 32768.0f;
    if (GRef<u8>(unit + 0x62) != 0xFF) {
        const u32 e = prox + 12u * GRef<u8>(unit + 0x62);
        if ((W(e) & 2u) != 0) nearest = F(e + 4);
    } else {
        for (s32 i = 0; i < UnitCount(); ++i) {
            if (me == static_cast<u32>(i)) continue;
            const u32 u = Unit(i);
            const u32 nf = ~W(u + 0x4C);
            if ((nf & 0x10000000u) != 0 || (nf & 0x01000000u) != 0) continue;
            const u8 cls = GRef<u8>(u + 0x64);
            if (cls == 0 || cls == 0x14) continue;
            if ((W(u + 0x48) & 3u) == team) continue;
            const u32 e = prox + 12u * static_cast<u32>(i);
            if ((~W(e) & 2u) != 0) continue;
            if (F(e + 4) < nearest) nearest = F(e + 4);
        }
    }
    const s32 left = X - sz, right = X + sz, top = Y - 0x400, bottom = Y + 0x400;
    const f32 fsz = fpu::IntToFloat(sz), fv = fpu::IntToFloat(GRef<s32>(0x00410A38));
    f32 range;
    if (16384.0f < nearest)
        range = 32768.0f;
    else
        range = 8192.0f < nearest ? 18432.0f : 12288.0f;
    const f32 sn = ee::sinf(F(unit + 0x14));
    const f32 cs = ee::cosf(F(unit + 0x14));
    guest::GuestFrame fr(0x20);
    const u32 rel = fr.Address(0);
    // Radar position of rel (x, y before the centre offset).
    const auto project = [&](f32 x, f32 z, s32& px, s32& py) {
        const f32 a = fpu::Sub(cs * x, sn * z) * fsz / range;
        const f32 b = -fpu::Add(sn * x, cs * z) * fv / range;
        px = fpu::FloatToInt(a);
        py = fpu::FloatToInt(b) + 0x100;
    };

    // Scissor to the radar box.
    u32 p = AllocBucketPacket(list, 3, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008001ull);
    {
        const s64 c188 = GRef<s16>(cam + 0x188), c18a = GRef<s16>(cam + 0x18A), c18e = GRef<s16>(cam + 0x18E);
        s64 x0, x1, y0, y1;
        if (Players() == 2) {
            x1 = c188 + ((sz * 2 + 0x100) >> 4) + 1;
            y0 = c18a + c18e - 0x91;
            y1 = c18a + 1 + (c18e + ((0x800 - 0x900) >> 4));
            x0 = c188 + 0xF;
        } else {
            x1 = c188 + ((sz * 2 + 0x180) >> 4) + 1;
            y0 = c18a + c18e - 0x9D;
            y1 = c18a + 1 + (c18e + ((0x800 - 0x9C0) >> 4));
            x0 = c188 + 0x17;
        }
        Put64(p, 2, static_cast<u64>(x0) | (static_cast<u64>(y1) << 48) | (static_cast<u64>(y0) << 32) | (static_cast<u64>(x1) << 16));
    }
    Put64(p, 3, 0x40);

    // Backing (alpha 0x80) into the alpha channel, then the dark panel and corner sprites.
    func_0015F350(list, 3, 1);
    p = AllocBucketPacket(list, 3, 4, 0, 0x50000004);
    Put64(p, 1, 0x551);
    Put64(p, 0, 0x3023400000008001ull);
    GRef<u32>(p + 0x10) = 0;
    GRef<u32>(p + 0x14) = 0;
    GRef<u32>(p + 0x18) = 0;
    GRef<u32>(p + 0x1C) = 0x80;
    GRef<s32>(p + 0x20) = left - lay(1);
    GRef<s32>(p + 0x24) = top - 0x80;
    GRef<u32>(p + 0x28) = 0xFFFFFE;
    GRef<u32>(p + 0x2C) = 0;
    GRef<s32>(p + 0x30) = right + lay(1);
    GRef<s32>(p + 0x34) = bottom + 0x80;
    GRef<u32>(p + 0x38) = 0xFFFFFE;
    GRef<u32>(p + 0x3C) = 0;
    HudTest(list, 3, 0x3000D);
    func_0015F350(list, 3, 0);
    func_0015F2B0(list, 3, 0, 0, 0, 0x40);
    RadarFrame(list, left, top, right, bottom, lay(3), 0xBC8, 0xCE8);
    {
        const s32 d = lay(3);
        p = AllocBucketPacket(list, 3, 7, 0, 0x50000007);
        Put64(p, 0, 0x2023400000008003ull);
        Put64(p, 1, 0x55);
        const s32 v[6][2] = {{left + 0x10, top + 0x80}, {left + d, bottom - 0x80}, {left + d, top + 0x10},
                             {right - d, bottom - 0x10}, {right - d, top + 0x80}, {right - 0x10, bottom - 0x80}};
        u32 o = p + 0x10;
        for (const auto& e : v) {
            GRef<s32>(o) = e[0];
            GRef<s32>(o + 4) = e[1];
            GRef<u32>(o + 8) = 0xFFFFFE;
            GRef<u32>(o + 0xC) = 0;
            o += 0x10;
        }
    }

    // Crosshair, range ticks and view cone.
    HudTest(list, 3, 0x34000);
    func_0015F2B0(list, 3, -0x80, -0x80, -0x80, 0x20);
    {
        const s32 v[4][2] = {{left + lay(2), Y + 0x100}, {right - lay(2), Y + 0x100}, {X, top + 0x40}, {X, bottom - 0x40}};
        RadarLines(list, 0x2020C00000008002ull, v, 4);
    }
    {
        s32 tick[2];
        u32 n;
        if (range == 32768.0f) {
            tick[0] = 0x100;
            tick[1] = 0x200;
            n = 2;
        } else if (range == 18432.0f) {
            tick[0] = 0x200;
            tick[1] = 0x400;
            n = 2;
        } else {
            tick[0] = 0x400;
            n = 1;
        }
        s32 v[8][2];
        for (u32 i = 0; i < n; ++i) {
            const s32 y = Y + 0x100 - tick[i];
            v[4 * i][0] = X + 0x80, v[4 * i][1] = y;
            v[4 * i + 1][0] = X + 0x40, v[4 * i + 1][1] = y;
            v[4 * i + 2][0] = X - 0x80, v[4 * i + 2][1] = y;
            v[4 * i + 3][0] = X - 0x40, v[4 * i + 3][1] = y;
        }
        RadarLines(list, static_cast<u64>(2 * n) | 0x8000u | (0x8083ull << 46), v, 4 * n);
    }
    p = AllocBucketPacket(list, 3, 4, 0, 0x50000004);
    Put64(p, 1, 5);
    Put64(p, 0, 0x1021400000008003ull);
    {
        const s32 v[3][2] = {{X - 0x1A0, top + 0x40}, {X, Y + 0x100}, {X + 0x1A0, top + 0x40}};
        u32 o = p + 0x10;
        for (const auto& e : v) {
            GRef<s32>(o) = e[0];
            GRef<s32>(o + 4) = e[1];
            GRef<u32>(o + 8) = 0xFFFFFE;
            GRef<u32>(o + 0xC) = 0;
            o += 0x10;
        }
    }
    const u32 st = W(unit + 0x44);

    // Mission 16: sweep pulses at the listed units (alternating order every frame).
    if (Game(0x27) == 0x10) {
        HudTest(list, 3, 0x30000);
        const u32 ul = UnitList();
        if (GRef<u8>(ul + 0x83) != 0) {
            f32 r;
            if (range < 18432.0f)
                r = 4096.0f;
            else
                r = range < 32768.0f ? 2048.0f : 1024.0f;
            for (u32 k = 0; k < GRef<u8>(UnitList() + 0x83); ++k) {
                const u32 a = UnitList();
                const u32 idx = (W(Scene() + 0x165C) & 1u) != 0 ? GRef<u8>(a + 0x83) - k - 1 : k;
                const u32 u = Unit(GRef<u8>(a + 0x80 + idx));
                if ((~W(u + 0x4C) & 0x01000000u) != 0) continue;
                sceVu0SubVector(rel, u, unit);
                s32 px, py;
                project(F(rel), F(rel + 8), px, py);
                func_00168CD0(r, list, px + X, py + Y);
            }
        }
        HudTest(list, 3, 0x34000);
    }

    // Unit icons.
    s32 isz[3];
    if (range < 18432.0f) {
        isz[0] = 0x80, isz[1] = 0x40, isz[2] = 0x90;
    } else if (range < 32768.0f) {
        isz[0] = 0x60, isz[1] = 0x30, isz[2] = 0x70;
    } else {
        isz[0] = 0x40, isz[1] = 0x20, isz[2] = 0x50;
    }
    if (Wide()) {
        for (s32& v : isz) {
            const s32 t = v * 3;
            v = (t < 0 ? t + 3 : t) >> 2;
        }
    }
    func_0015F350(list, 3, 1);
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u32 nf = ~W(u + 0x4C);
        if ((nf & 0x10000000u) != 0 || (nf & 0x01000000u) != 0) continue;
        const u8 cls = GRef<u8>(u + 0x64);
        if (cls == 0) continue;
        if (Game(0x27) == 0xB && (~StatusFlags() & 0x10000000u) != 0 && cls == 0x14 &&
            ((W(Scene() + 0x165C) >> 3) & 7u) != (static_cast<u32>(i) & 7u))
            continue;
        if (static_cast<u32>(i) == GRef<u8>(unit + 0x62) && (W(Scene() + 0x165C) & 8u) != 0) continue;
        sceVu0SubVector(rel, u, unit);
        s32 px, py;
        project(F(rel), F(rel + 8), px, py);
        if (sz < px || px < -sz || 0x400 < py || py < -0x400) continue;
        px += X;
        py += Y;
        s32 c[4];
        if (static_cast<u32>(i) == me) {
            c[0] = c[1] = c[2] = 0xFF;
        } else if (GRef<u8>(u + 0x54) == 0) {
            c[0] = 0, c[1] = 0x80, c[2] = 0x40;
        } else if ((W(u + 0x48) & 3u) == team) {
            c[0] = 0, c[1] = 0x3C, c[2] = 0x78;
        } else if (cls == 0x14) {
            c[0] = c[1] = c[2] = 0x60;
        } else if (GRef<s32>(u + 0x48) < 0) {
            c[0] = 0xB4, c[1] = 0x14, c[2] = 0;
        } else {
            const s16 pri = GRef<s16>(u + 0x60);
            if (!(pri < GRef<s16>(Rules() + 0x24))) {
                c[0] = 0xF2, c[1] = 0x65, c[2] = 0x22;
            } else if (pri < GRef<s16>(Rules() + 0x26)) {
                c[0] = c[1] = c[2] = 0x80;
            } else {
                c[0] = 0xF8, c[1] = 0xBD, c[2] = 0x40;
            }
        }
        if ((cls & 8) != 0) {
            if ((W(u + 0x4C) & 0x4000u) == 0) continue;
            const f32 d = F(prox + 12u * static_cast<u32>(i) + 4);
            c[3] = static_cast<s32>(fpu::FixUnsSfSi(fpu::Sub(32768.0f, d) * 96.0f * 3.0517578125e-05f));
        } else {
            c[3] = 0x60;
        }
        s32 half, cu, cv, cw, ch;
        if ((cls & 1) != 0) {
            const f32 h = fpu::Sub(fpu::Sub(F(u + 0x14), F(unit + 0x14)), F(0x0040E974)) / F(0x0040E978);
            cu = (fpu::FloatToInt(h) & 0xF) * 0xE0;
            cv = 0xF00;
            cw = 0xC0;
            ch = 0xC0;
            half = isz[0];
        } else {
            half = isz[1];
            cu = 0xEC0;
            cv = 0x9C0;
            cw = 0x60;
            ch = 0x60;
        }
        const auto sprite = [&](s32 u0, s32 v0, s32 u1, s32 v1, s32 hs) {
            const u32 q = AllocBucketPacket(list, 3, 6, 0, 0x50000006);
            Put64(q, 0, 0x50AB400000008001ull);
            Put64(q, 1, 0x53531);
            for (u32 j = 0; j < 4; ++j) GRef<s32>(q + 0x10 + 4 * j) = c[j];
            GRef<s32>(q + 0x20) = u0;
            GRef<s32>(q + 0x24) = v0;
            GRef<u32>(q + 0x28) = 0;
            GRef<u32>(q + 0x2C) = 0;
            GRef<s32>(q + 0x30) = px - hs;
            GRef<s32>(q + 0x34) = py - hs;
            GRef<u32>(q + 0x38) = 0xFFFFFF;
            GRef<u32>(q + 0x3C) = 0;
            GRef<s32>(q + 0x40) = u1;
            GRef<s32>(q + 0x44) = v1;
            GRef<u32>(q + 0x48) = 0;
            GRef<u32>(q + 0x4C) = 0;
            GRef<s32>(q + 0x50) = px + hs;
            GRef<s32>(q + 0x54) = py + hs;
            GRef<u32>(q + 0x58) = 0xFFFFFF;
            GRef<u32>(q + 0x5C) = 0;
        };
        sprite(cu, cv, cu + cw, cv + ch, half);
        if (GRef<s32>(u + 0x48) < 0) sprite(0xDC0, 0x9C0, 0xEA0, 0xAA0, isz[2]);
    }

    // Missiles and specials (blinking).
    if ((~W(st + 0x188) & 0x08000000u) != 0 && (~StatusFlags() & 0x10000000u) != 0 && (W(Scene() + 0x165C) & 2u) != 0) {
        func_0015F2B0(list, 3, -1, -1, -1, 0x60);
        const auto dot = [&](u32 rec) {
            sceVu0SubVector(rel, rec, unit);
            s32 px, py;
            project(F(rel), F(rel + 8), px, py);
            if (sz < px || px < -sz || 0x400 < py || py < -0x400) return;
            RadarDot(list, px + X, py + Y);
        };
        for (u32 i = 0; i < 0x20; ++i) {
            const u32 pool = W(0x004106AC);
            if (((~W(pool + 0x8000 + 0xA28) >> i) & 1u) != 0) continue;
            const u32 rec = pool + 0x4000 + 0x60 * i;
            if ((W(rec + 0x5C) & 1u) != 0) continue;
            dot(rec);
        }
        for (u32 i = 0; i < 0x20; ++i) {
            const u32 pool = W(0x004106AC);
            if (((~W(pool + 0x8000 + 0xA2C) >> i) & 1u) != 0) continue;
            const u32 rec = pool + 0x4C00 + 0x80 * i;
            if ((W(rec + 0x70) & 1u) != 0) continue;
            dot(rec);
        }
    }

    // Mission area border, then the line z = rules +0x1C.
    f32 bx[4], bz[4];
    {
        const u32 r = Rules();
        const f32 ux = F(unit), uz = F(unit + 8);
        if ((StatusFlags() & 0x10000000u) != 0) {
            bx[0] = bx[1] = fpu::Sub(fpu::Add(F(r), -32768.0f), ux);
            bx[2] = bx[3] = fpu::Sub(fpu::Add(F(r), 32768.0f), ux);
            bz[0] = bz[3] = fpu::Sub(fpu::Add(F(r + 8), -32768.0f), uz);
            bz[1] = bz[2] = fpu::Sub(fpu::Add(F(r + 8), 32768.0f), uz);
        } else {
            bx[0] = bx[1] = fpu::Sub(-F(r + 0x20), ux);
            bx[2] = bx[3] = fpu::Sub(F(r + 0x20), ux);
            bz[0] = bz[3] = fpu::Sub(-F(r + 0x20), uz);
            bz[1] = bz[2] = fpu::Sub(F(r + 0x20), uz);
        }
    }
    const f32 dz = fpu::Sub(F(Rules() + 0x1C), F(unit + 8));
    s32 cx[4], cy[4];
    for (u32 k = 0; k < 4; ++k) project(bx[k], bz[k], cx[k], cy[k]);
    s32 lx[2], ly[2];
    for (u32 k = 0; k < 2; ++k) project(k == 0 ? bx[0] : bx[2], dz, lx[k], ly[k]);
    for (u32 k = 0; k < 4;) {
        const u32 n = k + 1 < 4 ? k + 1 : 0;
        s32 x0 = cx[k], y0 = cy[k], x1 = cx[n], y1 = cy[n];
        if (ClipRadarLine(x0, y0, x1, y1, sz, 0x400)) {
            func_0015F2B0(list, 3, -1, 0, 0, 0x60);
            const s32 v[2][2] = {{x0 + X, y0 + Y}, {x1 + X, y1 + Y}};
            RadarLines(list, 0x2020C00000008001ull, v, 2);
        }
        ++k;
        if (!(k < 4)) break;
        if (Game(0x27) < 0x13 && (~StatusFlags() & 0x10000000u) != 0 && Game(0x29) != 3 && k == 3) break;
    }
    if ((Game(0x27) < 0x13 || Game(0x27) == 0x27) && (~StatusFlags() & 0x10000000u) != 0 && Game(0x29) != 3) {
        s32 x0 = lx[0], y0 = ly[0], x1 = lx[1], y1 = ly[1];
        if (ClipRadarLine(x0, y0, x1, y1, sz, 0x400)) {
            x0 += X, x1 += X, y0 += Y, y1 += Y;
            const s32 ddx = x0 - x1, ddy = y0 - y1;
            const s32 sq = static_cast<s32>(static_cast<u32>(ddx) * static_cast<u32>(ddx) + static_cast<u32>(ddy) * static_cast<u32>(ddy));
            s32 len = fpu::FloatToInt(fpu::Sqrt(fpu::IntToFloat(sq)));
            if (!(len < 0x1001)) len = 0xFFF;
            func_0015F2B0(list, 3, -1, -1, -1, 0x60);
            p = AllocBucketPacket(list, 3, 5, 0, 0x50000005);
            Put64(p, 1, 0x5353);
            Put64(p, 0, 0x40A8C00000008001ull);
            GRef<u32>(p + 0x10) = 0x10;
            GRef<u32>(p + 0x14) = 0x2D8;
            GRef<u32>(p + 0x18) = 0;
            GRef<u32>(p + 0x1C) = 0;
            GRef<s32>(p + 0x20) = x0;
            GRef<s32>(p + 0x24) = y0;
            GRef<u32>(p + 0x28) = 0xFFFFFE;
            GRef<u32>(p + 0x2C) = 0;
            GRef<s32>(p + 0x30) = len + 0x10;
            GRef<u32>(p + 0x34) = 0x2D8;
            GRef<u32>(p + 0x38) = 0;
            GRef<u32>(p + 0x3C) = 0;
            GRef<s32>(p + 0x40) = x1;
            GRef<s32>(p + 0x44) = y1;
            GRef<u32>(p + 0x48) = 0xFFFFFE;
            GRef<u32>(p + 0x4C) = 0;
        }
    }
    if ((StatusFlags() & 0x01000000u) != 0) func_00168620(range, sn, cs, list, unit, X, Y, sz, 0x400);
    if (Game(0x27) == 0xF && (~StatusFlags() & 0x10000000u) != 0) {
        func_00167EC0(range, sn, cs, list, unit, X, Y, sz, 0x400);
        func_00168168(range, sn, cs, list, unit, X, Y, sz, 0x400);
    }

    // HUD-coloured frame, then restore the scissor.
    HudTest(list, 3, 0x30000);
    HudColour(list, 3);
    RadarFrame(list, left, top, right, bottom, lay(4), 0xA88, 0xBA8);
    HudColour(list, 3);
    {
        const s32 d = lay(4);
        const s32 v[8][2] = {{left + d, top}, {right - d, top}, {left + d, bottom - 0x10}, {right - d, bottom - 0x10},
                             {left, top + 0x80}, {left, bottom - 0x80}, {right - 0x10, top + 0x80}, {right - 0x10, bottom - 0x80}};
        RadarLines(list, 0x2020C00000008004ull, v, 8);
    }
    p = AllocBucketPacket(list, 3, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008001ull);
    Put64(p, 2, GRef<u64>(Scene() + 0x1800));
    Put64(p, 3, 0x40);
}

// EE: 0x0016B7C8
// Radar map front band (bucket 3, CLAMP region repeat): a green gradient strip of width w from
// x across the map rows of rules +0x2C (alpha 0) through their midpoint (alpha by objective count
// list +0x83 between rules +0x2B and +0x2A: 0x20..0x80, or up to 0x20 below +0x2B) to rules +0x30
// (alpha 0); world z maps to y0 + ((-z / 2^19 + 0.5 - off) / scale) x h. Texture offset random.
// fpucheck: 7 adds
void func_0016B7C8(f32 scale, f32 off, u32 list, s32 x, s32 y0, s32 w, s32 h) {
    const u32 r = Rules();
    const f32 a = F(r + 0x2C), b = F(r + 0x30);
    const f32 mid = fpu::Add(a, b) * 0.5f;
    const s32 cnt = GRef<u8>(UnitList() + 0x83), lo = GRef<u8>(r + 0x2B);
    s32 alpha;
    if (lo < cnt) {
        alpha = (cnt - lo) * 0x60 / (static_cast<s32>(GRef<u8>(r + 0x2A)) - lo) + 0x20;
        if (!(alpha < 0x81)) alpha = 0x80;
        if (!(0x1F < alpha)) alpha = 0x20;
    } else {
        alpha = (cnt << 5) / lo;
        if (!(alpha < 0x21)) alpha = 0x20;
        if (!(-1 < alpha)) alpha = 0;
    }
    func_0015F350(list, 3, 0);
    u32 p = AllocBucketPacket(list, 3, 2, 0, 0x50000002);
    Put64(p, 0, 0x1000400000008001ull);
    Put64(p, 1, 0xE);
    Put64(p, 3, 8);
    Put64(p, 2, 0x000002C00F1801FFull);
    p = AllocBucketPacket(list, 3, 0x13, 0, 0x50000013);
    Put64(p, 1, 0x531);
    Put64(p, 0, 0x30AE400000008006ull);
    const s32 u = fpu::FloatToInt(NextRandomFloat() * 512.0f);
    const s32 v = fpu::FloatToInt(NextRandomFloat() * 256.0f);
    const f32 fh = fpu::IntToFloat(h);
    const auto row = [&](f32 z) {
        return y0 + fpu::FloatToInt(fpu::Sub(fpu::Add(-z * 1.9073486328125e-06f, 0.5f), off) / scale * fh);
    };
    const s32 y1 = row(a), y2 = row(mid), y3 = row(b);
    const s32 ys[3] = {y1, y2, y3};
    const s32 as[3] = {0, alpha, 0};
    u32 o = p + 0x10;
    for (u32 k = 0; k < 3; ++k) {
        for (u32 side = 0; side < 2; ++side) {
            GRef<u32>(o) = 0;
            GRef<u32>(o + 4) = 0xFF;
            GRef<u32>(o + 8) = 0x80;
            GRef<s32>(o + 0xC) = as[k];
            GRef<s32>(o + 0x10) = side == 0 ? u : u + 0x200;
            GRef<s32>(o + 0x14) = v + 0x100 * static_cast<s32>(k);
            GRef<u32>(o + 0x18) = 0;
            GRef<u32>(o + 0x1C) = 0;
            GRef<s32>(o + 0x20) = side == 0 ? x : x + w;
            GRef<s32>(o + 0x24) = ys[k];
            GRef<u32>(o + 0x28) = 0xFFFFFF;
            GRef<u32>(o + 0x2C) = 0;
            o += 0x30;
        }
    }
    p = AllocBucketPacket(list, 3, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008001ull);
    Put64(p, 3, 8);
    Put64(p, 2, 0);
}

// port: digital map cycle (port_hooks.h InputHooks): a pressure pad (type 5) whose player's device
// gives square no pressure takes the analog pad's (type 4) map-button cycle; the original type-4 code
// runs unchanged.
namespace {
bool MapCycle(s32 p, s32 mode) {
    return mode == 4 || (mode == 5 && static_cast<u32>(p) < 2 && port::Input().digital_map_cycle[p]);
}
}  // namespace

// EE: 0x0016BC68
// Radar map (bucket 3; replaces the radar while the map is selected: pad mode 4 / versus, or the
// analog mode by the pressure of PadExt +0x1D): the pre-built map image (scene +0x16F0, called
// into bucket 0) is drawn as a textured sprite (width 0x00410A40 / 16:9 0x00410A48, height 0xCD0,
// at left + 0x180 / 0x100, top + 0xD70 / 0xE30) showing the zoom window z (1, 0.5 or 0.25 of the
// 524288-unit map, HudState +0x72) around the player, clamped to the map. Then mission 16 sweep
// pulses, the player arrow (unless status flag 0x10000000), every other shown unit whose map cell
// was revealed (HudState +0x44 bit grid, 16 x 16) as a coloured box clipped toward the player
// (flagged units get the marker), the mission 11 front band (0x0016B7C8), the mission area border
// (rules +0x20) in red and, in some missions, the line z = rules +0x1C; restores the scissor.
// fpucheck: verified 44 (the mapx / mapy lambdas hold 2 each)
void DrawRadarMap(u32 cam, u32 list, u32 player) {
    const u32 lw = Wide() ? 0x00410A48u : 0x00410A40u;
    const u32 P = Wide() ? 0x00410A58u : 0x00410A50u;
    func_00104338(list, 0, W(Scene() + 0x16F0), 0);
    const s32 mw = GRef<s16>(lw);
    s32 X0, Y0;
    if (Players() == 2) {
        Y0 = static_cast<s32>(HudU16(0x1E) + 0xE30u);
        X0 = static_cast<s32>(HudU16(0x1C) + 0x100u);
    } else {
        Y0 = static_cast<s32>(HudU16(0x1E) + 0xD70u);
        X0 = static_cast<s32>(HudU16(0x1C) + 0x180u);
    }
    const s32 mode = GRef<s32>(PadState(static_cast<s32>(player)));
    f32 z;
    if (MapCycle(static_cast<s32>(player), mode) || GRef<s8>(Scene() + 0x1653) == 2) {
        const u8 sel = GRef<u8>(HudState() + 0x72);
        z = sel == 1 ? 0.25f : sel == 2 ? 0.5f : 1.0f;
    } else if (static_cast<u32>(mode - 5) < 2) {
        const f32 t = fpu::IntToFloat(GRef<u8>(PadExt(static_cast<s32>(player)) + 0x1D));
        if (192.0f < t) {
            z = 1.0f;
        } else if (184.0f < t) {
            z = fpu::Add(fpu::Sub(t, 184.0f) * 0.125f * 0.5f, 0.5f);
        } else if (88.0f < t) {
            z = 0.5f;
        } else if (80.0f < t) {
            z = fpu::Add(fpu::Sub(t, 80.0f) * 0.125f * 0.25f, 0.25f);
        } else {
            z = 0.25f;
        }
    } else {
        z = 0.0f;
    }
    constexpr f32 k = 1.9073486328125e-06f;  // 1 / 524288
    const u32 pu = Unit(static_cast<s32>(player));
    const f32 hz = z * 0.5f;
    f32 u0 = fpu::Sub(fpu::Add(F(pu) * k, 0.5f), hz);
    f32 v0 = fpu::Sub(fpu::Add(-F(pu + 8) * k, 0.5f), hz);
    if (u0 < 0.0f) u0 = 0.0f;
    f32 u1 = fpu::Add(u0, z);
    if (1.0f < u1) {
        u0 = fpu::Sub(1.0f, z);
        u1 = fpu::Add(u0, z);
    }
    if (v0 < 0.0f) v0 = 0.0f;
    f32 v1 = fpu::Add(v0, z);
    if (1.0f < v1) {
        v0 = fpu::Sub(1.0f, z);
        v1 = fpu::Add(v0, z);
    }

    // Map image (TEX0, TEXFLUSH, SCISSOR to the map box) and its sprite.
    func_0015F350(list, 3, 0);
    u32 p = AllocBucketPacket(list, 3, 0xA, 0, 0x5000000A);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000000000008003ull);
    Put64(p, 3, 6);
    Put64(p, 2, 0x2004E005DD30A600ull);
    Put64(p, 5, 0x3F);
    Put64(p, 4, 0);
    {
        const s64 c188 = GRef<s16>(cam + 0x188);
        s64 x0, x1, y0, y1;
        if (Players() == 2) {
            x1 = c188 + ((mw >> 4) + 0x10);
            y0 = 0x1B0 - (0xCD0 >> 4);
            y1 = 0x1B0;
            x0 = c188 + 0x10;
        } else {
            x1 = c188 + ((mw >> 4) + 0x18);
            y0 = 0x1A4 - (0xCD0 >> 4);
            y1 = 0x1A4;
            x0 = c188 + 0x18;
        }
        Put64(p, 6, static_cast<u64>(x0) | (static_cast<u64>(y1) << 48) | (static_cast<u64>(y0) << 32) | (static_cast<u64>(x1) << 16));
    }
    Put64(p, 7, 0x40);
    Put64(p, 8, 0x502B400000008001ull);
    Put64(p, 9, 0x52521);
    GRef<u32>(p + 0x50) = 0x80;
    GRef<u32>(p + 0x54) = 0x80;
    GRef<u32>(p + 0x58) = 0x80;
    GRef<u32>(p + 0x5C) = 0x60;
    GRef<f32>(p + 0x60) = u0;
    GRef<f32>(p + 0x64) = v0;
    GRef<f32>(p + 0x68) = 1.0f;
    GRef<u32>(p + 0x6C) = 0;
    GRef<s32>(p + 0x70) = X0;
    GRef<s32>(p + 0x74) = Y0;
    GRef<u32>(p + 0x78) = 0xFFFFFE;
    GRef<u32>(p + 0x7C) = 0;
    GRef<f32>(p + 0x80) = u1;
    GRef<f32>(p + 0x84) = v1;
    GRef<f32>(p + 0x88) = 1.0f;
    GRef<u32>(p + 0x8C) = 0;
    GRef<s32>(p + 0x90) = X0 + mw;
    GRef<s32>(p + 0x94) = Y0 + 0xCD0;
    GRef<u32>(p + 0x98) = 0xFFFFFE;
    GRef<u32>(p + 0x9C) = 0;
    p = AllocBucketPacket(list, 3, 3, 0, 0x50000003);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000000000008002ull);
    Put64(p, 3, 6);
    Put64(p, 2, 0x2004B00621412500ull);
    Put64(p, 5, 0x3F);
    Put64(p, 4, 0);
    const f32 fw = fpu::IntToFloat(mw), fh = fpu::IntToFloat(GRef<s32>(0x00410A60));
    const auto mapx = [&](f32 x) { return fpu::FloatToInt(fpu::Sub(fpu::Add(x * k, 0.5f), u0) / z * fw); };
    const auto mapy = [&](f32 wz) { return fpu::FloatToInt(fpu::Sub(fpu::Add(-wz * k, 0.5f), v0) / z * fh); };

    // Mission 16: sweep pulses at the listed units.
    if (Game(0x27) == 0x10 && GRef<u8>(UnitList() + 0x83) != 0) {
        for (u32 i = 0; i < GRef<u8>(UnitList() + 0x83); ++i) {
            const u32 a = UnitList();
            const u32 idx = (W(Scene() + 0x165C) & 1u) != 0 ? GRef<u8>(a + 0x83) - i - 1 : i;
            const u32 u = Unit(GRef<u8>(a + 0x80 + idx));
            if ((~W(u + 0x4C) & 0x01000000u) != 0) continue;
            const f32 r = F(0x0040E97C) / z;
            func_00168CD0(r, list, mapx(F(u)) + X0, mapy(F(u + 8)) + Y0);
        }
    }
    HudTest(list, 3, 0x34000);
    func_0015F350(list, 3, 0);
    const u32 team = W(pu + 0x48) & 3u;
    const s32 ppx = mapx(F(pu)), ppy = mapy(F(pu + 8));
    const f32 sn = ee::sinf(F(pu + 0x14));
    const f32 cs = ee::cosf(F(pu + 0x14));

    if ((~StatusFlags() & 0x10000000u) != 0) {
        // Player arrow.
        p = AllocBucketPacket(list, 3, 5, 0, 0x50000005);
        Put64(p, 1, 0x5551);
        Put64(p, 0, 0x4021C00000008001ull);
        GRef<u32>(p + 0x10) = 0xFF;
        GRef<u32>(p + 0x14) = 0xFF;
        GRef<u32>(p + 0x18) = 0xFF;
        GRef<u32>(p + 0x1C) = 0x40;
        const f32 px = fpu::IntToFloat(ppx + X0), py = fpu::IntToFloat(ppy + Y0);
        GRef<s32>(p + 0x20) = fpu::FloatToInt(fpu::Add(px, sn * F(P)));
        GRef<s32>(p + 0x24) = fpu::FloatToInt(fpu::Sub(py, cs * 128.0f));
        GRef<u32>(p + 0x28) = 0xFFFFFF;
        GRef<u32>(p + 0x2C) = 0;
        const f32 pc = fpu::Add(py, cs * 64.0f);
        const f32 sn64 = sn * 64.0f;
        GRef<s32>(p + 0x30) = fpu::FloatToInt(fpu::Add(fpu::Sub(px, sn * F(P + 4)), cs * F(P + 4)));
        GRef<s32>(p + 0x34) = fpu::FloatToInt(fpu::Add(pc, sn64));
        GRef<u32>(p + 0x38) = 0xFFFFFF;
        GRef<u32>(p + 0x3C) = 0;
        GRef<s32>(p + 0x40) = fpu::FloatToInt(fpu::Sub(fpu::Sub(px, sn * F(P + 4)), cs * F(P + 4)));
        GRef<s32>(p + 0x44) = fpu::FloatToInt(fpu::Sub(pc, sn64));
        GRef<u32>(p + 0x48) = 0xFFFFFF;
        GRef<u32>(p + 0x4C) = 0;

        // Other units in revealed map cells.
        for (s32 i = 0; i < UnitCount(); ++i) {
            if (static_cast<u32>(i) == player) continue;
            const u32 u = Unit(i);
            const u32 nf = ~W(u + 0x4C);
            if ((nf & 0x10000000u) != 0 || (nf & 0x01000000u) != 0) continue;
            const u8 cls = GRef<u8>(u + 0x64);
            if (cls == 0) continue;
            const s32 gy = fpu::FloatToInt(fpu::Add(F(u + 8), 262144.0f) * 16.0f * k);
            const s32 gx = fpu::FloatToInt(fpu::Add(F(u), 262144.0f) * 16.0f * k);
            if (((~static_cast<u32>(GRef<u16>(HudState() + 2u * static_cast<u32>(gy) + 0x44)) >> (gx & 0x1F)) & 1u) != 0) continue;
            s32 x = mapx(F(u)), y = mapy(F(u + 8));
            if (x < 0) {
                if (ppx != x) y = Lerp32(y, ppy, 0, ppx, x, ppx, ppy);
                x = 0;
            }
            if (mw < x) {
                if (ppx != x) y = Lerp32(y, ppy, mw, ppx, x, ppx, ppy);
                x = mw;
            }
            if (y < 0) {
                if (ppy != y) x = Lerp32(x, ppx, 0, ppy, y, ppy, ppx);
                y = 0;
            }
            if (0xCD0 < y) {
                if (ppy != y) x = Lerp32(x, ppx, 0xCD0, ppy, y, ppy, ppx);
                y = 0xCD0;
            }
            p = AllocBucketPacket(list, 3, 4, 0, 0x50000004);
            Put64(p, 1, 0x551);
            Put64(p, 0, 0x3023400000008001ull);
            s32 c[4] = {0, 0, 0, 0x80};
            if (GRef<u8>(u + 0x54) == 0) {
                c[0] = 0x56, c[1] = 0xE6, c[2] = 0xD2;
            } else if ((W(u + 0x48) & 3u) == team) {
                c[0] = 0, c[1] = 0x5B, c[2] = 0xB5;
            } else if (cls == 0x14) {
                c[0] = 0, c[1] = 0xFF, c[2] = 0x80;
                c[3] = ((W(Scene() + 0x165C) >> 4) & 7u) != (static_cast<u32>(i) & 7u) ? 0 : 0x80;
            } else if (GRef<s32>(u + 0x48) < 0) {
                c[0] = 0xE8, c[1] = 0x1A, c[2] = 0;
            } else {
                const s16 pri = GRef<s16>(u + 0x60);
                if (!(pri < GRef<s16>(Rules() + 0x24))) {
                    c[0] = 0xFF, c[1] = 0x79, c[2] = 0x3A;
                } else if (pri < GRef<s16>(Rules() + 0x26)) {
                    c[0] = 0x80, c[1] = 0x80, c[2] = 0x80;
                } else {
                    c[0] = 0xFF, c[1] = 0xA5, c[2] = 0x39;
                }
            }
            for (u32 j = 0; j < 4; ++j) GRef<s32>(p + 0x10 + 4 * j) = c[j];
            const s32 sx = x + X0, sy = y + Y0;
            GRef<s32>(p + 0x20) = sx - 0x10;
            GRef<s32>(p + 0x24) = sy - 0x10;
            GRef<u32>(p + 0x28) = 0xFFFFFF;
            GRef<u32>(p + 0x2C) = 0;
            GRef<s32>(p + 0x30) = sx + 0x10;
            GRef<s32>(p + 0x34) = sy + 0x10;
            GRef<u32>(p + 0x38) = 0xFFFFFF;
            GRef<u32>(p + 0x3C) = 0;
            if (GRef<s32>(u + 0x48) < 0) {
                p = AllocBucketPacket(list, 3, 5, 0, 0x50000005);
                Put64(p, 1, 0x5353);
                Put64(p, 0, 0x40AB400000008001ull);
                const s32 mx = GRef<s16>(lw + 2);
                GRef<u32>(p + 0x10) = 0xDC0;
                GRef<u32>(p + 0x14) = 0x9C0;
                GRef<u32>(p + 0x18) = 0;
                GRef<u32>(p + 0x1C) = 0;
                GRef<s32>(p + 0x20) = sx - mx;
                GRef<s32>(p + 0x24) = sy - 0x40;
                GRef<u32>(p + 0x28) = 0xFFFFFF;
                GRef<u32>(p + 0x2C) = 0;
                GRef<u32>(p + 0x30) = 0xEA0;
                GRef<u32>(p + 0x34) = 0xAA0;
                GRef<u32>(p + 0x38) = 0;
                GRef<u32>(p + 0x3C) = 0;
                GRef<s32>(p + 0x40) = sx + mx;
                GRef<s32>(p + 0x44) = sy + 0x40;
                GRef<u32>(p + 0x48) = 0xFFFFFF;
                GRef<u32>(p + 0x4C) = 0;
            }
        }
    }

    // Front band (mission 11), area border, and the line z = rules +0x1C.
    HudTest(list, 3, 0x30000);
    if (Game(0x27) == 0xB && Game(0x29) != 3) func_0016B7C8(z, v0, list, X0, Y0, mw, 0xCD0);
    func_0015F350(list, 3, 1);
    const f32 e = F(Rules() + 0x20);
    const f32 ep = fpu::Add(e * k, 0.5f), en = fpu::Add(-e * k, 0.5f);
    const s32 xR = fpu::FloatToInt(fpu::Sub(ep, u0) / z * fw);
    const s32 yA = fpu::FloatToInt(fpu::Sub(ep, v0) / z * fh);
    const s32 yB = fpu::FloatToInt(fpu::Sub(en, v0) / z * fh);
    const s32 xL = fpu::FloatToInt(fpu::Sub(en, u0) / z * fw);
    func_0015F2B0(list, 3, -1, 0, 0, 0x60);
    const s32 edge[4][4] = {{xL, yA, xL, yB}, {xL, yB, xR, yB}, {xR, yB, xR, yA}, {xR, yA, xL, yA}};
    for (const auto& eg : edge) {
        s32 x0 = eg[0], y0 = eg[1], x1 = eg[2], y1 = eg[3];
        if (mw < x0 && mw < x1) continue;
        if (x0 < 0 && x1 < 0) continue;
        if (0xCD0 < y0 && 0xCD0 < y1) continue;
        if (y0 < 0 && y1 < 0) continue;
        if (!(-1 < y1)) y1 = 0;
        if (!(-1 < y0)) y0 = 0;
        if (0xCD0 < y0) y0 = 0xCD0;
        if (!(-1 < x0)) x0 = 0;
        if (!(-1 < x1)) x1 = 0;
        if (0xCD0 < y1) y1 = 0xCD0;
        if (mw < x0) x0 = mw;
        if (mw < x1) x1 = mw;
        const s32 v[2][2] = {{x0 + X0, y0 + Y0}, {x1 + X0, y1 + Y0}};
        RadarLines(list, 0x2020C00000008001ull, v, 2);
    }
    if (Game(0x29) != 3 && (Game(0x27) < 0x13 || Game(0x27) == 0x27)) {
        s32 y = fpu::FloatToInt(fpu::Sub(fpu::Add(-F(Rules() + 0x1C) * k, 0.5f), v0) / z * fh);
        if (y < 0xCD0 && 0 < y) {
            y += Y0;
            s32 a = xL, b = xR;
            if (!(-1 < b)) b = 0;
            if (!(-1 < a)) a = 0;
            if (mw < a) a = mw;
            if (mw < b) b = mw;
            b += X0;
            a += X0;
            const s32 d = a - b;
            s32 len = fpu::FloatToInt(fpu::Sqrt(fpu::IntToFloat(static_cast<s32>(static_cast<u32>(d) * static_cast<u32>(d)))));
            if (!(len < 0x1001)) len = 0xFFF;
            func_0015F2B0(list, 3, -1, -1, -1, 0x60);
            p = AllocBucketPacket(list, 3, 5, 0, 0x50000005);
            Put64(p, 1, 0x5353);
            Put64(p, 0, 0x40A8C00000008001ull);
            GRef<u32>(p + 0x10) = 0x10;
            GRef<u32>(p + 0x14) = 0x2D8;
            GRef<u32>(p + 0x18) = 0;
            GRef<u32>(p + 0x1C) = 0;
            GRef<s32>(p + 0x20) = a;
            GRef<s32>(p + 0x24) = y;
            GRef<u32>(p + 0x28) = 0xFFFFFE;
            GRef<u32>(p + 0x2C) = 0;
            GRef<s32>(p + 0x30) = len + 0x10;
            GRef<u32>(p + 0x34) = 0x2D8;
            GRef<u32>(p + 0x38) = 0;
            GRef<u32>(p + 0x3C) = 0;
            GRef<s32>(p + 0x40) = b;
            GRef<s32>(p + 0x44) = y;
            GRef<u32>(p + 0x48) = 0xFFFFFE;
            GRef<u32>(p + 0x4C) = 0;
        }
    }
    p = AllocBucketPacket(list, 3, 2, 0, 0x50000002);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000000000008001ull);
    Put64(p, 2, GRef<u64>(Scene() + 0x1800));
    Put64(p, 3, 0x40);
}

// EE: 0x0016CF88
// Picks the HUD warning (HudState +0x6E) from the position 600 frames ahead (pos + vel x 600):
// outside the mission area (status flag 0x10000000: rules +0x00 +-32768; flag 0x01 in missions 4
// and 6: none; else +-rules +0x20, the south edge only past mission 18 or in mode 3) clears it;
// else 2 (unit +0x50 bits 0x03000000), 3 (bits 0x60000000), 5 / 4 beyond the line z = rules +0x1C
// (5 for aircraft types 2, 4, 8, 0xE, 0x15), 6 (fuel / damage: state +0x2E0 < 0.5 or +0x2DC < 1
// with status flag 0x10000000, else state flag 0x01000000; not with state flag 0x40000000), 7
// (state flag 0x02000000), 8 (low over the terrain, above -256) or none (0xFF). The shown warning
// (+0x6C) changes through a fade (+0x70 phase, +0x64 frame counter: 8 frames in, hold while equal,
// 8 frames out).
// fpucheck: verified 5
void SelectHudWarning(u32 unit) {
    const u32 st = W(unit + 0x44);
    guest::GuestFrame fr(0x10);
    const u32 P = fr.Address(0);
    sceVu0ScaleVector(P, unit + 0x20, 600.0f);
    sceVu0AddVector(P, P, unit);
    const u32 r = Rules();
    bool out = false;
    const auto area = [&] {
        const f32 e = F(r + 0x20);
        if (F(P) < -e || e < F(P) || e < F(P + 8)) {
            out = true;
            return;
        }
        if (Game(0x27) < 0x13 && Game(0x29) != 3) return;
        if (F(P + 8) < -e) out = true;
    };
    if ((StatusFlags() & 0x10000000u) != 0) {
        if (F(P) < fpu::Add(F(r), -32768.0f) || fpu::Add(F(r), 32768.0f) < F(P) || F(P + 8) < fpu::Add(F(r + 8), -32768.0f) ||
            fpu::Add(F(r + 8), 32768.0f) < F(P + 8))
            out = true;
    } else if ((StatusFlags() & 1u) != 0) {
        if (Game(0x27) != 4 && Game(0x27) != 6) area();
    } else {
        area();
    }
    const u32 f188 = W(st + 0x188);
    bool low = false;
    if ((~f188 & 0x40000000u) != 0) {
        if ((StatusFlags() & 0x10000000u) != 0) {
            if (F(st + 0x2E0) < 0.5f || F(st + 0x2DC) < 1.0f) low = true;
        } else if ((f188 & 0x01000000u) != 0) {
            low = true;
        }
    }
    const u32 hs = HudState();
    if (out) {
        GRef<u8>(hs + 0x6E) = 0;
    } else {
        const u32 f50 = W(unit + 0x50);
        u8 w;
        if ((f50 & 0x03000000u) != 0) {
            w = 2;
        } else if ((f50 & 0x60000000u) != 0) {
            w = 3;
        } else if (F(P + 8) < F(r + 0x1C)) {
            const u8 t = GRef<u8>(unit + 0x69);
            w = (t == 2 || t == 4 || t == 8 || t == 0xE || t == 0x15) ? 5 : 4;
        } else if (low) {
            w = 6;
        } else if ((f188 & 0x02000000u) != 0) {
            w = 7;
        } else if ((~StatusFlags() & 0x10000000u) != 0 && -256.0f < fpu::Sub(F(unit + 4), func_0013E338(Terrain(), unit))) {
            w = 8;
        } else {
            w = 0xFF;
        }
        GRef<u8>(HudState() + 0x6E) = w;
    }
    const u32 h = HudState();
    const u8 cur = GRef<u8>(h + 0x6C);
    if (cur == 0xFF) {
        const u8 nxt = GRef<u8>(h + 0x6E);
        if (nxt == 0xFF) return;
        GRef<u8>(h + 0x6C) = nxt;
        GRef<u8>(h + 0x70) = 0;
        GRef<u16>(HudState() + 0x64) = 0;
        return;
    }
    const u8 ph = GRef<u8>(h + 0x70);
    const u16 t = static_cast<u16>(GRef<u16>(h + 0x64) + 1u);
    GRef<u16>(h + 0x64) = t;
    if (ph == 0) {
        if (static_cast<s16>(t) < 8) return;
        GRef<u8>(h + 0x70) = 1;
    } else if (ph == 1) {
        if (cur == GRef<u8>(h + 0x6E)) return;
        GRef<u8>(h + 0x70) = 2;
    } else if (ph == 2) {
        if (static_cast<s16>(t) < 8) return;
        GRef<u8>(h + 0x6C) = 0xFF;
        GRef<u8>(HudState() + 0x70) = 0;
        GRef<u16>(HudState() + 0x64) = 0;
        return;
    } else {
        return;
    }
    GRef<u16>(HudState() + 0x64) = 0;
}

// EE: 0x0016D348
// Draws the shown HUD warning (HudState +0x6C; bucket 4): table 0x002B7450 + 4 x warning holds up
// to two messages (0xFF = none) and a colour kind (+2: 1 = blue, else red); each message (table
// 0x002B73D8: text, half width, 16:9 half width) is drawn as text in a box outline, the first
// above the centre (-0x520) and the second below (+0x3E0, always green); alpha fades with +0x70 /
// +0x64 (x 12 per frame, 0x60 when held).
void DrawHudWarning(u32 list) {
    const u32 h = HudState();
    const u8 w = GRef<u8>(h + 0x6C);
    if (w == 0xFF) return;
    const u32 tab = 0x002B7450 + 4u * w;
    s32 R[2], G[2], B[2], A[2];
    if (GRef<s8>(tab + 2) == 1) {
        R[0] = 0, B[0] = 0x80, G[0] = 0;
    } else {
        G[0] = 0, R[0] = 0x80, B[0] = 0;
    }
    G[1] = 0x80, B[1] = 0x40, R[1] = 0;
    const u8 ph = GRef<u8>(h + 0x70);
    s32 a;
    if (ph == 0)
        a = GRef<s16>(h + 0x64) * 0xC;
    else if (ph == 2)
        a = (8 - GRef<s16>(h + 0x64)) * 0xC;
    else
        a = 0x60;
    A[0] = A[1] = a;
    func_0015F350(list, 4, 1);
    for (u32 i = 0; i < 2; ++i) {
        const u8 m = GRef<u8>(tab + i);
        if (m == 0xFF) continue;
        const u32 e = 0x002B73D8 + 8u * m;
        s32 x0, x1, tx;
        if (Wide()) {
            tx = 0x90;
            x0 = static_cast<s32>(HudU16(0x14)) - GRef<s16>(e + 6);
            x1 = static_cast<s32>(HudU16(0x14)) + GRef<s16>(e + 6) + 0x10;
        } else {
            tx = 0xC0;
            x1 = static_cast<s32>(HudU16(0x14)) + GRef<s16>(e + 4);
            x0 = static_cast<s32>(HudU16(0x14)) - GRef<s16>(e + 4);
        }
        const s32 y0 = i == 0 ? static_cast<s32>(HudU16(0x16)) - 0x520 : static_cast<s32>(HudU16(0x16)) + 0x3E0;
        const s32 y1 = y0 + 0x140;
        func_0015F2B0(list, 4, static_cast<s8>(R[i]), static_cast<s8>(G[i]), static_cast<s8>(B[i]), static_cast<s8>(A[i]));
        func_0015F6A0(list, 4, W(e), static_cast<u16>(x0 + tx), static_cast<u16>(y0 + 0x60), 8);
        const u32 p = AllocBucketPacket(list, 4, 7, 0, 0x50000007);
        Put64(p, 0, 0x6021400000008001ull);
        Put64(p, 1, 0x555551);
        GRef<s32>(p + 0x10) = R[i];
        GRef<s32>(p + 0x14) = G[i];
        GRef<s32>(p + 0x18) = B[i];
        GRef<s32>(p + 0x1C) = A[i];
        const s32 v[5][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
        u32 o = p + 0x20;
        for (const auto& c : v) {
            GRef<s32>(o) = c[0];
            GRef<s32>(o + 4) = c[1];
            GRef<u32>(o + 8) = 0xFFFFFF;
            GRef<u32>(o + 0xC) = 0;
            o += 0x10;
        }
    }
}

// EE: 0x0016D688
// Weapon message selection (HudState +0x6D) from the pad edges of the unit's player (PadExt +4,
// +0): switch-weapon (bit 2) to an empty missile rack 0 / empty special 1, fire (+0 bit 1) with no
// gun rounds 2, select (bit 4) 3 (missiles) / 4 (special); a new message holds for 120 frames
// (+0x6A, counter +0x66) and remembers the special weapon type (+0x71).
void func_0016D688(u32 unit) {
    const u32 st = W(unit + 0x44);
    const u32 pe = PadExt(GRef<u8>(unit + 0x55));
    const u32 edge = W(pe + 4);
    u8 m = GRef<u8>(HudState() + 0x6D);
    if ((edge & 2u) != 0) {
        if ((W(st + 0x14C) & 0x8000u) != 0) {
            if (GRef<u8>(st + 0x15B) == 0) m = 1;
        } else if (GRef<u8>(st + 0x15A) == 0) {
            m = 0;
        }
    }
    if ((W(pe) & 1u) != 0 && GRef<u16>(st + 0x158) == 0) m = 2;
    if ((edge & 4u) != 0) m = (W(st + 0x14C) & 0x8000u) != 0 ? 4 : 3;
    if (GRef<u8>(HudState() + 0x6D) != m) {
        GRef<u8>(HudState() + 0x6D) = m;
        const u32 h = HudState();
        GRef<u16>(h + 0x6A) = 0x78;
        GRef<u16>(h + 0x66) = 0;
        GRef<u8>(h + 0x71) = GRef<u8>(st + 0x173);
    }
    const u32 h = HudState();
    if (GRef<u8>(h + 0x6D) == 0xFF) return;
    if (!(GRef<s16>(h + 0x66) < GRef<s16>(h + 0x6A))) {
        GRef<u8>(h + 0x6D) = 0xFF;
        return;
    }
    GRef<u16>(h + 0x66) = static_cast<u16>(GRef<u16>(h + 0x66) + 1u);
}

// EE: 0x0016D778
// Draws the weapon message (HudState +0x6D; bucket 1, below the centre +0x740): "MSSL EMPTY",
// "<special> EMPTY", "GUN EMPTY" in red, "WEP1:MSSL" / "WEP2:<special>" in green (texts in .rodata
// 0x00404E68.. and .sdata 0x00410A90 / 0x00410A98, special code 0x0011B840 of +0x71); alpha fades
// in / out over 8 frames of the 120-frame hold.
void func_0016D778(u32 list) {
    const u32 h = HudState();
    const u8 m = GRef<u8>(h + 0x6D);
    if (m == 0xFF) return;
    const s32 t = GRef<s16>(h + 0x66);
    s32 a;
    if (t < 8) {
        a = t * 0xC;
    } else {
        const s32 lim = GRef<s16>(h + 0x6A);
        a = lim - 8 < t ? (lim - t) * 0xC : 0x60;
    }
    const s8 al = static_cast<s8>(a);
    const u16 y = static_cast<u16>(HudU16(0x16) + 0x740u);
    const auto x = [](u32 d) { return static_cast<u16>(HudU16(0x14) + d); };
    switch (m) {
        case 0:
            func_0015F2B0(list, 1, -0x80, 0, 0, al);
            func_0015F6A0(list, 1, 0x00404E68, x(0u - 0x320u), y, 8);
            break;
        case 1: {
            const u32 name = GetSpecialWeaponCode(GRef<u8>(HudState() + 0x71));
            func_0015F2B0(list, 1, -0x80, 0, 0, al);
            func_0015F6A0(list, 1, name, x(0u - 0x320u), y, 8);
            func_0015F6A0(list, 1, 0x00410A90, x(0u - 0x60u), y, 8);
            break;
        }
        case 2:
            func_0015F2B0(list, 1, -0x80, 0, 0, al);
            func_0015F6A0(list, 1, 0x00404E78, x(0u - 0x280u), y, 8);
            break;
        case 3:
            func_0015F2B0(list, 1, 0, -0x80, 0x40, al);
            func_0015F6A0(list, 1, 0x00404E88, x(0u - 0x280u), y, 8);
            break;
        case 4: {
            const u32 name = GetSpecialWeaponCode(GRef<u8>(HudState() + 0x71));
            func_0015F2B0(list, 1, 0, -0x80, 0x40, al);
            func_0015F6A0(list, 1, 0x00410A98, x(0u - 0x280u), y, 8);
            func_0015F6A0(list, 1, name, x(0xA0u), y, 8);
            break;
        }
        default:
            break;
    }
    HudColour(list, 1);
}

// EE: 0x0016DB40
// Centre message tick for every player (record 0x00410AF0 [p]: {s16 frame, s16 duration, u8 kind},
// kind 0xFF = none): counts the frame up to the duration, then clears the kind; message 8 beeps
// (menu sound 10 / 0x44) every 32 frames.
void func_0016DB40() {
    for (s32 i = 0; i < Players(); ++i) {
        const u32 m = W(0x00410AF0 + 4u * static_cast<u32>(i));
        u8 kind = GRef<u8>(m + 4);
        if (kind == 0xFF) continue;
        if (GRef<s16>(m) < GRef<s16>(m + 2)) {
            GRef<u16>(m) = static_cast<u16>(GRef<u16>(m) + 1u);
        } else {
            GRef<u8>(m + 4) = 0xFF;
            kind = 0xFF;
            GRef<u16>(m + 2) = 0;
        }
        if (kind == 8 && (GRef<u16>(m) & 0x1Fu) == 0) PlayMenuSe(10, 0x44);
    }
}

// EE: 0x0016DC08
// Draws the centre message of player p (bucket via 0x0017F110 / 0x0017F408): the window opens
// over 8 frames at the camera's screen centre (cam +0x178 / +0x17C, x - 0x720 / 16:9 0x550) and
// closes in the last 8; when open, the text of table 0x002B7488 + 16 x kind ({text, width,
// 16:9 width, blink, default duration}) fades in / out over 16 frames and, for blinking messages,
// blinks every 8 frames after frame 60.
void func_0016DC08(u32 cam, s32 p, u32 list) {
    const u32 m = W(0x00410AF0 + 4u * static_cast<u32>(p));
    if (GRef<u8>(m + 4) == 0xFF) return;
    const s32 x = (fpu::FloatToInt(F(cam + 0x178)) << 4) - (Wide() ? 0x550 : 0x720);
    const s32 t = GRef<s16>(m), dur = GRef<s16>(m + 2);
    s32 open;
    if (t < dur - 8)
        open = 7 < t ? 8 : t;
    else
        open = dur - t;
    func_0017F110(list, x, (fpu::FloatToInt(F(cam + 0x17C)) << 4) - 0x200, open);
    const s32 y = (fpu::FloatToInt(F(cam + 0x17C)) << 4) - 0xC0;
    if (open != 8) return;
    const s32 dur2 = GRef<s16>(m + 2);
    const s32 t2 = GRef<s16>(m);
    const u32 e = 0x002B7488 + 16u * GRef<u8>(m + 4);
    s32 fade;
    if (!(t2 < dur2 - 0x10)) {
        fade = 0x10 - (dur2 - t2);
    } else if (t2 < 0x10) {
        fade = 0x10 - t2;
    } else if (GRef<s32>(e + 8) != 1) {
        fade = 0;
    } else if (t2 < 0x3D) {
        fade = 0;
    } else {
        fade = (GRef<u16>(m) & 8u) != 0 ? 0 : 8;
    }
    const s32 w = Wide() ? GRef<s16>(e + 6) : GRef<s16>(e + 4);
    func_0017F408(list, x, y, w, fade, W(e));
}

// EE: 0x0016DDB8
// Shows centre message `kind` for player p for n frames (0: the default of table 0x002B7488):
// the same message extends its time, a lower-numbered (more important) one replaces the current.
void ShowCenterMessage(s32 p, s32 kind, s32 n) {
    if (n == 0) n = GRef<s32>(0x002B7488 + 16u * static_cast<u32>(kind) + 0xC);
    const u32 slot = 0x00410AF0 + 4u * static_cast<u32>(p);
    const u32 m = W(slot);
    const u32 cur = GRef<u8>(m + 4);
    if (cur == static_cast<u32>(kind)) {
        GRef<u16>(m + 2) = static_cast<u16>(GRef<u16>(m) + static_cast<u32>(n));
        return;
    }
    if (static_cast<s64>(kind) < static_cast<s64>(cur)) {
        GRef<u8>(m + 4) = static_cast<u8>(kind);
        const u32 m2 = W(slot);
        GRef<u16>(m2 + 2) = static_cast<u16>(n);
        GRef<u16>(m2) = 0;
    }
}

// EE: 0x0015E928
// Per-frame HUD update for player p: selects its HUD state (0x00410AA0 [p] -> 0x004117D0), runs
// the warning-flash timer +0x42 (held at least 8 / 0x20 / 0x10 frames while unit +0x50 bits
// 0x10000 / 0x60000 / 0x80000 are set), reveals the 3 x 3 radar-map cells around the unit (+0x44
// bit grid, 16 x 16 over the 524288-unit map), then 0x00162F98, SelectHudWarning and the weapon
// message selection 0x0016D688.
// fpucheck: verified 2
void UpdateHud(s32 p) {
    GRef<u32>(0x004117D0) = W(0x00410AA0 + 4u * static_cast<u32>(p));
    const u32 h = HudState();
    const u32 u = Unit(p);
    if (0 < GRef<s16>(h + 0x42)) GRef<u16>(h + 0x42) = static_cast<u16>(GRef<u16>(h + 0x42) - 1u);
    const u32 f = W(u + 0x50);
    const auto hold = [](s16 n) {
        if (GRef<s16>(HudState() + 0x42) < n) GRef<s16>(HudState() + 0x42) = n;
    };
    if ((f & 0x10000u) != 0)
        hold(8);
    else if ((f & 0x60000u) != 0)
        hold(0x20);
    else if ((f & 0x80000u) != 0)
        hold(0x10);
    constexpr f32 k = 1.9073486328125e-06f;
    const s32 gx = fpu::FloatToInt(fpu::Add(F(u), 262144.0f) * 16.0f * k);
    const s32 gz = fpu::FloatToInt(fpu::Add(F(u + 8), 262144.0f) * 16.0f * k);
    for (s32 x = gx - 1; x <= gx + 1; ++x) {
        for (s32 z = gz - 1; z <= gz + 1; ++z) {
            if (static_cast<u32>(x) < 0x10 && -1 < z && z < 0x10) {
                const u32 a = HudState() + 0x44 + 2u * static_cast<u32>(z);
                GRef<u16>(a) = static_cast<u16>(GRef<u16>(a) | (1u << (x & 0x1F)));
            }
        }
    }
    func_00162F98();
    SelectHudWarning(u);
    func_0016D688(u);
}

// EE: 0x0015EAE0
// Draws the HUD of player p (unit / state, camera cam) into the display list: HUD colour green, or
// red while unit +0x50 bits 0x03000000 are set or the flash timer +0x42 blinks; screen centre,
// size and edges from the camera (+0x178 / +0x17C / +0x18C / +0x18E, in 1/16 pixels); the nose
// point (camera distance +0x168 ahead) and whether it lies inside the HUD box; bucket set-up
// (0x0015F2B0 / F4F0 / F350 / F420 for buckets 0, 1, 2, 4); the fixed instruments, the mission
// panels by kind, the flight instruments (pitch ladder etc. only with the nose on screen; state
// +0x149: 0 normal, 2 alternate view), the landing guidance in sub-state 0x15, the reticles and
// markers (not with status flag 0x10000000), the radar or the radar map (selected with the pad:
// digital select button cycles HudState +0x72, analog pressure in modes 5 / 6), the HUD warning
// (not in sub-state 0x1D) and the weapon message. Nothing while context flag 0x20000 is set.
void DrawHud(u32 cam, s32 p, u32 list) {
    const u32 unit = Unit(p);
    GRef<u32>(0x004117D0) = W(0x00410AA0 + 4u * static_cast<u32>(p));
    const u32 st = W(unit + 0x44);
    if ((Ctx()->flags & 0x20000) != 0) return;
    const u32 h = HudState();
    const bool red = (W(unit + 0x50) & 0x03000000u) != 0 || (0 < GRef<s16>(h + 0x42) && (GRef<u16>(h + 0x42) & 4u) != 0);
    if (red) {
        GRef<u8>(kHud + 0x13) = 0x60;
        GRef<u8>(kHud + 0x10) = 0x80;
        GRef<u8>(kHud + 0x11) = 0;
        GRef<u8>(kHud + 0x12) = 0;
    } else {
        GRef<u8>(kHud + 0x11) = 0x80;
        GRef<u8>(kHud + 0x13) = 0x60;
        GRef<u8>(kHud + 0x12) = 0x40;
        GRef<u8>(kHud + 0x10) = 0;
    }
    const u32 cx = static_cast<u32>(fpu::FloatToInt(F(cam + 0x178))) << 4;
    const u32 cy = static_cast<u32>(fpu::FloatToInt(F(cam + 0x17C))) << 4;
    guest::GuestFrame fr(0x60);
    const u32 v = fr.Address(0), P = fr.Address(0x10), m = fr.Address(0x20);
    GRef<u32>(v + 8) = W(cam + 0x168);
    GRef<u16>(kHud + 0x14) = static_cast<u16>(cx);
    GRef<u16>(kHud + 0x16) = static_cast<u16>(cy);
    GRef<f32>(v) = 0.0f;
    GRef<f32>(v + 0xC) = 1.0f;
    GRef<f32>(v + 4) = 0.0f;
    const u32 w = static_cast<u32>(GRef<u16>(cam + 0x18C)) << 4;
    GRef<u16>(kHud + 0x18) = static_cast<u16>(w);
    const u32 hw = (w & 0xFFF0u) >> 1;
    const u32 hh0 = static_cast<u32>(GRef<u16>(cam + 0x18E)) << 4;
    GRef<u16>(kHud + 0x1A) = static_cast<u16>(hh0);
    const u32 hh = (hh0 & 0xFFF0u) >> 1;
    GRef<u16>(kHud + 0x20) = static_cast<u16>(cx + hw);
    GRef<u16>(kHud + 0x1C) = static_cast<u16>(cx - hw);
    GRef<u16>(kHud + 0x22) = static_cast<u16>(cy + hh);
    GRef<u16>(kHud + 0x1E) = static_cast<u16>(cy - hh);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, unit + 0x10);
    sceVu0TransMatrix(m, m, cam);
    sceVu0MulMatrix(m, cam + 0xE0, m);
    sceVu0RotTransPers(P, m, v, 0);
    GRef<u16>(kHud + 0x24) = GRef<u16>(P);
    GRef<u16>(kHud + 0x26) = GRef<u16>(P + 4);
    s32 on = 0;
    if (0 < GRef<s32>(P + 0xC)) {
        const s32 px = GRef<s32>(P), py = GRef<s32>(P + 4);
        const s32 ccx = HudU16(0x14), sw = HudU16(0x18), ccy = HudU16(0x16), sh = HudU16(0x1A);
        if (px < ccx + sw && ccx - sw < px && py < ccy + sh) on = ccy - sh < py ? 1 : 0;
    }
    HudColour(list, 0);
    func_0015F4F0(list, 0, 0);
    func_0015F350(list, 0, 1);
    func_0015F420(list, 0, 0, 0);
    HudColour(list, 1);
    func_0015F4F0(list, 1, Wide() ? 1 : 0);
    func_0015F350(list, 1, 1);
    func_0015F420(list, 1, 0, 0);
    HudColour(list, 2);
    func_0015F4F0(list, 2, 1);
    func_0015F350(list, 2, 1);
    func_0015F420(list, 2, 0, 0);
    func_0015F4F0(list, 4, Wide() ? 1 : 0);
    func_0015F420(list, 4, 0, 0);
    func_001633C8(list);
    func_00162958(list, unit);
    func_00162D98(list, unit);
    func_001630A0(list);  // the original also passes the unit in a1; 0x001630A0 does not read it
    func_00163C08(list, unit);
    func_00163818(list, st);
    func_00160598(cam, list, unit);
    if (GRef<s8>(Scene() + 0x1653) == 1) func_00167758(list, unit);
    if (GRef<s8>(Scene() + 0x1653) == 2) func_00167D68(list, unit);
    if (Ctx()->sub != 0x1D) {
        if (GRef<s8>(st + 0x149) == 2) {
            func_00160E78(list, unit);
            func_00161720(list, unit);
        } else {
            if (on == 1) {
                func_00162030(cam, list, unit);
                func_00161BB0(list, unit);
                func_001609D8(list, unit);
                func_00161250(list, unit);
                func_00160FF8(list, unit, st);
                func_00161890(list, unit);
                func_00160698(list);
            }
            if (GRef<u8>(st + 0x149) == 0) func_00160200(cam, list, unit);
        }
    }
    if (Ctx()->sub == 0x15) func_00166A10(cam, list, unit, on);
    func_00166588(cam, list, unit);
    if ((~StatusFlags() & 0x10000000u) != 0) {
        if (GRef<u16>(st + 0x158) != 0) func_00164440(cam, list, unit);
        u32 f188;
        if ((W(st + 0x14C) & 0x8000u) != 0) {
            f188 = W(st + 0x188);
            if ((~f188 & 0x40000u) != 0 && GRef<u8>(st + 0x15B) != 0) {
                func_00164F00(cam, list, unit);
                f188 = W(st + 0x188);
            }
        } else {
            f188 = W(st + 0x188);
        }
        if ((f188 & 0xFF00u) != 0) func_00164BD8(cam, list, unit);
    }
    func_00165AB8(cam, list, unit);
    const s32 mode = GRef<s32>(PadState(p));
    const u32 pe = PadExt(p);
    const u32 sel = HudState() + 0x72;
    if (GRef<s8>(Scene() + 0x1653) == 2) {
        if (MapCycle(p, mode)) {
            if ((W(pe + 4) & 0x200u) != 0) GRef<u8>(sel) = static_cast<u8>(GRef<u8>(sel) + 1u);
            if (!(GRef<u8>(HudState() + 0x72) < 3)) GRef<u8>(HudState() + 0x72) = 0;
        } else if (static_cast<u32>(mode - 5) < 2) {
            const u8 a = GRef<u8>(pe + 0x1D);
            if (!(a < 0x81))
                GRef<u8>(HudState() + 0x72) = 2;
            else
                GRef<u8>(HudState() + 0x72) = a != 0 ? 1 : 0;
        } else {
            GRef<u8>(HudState() + 0x72) = 0;
        }
        const u8 s = GRef<u8>(HudState() + 0x72);
        if (s == 0)
            func_00160710(list, unit);
        else if (s == 1)
            DrawRadar(cam, list, unit);
        else
            DrawRadarMap(cam, list, static_cast<u32>(p));
    } else {
        if (MapCycle(p, mode)) {
            if ((W(pe + 4) & 0x200u) != 0) GRef<u8>(sel) = static_cast<u8>(GRef<u8>(sel) + 1u);
            if (!(GRef<u8>(HudState() + 0x72) < 4)) GRef<u8>(HudState() + 0x72) = 0;
        } else if (static_cast<u32>(mode - 5) < 2 && GRef<u8>(pe + 0x1D) != 0) {
            GRef<u8>(HudState() + 0x72) = 1;
        } else {
            GRef<u8>(HudState() + 0x72) = 0;
        }
        if (GRef<u8>(HudState() + 0x72) != 0)
            DrawRadarMap(cam, list, static_cast<u32>(p));
        else
            DrawRadar(cam, list, unit);
    }
    if (Ctx()->sub != 0x1D) DrawHudWarning(list);
    if ((~StatusFlags() & 0x10000000u) != 0) func_0016D778(list);
}

namespace {

// Message window colour for step k of 8: (0xEC, 0x6B, 0x76) brightening to white at k = 8.
s32 WindowShade(s32 k, s32 mul, s32 base) { return (k * mul) / 8 + base; }

// Message window (bucket 4): a shaded box (x + 0x10, y + 0x10)-(x1, y1) with alpha 0x40 for open
// step k, then (via draw) the content, then the 4-line outline in black alpha 0x40 (Z 0xFFFFFE).
template <typename Content>
void MessageWindow(u32 list, s32 x, s32 y, s32 x1, s32 y1, s32 k, Content draw) {
    const s32 r = WindowShade(k, 0x13, 0xEC), g = WindowShade(k, 0x94, 0x6B), b = WindowShade(k, 0x89, 0x76);
    u32 p = AllocBucketPacket(list, 4, 4, 0, 0x50000004);
    Put64(p, 1, 0x551);
    Put64(p, 0, 0x3023400000008001ull);
    GRef<s32>(p + 0x10) = r;
    GRef<s32>(p + 0x14) = g;
    GRef<s32>(p + 0x18) = b;
    GRef<u32>(p + 0x1C) = 0x40;
    GRef<s32>(p + 0x20) = x + 0x10;
    GRef<s32>(p + 0x24) = y + 0x10;
    GRef<u32>(p + 0x28) = 0xFFFFFF;
    GRef<u32>(p + 0x2C) = 0;
    GRef<s32>(p + 0x30) = x1;
    GRef<s32>(p + 0x34) = y1;
    GRef<u32>(p + 0x38) = 0xFFFFFF;
    GRef<u32>(p + 0x3C) = 0;
    func_0015F2B0(list, 4, 0, 0, 0, -0x80);
    draw();
    func_0015F2B0(list, 4, 0, 0, 0, 0x40);
    p = AllocBucketPacket(list, 4, 9, 0, 0x50000009);
    Put64(p, 1, 0x55);
    Put64(p, 0, 0x2020C00000008004ull);
    const s32 v[8][2] = {{x + 0x10, y}, {x1, y}, {x + 0x10, y1}, {x1, y1}, {x1, y + 0x10}, {x1, y1}, {x, y + 0x10}, {x, y1}};
    u32 o = p + 0x10;
    for (const auto& e : v) {
        GRef<s32>(o) = e[0];
        GRef<s32>(o + 4) = e[1];
        GRef<u32>(o + 8) = 0xFFFFFE;
        GRef<u32>(o + 0xC) = 0;
        o += 0x10;
    }
}

}  // namespace

// EE: 0x0017F110
// Centre message window frame (bucket 4, alpha blend on, no test) at (x, y) (box to x + 0x360, y + 0x100), open
// step k (0..8): shaded box, the title sprite (cell 0x520, 0x900, 0x140 x 0x50) and the outline.
void func_0017F110(u32 list, s32 x, s32 y, s32 k) {
    func_0015F4F0(list, 4, 1);
    func_0015F350(list, 4, 0);
    func_0015F420(list, 4, 0, 0);
    MessageWindow(list, x, y, x + 0x360, y + 0x100, k, [&] {
        const s32 sx = Wide() ? x + 0x3C : x + 0x50;
        DrawHudSprite(list, 4, static_cast<u16>(sx), static_cast<u16>(y + 0x50), 0x520, 0x900, 0x140, 0x50, 0);
    });
}

// EE: 0x0017F408
// Message text window (bucket 4) at (x, y), width w, height 0x180, shade step k: shaded box, the
// text (mode 10, at x + 0x150 / 16:9 0xE0, y + 0x90) and the outline.
void func_0017F408(u32 list, s32 x, s32 y, s32 w, s32 k, u32 text) {
    func_0015F4F0(list, 4, 1);
    MessageWindow(list, x, y, x + w, y + 0x180, k, [&] {
        const s32 tx = Wide() ? x + 0xE0 : x + 0x150;
        func_0015F6A0(list, 4, text, static_cast<u16>(tx), static_cast<u16>(y + 0x90), 10);
    });
}

// EE: 0x00246D98
// Cubic Bernstein weights of t into out: (1-t)^3, 3(1-t)^2 t, 3(1-t) t^2, t^3.
// fpucheck: verified 1
void func_00246D98(f32 t, u32 out) {
    const f32 s = fpu::Sub(1.0f, t);
    const f32 s3 = s * 3.0f;
    GRef<f32>(out + 0xC) = t * t * t;
    GRef<f32>(out) = s * s * s;
    GRef<f32>(out + 4) = s3 * s * t;
    GRef<f32>(out + 8) = s3 * t * t;
}

// EE: 0x00246DE8
// out = p0 w.x + p1 w.y + p2 w.z + p3 w.w (VU0 vmulax / vmadday / vmaddaz / vmaddw).
void func_00246DE8(u32 out, u32 p0, u32 p1, u32 p2, u32 p3, u32 w) {
    const auto load = [](u32 a) { return vu0::Vec4{F(a), F(a + 4), F(a + 8), F(a + 12)}; };
    const vu0::Vec4 r = vu0::MulMatrix(load(p0), load(p1), load(p2), load(p3), load(w));
    GRef<f32>(out) = r.x;
    GRef<f32>(out + 4) = r.y;
    GRef<f32>(out + 8) = r.z;
    GRef<f32>(out + 0xC) = r.w;
}

namespace {

// DMA cnt + DIRECT 4 + GIF tag (reglist PRIM, RGBAQ, UV, XYZF2, UV, XYZF2) of a textured sprite at
// q; the words the original leaves unwritten (q +0x1C, +0x34, +0x44) are not touched. Returns q +
// 0x50.
u32 TexSprite(u32 q, u32 rgba, u32 uv0, u32 xy0, u32 uv1, u32 xy1) {
    GRef<u32>(q + 4) = 0;
    GRef<u32>(q) = 0x10000004;
    GRef<u32>(q + 8) = 0;
    GRef<u32>(q + 0xC) = 0x50000004;
    GRef<u32>(q + 0x10) = 0x8001;
    GRef<u32>(q + 0x14) = 0x64000000;
    GRef<u32>(q + 0x18) = 0x00434310;
    GRef<u64>(q + 0x20) = 0x156;
    GRef<u32>(q + 0x28) = rgba;
    GRef<f32>(q + 0x2C) = 1.0f;
    GRef<u32>(q + 0x30) = uv0;
    GRef<u32>(q + 0x38) = xy0;
    GRef<u32>(q + 0x3C) = 0;
    GRef<u32>(q + 0x40) = uv1;
    GRef<u32>(q + 0x48) = xy1;
    GRef<u32>(q + 0x4C) = 0;
    return q + 0x50;
}

}  // namespace

// EE: 0x0017CEB8
// Mission label overlay (object o = 0x00410B40; replay HUD 0x0017D8F0) built directly at p:
// ALPHA / TEST / the context's +0xE0 register with its high word 1 / CLAMP; the label texture
// (list +0x40, index +0x50 < count +0x53) at (0x719, 0x739) pulsing with alpha 128 x the Bezier
// (+0x00..+0x30) of the 60-frame triangle wave of +0x4C; the sub-label (list +0x44, index +0x51 <
// +0x54) at (+0x5C, +0x5E); while +0x50 < 2 the counter strip from texture +0x48 (two parts, then
// +0x58 boxes: filled for the bits of +0x5A, and the blinking cursor over box +0x59); then the
// context's +0xE0 register restored. Returns the end of the data.
u32 func_0017CEB8(u32 o, u32 p) {
    GRef<u32>(p) = 0x10000005;
    GRef<u32>(p + 4) = 0;
    GRef<u32>(p + 8) = 0;
    GRef<u32>(p + 0xC) = 0x50000005;
    GRef<u32>(p + 0x10) = 0x8004;
    GRef<u32>(p + 0x18) = 0xE;
    GRef<u32>(p + 0x14) = 0x10000000;
    GRef<u32>(p + 0x20) = 0x48;
    GRef<u32>(p + 0x28) = 0x42;
    GRef<u32>(p + 0x24) = 0;
    GRef<u32>(p + 0x2C) = 0;
    GRef<u32>(p + 0x30) = 0x30003;
    GRef<u32>(p + 0x38) = 0x47;
    CopyQ(p + 0x40, CtxAddr() + 0xE0);
    GRef<u32>(p + 0x44) = 1;
    GRef<u32>(p + 0x58) = 8;
    GRef<u32>(p + 0x50) = 0;
    GRef<u32>(p + 0x54) = 0;
    u32 q = p + 0x60;
    if (W(o + 0x40) != 0 && GRef<u8>(o + 0x50) < GRef<u8>(o + 0x53) && GRef<u8>(o + 0x50) != 0xFF) {
        s32 t = GRef<s32>(o + 0x4C) % 0x3C - 0x1E;
        if (t < 0) t = -t;
        guest::GuestFrame fr(0x20);
        const u32 w = fr.Address(0), pt = fr.Address(0x10);
        func_00246D98(fpu::IntToFloat(t) / 30.0f, w);
        func_00246DE8(pt, o, o + 0x10, o + 0x20, o + 0x30, w);
        const u32 tex = W(o + 0x40) + GRef<u8>(o + 0x50) * 0x28u;
        const s32 a = fpu::FloatToInt(F(pt) * 128.0f);
        q = BuildTextureUpload(tex, q, 1, 0);
        const u32 hdr = W(tex + 4);
        const u32 tw = GRef<u16>(hdr + 0xC), th = GRef<u16>(hdr + 0xE);
        q = TexSprite(q, (static_cast<u32>(a) << 24) | 0x808080u, 0x80008, 0x73907190, (tw * 16 + 8) | ((th * 16 + 8) << 16),
                      ((tw + 0x719) << 4) | ((th + 0x739) << 20));
    }
    if (W(o + 0x44) != 0) {
        const u32 i = GRef<u8>(o + 0x51);
        if (i < GRef<u8>(o + 0x54) && i != 0xFF) {
            const u32 tex = i * 0x28 + W(o + 0x44);
            q = BuildTextureUpload(tex, q, 1, 0);
            const u32 hdr = W(tex + 4);
            const u32 tw = GRef<u16>(hdr + 0xC), th = GRef<u16>(hdr + 0xE);
            const s32 x = GRef<s16>(o + 0x5C), y = GRef<s16>(o + 0x5E);
            const u32 xy0 = (static_cast<u32>(x + 0x800) << 4) | (static_cast<u32>(y + 0x800) << 20);
            const u32 xy1 = (static_cast<u32>(x + static_cast<s32>(tw) + 0x800) << 4) | (static_cast<u32>(y + static_cast<s32>(th) + 0x800) << 20);
            q = TexSprite(q, 0x80808080, 0x80008, xy0, (tw * 16 + 8) | ((th * 16 + 8) << 16), xy1);
        }
    }
    if (GRef<u8>(o + 0x50) < 2) {
        const u32 tex = W(o + 0x48);
        q = BuildTextureUpload(tex, q, 1, 0);
        const u32 hdr = W(tex + 4);
        const auto th = [hdr] { return static_cast<u32>(GRef<u16>(hdr + 0xE)); };
        const auto v1 = [&](u32 u) { return ((th() * 16 + 8) << 16) | u; };
        const auto y1 = [&] { return (th() + 0x8A7) << 20; };
        q = TexSprite(q, 0x80808080, 0x80008, 0x8A707190, v1(0x708), y1() | 0x7890);
        q = TexSprite(q, 0x80808080, 0x80708, 0x8A707910, v1(0x788), y1() | 0x7990);
        const u32 n = GRef<u8>(o + 0x58);
        q = TexSprite(q, 0x80808080, 0x80788, ((n * 10 + 0x79D) << 4) | 0x8A700000u, v1(0x808),
                      ((n * 10 + 10 + 0x79D) << 4) | y1());
        if (GRef<u8>(o + 0x58) != 0) {
            u32 k = 0;
            do {
                const bool on = ((static_cast<u32>(GRef<u16>(o + 0x5A)) >> (k & 0x1F)) & 1u) != 0;
                const u32 x0 = ((k * 10 + 0x79D) << 4) | 0x8A700000u;
                const u32 x1 = (((k + 1) * 10 + 0x79D) << 4) | y1();
                q = on ? TexSprite(q, 0x80808080, 0x80828, x0, v1(0x8C8), x1) : TexSprite(q, 0x80808080, 0x808C8, x0, v1(0x968), x1);
                ++k;
                const s32 c = GRef<s32>(o + 0x4C);
                GRef<u32>(q + 4) = 0;
                GRef<u32>(q) = 0x10000003;
                GRef<u32>(q + 8) = 0;
                GRef<u32>(q + 0xC) = 0x50000003;
                GRef<u32>(q + 0x10) = 0x8001;
                GRef<u32>(q + 0x14) = 0x44000000;
                GRef<u32>(q + 0x18) = 0x4410;
                GRef<u64>(q + 0x20) = 0x146;
                GRef<u32>(q + 0x28) = (static_cast<u32>(c % 32) << 24) | 0x303080u;
                GRef<f32>(q + 0x2C) = 1.0f;
                GRef<u32>(q + 0x34) = 0;
                GRef<u32>(q + 0x30) = ((GRef<u8>(o + 0x59) * 10u + 0x79F) << 4) | 0x8A900000u;
                GRef<u32>(q + 0x3C) = 0;
                GRef<u32>(q + 0x38) = ((GRef<u8>(o + 0x59) * 10u + 0x7A3) << 4) | 0x8AF00000u;
                q += 0x40;
            } while (static_cast<s32>(k) < GRef<u8>(o + 0x58));
        }
    }
    GRef<u32>(q + 4) = 0;
    GRef<u32>(q) = 0x10000002;
    GRef<u32>(q + 8) = 0;
    GRef<u32>(q + 0xC) = 0x50000002;
    GRef<u32>(q + 0x10) = 0x8001;
    GRef<u32>(q + 0x14) = 0x10000000;
    GRef<u32>(q + 0x18) = 0xE;
    CopyQ(q + 0x20, CtxAddr() + 0xE0);
    return q + 0x30;
}

// EE: 0x0017CC80
// Resets the mission label object o: no label / sub-label / counter (+0x50..+0x52 = 0xFF),
// counter boxes and cursor 0, clock +0x4C 0, bits +0x5A 0.
void func_0017CC80(u32 o) {
    GRef<u8>(o + 0x59) = 0;
    GRef<u8>(o + 0x52) = 0xFF;
    GRef<u8>(o + 0x50) = 0xFF;
    GRef<u8>(o + 0x51) = 0xFF;
    GRef<u32>(o + 0x4C) = 0;
    GRef<u8>(o + 0x58) = 0;
    GRef<u16>(o + 0x5A) = 0;
}

// EE: 0x0017D840
// Sets the label index (+0x50) of object o.
void func_0017D840(u32 o, u8 v) { GRef<u8>(o + 0x50) = v; }

// EE: 0x0017D848
// Sets the sub-label (+0x51) of object o, when it has sub-labels (+0x54), with its position
// preset: (-0x71, 0xA4) or, for preset 0, (-0xE7, 0xB7).
void func_0017D848(u32 hud, u8 label, s8 preset) {
    if (GRef<u8>(hud + 0x54) == 0) return;
    GRef<u8>(hud + 0x51) = label;
    s16 x, y;
    if (static_cast<u8>(preset) == 0) {
        x = -0xE7;
        y = 0xB7;
    } else {
        x = -0x71;
        y = 0xA4;
    }
    GRef<s16>(hud + 0x5E) = y;
    GRef<s16>(hud + 0x5C) = x;
}

// EE: 0x0017D888
// Sets the counter box count (+0x58) of object o.
void func_0017D888(u32 o, u8 v) { GRef<u8>(o + 0x58) = v; }

// EE: 0x0017D8E8
// Resets the mission label object 0x00410B40.
void func_0017D8E8() { func_0017CC80(W(0x00410B40)); }

// EE: 0x0017D8F0
// Draws the mission label object 0x00410B40 into bucket `index` of list as one packet.
void func_0017D8F0(u32 list, s32 index) { EndPacket(list, index, func_0017CEB8(W(0x00410B40), BeginPacket())); }

// EE: 0x0011DC20
// Per-frame sound listener set-up for every player p (unit list +0x4C [p]: {player, code, pos,
// dir}; +4 + 16 p: 4 emitter records {unit, kind, vel, rot, unit}): the listener follows the
// scene camera +0xB0 / +0xC0 (code scene +9) in sub-state 0x11, else the player unit or, in the
// external view (state +0x188 bit 2), its per-player camera (scene + 0x440 p; code scene +0x1109,
// bit 0x80 with state bit 8). The 4 nearest audible units (shown, sound kind +0x9B; for
// non-aircraft also +0x4C bit 0x20000000; by distance to the listener, or else by the player's
// proximity entry when in range) become the emitters, their kind mapped from +0x9B and the
// unit's 0x01000000 flag; then handed to the 3D sound update 0x0021B3A8.
void func_0011DC20() {
    const u32 np = Players();
    for (u32 p = 0; p < np; ++p) {
        guest::GuestFrame fr(0x20);
        const u32 dist = fr.Address(0), idx = fr.Address(0x10);
        for (u32 k = 0; k < 4; ++k) {
            GRef<f32>(dist + 4 * k) = 262144.0f;
            GRef<s32>(idx + 4 * k) = -1;
        }
        const auto insert = [&](f32 d, s32 i) {
            for (s32 k = 3; k >= 0; --k) {
                const u32 dk = dist + 4u * static_cast<u32>(k), ik = idx + 4u * static_cast<u32>(k);
                const f32 cur = F(dk);
                if (!(d < cur)) return;
                if (k < 3) {
                    GRef<f32>(dk + 4) = cur;
                    GRef<u32>(ik + 4) = W(ik);
                }
                GRef<f32>(dk) = d;
                GRef<s32>(ik) = i;
            }
        };
        const u32 L = W(UnitList() + 4 * p + 0x4C);
        const u32 sc = Scene();
        if (Ctx()->sub == 0x11) {
            GRef<u32>(L + 0xC) = sc + 0xC0;
            GRef<u32>(L) = p;
            GRef<u32>(L + 8) = sc + 0xB0;
            GRef<u32>(L + 4) = GRef<u8>(sc + 9);
            for (s32 i = 0; i < UnitCount(); ++i) {
                const u32 u = Unit(i);
                const u32 nf = ~W(u + 0x4C);
                if ((nf & 0x10000000u) != 0) continue;
                if (GRef<u8>(u + 0x54) == 0) {
                    if ((nf & 0x20000000u) != 0) continue;
                } else if (GRef<u8>(u + 0x9B) == 0) {
                    continue;
                }
                insert(func_00104F10(u, W(L + 8)), i);
            }
        } else {
            GRef<u32>(L) = p;
            const u32 pu = Unit(static_cast<s32>(p));
            const u32 st = W(pu + 0x44);
            if ((W(st + 0x188) & 2u) != 0) {
                const u32 c = p * 0x440 + sc;
                GRef<u32>(L + 0xC) = c + 0xC0;
                GRef<u32>(L + 8) = c + 0xB0;
            } else {
                GRef<u32>(L + 8) = pu;
                GRef<u32>(L + 0xC) = pu + 0x10;
            }
            const u32 c4 = 4 * 0x440 + sc;
            GRef<u32>(L + 4) = GRef<u8>(c4 + 9);
            if ((W(st + 0x188) & 8u) != 0) GRef<u32>(L + 4) = GRef<u8>(c4 + 9) | 0x80u;
            for (s32 i = 0; i < UnitCount(); ++i) {
                if (static_cast<u32>(i) == p) continue;
                const u32 u = Unit(i);
                const u32 e = W(0x00410648 + 4 * p) + 12u * static_cast<u32>(i);
                const u32 nf = ~W(u + 0x4C);
                if ((nf & 0x10000000u) != 0) continue;
                if (GRef<u8>(u + 0x54) == 0 && (nf & 0x20000000u) != 0) continue;
                if (GRef<u8>(u + 0x9B) == 0) continue;
                if ((W(e) & 2u) == 0) continue;
                insert(F(e + 4), i);
            }
        }
        for (u32 k = 0; k < 4; ++k) {
            const s32 i = GRef<s32>(idx + 4 * k);
            const u32 em = W(UnitList() + 16 * p + 4 + 4 * k);
            GRef<s32>(em) = i;
            if (i == -1) continue;
            const u32 u = Unit(i);
            const bool f = (W(u + 0x4C) & 0x01000000u) != 0;
            switch (GRef<u8>(u + 0x9B)) {
                case 1: GRef<u32>(em + 4) = f ? 0u : 1u; break;
                case 2: GRef<u32>(em + 4) = f ? 2u : 3u; break;
                case 3: GRef<u32>(em + 4) = f ? 4u : 5u; break;
                case 4: GRef<u32>(em + 4) = f ? 6u : 7u; break;
                case 5: GRef<u32>(em + 4) = f ? 8u : 9u; break;
                case 6: GRef<u32>(em + 4) = 0xB; break;
                case 7: GRef<u32>(em + 4) = 0xC; break;
                default: break;
            }
            GRef<u32>(em + 0xC) = u + 0x10;
            GRef<u32>(em + 8) = u + 0x20;
            GRef<u32>(em + 0x10) = u;
        }
        func_0021B3A8(static_cast<s32>(p), UnitList() + 16 * p + 4);
    }
}

// EE: 0x0011E560
// Radio speaker request `msg` (0x00252B88) for a message about unit `from` (speaker voice state
// +0xF5 / +0xF6, defaults 4 / 8) concerning unit `to` (its class code +0x99, default 0x31, and
// for aircraft its voice +0xF5, default 6).
void func_0011E560(s32 msg, u32 from, u32 to) {
    s32 b = 4, c = 8, d = 6, cls = 0x31;
    if (from != 0) {
        const u32 st = W(from + 0x44);
        c = GRef<u8>(st + 0xF6);
        b = GRef<u8>(st + 0xF5);
    }
    if (to != 0) {
        cls = GRef<u8>(to + 0x99);
        if (GRef<u8>(to + 0x54) == 1) d = GRef<u8>(W(to + 0x44) + 0xF5);
    }
    func_00252B88(msg, b, c, d, cls);
}

// EE: 0x0017B2B0
// Clears the landing radio hints given to player 0 (state +0x28C).
void func_0017B2B0() { GRef<u32>(W(Unit(0) + 0x44) + 0x28C) = 0; }

namespace {

// XYZ2 doubleword from 32-bit x, y as the EE builds it with zero-extended words (Z 0xFFFFFF).
u64 XyU(u32 x, u32 y) { return static_cast<u64>(x) | (static_cast<u64>(y) << 16) | kZFar; }

}  // namespace

// EE: 0x0017EC88
// Special weapon loadout panel (bucket 7, mission menu): for the special weapon of aircraft
// `model` in slot `slot` (0x0011B920, its slot bits 0x002A1F00), the 5 label sprites (cells
// 0x002B8498 / from 3 on 0x002B84A0, at 0x002B84B8 [k]) each followed by its box, filled (cell
// 0x002B8470 [k]) when the bit is set or empty (0x002B84A8 / 0x002B84B0); then the aircraft
// picture (scene +0x1768) and the slot's frame sprite (column slot x 0x360).
void func_0017EC88(u32 list, u8 model, s32 slot) {
    const u32 bits = GRef<u8>(0x002A1F00 + func_0011B920(model, slot));
    u32 p = AllocBucketPacket(list, 7, 3, 0, 0x50000003);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008002ull);
    Put64(p, 2, 0x48);
    Put64(p, 3, 0x42);
    Put64(p, 4, 0x2004B00621412500ull);
    Put64(p, 5, 6);
    p = AllocBucketPacket(list, 7, 0x17, 0, 0x50000017);
    Put64(p, 1, 0x10);
    Put64(p, 0, 0x2400000000008001ull);
    Put64(p, 2, 0x156);
    Put64(p, 3, 0x160408000ull);
    Put64(p, 4, 0x8400000000008005ull);
    Put64(p, 5, 0x53535353);
    const auto uv = [](u32 c) { return static_cast<u64>(GRef<u16>(c)) | (static_cast<u64>(GRef<u16>(c + 2)) << 16); };
    const auto uv1 = [](u32 c) {
        return (static_cast<u64>(GRef<u16>(c)) + GRef<u16>(c + 4)) | ((static_cast<u64>(GRef<u16>(c + 2)) + GRef<u16>(c + 6)) << 16);
    };
    for (u32 k = 0; k < 5; ++k) {
        const u32 lbl = k < 2 ? 0x002B8498u : 0x002B84A0u;
        const s32 bw = k < 2 ? 0x80 : 0x40;
        const u32 c = ((bits >> k) & 1u) != 0 ? 0x002B8470u + 8 * k : lbl + 0x10;
        const u32 pos = 0x002B84B8 + 8 * k;
        const u32 x = W(pos), y = W(pos + 4);
        const u32 o = p + 0x30 + 0x40 * k;
        GRef<u64>(o) = uv(lbl);
        GRef<u64>(o + 8) = XyU(x, y);
        GRef<u64>(o + 0x10) = uv1(lbl);
        GRef<u64>(o + 0x18) = XyU(x + GRef<u16>(lbl + 4), y + GRef<u16>(lbl + 6));
        GRef<u64>(o + 0x20) = uv(c);
        GRef<u64>(o + 0x28) = XyU(x + static_cast<u32>(bw), y + 0x40);
        GRef<u64>(o + 0x30) = uv1(c);
        GRef<u64>(o + 0x38) = XyU(x + GRef<u16>(c + 4) + static_cast<u32>(bw), y + GRef<u16>(c + 6) + 0x40);
    }
    func_00104338(list, 7, W(Scene() + 0x1768), 0);
    p = AllocBucketPacket(list, 7, 3, 0, 0x50000003);
    Put64(p, 1, 0xE);
    Put64(p, 0, 0x1000400000008002ull);
    Put64(p, 3, 0x3F);
    Put64(p, 2, 0);
    Put64(p, 4, 0x2004C00619412590ull);
    Put64(p, 5, 6);
    p = AllocBucketPacket(list, 7, 5, 0, 0x50000005);
    const s64 col = static_cast<s64>(slot * 0x360);
    Put64(p, 0, 0x2400000000008001ull);
    Put64(p, 1, 0x10);
    Put64(p, 2, 0x156);
    Put64(p, 3, 0x160408000ull);
    Put64(p, 4, 0x4400000000008001ull);
    Put64(p, 5, 0x5353);
    Put64(p, 7, 0x00FFFFFF7A007E90ull);
    Put64(p, 6, (static_cast<u64>(col + 8) << 16) | 8);
    Put64(p, 9, 0x00FFFFFF7D508180ull);
    Put64(p, 8, (static_cast<u64>(col + 0x358) << 16) | 0x2F8);
}
}  // namespace ac04::game
