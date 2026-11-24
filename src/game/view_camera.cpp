// Flight view cameras (block A of S5): the per-player view records S + p * 0x440 (camera at +0xB0,
// copy at +0x270, flags +8, eye/target +0x10/+0x20, attach +0x30, up +0x40, follow offsets
// +0x50/+0x60, look-at +0x70, extra look +0xA0, orbit +0x430..+0x43C) and the projectile-follow
// states (0x00410680[2]). docs/subsystems/camera-hud.md.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "game/main_context.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

namespace {

void ZeroQword(u32 dst) {
    for (u32 o = 0; o < 16; o += 4) GRef<u32>(dst + o) = 0;
}

u32 FollowState(s32 n) { return GRef<u32>(0x00410680 + 4 * static_cast<u32>(n)); }

}  // namespace

// EE: 0x00102838
// Copies the projection parameters of camera b to camera a.
void func_00102838(u32 a, u32 b) {
    const u32 v = GRef<u32>(b + 0x198);
    GRef<u32>(a + 0x160) = GRef<u32>(b + 0x160);
    GRef<u32>(a + 0x198) = v;
    for (u32 o = 0x164; o <= 0x170; o += 4) GRef<u32>(a + o) = GRef<u32>(b + o);
}

// EE: 0x00102990
// Copies the viewport parameters of camera b to camera a.
void func_00102990(u32 a, u32 b) {
    GRef<u16>(a + 0x188) = GRef<u16>(b + 0x188);
    GRef<u32>(a + 0x190) = GRef<u32>(b + 0x190);
    GRef<u16>(a + 0x18A) = GRef<u16>(b + 0x18A);
    GRef<u32>(a + 0x194) = GRef<u32>(b + 0x194);
    GRef<u16>(a + 0x18C) = GRef<u16>(b + 0x18C);
    GRef<u32>(a + 0x178) = GRef<u32>(b + 0x178);
    GRef<u32>(a + 0x17C) = GRef<u32>(b + 0x17C);
    GRef<u32>(a + 0x198) = GRef<u32>(b + 0x198);
    GRef<u16>(a + 0x18E) = GRef<u16>(b + 0x18E);
}

// EE: 0x0010C850
// Orbit parameters of a view (f12, f13, f14 -> +0x434, +0x438, +0x43C; a1 -> +0x430).
void func_0010C850(f32 a, f32 b, f32 c, u32 view, u32 d) {
    GRef<f32>(view + 0x43C) = c;
    GRef<u32>(view + 0x430) = d;
    GRef<f32>(view + 0x434) = a;
    GRef<f32>(view + 0x438) = b;
}

// EE: 0x0010C9D0
// View cameras per player: full screen, or left/right halves when game[0x26] == 1 (split screen);
// 16:9 width aware. The camera (+0xB0) is copied to +0x270.
void func_0010C9D0() {
    f32 width = 640.0f;
    if ((Ctx()->flags & 0x10000) != 0) width = GRef<f32>(0x0040DAA0);
    // port: Hor+ ultrawide (EE 0x0010C9F4 loads the 640.0 that the PCSX2 21:9 patch raised to
    // 1136.0). With the 4:3 field of view kept the width is 640 x window_aspect / (4/3); split screen
    // halves the expanded width below, as in the original. Unchanged when not ultrawide.
    if ((Ctx()->flags & 0x10000) != 0) width = port::UltrawideProjectionWidth(640.0f, width);
    const u32 game = g_GameState().addr;
    GRef<u8>(g_FlightScene() + 0x1650) = GRef<u8>(game + 0x25);
    for (u32 p = 0; p < GRef<u8>(g_GameState().addr + 0x25); ++p) {
        const u32 v = g_FlightScene() + p * 0x440;
        GRef<u8>(v + 7) = 0;
        const u32 cam = v + 0xB0;
        GRef<u8>(v + 4) = 0;
        const u32 copy = v + 0x270;
        GRef<u8>(v + 5) = static_cast<u8>(p);
        GRef<u8>(v + 6) = static_cast<u8>(p);
        GRef<u32>(v + 0) = 0x8000000F;
        const u32 terrain = GRef<u32>(0x00410694);
        if (GRef<u8>(g_GameState().addr + 0x26) == 1) {
            const f32 aspect = GRef<f32>(copy + 0x168) * 0.0625f;
            SetCameraProjection(width * 0.5f, 480.0f, GRef<f32>(0x0040DAA4), aspect, GRef<f32>(terrain + 0xA4), cam);
            const u16 half = static_cast<u16>(Ctx()->draw_w >> 1);
            SetCameraViewport(1.0f, GRef<f32>(0x0040DAA8), cam, static_cast<s16>(p * half), 0, static_cast<s16>(half),
                              static_cast<s16>(Ctx()->draw_h));
        } else {
            SetCameraProjection(width, 480.0f, GRef<f32>(0x0040DAAC), 1.0f, GRef<f32>(terrain + 0xA4), cam);
            SetCameraViewport(1.0f, GRef<f32>(0x0040DAB0), cam, 0, 0, static_cast<s16>(Ctx()->draw_w),
                              static_cast<s16>(Ctx()->draw_h));
        }
        func_00102838(copy, cam);
        func_00102990(copy, cam);
    }
}

// EE: 0x0010D590
// Dead (no reference): sets a view record in one call: flags (+8), eye (+0x10), target (+0x20),
// attach (+0x30), up (+0x40), follow offset (+0x50) and look-at (+0x70).
void func_0010D590(u32 view, u8 flags, u32 eye, u32 target, u32 attach, u32 up, u32 follow, u32 look) {
    GRef<u8>(view + 8) = flags;
    CopyQword(view + 0x10, eye);
    CopyQword(view + 0x20, target);
    CopyQword(view + 0x30, attach);
    CopyQword(view + 0x40, up);
    CopyQword(view + 0x50, follow);
    CopyQword(view + 0x70, look);
}

// EE: 0x0010D5E8
void func_0010D5E8(u32 view, u8 flags) {
    GRef<u8>(view + 8) = flags;
}

// EE: 0x0010D5F0
void func_0010D5F0(u32 view, u32 eye, u32 target) {
    CopyQword(view + 0x10, eye);
    CopyQword(view + 0x20, target);
}

// EE: 0x0010D610
void func_0010D610(u32 view, u32 attach) {
    GRef<u8>(view + 8) |= 4;
    CopyQword(view + 0x30, attach);
}

// EE: 0x0010D630
void func_0010D630(u32 view, u32 up) {
    GRef<u8>(view + 8) |= 2;
    CopyQword(view + 0x40, up);
}

// EE: 0x0010D650
void func_0010D650(u32 view, u32 a, u32 b) {
    GRef<u8>(view + 8) |= 1;
    CopyQword(view + 0x50, a);
    CopyQword(view + 0x60, b);
}

// EE: 0x0010D680
void func_0010D680(u32 view, u32 look) {
    GRef<u8>(view + 8) |= 8;
    CopyQword(view + 0x70, look);
}

// EE: 0x0010D6A0
// View reset: header bytes and the vector block +0x10..+0x8F and +0xA0 zeroed (128-bit zero
// stores), both cameras re-initialised, orbit words cleared.
void func_0010D6A0(u32 view) {
    GRef<u32>(view + 0) = 0;
    GRef<u8>(view + 4) = 0;
    GRef<u8>(view + 5) = 0;
    GRef<u8>(view + 6) = 0;
    GRef<u8>(view + 7) = 0;
    GRef<u8>(view + 8) = 0;
    GRef<u8>(view + 10) = 0;
    GRef<u32>(view + 0xC) = 0;
    for (u32 o = 0x10; o <= 0x80; o += 0x10) ZeroQword(view + o);
    ZeroQword(view + 0xA0);
    Init2dCamera(view + 0xB0);
    Init2dCamera(view + 0x270);
    GRef<u32>(view + 0x43C) = 0;
    GRef<u32>(view + 0x430) = 0;
    GRef<u32>(view + 0x434) = 0;
    GRef<u32>(view + 0x438) = 0;
}

// EE: 0x0010D748
void func_0010D748(u32 view, u32 look) {
    CopyQword(view + 0xA0, look);
    GRef<u8>(view + 10) = 0;
}

namespace {

// Wraps v into the range given by four .lit4 constants (while v > hi: v -= hi_step; while v < lo:
// v += lo_step), as the solve does for each angle.
f32 WrapAngle(f32 v, u32 hi, u32 hi_step, u32 lo, u32 lo_step) {
    while (K(hi) < v) v = fpu::Sub(v, K(hi_step));
    while (v < K(lo)) v = fpu::Add(v, K(lo_step));
    return v;
}

}  // namespace

// EE: 0x0010D760
// View camera solve (docs/subsystems/camera-hud.md "func_0010D760(V) camera solve"): eye and
// angles of camera C0 (V+0xB0) and its copy C1 (V+0x270) from the attach point, look-at, follow
// offsets and up vector selected by the flags at V+8; angles snapped or smoothed (V+0 bit 0x100);
// the follow camera is kept out of the terrain with segment tests (func_0013D698).
// fpucheck: look-at 3 sub + 2 add; flag 2: 1 + 3 add; smooth: 3 sub + wraps + 3 add; follow: 3;
// step 9: wraps; step 10: 2 add; probe 1 sub 1 add; clearance 1 add, 2 sub, 1 add/sub, fold.
void func_0010D760(u32 v) {
    guest::GuestFrame frame(0x120);
    const u32 sp = frame.Address();
    const u32 acc = sp, zero = sp + 0x10, hit = sp + 0x20, nrm = sp + 0x30, q = sp + 0x40, old = sp + 0x50,
              m = sp + 0xA0, t = sp + 0x70, o = sp + 0x80, from2 = sp + 0x90, w = sp + 0xF0, to2 = sp + 0xE0,
              attr = sp + 0x100;
    const u32 c0 = v + 0xB0, c1 = v + 0x270, ang = c0 + 0x10, ang1 = c1 + 0x10;
    ee::memset(acc, 0, 0x10);
    ee::memset(zero, 0, 0x10);
    GRef<f32>(zero + 0xC) = 1.0f;
    const u32 flags = g_FlightScene() + 0x1668;
    if ((GRef<u32>(flags) & 0x100) != 0) {
        GRef<u32>(flags) &= 0xFFFFFEFFu;
        GRef<u32>(v) &= 0xFFFFFEFFu;
    }
    if ((GRef<u8>(v + 8) & 4) != 0) {
        func_00103070(c0, v + 0x10, v + 0x20, v + 0x30);
        func_00103070(c1, zero, v + 0x20, v + 0x30);
    } else {
        CopyQword(c0, v + 0x10);
        CopyQword(c1, zero);
    }
    CopyQword(ang, v + 0x20);
    u8 f = GRef<u8>(v + 8);
    if ((f & 8) != 0 && !(GRef<f32>(v + 0x70) == GRef<f32>(c0) && GRef<f32>(v + 0x74) == GRef<f32>(c0 + 4) &&
                          GRef<f32>(v + 0x78) == GRef<f32>(c0 + 8))) {
        GRef<f32>(old + 0) = fpu::Sub(GRef<f32>(v + 0x70), GRef<f32>(c0));
        GRef<f32>(old + 4) = fpu::Sub(GRef<f32>(v + 0x74), GRef<f32>(c0 + 4));
        GRef<f32>(old + 8) = fpu::Sub(GRef<f32>(v + 0x78), GRef<f32>(c0 + 8));
        GRef<u32>(old + 0xC) = 0;  // stack garbage on the PS2; only x, y, z of the result are used
        const u32 r = sp + 0x60;
        sceVu0UnitMatrix(r);
        func_00102D28(r, r, ang);
        sceVu0InversMatrix(r, r);
        sceVu0ApplyMatrix(old, r, old);
        const f32 pitch = ee::asinf(fpu::Div(-GRef<f32>(old + 4), GetVectorLength(old)));
        GRef<f32>(acc) = fpu::Add(GRef<f32>(acc), pitch);
        const f32 yaw = ee::atan2f(GRef<f32>(old), GRef<f32>(old + 8));
        GRef<f32>(acc + 8) = GRef<f32>(v + 0xC);
        GRef<f32>(acc + 4) = fpu::Add(GRef<f32>(acc + 4), yaw);
        f = GRef<u8>(v + 8);
    }
    if ((f & 1) != 0) {
        func_00102EF0(ang, ang, v + 0x60);
        f = GRef<u8>(v + 8);
    }
    if ((f & 2) != 0) {
        if ((f & 1) != 0) {
            CopyQword(q, v + 0x10);
            const f32 len = GetVectorLength(v + 0x50);
            GRef<f32>(q + 4) = fpu::Add(GRef<f32>(q + 4), len * K(0x0040DAEC));
            if (func_0013D698(GRef<u32>(0x00410694), hit, attr, v + 0x10, q, nrm) == 1)
                for (u32 k = 0; k < 16; k += 4) GRef<u32>(v + 0x40 + k) = 0;
        }
        GRef<f32>(acc + 0) = fpu::Add(GRef<f32>(acc + 0), GRef<f32>(v + 0x40));
        GRef<f32>(acc + 4) = fpu::Add(GRef<f32>(acc + 4), GRef<f32>(v + 0x44));
        GRef<f32>(acc + 8) = fpu::Add(GRef<f32>(acc + 8), GRef<f32>(v + 0x48));
    }
    CopyQword(old, ang);
    CopyQword(sp + 0x60, v + 0x80);
    if ((GRef<u32>(v) & 0x100) != 0) {
        const f32 bx = GRef<f32>(v + 0x80), by = GRef<f32>(v + 0x84), bz = GRef<f32>(v + 0x88);
        f32 x = fpu::Sub(GRef<f32>(acc), bx), y = fpu::Sub(GRef<f32>(acc + 4), by), z = fpu::Sub(GRef<f32>(acc + 8), bz);
        x = WrapAngle(x, 0x0040DAF0, 0x0040DAF4, 0x0040DAF8, 0x0040DAFC);
        y = WrapAngle(y, 0x0040DB00, 0x0040DB04, 0x0040DB08, 0x0040DB0C);
        z = WrapAngle(z, 0x0040DB10, 0x0040DB14, 0x0040DB18, 0x0040DB1C);
        GRef<f32>(acc) = x;
        GRef<f32>(acc + 4) = y;
        GRef<f32>(acc + 8) = z;
        GRef<u32>(v + 0x8C) = 0;
        GRef<f32>(v + 0x88) = fpu::Add(bz, z * 0.125f);
        GRef<f32>(v + 0x80) = fpu::Add(bx, x * 0.125f);
        GRef<f32>(v + 0x84) = fpu::Add(by, y * 0.125f);
    } else {
        const f32 x = WrapAngle(GRef<f32>(acc), 0x0040DB20, 0x0040DB24, 0x0040DB28, 0x0040DB2C);
        const f32 y = WrapAngle(GRef<f32>(acc + 4), 0x0040DB30, 0x0040DB34, 0x0040DB38, 0x0040DB3C);
        const f32 z = WrapAngle(GRef<f32>(acc + 8), 0x0040DB40, 0x0040DB44, 0x0040DB48, 0x0040DB4C);
        GRef<f32>(acc) = x;
        GRef<f32>(acc + 4) = y;
        GRef<f32>(acc + 8) = z;
        GRef<f32>(v + 0x80) = x;
        GRef<f32>(v + 0x84) = y;
        GRef<f32>(v + 0x88) = z;
        GRef<u32>(v + 0x8C) = 0;
    }
    func_00102EF0(ang, ang, v + 0x80);
    CopyQword(ang1, ang);
    if ((GRef<u8>(v + 8) & 1) == 0) return;
    // Follow camera.
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, ang);
    CopyQword(o, v + 0x50);
    sceVu0ApplyMatrix(t, m, o);
    sceVu0ScaleVector(t, t, K(0x0040DB50));
    sceVu0AddVector(q, c0, t);
    if (func_0013D698(GRef<u32>(0x00410694), hit, attr, v + 0x10, q, nrm) == 1) {
        const f32 a = fpu::Sub(1.0f, GRef<f32>(v + 0x90));
        const f32 oy = GRef<f32>(o + 4);
        GRef<u32>(o + 4) = 0;
        GRef<f32>(o + 8) = fpu::Add(GRef<f32>(o + 8), oy);
        GRef<f32>(v + 0x90) = fpu::Sub(1.0f, a * K(0x0040DB54));
    } else {
        GRef<f32>(v + 0x90) = GRef<f32>(v + 0x90) * K(0x0040DB58);
    }
    const f32 lim_hi = K(0x0040DB5C), lim_lo = K(0x0040DB6C);
    sceVu0SubVector(acc, old, ang);
    for (u32 k = 0; k < 3; ++k)
        if (lim_hi < GRef<f32>(acc + 4 * k)) GRef<f32>(acc + 4 * k) = fpu::Sub(GRef<f32>(acc + 4 * k), K(0x0040DB60 + 4 * k));
    for (u32 k = 0; k < 3; ++k)
        if (GRef<f32>(acc + 4 * k) < lim_lo) GRef<f32>(acc + 4 * k) = fpu::Add(GRef<f32>(acc + 4 * k), K(0x0040DB70 + 4 * k));
    sceVu0ScaleVector(acc, acc, GRef<f32>(v + 0x90));
    sceVu0AddVector(ang, ang, acc);
    for (u32 k = 0; k < 3; ++k)
        if (lim_hi < GRef<f32>(ang + 4 * k)) GRef<f32>(ang + 4 * k) = fpu::Sub(GRef<f32>(ang + 4 * k), K(0x0040DB7C + 4 * k));
    for (u32 k = 0; k < 3; ++k)
        if (GRef<f32>(ang + 4 * k) < lim_lo) GRef<f32>(ang + 4 * k) = fpu::Add(GRef<f32>(ang + 4 * k), K(0x0040DB88 + 4 * k));
    CopyQword(ang1, ang);
    const f32 sz = fpu::Add(GRef<f32>(v + 0xA8) * K(0x0040DB94), GRef<f32>(o + 8) * K(0x0040DB98));
    const f32 sy = fpu::Add(GRef<f32>(v + 0xA4) * K(0x0040DB94), GRef<f32>(o + 4) * K(0x0040DB98));
    GRef<f32>(v + 0xA8) = sz;
    GRef<f32>(o + 8) = sz;
    GRef<f32>(o + 4) = sy;
    GRef<f32>(v + 0xA4) = sy;
    sceVu0ApplyMatrix(t, m, o);
    sceVu0AddVector(q, t, c0);
    const s32 blocked = func_0013D698(GRef<u32>(0x00410694), hit, attr, v + 0x10, q, nrm);
    if (blocked != 1) {
        func_00103070(c0, c0, ang, o);
        func_00103070(c1, c1, ang1, o);
        return;
    }
    CopyQword(from2, q);
    CopyQword(to2, q);
    GRef<f32>(from2 + 4) = fpu::Sub(GRef<f32>(from2 + 4), 128.0f);
    GRef<f32>(to2 + 4) = fpu::Add(GRef<f32>(to2 + 4), 128.0f);
    f32 ground = 0.0f;
    if (func_0013D698(GRef<u32>(0x00410694), hit, attr, from2, to2, nrm) == blocked) ground = GRef<f32>(hit + 4);
    if (!(ground < GRef<f32>(q + 4))) {
        func_00103070(c0, c0, ang, o);
        func_00103070(c1, c1, ang1, o);
        return;
    }
    const f32 ty = GRef<f32>(t + 4), tz = GRef<f32>(t + 8);
    const f32 h = fpu::Sqrt(fpu::Add(ty * ty, tz * tz));
    const f32 a1 = ee::asinf(fpu::Div(-ty, h));
    const f32 a2 = ee::asinf(fpu::Div(-fpu::Sub(ground, GRef<f32>(c0 + 4)), h));
    const f32 delta = fpu::Sub(a1, a2);
    CopyQword(w, o);
    GRef<u32>(w + 4) = 0;
    sceVu0ApplyMatrix(w, m, w);
    const bool wz = 0.0f < GRef<f32>(w + 8), tzp = 0.0f < GRef<f32>(t + 8);
    GRef<f32>(ang) = wz != tzp ? fpu::Sub(GRef<f32>(ang), delta) : fpu::Add(GRef<f32>(ang), delta);
    const f32 x = GRef<f32>(ang);
    if (x < K(0x0040DB9C)) {
        GRef<f32>(ang) = fpu::Sub(K(0x0040DBA0), x);
        const f32 y = GRef<f32>(ang + 4);
        GRef<f32>(ang + 4) = y < 0.0f ? fpu::Add(y, K(0x0040DBA4)) : fpu::Sub(y, K(0x0040DBA8));
        const f32 z = GRef<f32>(ang + 8);
        GRef<f32>(ang + 8) = z < 0.0f ? fpu::Add(z, K(0x0040DBAC)) : fpu::Sub(z, K(0x0040DBBC));
    } else if (K(0x0040DBB0) < x) {
        const f32 k = K(0x0040DBB4);
        GRef<f32>(ang) = fpu::Sub(k, x);
        const f32 y = GRef<f32>(ang + 4);
        GRef<f32>(ang + 4) = y < 0.0f ? fpu::Add(y, k) : fpu::Sub(y, k);
        const f32 z = GRef<f32>(ang + 8);
        GRef<f32>(ang + 8) = z < 0.0f ? fpu::Add(z, K(0x0040DBB8)) : fpu::Sub(z, K(0x0040DBBC));
    }
    CopyQword(ang1, ang);
    func_00103070(c0, c0, ang, o);
    func_00103070(c1, c1, ang1, o);
}

// EE: 0x00175B08
// Replay camera for the followed projectile of view V's player: special weapon (flag 2 and a slot,
// 0x001748A8) or missile (0x00174640); V+7 = 0x43 (0x41 in mission kind 2).
void func_00175B08(u32 v) {
    const u32 f = GRef<u32>(0x00410680 + 4 * static_cast<u32>(GRef<u8>(v + 6)));
    const u8 mode = GRef<s8>(g_FlightScene() + 0x1653) == 2 ? 0x41 : 0x43;
    if ((GRef<u32>(f + 0xC) & 2) != 0 && GRef<s32>(f + 0x14) != -1) {
        GRef<u8>(v + 7) = mode;
        func_001748A8(v);
        return;
    }
    if (GRef<s32>(f + 0x10) != -1) {
        GRef<u8>(v + 7) = mode;
        func_00174640(v);
    }
}

// EE: 0x0010CB80
// Per-frame view cameras (docs/subsystems/camera-hud.md "UpdateViewCameras 0x0010CB80"): camera
// set-up by state (death orbit, mission end, scene-2 subs 0x14/0x1B and 0x19, replay follow, player
// views), the solve, the zoom ray, the projection factor and the camera matrices.
// fpucheck: death 1 sub; end 1 add; target zoom 1 sub + 1 add; zoom ray 1 add.
void UpdateViewCameras() {
    func_0010C9D0();
    GRef<u8>(g_FlightScene() + 0x1650) = GRef<u8>(g_GameState().addr + 0x25);
    guest::GuestFrame frame(0xA0);
    const u32 sp = frame.Address();
    for (u32 p = 0; p < GRef<u8>(g_GameState().addr + 0x25); ++p) {
        const u32 u = GRef<u32>(0x002A1F70 + 4 * p);
        const u32 pl = GRef<u32>(u + 0x44);
        const u32 v = g_FlightScene() + p * 0x440;
        const u32 c0 = v + 0xB0, c1 = v + 0x270;
        f32 z0 = GRef<f32>(0x0040DAB4);
        auto set_zoom = [&](f32 z) {
            GRef<f32>(c0 + 0x168) = z;
            GRef<f32>(c1 + 0x168) = z;
        };
        const u32 rules = GRef<u32>(0x004104F4);
        if ((~GRef<u32>(u + 0x4C) & 0x1000000) != 0) {
            // Death camera: orbit around the falling unit.
            GRef<u32>(v) &= 0xFFFFFEFDu;
            GRef<u32>(v + 0xC) = 0;
            GRef<u8>(v + 4) = 5;
            GRef<u8>(v + 7) = 1;
            GRef<u8>(v + 9) = 4;
            func_0010D5E8(v, 0);
            const f32 d = fpu::Sub(-2048.0f, fpu::IntToFloat(GRef<s32>(v + 0x430)) * 8192.0f / 600.0f);
            const u32 eye = sp, tgt = sp + 0x10, look = sp + 0x20;
            if (GRef<u8>(v + 0xB) < 4) {
                GRef<f32>(eye + 0) = ee::sinf(GRef<f32>(v + 0x434)) * d;
                GRef<f32>(eye + 4) = d;
                GRef<f32>(eye + 8) = ee::cosf(GRef<f32>(v + 0x434)) * d;
                GRef<u32>(eye + 0xC) = 0;
                sceVu0AddVector(eye, eye, u);
                for (u32 k = 0; k < 16; k += 4) GRef<u32>(tgt + k) = 0;
                func_0010D5F0(v, eye, tgt);
                sceVu0ScaleVector(look, u + 0x20, 60.0f);
                sceVu0AddVector(look, look, u);
                func_0010D680(v, look);
            } else {
                GRef<f32>(eye + 4) = d;
                GRef<f32>(eye + 8) = -1.0f;
                GRef<u32>(eye + 0) = 0;
                GRef<u32>(eye + 0xC) = 0;
                sceVu0AddVector(eye, eye, u);
                for (u32 k = 0; k < 16; k += 4) GRef<u32>(tgt + k) = 0;
                GRef<f32>(tgt) = GRef<f32>(0x0040DAB8);
                func_0010D5F0(v, eye, tgt);
            }
            z0 = GRef<f32>(0x0040DABC);
            GRef<u32>(v + 0xC) = 0;
            set_zoom(z0);
            if ((Ctx()->flags & 0x100) == 0 || Ctx()->sub == 0xC) GRef<s32>(v + 0x430) = GRef<s32>(v + 0x430) + 1;
        } else if ((GRef<u32>(rules + 0x20) & 0x100000) != 0) {
            // Mission-end camera.
            func_0010D5E8(v, 0);
            GRef<u32>(v) &= 0xFFFFFFFDu;
            GRef<u8>(v + 9) = 4;
            GRef<u8>(v + 7) = 0;
            GRef<u32>(v + 0xC) = 0;
            GRef<u32>(v + 0x90) = 0;
            const u32 eye = sp, tgt = sp + 0x10;
            for (u32 k = 0; k < 16; k += 4) GRef<u32>(tgt + k) = 0;
            for (u32 k = 0; k < 16; k += 4) GRef<u32>(eye + k) = GRef<u32>(0x002A16E0 + k);
            GRef<f32>(eye + 4) = fpu::Add(GRef<f32>(eye + 4), fpu::IntToFloat(GRef<s32>(v + 0x430)) * -20.0f);
            func_0010D5F0(v, eye, tgt);
            func_0010D680(v, 0x002A16F0);
            set_zoom(z0);
            if (Ctx()->sub == 0xC) GRef<s32>(v + 0x430) = GRef<s32>(v + 0x430) + 1;
        } else if (Ctx()->sub == 0x14 || Ctx()->sub == 0x1B) {
            func_0010D5E8(v, 0);
            func_0010D5F0(v, u, u + 0x10);
            GRef<u8>(v + 7) = 0x83;
            GRef<u8>(v + 9) = 2;
            GRef<u32>(v) &= 0xFFFFFFFDu;
            GRef<u32>(v + 0xC) = 0;
            GRef<u32>(v + 0x90) = 0;
            const u32 t = 0x002A1700 + (GRef<u32>(rules + 0x24) % 3) * 12;
            const u32 a = sp, up = sp + 0x10;
            for (u32 k = 0; k < 16; k += 4) GRef<u32>(a + k) = GRef<u32>(pl + 0x40 + k);
            GRef<f32>(a + 4) = fpu::Div(GRef<f32>(a + 4), GRef<f32>(t + 8));
            func_0010D610(v, a);
            for (u32 k = 0; k < 16; k += 4) GRef<u32>(a + k) = GRef<u32>(pl + 0x30 + k);
            GRef<f32>(a + 4) = fpu::Div(GRef<f32>(a + 4), GRef<f32>(t + 8));
            z0 = GRef<f32>(0x0040DAC0);
            func_0010D650(v, a, pl + 0x70);
            func_0010D748(v, a);
            GRef<f32>(up + 4) = GRef<f32>(t + 4);
            GRef<f32>(up + 0) = GRef<f32>(t + 0);
            GRef<u32>(up + 8) = 0;
            GRef<u32>(up + 0xC) = 0;
            func_0010D630(v, up);
            // port: ultrawide with the 4:3 field of view keeps the 4:3 camera distance.
            if ((Ctx()->flags & 0x10000) != 0 && !port::UltrawideUses4x3Distances()) z0 = GRef<f32>(0x0040DAC4);
            set_zoom(z0);
        } else if (Ctx()->sub == 0x19) {
            const u32 bits = GRef<u32>(rules + 0x20);
            u32 k = GRef<u32>(rules + 0x24) % 3;
            if ((bits & 0x2000000) != 0) k = (bits & 0x4000000) != 0 ? k + 3 : k + 6;
            func_0010D5E8(v, 0);
            GRef<u32>(v) &= 0xFFFFFFFDu;
            GRef<u8>(v + 9) = 2;
            GRef<u8>(v + 7) = 3;
            GRef<u32>(v + 0xC) = 0;
            GRef<u32>(v + 0x90) = 0;
            for (u32 o = 0; o < 16; o += 4) GRef<u32>(sp + o) = 0;
            func_0010D5F0(v, 0x002A1730 + 16 * k, sp);
            func_0010D680(v, 0x002A17C0 + 16 * k);
            set_zoom(GRef<f32>(0x002A1850 + 4 * k));
        } else if ((GRef<u32>(pl + 0x188) & 2) != 0) {
            GRef<u8>(v + 7) = 3;
            func_00136770(static_cast<s32>(p), (GRef<u32>(pl + 0x188) & 4) != 0 ? 1 : 0);
            func_00175B08(v);
            GRef<u32>(v) &= 0xFFFFFFFDu;
            GRef<u8>(v + 9) = 3;
            GRef<u32>(v + 0x90) = 0;
            for (u32 o = 0; o < 16; o += 4) GRef<u32>(v + 0x40 + o) = 0;
        } else {
            // Player views: s8 P+0x149 = 0 HUD only, 1 cockpit, 2 external.
            func_0010D5E8(v, 0);
            func_0010D5F0(v, u, u + 0x10);
            const s8 view = GRef<s8>(pl + 0x149);
            if (view == 2) {
                GRef<u8>(v + 9) = 2;
                GRef<u8>(v + 7) = 0x83;
                z0 = GRef<f32>(0x0040DAC8);
                func_0010D610(v, pl + 0x40);
                func_0010D650(v, pl + 0x30, pl + 0x70);
                // port: ultrawide with the 4:3 field of view keeps the 4:3 external-view distance
                // (0x0040DACC is the 16:9 one; the PCSX2 16:9 fix by nemesis2000 lowers it to 627.87).
                if ((Ctx()->flags & 0x10000) != 0 && !port::UltrawideUses4x3Distances()) z0 = GRef<f32>(0x0040DACC);
            } else if (view == 1) {
                GRef<u8>(v + 9) = 1;
                GRef<u8>(v + 7) = 0x82;
                func_0010D610(v, pl + 0x50);
            } else {
                GRef<u8>(v + 7) = 0;
                GRef<u8>(v + 9) = 0;
                func_0010D610(v, pl + 0x50);
            }
            func_0010D630(v, pl + 0x10);
            if ((GRef<u32>(pl + 0x188) & 0x40) != 0) GRef<u32>(pl + 0x188) &= 0xFFFFFFBFu;
            else GRef<u32>(v) |= 0x100;
            if ((GRef<u32>(pl + 0x188) & 1) != 0) {
                func_0010D680(v, pl + 0x60);
                f32 x = func_00104F10(GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(GRef<u8>(pl + 0x14A))), u) * GRef<f32>(0x0040DAD0);
                if (4096.0f < x) x = 4096.0f;
                if (x < z0) x = z0;
                set_zoom(fpu::Add(fpu::Sub(x, z0) * GRef<f32>(pl + 0x18C), z0));
            } else if (0.0f < GRef<f32>(pl + 0x18C)) {
                set_zoom(fpu::Add(fpu::Sub(4096.0f, z0) * GRef<f32>(pl + 0x18C), z0));
            } else {
                set_zoom(z0);
            }
            if (4096.0f < GRef<f32>(c0 + 0x168)) set_zoom(4096.0f);
        }
        func_0010D760(v);
        if (0.0f < GRef<f32>(pl + 0x18C)) {
            // Zoom ray: keep the zoomed view in front of the terrain.
            const u32 from = sp, to = sp + 0x10, hit = sp + 0x20, dir = sp + 0x30, nrm = sp + 0x40, m = sp + 0x50,
                      attr = sp + 0x90;
            ee::memset(dir, 0, 0x10);
            GRef<f32>(dir + 8) = 4096.0f;
            GRef<f32>(dir + 8) = fpu::Add(GRef<f32>(c0 + 0x168), GRef<f32>(c0 + 0x168));
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, c0 + 0x10);
            sceVu0ApplyMatrix(dir, m, dir);
            for (u32 k = 0; k < 16; k += 4) GRef<u32>(from + k) = GRef<u32>(c0 + k);
            sceVu0AddVector(to, from, dir);
            if (func_0013D698(GRef<u32>(0x00410694), hit, attr, from, to, nrm) == 1) {
                sceVu0InversMatrix(m, m);
                sceVu0SubVector(hit, hit, from);
                sceVu0ApplyMatrix(hit, m, hit);
                const f32 h = GRef<f32>(hit + 8) * 0.5f;
                GRef<f32>(hit + 8) = h < z0 ? z0 : h;
                set_zoom(GRef<f32>(hit + 8));
            }
        }
        const f32 k = (GRef<u8>(v + 7) & 3) == 2 ? GRef<f32>(0x0040DAD8) : GRef<f32>(0x0040DAD4);
        GRef<f32>(c1 + 0x16C) = GRef<f32>(c1 + 0x168) * k;
        GRef<f32>(c0 + 0x16C) = GRef<f32>(c0 + 0x168) * k;
        UpdateCameraMatrices(c1);
        UpdateCameraMatrices(c0);
    }
}

// EE: 0x0010C868
// Death camera of player p: up to 4 orbit angles from theta (step *0x0040DA94, wrapped above
// *0x0040DA98) until the ray from the unit to (sin, 1, cos) * -16384 is clear of the terrain;
// V+0xB = the try that was clear (0xFF when none), orbit angle and counter reset.
// fpucheck: 1 add + 1 sub per try
void func_0010C868(f32 theta, s32 p) {
    const u32 u = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(p));
    const u32 v = g_FlightScene() + static_cast<u32>(p) * 0x440;
    GRef<u8>(v + 0xB) = 0xFF;
    const f32 step = GRef<f32>(0x0040DA94), wrap = GRef<f32>(0x0040DA9C), lim = GRef<f32>(0x0040DA98);
    guest::GuestFrame frame(0x40);
    const u32 q = frame.Address(), hit = frame.Address(0x10), nrm = frame.Address(0x20), attr = frame.Address(0x30);
    for (s32 k = 0; k < 4; ++k) {
        GRef<f32>(q + 0) = ee::sinf(theta) * -16384.0f;
        GRef<f32>(q + 4) = -16384.0f;
        GRef<f32>(q + 8) = ee::cosf(theta) * -16384.0f;
        GRef<u32>(q + 0xC) = 0;
        sceVu0AddVector(q, q, u);
        if (func_0013D698(GRef<u32>(0x00410694), hit, attr, u, q, nrm) != 1) {
            GRef<u8>(v + 0xB) = static_cast<u8>(k);
            break;
        }
        theta = fpu::Add(theta, step);
        if (lim < theta) theta = fpu::Sub(theta, wrap);
    }
    func_0010C850(theta, 0.0f, 0.0f, v, 0);
}

namespace {

// Camera shake with scale c on (x, y) of the player record (three random-table reads, only when the
// rumble factor is non-zero).
// fpucheck: 2 sub + 2 add
void Shake(s32 p, u32 x, u32 y, f32 c) {
    const f32 k = func_00257360(p);
    if (k == 0.0f) return;
    f32 s = k * c;
    GRef<f32>(x) = fpu::Add(GRef<f32>(x), fpu::Sub(NextRandomFloat(), 0.5f) * s);
    if (NextRandomFloat() < 0.5f) s = -s;
    GRef<f32>(y) = fpu::Add(GRef<f32>(y), fpu::Sub(NextRandomFloat(), 0.5f) * s);
}

f32 Pad(u32 k, u32 o) { return fpu::IntToFloat(GRef<u8>(k + o)); }

}  // namespace

// EE: 0x00125B70
// Player view control (docs/subsystems/camera-hud.md "func_00125B70(p) player view control"): view
// cycling (pressed 0x10), mission-kind view rules, projectile follow (held 2), target selection
// (pressed 8) and tracking (held 8), cockpit look angles with limits and shake, the external offsets
// and the pressure zoom (P+0x18C). Random-table reads only in the shakes.
// fpucheck: view 1: 2 + 4 + 2 + 2 + q 3 + 2 add; others: 2 + 1 add, view 2: 3 sub; wrap 2; zoom 2
void func_00125B70(s32 p) {
    const u32 pp = static_cast<u32>(p);
    const u32 k = g_pMainContext().addr + 0x2D0 + 0x20 * pp;
    const u32 u = GRef<u32>(0x002A1F70 + 4 * pp);
    const u32 a = GRef<u32>(0x00410650 + 4 * pp);
    const u32 pl = GRef<u32>(u + 0x44);
    const u32 view = g_FlightScene() + pp * 0x440;
    if ((GRef<u32>(k + 4) & 0x10) != 0) {
        const u8 n = static_cast<u8>(GRef<u8>(pl + 0x149) + 1);
        GRef<u32>(pl + 0x188) |= 0x40;
        GRef<u8>(pl + 0x149) = n;
        if (static_cast<s8>(n) >= 3) GRef<u8>(pl + 0x149) = 0;
        const s8 v = GRef<s8>(pl + 0x149);
        if (v == 2) func_0010D748(view, a);
        else if (v == 1)
            for (u32 o = 0; o < 16; o += 4) GRef<u32>(pl + 0x20 + o) = 0;
    }
    if (GRef<s8>(g_FlightScene() + 0x1653) == 2) GRef<u8>(pl + 0x149) = 0;
    if (GRef<s8>(g_FlightScene() + 0x1653) == 3) {
        if ((GRef<u8>(pl + 0x185) & 0x10) != 0 || GRef<s8>(GRef<u32>(0x004104F4) + 0x7F) != 0) {
            if (GRef<s8>(pl + 0x149) != 2) func_0010D748(g_FlightScene() + pp * 0x440, a);
            GRef<u8>(pl + 0x149) = 2;
        } else {
            GRef<u8>(pl + 0x149) = 0;
        }
    }
    // Projectile follow (held 2).
    const u32 f = GRef<u32>(pl + 0x188) & 0xFFFFFFF3u;
    GRef<u32>(pl + 0x188) = f;
    const u32 follow = GRef<u32>(0x00410680 + 4 * pp);
    if ((GRef<u32>(k) & 2) != 0 && (GRef<u32>(follow + 0xC) & 1) != 0) {
        const u8 c = GRef<u8>(pl + 0x150);
        if (c < 0x1E) {
            GRef<u8>(pl + 0x150) = static_cast<u8>(c + 1);
            GRef<u32>(pl + 0x188) = f & 0xFFFFFFFDu;
        } else {
            if ((~f & 2) != 0) GRef<u32>(pl + 0x188) = f | 4;
            GRef<u32>(pl + 0x188) |= 2;
        }
    } else {
        u32 v = GRef<u32>(pl + 0x188);
        if ((v & 2) != 0) {
            v |= 8;
            GRef<u32>(pl + 0x188) = v;
        }
        GRef<u8>(pl + 0x150) = 0;
        GRef<u32>(pl + 0x188) = v & 0xFFFFFFFDu;
    }
    // Target selection (pressed 8).
    if ((GRef<u32>(k + 4) & 8) != 0) {
        const u8 lock = GRef<u8>(u + 0x62);
        if (lock != 0xFF) {
            GRef<u8>(pl + 0x14A) = lock;
        } else if (GRef<u8>(pl + 0x14A) != 0xFF) {
            GRef<u8>(pl + 0x14A) = 0xFF;
        } else {
            const u32 side = GRef<u32>(u + 0x48) & 3;
            f32 best = 0.0f;
            u32 found = 0xFF;
            for (s32 i = 2; i < GRef<s32>(g_UnitList()); ++i) {
                const u32 w = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(i));
                const u32 wf = GRef<u32>(w + 0x4C);
                if ((~wf & 0x10000000) != 0 || (~wf & 0x4000) == 0) continue;
                if ((GRef<u32>(w + 0x48) & 3) != side) continue;
                const f32 d = GRef<f32>(GRef<u32>(0x00410648 + 4 * pp) + 12 * static_cast<u32>(i) + 8);
                if (d < GRef<f32>(0x0040DE50)) continue;
                if (best < d) {
                    best = d;
                    found = static_cast<u32>(i);
                }
            }
            GRef<u8>(pl + 0x14A) = static_cast<u8>(found);
        }
    }
    // Target tracking (held 8).
    if ((GRef<u32>(k) & 8) != 0 && GRef<u8>(pl + 0x14A) != 0xFF) {
        const u32 w = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(GRef<u8>(pl + 0x14A)));
        if ((~GRef<u32>(w + 0x4C) & 0x10000000) != 0 || (GRef<u32>(u + 0x4C) & 0x40000000) != 0) {
            const s32 c = GRef<s32>(pl + 0x190) + 1;
            GRef<s32>(pl + 0x190) = c;
            if (c >= 0x79) GRef<u8>(pl + 0x14A) = 0xFF;
        }
    } else {
        GRef<u32>(pl + 0x190) = 0;
    }
    guest::GuestFrame frame(0xA0);
    const u32 sp = frame.Address();
    const s8 v = GRef<s8>(pl + 0x149);
    if (v == 1) {
        // Cockpit view: look angles P+0x20 (pitch) / P+0x24 (yaw).
        const u32 o = sp;
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(o + q) = GRef<u32>(pl + 0x20 + q);
        GRef<u32>(pl + 0x188) &= 0xFFFFFFFEu;
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x20 + q) = 0;
        if ((GRef<u32>(k) & 8) != 0 && GRef<u8>(pl + 0x14A) != 0xFF) {
            const u32 w = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(GRef<u8>(pl + 0x14A)));
            const u32 m = sp + 0x50, inv = sp + 0x10, d = sp + 0x90;
            sceVu0UnitMatrix(m);
            func_00102D28(m, m, u + 0x10);
            sceVu0InversMatrix(inv, m);
            sceVu0SubVector(d, w, u);
            sceVu0ApplyMatrix(d, inv, d);
            GRef<f32>(pl + 0x20) = ee::asinf(fpu::Div(-GRef<f32>(d + 4), GetVectorLength(d)));
            GRef<f32>(pl + 0x24) = ee::atan2f(GRef<f32>(d), GRef<f32>(d + 8));
        } else {
            GRef<u32>(pl + 0x190) = 0;
        }
        Shake(p, pl + 0x20, pl + 0x24, GRef<f32>(0x0040DE54));
        const bool wide = (Ctx()->flags & 0x10000) != 0;
        const u32 lim = a + (wide ? 0xD4 : 0x30);
        const f32 la = GRef<f32>(lim), lb = GRef<f32>(lim + 4), lc = GRef<f32>(lim + 8), ld = GRef<f32>(lim + 0xC),
                  le = GRef<f32>(lim + 0x10);
        const f32 nc = -lc;
        f32 y = fpu::Add(GRef<f32>(pl + 0x24), Pad(k, 0x1B) * lc / 255.0f);
        GRef<f32>(pl + 0x24) = y;
        y = fpu::Add(y, Pad(k, 0x1A) * nc / 255.0f);
        GRef<f32>(pl + 0x24) = y;
        if (lc < y) GRef<f32>(pl + 0x24) = lc;
        y = GRef<f32>(pl + 0x24);
        if (y < nc) {
            GRef<f32>(pl + 0x24) = nc;
            y = nc;
        }
        GRef<f32>(pl + 0x24) = fpu::Add(y * 0.125f, GRef<f32>(o + 4) * 0.875f);
        const f32 pk = GRef<f32>(0x0040DE58), plo = GRef<f32>(0x0040DE5C), phi = GRef<f32>(0x0040DE60);
        f32 x = fpu::Add(GRef<f32>(pl + 0x20), Pad(k, 0x18) * pk * 0.5f / 255.0f);
        GRef<f32>(pl + 0x20) = x;
        x = fpu::Sub(x, Pad(k, 0x19) * pk * 0.5f / 255.0f);
        GRef<f32>(pl + 0x20) = x;
        if (x < plo) GRef<f32>(pl + 0x20) = plo;
        x = GRef<f32>(pl + 0x20);
        if (phi < x) {
            GRef<f32>(pl + 0x20) = phi;
            x = phi;
        }
        GRef<f32>(pl + 0x20) = fpu::Add(x * 0.125f, GRef<f32>(o) * 0.875f);
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x10 + q) = GRef<u32>(pl + 0x20 + q);
        const f32 py = GRef<f32>(pl + 0x14);
        f32 qv;
        if (phi < py || py < plo) {
            f32 num, den;
            if (phi < py) {
                num = fpu::Sub(py, phi);
                den = fpu::Sub(lc, phi);
            } else {
                num = fpu::Add(py, phi);
                den = fpu::Add(nc, phi);
            }
            const f32 span = fpu::Sub(ld, le);
            f32 r = fpu::Div(num, den);
            r = r * r;
            qv = fpu::Add(r * span, le);
        } else {
            qv = fpu::Div(py, phi);
            if (qv < 0.0f) qv = -qv;
            qv = qv * le;
        }
        if (GRef<f32>(pl + 0x10) < qv) GRef<f32>(pl + 0x10) = qv;
        if (GRef<f32>(0x0040DE64) < GRef<f32>(pl + 0x10)) GRef<f32>(pl + 0x10) = GRef<f32>(0x0040DE64);
        GRef<u32>(pl + 0x18) = 0;
        GRef<u32>(pl + 0x1C) = 0;
        GRef<u32>(pl + 0x188) |= 0x40;
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x50 + q) = GRef<u32>(a + 0x20 + q);
        const f32 yaw = GRef<f32>(pl + 0x24);
        if (0.0f < yaw) GRef<f32>(pl + 0x50) = fpu::Add(GRef<f32>(pl + 0x50), la * yaw / lc);
        if (yaw < 0.0f) GRef<f32>(pl + 0x50) = fpu::Add(GRef<f32>(pl + 0x50), lb * yaw / nc);
    } else {
        if ((GRef<u32>(k) & 8) != 0 && GRef<u8>(pl + 0x14A) != 0xFF) {
            const u32 w = GRef<u32>(0x002A1F70 + 4 * static_cast<u32>(GRef<u8>(pl + 0x14A)));
            GRef<u32>(pl + 0x188) |= 1;
            for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x60 + q) = GRef<u32>(w + q);
        } else {
            GRef<u32>(pl + 0x188) &= 0xFFFFFFFEu;
        }
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x10 + q) = 0;
        if (GRef<s8>(pl + 0x149) == 0) Shake(p, pl + 0x10, pl + 0x14, GRef<f32>(0x0040DE68));
        const f32 kk = GRef<f32>(0x0040DE6C);
        const s32 dx = static_cast<s32>(GRef<u8>(k + 0x18)) - static_cast<s32>(GRef<u8>(k + 0x19));
        GRef<f32>(pl + 0x10) = fpu::Add(GRef<f32>(pl + 0x10), fpu::IntToFloat(dx) * kk * 0.5f / 255.0f);
        const s32 dy = static_cast<s32>(GRef<u8>(k + 0x1B)) - static_cast<s32>(GRef<u8>(k + 0x1A));
        f32 y = fpu::Add(GRef<f32>(pl + 0x14), fpu::IntToFloat(dy) * kk * 0.5f / 255.0f);
        GRef<f32>(pl + 0x14) = y;
        if ((GRef<u32>(k) & 0x20) != 0) GRef<f32>(pl + 0x14) = fpu::Add(y, kk);
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x50 + q) = GRef<u32>(a + 0x20 + q);
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x30 + q) = GRef<u32>(a + q);
        for (u32 q = 0; q < 16; q += 4) GRef<u32>(pl + 0x40 + q) = GRef<u32>(a + 0x10 + q);
        if (GRef<s8>(pl + 0x149) == 2) {
            const f32 x0 = GRef<f32>(pl + 0), y0 = GRef<f32>(pl + 4), z0 = GRef<f32>(pl + 8);
            const f32 roll = fpu::Sub(GRef<f32>(pl + 0x240), 0.5f) * 10.0f;
            GRef<f32>(pl + 0x44) = fpu::Sub(GRef<f32>(pl + 0x44), x0 * 60.0f * 5.0f);
            GRef<f32>(pl + 0x70) = -x0 * 6.0f;
            GRef<f32>(pl + 0x74) = -y0 * 12.0f;
            GRef<f32>(pl + 0x78) = -z0 * 3.0f;
            GRef<f32>(pl + 0x40) = fpu::Sub(GRef<f32>(pl + 0x40), y0 * 60.0f * 5.0f);
            GRef<f32>(pl + 0x48) = fpu::Sub(GRef<f32>(pl + 0x48), roll);
            Shake(p, pl + 0x40, pl + 0x44, GRef<f32>(0x0040DE70));
        }
    }
    f32 y = GRef<f32>(pl + 0x14);
    if (GRef<f32>(0x0040DE74) < y) {
        y = fpu::Sub(y, GRef<f32>(0x0040DE78));
        GRef<f32>(pl + 0x14) = y;
    }
    if (y < GRef<f32>(0x0040DE7C)) GRef<f32>(pl + 0x14) = fpu::Add(y, GRef<f32>(0x0040DE80));
    if (GRef<u8>(pl + 0x149) != 0) {
        GRef<u32>(pl + 0x18C) = 0;
    } else if ((GRef<u32>(k) & 8) != 0) {
        const u32 mode = GRef<u32>(g_pMainContext().addr + 0x210 + 0x50 * pp);
        f32 pv = mode - 5u < 2u ? Pad(k, 0x1C) : 0.0f;
        pv = 128.0f <= pv ? fpu::Sub(pv, 128.0f) * 0.0078125f : 0.0f;
        GRef<f32>(pl + 0x18C) = fpu::Add(GRef<f32>(pl + 0x18C) * 3.0f, pv) * 0.25f;
    } else {
        GRef<u32>(pl + 0x18C) = 0;
    }
}

// ---- Projectile follow ----------------------------------------------------------------------

// EE: 0x00136570
// Projectile-follow state n: missile and special slots -1, pointers and flags 0.
void func_00136570(s32 n) {
    const u32 f = FollowState(n);
    GRef<s32>(f + 0x14) = -1;
    GRef<u32>(f + 8) = 0;
    GRef<s32>(f + 0x10) = -1;
    GRef<u32>(f + 0) = 0;
    GRef<u32>(f + 4) = 0;
    GRef<u32>(f + 0xC) = 0;
}

// EE: 0x00136518
void func_00136518() {
    for (s32 n = 0; n < 2; ++n) {
        GRef<u32>(0x00410680 + 4 * static_cast<u32>(n)) = WorkHeapAlloc(0x18, 4);
        func_00136570(n);
    }
}

// EE: 0x001365A0
// Per frame: ages both follow states; drops the followed missile / special weapon once its pool
// occupancy bit (+0x8A28 / +0x8A2C) is clear.
void func_001365A0() {
    const u32 pool = GRef<u32>(0x004106AC);
    for (s32 n = 0; n < 2; ++n) {
        const u32 f = FollowState(n);
        const u32 flags = GRef<u32>(f + 0xC);
        if ((flags & 1) == 0) continue;
        if ((flags & 2) == 0) {
            const u32 free = ~GRef<u32>(pool + 0x8A28);
            // dsrlv: the shift amount is the low 6 bits of the slot.
            if (((static_cast<u64>(free) >> (GRef<u32>(f + 0x10) & 63)) & 1) != 0) {
                GRef<u32>(f + 0xC) = flags & 0xFFFFFFFEu;
                GRef<s32>(f + 0x10) = -1;
                GRef<u32>(f + 0) = 0;
            }
        } else if (((static_cast<s32>(~GRef<u32>(pool + 0x8A2C)) >> (GRef<u32>(f + 0x14) & 0x1F)) & 1) != 0) {
            GRef<u32>(f + 0xC) = flags & 0xFFFFFFFEu;
            GRef<s32>(f + 0x14) = -1;
            GRef<u32>(f + 4) = 0;
        }
        GRef<s32>(f + 8) = GRef<s32>(f + 8) + 1;
    }
}

// EE: 0x00136770
// Flag 0x100 of follow state n (set while scene-2 sub 0x11, replay, started the follow).
void func_00136770(s32 n, s32 on) {
    const u32 f = FollowState(n);
    if (on == 1) GRef<u32>(f + 0xC) |= 0x100;
    else GRef<u32>(f + 0xC) &= 0xFFFFFEFFu;
}

// EE: 0x00136668
// Follows a projectile for player n: kind 0 missile slot (pool +0x4000 + slot * 0x60), kind 1
// special weapon slot (pool +0x4C00 + slot * 0x80; not weapon kind 0x10). Ignored during replay
// (sub 0x11) when a follow is already active.
void FollowProjectile(s32 n, s64 kind, s32 slot) {
    const u32 pool = GRef<u32>(0x004106AC);
    const u32 f = FollowState(n);
    if (Ctx()->sub == 0x11 && (GRef<u32>(f + 0xC) & 1) != 0) return;
    bool replay = false;
    if (kind == 0) {
        GRef<u32>(f + 0xC) = (GRef<u32>(f + 0xC) & 0xFFFFFFFDu) | 1;
        GRef<s32>(f + 0x10) = slot;
        GRef<u32>(f + 0) = pool + static_cast<u32>(slot) * 0x60 + 0x4000;
        replay = Ctx()->sub == 0x11;
    } else if (kind == 1) {
        if (GRef<s8>(pool + static_cast<u32>(slot) * 0x80 + 0x4C5A) == 0x10) return;
        GRef<s32>(f + 0x14) = slot;
        GRef<u32>(f + 0xC) |= 3;
        GRef<u32>(f + 4) = pool + static_cast<u32>(slot) * 0x80 + 0x4C00;
        replay = Ctx()->sub == 0x11;
    }
    if (Ctx()->sub == 0x11) func_00136770(n, replay ? 1 : 0);
    GRef<u32>(f + 8) = 0;
}

}  // namespace ac04::game
