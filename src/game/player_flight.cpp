// Player aircraft per frame (slice S5c): controls, flight model, landing gear, engine and warning
// sounds, effect emitters. Behaviour notes per function: docs/port/s5-blockB-inventory.csv and
// docs/subsystems/flight.md. Unit u = 0x002A1F70[p] (+0x00 position, +0x10 rotation, +0x20 velocity,
// +0x44 player state P), aircraft record R = 0x00410650[p].
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "game/flight_common.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

f32 AsF32(u32 bits) {
    f32 f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// The flight-model rate update of 0x001282B8 / 0x00128510: rate +0x138 (state[0x4E]) towards
// stick +0x11C * lift +0x2E4 * R+0x78, scaled in double precision by (1 - |+0x2E8|) (libgcc
// __extendsfdf2 / __muldf3 / __subdf3 / __truncdfsf2: IEEE double, the host gives the same bits).
// fpucheck: 1 add (coupling) + 4 rate response + 1 final = 10 static adds in the original.
void PitchRate(u32 P, u32 R, u32 k_up, u32 k_down) {
    const f32 lift = F(P + 0x11C) * F(P + 0x2E4);
    const double a = static_cast<double>(lift * F(R + 0x78));
    double b = static_cast<double>(F(P + 0x2E8));
    if (b < 0.0) b = 0.0 - b;
    f32 t = static_cast<f32>(a * (1.0 - b));
    const f32 aoa = F(P + 0x2E8);
    if (t < 0.0f)
        t = fpu::Sub(t * F(R + 0x7C), lift * F(R + 0x8C) * aoa);
    else
        t = fpu::Add(t, lift * F(R + 0x8C) * aoa);
    const f32 v = RateResponse(P + 0x138, t, F(R + 0x80), F(R + 0x84), F(k_up), F(k_down));
    GRef<f32>(P) = v;
    if ((~W(P + 0x188) & 0x40000000u) != 0) GRef<f32>(P) = fpu::Sub(v, F(R + 0x8C) * aoa);
}

}  // namespace

// EE: 0x0011D300
// Falling wrecks (flags 0x10000000 and 0x100): position += velocity, the climb damped by the
// terrain clearance and a nose-down pitch while climbing.
// fpucheck: 6 adds (h - y, + 16, 1 - k, x + .., h - y, 1 - k)
void func_0011D300() {
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u32 nf = ~W(u + 0x4C);
        if ((nf & 0x10000000u) != 0 || (nf & 0x100u) != 0) continue;
        f32 k = 0.0f;
        bool pitch = false;
        if (GRef<u8>(u + 0x54) == 0 && Game(0x28) == 0) {
            if (F(u + 0x24) <= 0.0f) {
                sceVu0AddVector(u, u, u + 0x20);
                continue;
            }
            k = fpu::Add(fpu::Sub(func_0013E338(Terrain(), u), F(u + 4)), 16.0f) * 0.00390625f;
            if (1.0f < k) k = 1.0f;
            if (k < 0.0f) k = 0.0f;
            guest::GuestFrame f(0x10);
            const u32 v = f.Address();
            GRef<u32>(v) = W(u + 0x20);
            GRef<f32>(v + 4) = F(u + 0x24) * k;
            GRef<u32>(v + 8) = W(u + 0x28);
            GRef<u32>(v + 12) = W(u + 0x2C);
            sceVu0AddVector(u, u, v);
            if (!(F(u + 0x10) < 0.0f)) continue;
            k = fpu::Sub(1.0f, k);
            pitch = true;
        } else {
            if (GRef<u8>(u + 0x54) != 1 || (W(u + 0x4C) & 0x1000000u) == 0 || F(u + 0x24) <= 0.0f ||
                GRef<u8>(W(u + 0x44) + 0x1B) == 6) {
                sceVu0AddVector(u, u, u + 0x20);
                continue;
            }
            k = fpu::Sub(func_0013E338(Terrain(), u), F(u + 4)) * 0.00048828125f;
            if (1.0f < k) k = 1.0f;
            if (k < 0.0f) k = 0.0f;
            guest::GuestFrame f(0x10);
            const u32 v = f.Address();
            GRef<u32>(v) = W(u + 0x20);
            GRef<f32>(v + 4) = F(u + 0x24) * k;
            GRef<u32>(v + 8) = W(u + 0x28);
            GRef<u32>(v + 12) = W(u + 0x2C);
            sceVu0AddVector(u, u, v);
            if (!(F(u + 0x10) < 0.0f) || k == 1.0f) continue;
            k = fpu::Sub(1.0f, k);
            pitch = true;
        }
        if (pitch) {
            const f32 s = k * F(0x0040DD5C);
            GRef<f32>(u + 0x10) = fpu::Add(F(u + 0x10), s * -ee::sinf(F(u + 0x10)));
        }
    }
}

// EE: 0x0011F3F8
// Player p per frame (docs/port/s5-blockB-inventory.csv): hit reactions, controls, flight model,
// terrain type, area-limit radio, gear, collision, view toggle, weapons, rumble, camera.
// fpucheck: 8 adds (3 x rot.x -= C, +16384, -16384, -x + 16384, R+0x78 - R+0x8C, k - C)
void UpdatePlayerAircraft(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 R = AircraftRec(p);
    const u32 pad = PadExt(p);
    if ((W(u + 0x50) & 0xF0000u) != 0)
        GRef<u32>(P + 0x188) |= 0x2000000u;
    else
        GRef<u32>(P + 0x188) &= 0xFDFFFFFFu;
    if ((W(u + 0x50) & 0x10000u) != 0) {
        func_002576C8(p, 4, 0.0f, 0.0f);
        const s8 view = GRef<s8>(P + 0x149);
        if (view == 0)
            PlayMenuSe(10, 0xD);
        else if (view == 1)
            PlayMenuSe(10, 0xE);
        else if (view == 2)
            PlayMenuSe(10, 0xF);
        if (MissionKind() == 0 && Game(0x29) != 3 && (W(u + 0x4C) & 0x1000000u) == 0 && GRef<u8>(u + 0x63) != 0xFF)
            GRef<u32>(Status() + 0x20) |= 0x2000u;
    }
    if ((W(u + 0x50) & 0x20000u) != 0) {
        func_002576C8(p, 5, 0.0f, 0.0f);
        GRef<f32>(P + 0xC) = 0.0f;
        GRef<f32>(P + 0) = 0.0f;
        GRef<f32>(P + 4) = 0.0f;
        GRef<f32>(P + 8) = 0.0f;
        GRef<f32>(u + 0x10) = fpu::Sub(F(u + 0x10), F(0x0040DD84));
        if (MissionKind() == 2 && GRef<u8>(u + 0x63) < Players()) ShowCenterMessage(GRef<u8>(u + 0x63), 0xC, 0);
        PlayMenuSe(10, 0x77);
        if (MissionKind() == 0 && Game(0x29) != 3) {
            if ((W(u + 0x4C) & 0x1000000u) != 0)
                QueueRadioChatter(5, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            else
                GRef<u32>(Status() + 0x20) |= 0x2000u;
        }
    }
    if ((W(u + 0x50) & 0x40000u) != 0) {
        GRef<u32>(P + 0) = 0;
        GRef<u32>(P + 4) = 0;
        GRef<u32>(P + 8) = 0;
        GRef<u32>(P + 0xC) = 0;
        GRef<f32>(u + 0x10) = fpu::Sub(F(u + 0x10), F(0x0040DD88));
        func_002576C8(p, 5, 0.0f, 0.0f);
        if (MissionKind() == 2 && GRef<u8>(u + 0x63) < Players()) ShowCenterMessage(GRef<u8>(u + 0x63), 0xC, 0);
    }
    if ((W(u + 0x50) & 0x80000u) != 0) {
        GRef<u32>(P + 0) = 0;
        GRef<u32>(P + 4) = 0;
        GRef<u32>(P + 8) = 0;
        GRef<u32>(P + 0xC) = 0;
        GRef<f32>(u + 0x10) = fpu::Sub(F(u + 0x10), F(0x0040DD8C));
        func_002576C8(p, 5, 0.0f, 0.0f);
    }
    if ((W(u + 0x50) & 0x400000u) != 0) func_002576C8(p, 6, 0.0f, 0.0f);
    const u32 vel = u + 0x20;
    if ((W(u + 0x50) & 0x800000u) != 0) {
        func_002576C8(p, 6, 0.0f, 0.0f);
        const u32 f = W(u + 0x4C);
        if ((~f & 0x1000000u) != 0) {
            GRef<u32>(u + 0x4C) = f & 0xEFFFFFFFu & 0xDFFFFFFFu;
            ZeroQ(vel);
            const u32 kind = W(P + 0x2D8);
            if (kind == 1) {
                SpawnImpactEffect(u, vel, vel, 8);
                func_00170C40(0x10, 0xFF, 8, u, vel, vel);
                func_00219668(0);  // the caller also passes the unit in a1; 0x00219668 does not read it
                func_00171158(4, static_cast<u8>(p), 0, u, 0);
            } else if (kind == 2) {
                SpawnImpactEffect(u, vel, vel, 9);
                func_00170C40(0x10, 0xFF, 9, u, vel, vel);
                PlayEventSe(2, u);
                func_00171158(3, 0, 2, u, 0);
            } else if (kind == 3) {
                SpawnImpactEffect(u, vel, vel, 10);
                func_00170C40(0x10, 0xFF, 10, u, vel, vel);
                func_00219668(0);  // the caller also passes the unit in a1; 0x00219668 does not read it
                func_00171158(4, static_cast<u8>(p), 0, u, 0);
            } else {
                SpawnImpactEffect(u, vel, vel, 0xB);
                func_00170C40(0x10, 0xFF, 0xB, u, vel, vel);
                func_00219668(1);  // the caller also passes the unit in a1; 0x00219668 does not read it
                func_00171158(4, static_cast<u8>(p), 1, u, 0);
            }
        }
    }
    if ((W(u + 0x4C) & 0x200000u) != 0) {
        func_0010C868(F(u + 0x14), p);
        if ((~W(u + 0x50) & 0x800000u) != 0) {
            func_001E9518(u, vel, u + 0x30, 0x81);
            func_00170C40(1, 0xFF, 0x81, u, vel, u + 0x30);
        }
        GRef<s32>(u + 0x84) = -1;
        if (MissionKind() == 2 && GRef<u8>(u + 0x63) < Players() && (W(u + 0x50) & 0xF0000u) != 0)
            ShowCenterMessage(GRef<u8>(u + 0x63), 0xB, 0);
        func_00219668(1);  // the caller also passes the unit in a1; 0x00219668 does not read it
        func_00171158(4, static_cast<u8>(p), 1, u, 0);
    }
    GRef<u8>(P + 0x2F2) = 0xFF;
    GRef<u8>(P + 0x2F0) = 0xFF;
    GRef<u8>(P + 0x2F1) = 0xFF;
    ReadPlayerControls(p);
    func_001205F8(p);
    UpdateAircraftMovingParts(p, 0);
    UpdateGearLights(p);
    if ((W(u + 0x4C) & 0x1000000u) == 0) {
        func_0012A2D0(p);
    } else if ((W(P + 0x188) & 0x20000000u) == 0) {
        UpdateFlightDynamics(p);
    } else if ((W(Status() + 0x20) & 0x2000000u) != 0 && static_cast<u32>(Sub() - 0x1A) < 2) {
        func_0012A0E0(p);
    } else {
        func_00129B58(p);
    }
    {
        guest::GuestFrame f(0x20);
        const u32 next = f.Address();
        sceVu0AddVector(next, u, vel);
        GRef<u8>(P + 0x2F3) = static_cast<u8>(func_0013E978(Terrain(), next));
        if (MissionKind() == 0 && Game(0x29) != 3) {
            const u32 n2 = f.Address(0x10);
            sceVu0AddVector(n2, vel, u);
            const f32 nz = F(n2 + 8), nx = F(n2);
            const f32 lim = fpu::Add(F(Rules() + 0x1C), 16384.0f);
            if (nz < lim && lim < F(u + 8)) QueueRadioChatter(0x16, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
            const f32 edge = F(Rules() + 0x20);
            const f32 lo = fpu::Sub(edge, 16384.0f);
            const f32 hi = fpu::Add(-edge, 16384.0f);
            if ((lo < nz && F(u + 8) < lo) || (lo < nx && F(u) < lo) || (nx < hi && hi < F(u)))
                QueueRadioChatter(0x17, 0, GRef<u8>(Radio() + 0x83), 0x31, 0, 0);
        }
    }
    UpdateLandingGear(p);
    UpdatePlayerCollision(p);
    if ((~W(Status() + 0x20) & 0x10000000u) != 0) {
        if (Game(0x27) == 0xF) {
            func_0011F2F8(p);
            if (GRef<u8>(P + 0x183) != 0) GRef<u8>(P + 0x2F3) = 0x80;
        }
        if (Game(0x27) == 0xB) {
            const u32 r = Rules();
            const f32 y = F(u + 4);
            if (F(r + 0x2C) < y && y < F(r + 0x30) && !(GRef<u8>(UnitList() + 0x83) < GRef<u8>(r + 0x2B)))
                GRef<u32>(P + 0x188) |= 0x8000000u;
            else
                GRef<u32>(P + 0x188) &= 0xF7FFFFFFu;
        }
        if ((W(pad + 4) & 4u) != 0) {
            const u32 old = W(P + 0x14C);
            GRef<u32>(P + 0x14C) = old ^ 0x8000u;
            if (GRef<u8>(P + 0x15B) == 0) GRef<u32>(P + 0x14C) = (old ^ 0x8000u) & 0xFFFF7FFFu;
            if ((W(P + 0x14C) & 0x8000u) != 0)
                PlayMenuSe(10, 4);
            else
                PlayMenuSe(10, 3);
            func_0011ED28(P);
            if ((old & 0x8000u) != (W(P + 0x14C) & 0x8000u)) {
                for (u32 i = 0; i < 4; ++i) {
                    GRef<u8>(P + 0x154 + i) = 0xFF;
                    GRef<u32>(P + 0x194 + 4 * i) = 0;
                    GRef<u32>(P + 0x1A4 + 4 * i) = 0;
                }
                GRef<u32>(P + 0x188) &= 0xFFFF00FFu;
            }
        }
        if (Sub() != 0xC) UpdatePlayerTargeting(p);
        FirePlayerWeapons(p);
    }
    if ((W(P + 0x188) & 0x40000000u) == 0 && F(P + 0x24C) != 0.0f) func_002576C8(p, 0, F(P + 0x24C), 0.0f);
    const u8 model = GRef<u8>(u + 0x69);
    if (model != 0xC && model != 5 && F(P + 0x244) == 1.0f) func_002576C8(p, 1, 0.0f, 0.0f);
    if (F(P + 0x2DC) < 1.0f)
        func_002576C8(p, 9, 0.0f, 0.0f);
    else if (F(P + 0x2E0) < 1.0f)
        func_002576C8(p, 8, 0.0f, F(P + 0x2E0));
    f32 g = -F(P) / fpu::Sub(F(R + 0x78), F(R + 0x8C)) * 0.5f;
    if (g < 0.0f) g = 0.0f;
    if (1.0f < g) g = 1.0f;
    if (F(0x0040DD90) < g) func_002576C8(p, 0xA, 0.0f, fpu::Sub(g, F(0x0040DD90)) * 10.0f);
    if (Sub() == 0x1D)
        ZeroQ(P + 0x10);
    else
        func_00125B70(p);
    func_00126758(p);
}

// EE: 0x0011FF30
// Pad state -> flight inputs of player p (inventory note).
// fpucheck: 4 adds (throttle x2, yaw x2, tutorial y - h, y - 5)
void ReadPlayerControls(s32 p) {
    if (static_cast<u32>(p) < 2) port::Input().in_flight[p] = true;  // port: the mouse steers only in flight
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 pe = PadExt(p);
    if (MissionKind() == 3) {
        GRef<u16>(pe + 0xE) &= 0xFFFEu;
    } else {
        const u16 opt = GRef<u16>(0x002A15D0 + 2u * static_cast<u32>(p));
        if ((opt & 1) != 0)
            GRef<u16>(pe + 0xE) |= 1u;
        else
            GRef<u16>(pe + 0xE) &= 0xFFFEu;
        if ((opt & 2) != 0) {
            const u8 a = GRef<u8>(pe + 0x10);
            GRef<u8>(pe + 0x10) = GRef<u8>(pe + 0x11);
            GRef<u8>(pe + 0x11) = a;
        }
        if ((opt & 4) != 0) {
            const u8 a = GRef<u8>(pe + 0x18);
            GRef<u8>(pe + 0x18) = GRef<u8>(pe + 0x19);
            GRef<u8>(pe + 0x19) = a;
        }
        if ((GRef<u16>(pe + 0xE) & 0x100) != 0) {
            const u32 ps = PadState(p);
            if ((W(ps + 4) & 0x8000u) != 0) GRef<u32>(pe) |= 4u;
            if ((W(ps + 8) & 0x8000u) != 0) GRef<u32>(pe + 4) |= 4u;
            if ((W(ps + 0xC) & 0x8000u) != 0) GRef<u32>(pe + 8) |= 4u;
        }
    }
    if (MissionKind() == 3) {
        GRef<u32>(pe + 4) &= 0xFFFFFEEFu;
        GRef<u32>(pe) &= 0xFFFFFEEFu;
        GRef<u32>(pe + 8) &= 0xFFFFFEEFu;
    }
    const u16 sub = Sub();
    u32 mask = 0;
    bool keep_analog = true;
    if (sub == 0xA) {
        GRef<u32>(pe + 4) &= 0x230u;
        GRef<u32>(pe) &= 0x230u;
        GRef<u32>(pe + 8) &= 0x230u;
        for (s32 i = 0; i < 0x10; ++i)
            if (static_cast<u32>(i - 8) > 3 && i != 0xD) GRef<u8>(pe + 0x10 + static_cast<u32>(i)) = 0;
    } else if (sub == 0x14 || sub == 0x19 || sub == 0x1D) {
        GRef<u8>(P + 0x185) = 1;
    } else if (sub == 0x1B) {
        GRef<u8>(P + 0x185) = 4;
    } else if ((W(pe) & 0x100u) != 0) {
        GRef<u8>(P + 0x185) = 0x12;
    } else if (GRef<s8>(Status() + 0x7F) != 0) {
        GRef<u8>(P + 0x185) = 0x41;
    } else if (GRef<s8>(Status() + 0x7E) == 0) {
        GRef<u8>(P + 0x185) = 0;
    } else {
        GRef<u8>(P + 0x185) = 0x20;
        switch (static_cast<s8>(GRef<u8>(Status() + 0x7E) - 1)) {
            case 0: mask = 0x40; keep_analog = false; break;
            case 1: mask = 0x42; keep_analog = false; break;
            case 2: mask = 6; keep_analog = false; break;
            case 3: mask = 1; keep_analog = false; break;
            case 4: mask = 4; keep_analog = false; break;
            case 5: mask = 0xFFFFFFFEu; break;
            case 6: mask = 0xFFFFFFFAu; break;
            case 7: mask = 0xFFFFFFF9u; break;
            default: break;
        }
    }
    u8 m = GRef<u8>(P + 0x185);
    if ((m & 1) != 0) {
        GRef<u32>(pe + 4) = 0;
        GRef<u32>(pe) = 0;
        GRef<u32>(pe + 8) = 0;
        for (s32 i = 0xF; i >= 0; --i) GRef<u8>(pe + 0x10 + static_cast<u32>(i)) = 0;
        m = GRef<u8>(P + 0x185);
    }
    if ((m & 2) != 0) {
        for (u32 i = 0x15; i >= 0x10; --i) GRef<u8>(pe + i) = 0;
        m = GRef<u8>(P + 0x185);
    }
    if ((m & 4) != 0) {
        GRef<u32>(pe + 4) = 0;
        GRef<u32>(pe) = 0;
        GRef<u32>(pe + 8) = 0;
        m = GRef<u8>(P + 0x185);
    }
    if ((m & 0x20) != 0) {
        GRef<u32>(pe + 4) &= mask;
        GRef<u32>(pe) &= mask;
        GRef<u32>(pe + 8) &= mask;
        if (!keep_analog)
            for (s32 i = 0xF; i >= 0; --i) GRef<u8>(pe + 0x10 + static_cast<u32>(i)) = 0;
        m = GRef<u8>(P + 0x185);
    }
    if ((m & 0x40) != 0) {
        bool reload = true;
        switch (static_cast<s8>(GRef<u8>(Status() + 0x7F) - 1)) {
            case 0:
                m = static_cast<u8>(m | 0x10);
                GRef<u8>(P + 0x185) = m;
                reload = false;
                break;
            case 1: GRef<u8>(pe + 0x10) = 0xFF; break;
            case 2: GRef<u8>(pe + 0x11) = 0xFF; break;
            case 3: GRef<u8>(pe + 0x13) = 0xFF; break;
            case 4: GRef<u8>(pe + 0x12) = 0xFF; break;
            case 6: GRef<u8>(pe + 0x1D) = 0x10; break;
            case 7: GRef<u8>(pe + 0x1D) = 100; break;
            case 8: GRef<u8>(pe + 0x1D) = 0xFF; break;
            default: reload = false; break;
        }
        if (reload) m = GRef<u8>(P + 0x185);
    }
    if ((m & 0x10) != 0 || GRef<s8>(Status() + 0x7F) != 0) ShowCenterMessage(p, 0x14, 0);
    if (MissionKind() == 3) {
        if ((GRef<u8>(P + 0x185) & 0x10) != 0) {
            const f32 h = func_0013E2B0(Terrain(), u);
            const f32 y = F(u + 4);
            if (-500.0f < fpu::Sub(y, h)) GRef<f32>(u + 4) = fpu::Add(y, -5.0f);
        }
        const u32 st = Status();
        if ((W(st + 0x20) & 0x1000u) != 0) {
            if ((~W(P + 0x14C) & 0x8000u) != 0) GRef<u32>(pe + 4) |= 4u;
            GRef<u32>(st + 0x20) &= 0xFFFFEFFFu;
        }
    }
    const s32 type = GRef<s32>(PadState(p));
    f32 t = fpu::IntToFloat(static_cast<s32>(GRef<u8>(pe + 0x16)) - static_cast<s32>(GRef<u8>(pe + 0x17))) / 255.0f;
    GRef<f32>(P + 0x114) = t;
    if (type == 5) {
        t = fpu::Add(t, t);
        GRef<f32>(P + 0x114) = t;
        if (1.0f < t)
            GRef<f32>(P + 0x114) = 1.0f;
        else if (t < -1.0f)
            GRef<f32>(P + 0x114) = -1.0f;
    }
    GRef<f32>(P + 0x11C) =
        fpu::IntToFloat(static_cast<s32>(GRef<u8>(pe + 0x10)) - static_cast<s32>(GRef<u8>(pe + 0x11))) / 255.0f;
    GRef<f32>(P + 0x118) =
        fpu::IntToFloat(static_cast<s32>(GRef<u8>(pe + 0x13)) - static_cast<s32>(GRef<u8>(pe + 0x12))) / 255.0f;
    f32 yaw = fpu::IntToFloat(static_cast<s32>(GRef<u8>(pe + 0x15)) - static_cast<s32>(GRef<u8>(pe + 0x14))) / 255.0f;
    GRef<f32>(P + 0x120) = yaw;
    if (type == 5) {
        yaw = fpu::Add(yaw, yaw);
        GRef<f32>(P + 0x120) = yaw;
        if (1.0f < yaw)
            GRef<f32>(P + 0x120) = 1.0f;
        else if (yaw < -1.0f)
            GRef<f32>(P + 0x120) = -1.0f;
    }
}

// EE: 0x001205F8
// Cockpit stick and throttle lever positions.
// fpucheck: 8 adds
void func_001205F8(s32 p) {
    const u32 P = W(Unit(p) + 0x44);
    const u16 mode = GRef<u16>(PadExt(p) + 0xE);
    const f32 roll = fpu::Add(F(P + 0x118), 1.0f) * 0.5f;
    GRef<f32>(P + 0x250) = fpu::Add(F(P + 0x11C), 1.0f) * 0.5f;
    GRef<f32>(P + 0x258) = roll;
    if ((mode & 1) == 0)
        GRef<f32>(P + 0x254) = fpu::Add(F(P + 0x120), 1.0f) * 0.5f;
    else
        GRef<f32>(P + 0x254) = fpu::Add(fpu::Sub(roll, 0.5f) * F(0x0040DD94), 0.5f);
    if ((W(P + 0x188) & 0x100000u) == 0)
        GRef<f32>(P + 0x248) =
            fpu::Add(fpu::Sub(F(P + 0x240) * 0.5f, F(P + 0x248)) * F(0x0040DD98), F(P + 0x248));
    else
        GRef<f32>(P + 0x248) = fpu::Sub(0.5f, F(P + 0x244) * 0.5f);
    GRef<u32>(P + 0x25C) = W(P + 0x130);
}

// EE: 0x00120D38
// Landing-gear state machine and the gear model channels.
// fpucheck: 5 adds (target - cur, cur + step, target - cur, cur - step, view term)
void UpdateGearLights(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    f32 target[3];
    const f32 gear = F(0x002A24C0 + 4u * GRef<u8>(u + 0x69));
    switch (GRef<u8>(P + 0x280)) {
        case 0:
            target[0] = target[1] = target[2] = gear;
            GRef<f32>(P + 0x260) = gear;
            GRef<f32>(P + 0x268) = gear;
            GRef<f32>(P + 0x264) = gear;
            break;
        case 1:
            target[0] = target[1] = target[2] = gear;
            if (GRef<u8>(P + 0x14B) < 0xB4)
                GRef<u8>(P + 0x14B) = static_cast<u8>(GRef<u8>(P + 0x14B) + 1);
            else
                GRef<u8>(P + 0x280) = 2;
            break;
        case 2:
            target[0] = target[1] = target[2] = 1.0f;
            if (F(P + 0x260) == 1.0f && F(P + 0x264) == 1.0f && F(P + 0x268) == 1.0f) GRef<u8>(P + 0x280) = 3;
            break;
        case 3:
            for (u32 i = 0; i < 3; ++i) target[i] = F(P + 0x260 + 4 * i);
            break;
        case 4:
            target[0] = target[1] = target[2] = gear;
            if (gear == F(P + 0x260) && gear == F(P + 0x264) && gear == F(P + 0x268)) GRef<u8>(P + 0x280) = 5;
            break;
        case 5:
            target[0] = target[1] = target[2] = gear;
            if (GRef<u8>(P + 0x14B) == 0)
                GRef<u8>(P + 0x280) = 0;
            else
                GRef<u8>(P + 0x14B) = static_cast<u8>(GRef<u8>(P + 0x14B) - 1);
            break;
        default:
            // Unreachable in the shipped game (states 0-5 only); the original leaves the stack
            // values undefined. Keep the current values.
            for (u32 i = 0; i < 3; ++i) target[i] = F(P + 0x260 + 4 * i);
            break;
    }
    const f32 up = F(0x0040DDAC), down = F(0x0040DDA8), lim = F(0x0040DDA4);
    for (u32 i = 0; i < 3; ++i) {
        const u32 a = P + 0x260 + 4 * i;
        const f32 t = target[i], c = F(a);
        if (c < t) {
            if (up <= fpu::Sub(t, c))
                GRef<f32>(a) = fpu::Add(c, up);
            else
                GRef<f32>(a) = t;
        } else if (t < c) {
            if (fpu::Sub(t, c) <= lim)
                GRef<f32>(a) = fpu::Sub(c, down);
            else
                GRef<f32>(a) = t;
        }
    }
    const u32 m = AircraftModels();
    if (GRef<u8>(P + 0x280) == 0) {
        func_00149D60(m, p, 0x28, 0);
        func_00149D60(m, p, 0x29, 1);
        func_00149D60(m, p, 0x2B, 1);
        func_00149D60(m, p, 0x2C, 1);
        func_00149D60(m, p, 0x2D, 1);
        func_00149C40(0.0f, m, p, 0x2A);
    } else {
        f32 k = fpu::Add(fpu::IntToFloat(GRef<u8>(P + 0x14B)) * 0.5f / 180.0f, 0.5f);
        func_00149D60(m, p, 0x28, 1);
        func_00149D60(m, p, 0x29, 0);
        func_00149D60(m, p, 0x2B, 0);
        func_00149D60(m, p, 0x2C, 0);
        func_00149D60(m, p, 0x2D, 0);
        func_00149C40(k, m, p, 0x29);
        const u16 s = Sub();
        if (static_cast<u32>(s - 0x14) > 1 && s != 0x17 && s != 0x18 && s != 0x16 && s != 0x10 && s != 0x11 && s != 0x12)
            k = 0.0f;
        func_00149C40(k, m, p, 0x2A);
    }
    for (s32 i = 0; i < 3; ++i) func_00149C40(F(P + 0x260 + 4u * static_cast<u32>(i)), AircraftModels(), p, i + 0x2B);
}

// EE: 0x00121318
// Gear reset to the model's gear blend.
void func_00121318(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 g = W(0x002A24C0 + 4u * GRef<u8>(u + 0x69));
    GRef<u32>(P + 0x274) = 0;
    GRef<u32>(P + 0x268) = g;
    GRef<u8>(P + 0x280) = 0;
    GRef<u8>(P + 0x14B) = 0;
    GRef<u32>(P + 0x260) = g;
    GRef<u32>(P + 0x264) = g;
    GRef<u32>(P + 0x26C) = 0;
    GRef<u32>(P + 0x270) = 0;
}

// EE: 0x00121370
// Wheels on the ground (inventory note). Reads the random table once per wheel with bit 4+i.
// fpucheck: 30 static adds; every executed one is an fpu:: call below.
void UpdateLandingGear(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    if (GRef<u8>(P + 0x280) != 3 || (W(u + 0x4C) & 0x1000000u) == 0) {
        GRef<u8>(P + 0x281) = 0;
        GRef<u32>(P + 0x278) = 0;
        GRef<u32>(P + 0x188) &= 0x9FFFFFFFu;
    } else {
        f32 k = fpu::Sub(F(P + 0x130), F(0x0040DDB0)) / F(0x0040DDB4);
        if (1.0f < k)
            k = 1.0f;
        else if (k < 0.0f)
            k = 0.0f;
        guest::GuestFrame f(0x230);
        // Stack layout of the original (sp offsets): wheel points 0x00, node matrix 0x60 (local
        // copies at 0x60.. are the transformed points), world points 0x30, segment ends 0x150/0x160,
        // hit 0x140, attr 0x1B0, normal 0x130, heights 0x120, body matrix 0xE0, carrier matrix 0x170.
        const u32 sp = f.Address();
        const u32 wheel = sp + 0x00, nodem = sp + 0xA0, local = sp + 0x30, world = sp + 0x60;
        const u32 from = sp + 0x150, to = sp + 0x160, hit = sp + 0x140, attr = sp + 0x1B0, normal = sp + 0x130;
        const u32 heights = sp + 0x120, body = sp + 0xE0, cm = sp + 0x170;
        const u32 vel = u + 0x20;
        auto wheel_world = [&](s32 i) {
            StoreVf0(wheel + 0x10u * static_cast<u32>(i));
            func_00149EC8(nodem, p, i + 0x1B, P);
            sceVu0ApplyMatrix(wheel + 0x10u * static_cast<u32>(i), nodem, wheel + 0x10u * static_cast<u32>(i));
            sceVu0ApplyMatrix(local + 0x10u * static_cast<u32>(i), body, wheel + 0x10u * static_cast<u32>(i));
            sceVu0AddVector(world + 0x10u * static_cast<u32>(i), u, local + 0x10u * static_cast<u32>(i));
            sceVu0AddVector(world + 0x10u * static_cast<u32>(i), vel, world + 0x10u * static_cast<u32>(i));
        };
        sceVu0UnitMatrix(body);
        func_00102D28(body, body, u + 0x10);
        for (s32 i = 0; i < 3; ++i) func_00149C40(1.0f, AircraftModels(), p, i + 0x2B);
        func_00148308(AircraftModels(), p);
        for (s32 i = 0; i < 3; ++i) {
            wheel_world(i);
            const u32 w = world + 0x10u * static_cast<u32>(i);
            CopyQ(from, w);
            GRef<f32>(from + 4) = fpu::Sub(F(from + 4), 64.0f);
            CopyQ(to, w);
            GRef<f32>(to + 4) = fpu::Add(F(to + 4), 64.0f);
            const u32 hgt = heights + 4u * static_cast<u32>(i);
            GRef<f32>(hgt) = 100.0f;
            const u32 sf = StatusFlags();
            if ((sf & 0x1000000u) != 0) {
                if (func_0013D698(Terrain(), hit, attr, from, to, normal) == 1) GRef<u32>(hgt) = W(hit + 4);
            } else if ((sf & 0x2000000u) != 0) {
                const u32 c = Unit(GRef<u8>(UnitList() + 0x88));
                sceVu0UnitMatrix(cm);
                func_00102D28(cm, cm, c + 0x10);
                sceVu0TransMatrix(cm, cm, c);
                if (func_00146808(UnitModels(), hit, from, to, cm, normal, GRef<u8>(c + 0x55)) == 1) {
                    const f32 y = F(hit + 4);
                    GRef<f32>(hgt) = y;
                    if (-96.0f < y || y < -94.0f) GRef<f32>(hgt) = -95.5f;
                    GRef<u8>(P + 0x2F3) = 0x81;
                }
            }
        }
        if ((GRef<u8>(P + 0x281) & 7) != 7) {
            for (s32 i = 0; i < 3; ++i) func_00149C40(0.0f, AircraftModels(), p, i + 0x2B);
            func_00148308(AircraftModels(), p);
            for (u32 i = 0; i < 3; ++i) {
                if (((static_cast<s32>(~static_cast<u32>(GRef<u8>(P + 0x281))) >> i) & 1) == 0) continue;
                const u32 w = world + 0x10 * i;
                const u32 hgt = heights + 4 * i;
                if (F(w + 4) <= F(hgt)) {
                    const u32 a = P + 0x260 + 4 * i;
                    f32 c = F(a);
                    if (c < 1.0f) {
                        c = fpu::Add(c, F(0x0040DDB8));
                        GRef<f32>(a) = c;
                    }
                    if (1.0f < c) GRef<f32>(a) = 1.0f;
                    continue;
                }
                CopyQ(normal, w);  // previous world point of the wheel
                StoreVf0(wheel + 0x10 * i);
                func_00149EC8(nodem, p, static_cast<s32>(i) + 0x1B, P);
                sceVu0ApplyMatrix(wheel + 0x10 * i, nodem, wheel + 0x10 * i);
                sceVu0ApplyMatrix(local + 0x10 * i, body, wheel + 0x10 * i);
                sceVu0AddVector(w, u, local + 0x10 * i);
                sceVu0AddVector(w, vel, w);
                const f32 wy = F(w + 4);
                if (wy <= F(hgt))
                    GRef<f32>(P + 0x260 + 4 * i) = fpu::Sub(F(hgt), wy) / fpu::Sub(F(normal + 4), wy);
                else
                    GRef<u32>(P + 0x260 + 4 * i) = 0;
                GRef<u32>(P + 0x26C + 4 * i) = 0;
                GRef<u8>(P + 0x281) = static_cast<u8>(GRef<u8>(P + 0x281) | (1u << i));
                func_002576C8(p, 2, 0.0f, 0.0f);
                const u32 rec = 0x00420540 + 0x30 * i;
                if (i == 0)
                    GRef<u32>(0x00420540) = 0;
                else
                    GRef<u32>(rec) = 1;
                CopyQ(0x00420560 + 0x30 * i, w);
                CopyQ(0x00420550 + 0x30 * i, vel);
                func_00216CE0(rec);
                if (Sub() == 0x15) func_00171158(2, static_cast<u8>(i), GRef<u8>(rec), w, vel);
                if (GRef<u8>(P + 0x283 + i) == 0) {
                    func_001CD360(w, vel);
                    func_00170B60(0xC, 0xFF, 0, w, vel);
                    GRef<u8>(P + 0x283 + i) = 0x78;
                }
            }
        }
        for (s32 i = 0; i < 3; ++i) func_00149C40(F(P + 0x260 + 4u * static_cast<u32>(i)), AircraftModels(), p, i + 0x2B);
        func_00148308(AircraftModels(), p);
        const f32 clear = F(0x0040DDBC);
        for (u32 i = 0; i < 3; ++i) {
            wheel_world(static_cast<s32>(i));
            if (fpu::Sub(F(world + 0x10 * i + 4), F(heights + 4 * i)) < clear)
                GRef<u8>(P + 0x281) = static_cast<u8>(GRef<u8>(P + 0x281) & ~(1u << i));
        }
        u8 b = GRef<u8>(P + 0x281);
        if ((b & 7) != 0) {
            const u32 corr = sp + 0x90;
            ZeroQ(corr);
            const u32 on = GRef<u8>(P + 0x281) & 7;
            if (on == 7) {
                func_00103818(corr, world, world + 0x10, world + 0x20);
            } else if (on == 3) {
                GRef<f32>(corr) = func_00103948(F(u + 0x14), world, world + 0x10);
                GRef<f32>(corr + 8) = F(0x0040DDC0);
            } else if (on == 5) {
                GRef<f32>(corr) = func_00103948(F(u + 0x14), world, world + 0x20);
                GRef<f32>(corr + 8) = F(0x0040DDC4);
            } else if (on == 6) {
                GRef<f32>(corr) = fpu::Sub(1.0f, k) * F(0x0040DDC8);
                GRef<f32>(corr + 8) = func_001039D8(F(u + 0x14), world + 0x10, world + 0x20);
            } else if (on == 1) {
                GRef<f32>(corr) = F(0x0040DDCC);
            } else if (on == 2) {
                GRef<f32>(corr + 8) = F(0x0040DDD0);
            } else if (on == 4) {
                GRef<f32>(corr + 8) = F(0x0040DDD4);
            }
            GRef<f32>(u + 0x10) = fpu::Sub(F(u + 0x10), F(corr));
            GRef<f32>(u + 0x18) = fpu::Sub(F(u + 0x18), F(corr + 8));
            sceVu0UnitMatrix(body);
            func_00102D28(body, body, u + 0x10);
            for (u32 i = 0; i < 3; ++i) {
                wheel_world(static_cast<s32>(i));
                GRef<u8>(P + 0x281) = static_cast<u8>(GRef<u8>(P + 0x281) & ~(0x10u << i));
                if ((StatusFlags() & 0x1000000u) != 0 && ((on >> i) & 1) != 0) {
                    const u32 w = world + 0x10 * i;
                    const f32 x = F(w), z = F(w + 8);
                    if (!(14336.0f <= x && x <= 14592.0f && 9984.0f <= z && z <= 24832.0f))
                        GRef<u8>(P + 0x281) = static_cast<u8>(GRef<u8>(P + 0x281) | (0x10u << i));
                }
            }
            b = GRef<u8>(P + 0x281);
            if ((b & 7) != 0) {
                // Lowest wheel against its ground height (sp+0x64/0x74/0x84 = world y, sp+0x120.. =
                // heights): push the aircraft up by the penetration.
                const f32 y0 = F(world + 4), y1 = F(world + 0x14), y2 = F(world + 0x24);
                s32 idx;
                f32 wy, gy;
                if (y1 < y0 && y2 < y0) {
                    sceVu0SubVector(u, world, local);
                    idx = 0;
                    wy = y0;
                    gy = F(heights);
                } else if (!(y1 < y0) && y2 < y1) {
                    sceVu0SubVector(u, world + 0x10, local + 0x10);
                    idx = 1;
                    wy = y1;
                    gy = F(heights + 4);
                } else {
                    sceVu0SubVector(u, world + 0x20, local + 0x20);
                    idx = 2;
                    wy = y2;
                    gy = F(heights + 8);
                }
                const f32 d = fpu::Sub(wy, gy);
                if (0.0f < d)
                    GRef<f32>(u + 4) = fpu::Sub(F(u + 4), d);
                else if (((GRef<u8>(P + 0x281) >> idx) & 1) != 0)
                    GRef<f32>(u + 4) = fpu::Sub(F(u + 4), d);
                sceVu0SubVector(u, u, vel);
                b = GRef<u8>(P + 0x281);
            }
        }
        if ((b & 7) == 7)
            GRef<u32>(P + 0x188) |= 0x20000000u;
        else
            GRef<u32>(P + 0x188) &= 0xDFFFFFFFu;
        if ((b & 7) == 0) {
            if ((W(P + 0x188) & 0x40000000u) != 0) {
                func_00216CA0();
                if (Sub() == 0x15) func_00171158(1, 0, 0, 0, 0);
                b = GRef<u8>(P + 0x281);
            }
            GRef<u32>(P + 0x188) &= 0xBFFFFFFFu;
        } else {
            GRef<u32>(P + 0x188) |= 0x40000000u;
        }
        if ((b & 0x70) == 0x70) {
            const f32 t = fpu::Add(F(P + 0x278), F(0x0040DDD8));
            GRef<f32>(P + 0x278) = t;
            if (3.0f < t) GRef<f32>(u + 0x10) = F(0x0040DDDC);
            GRef<u16>(u + 0x5E) = static_cast<u16>(GRef<u16>(u + 0x5E) - 1);
        } else {
            GRef<u32>(P + 0x278) = 0;
        }
        const f32 sp_k = fpu::Add(F(P + 0x11C) * k, 1.0f) * 0.5f;
        const f32 sr_k = fpu::Add(F(P + 0x118) * k, 1.0f) * 0.5f;
        for (u32 i = 0; i < 3; ++i) {
            const u8 bb = GRef<u8>(P + 0x281);
            f32 tgt = 0.0f;
            if (((bb >> i) & 1) == 0) {
                tgt = 1.0f;
            } else if ((bb & 7) == 7) {
                if (i == 0) tgt = sp_k;
                if (i == 1) tgt = fpu::Add(sr_k, fpu::Sub(1.0f, sp_k)) * 0.5f;
                if (i == 2) tgt = fpu::Add(fpu::Sub(1.0f, sr_k), fpu::Sub(1.0f, sp_k)) * 0.5f;
            }
            const u32 a = P + 0x260 + 4 * i;
            if ((bb & (0x10u << i)) != 0) {
                const f32 r = NextRandomFloat() * 0.5f;
                const f32 v = fpu::Add(F(a), r);
                GRef<f32>(a) = v;
                if (1.0f < v) GRef<f32>(a) = 1.0f;
            }
            const f32 dv = fpu::Sub(tgt, F(a)) * F(0x0040DDE0);
            const u32 va = P + 0x26C + 4 * i;
            const f32 nv = fpu::Add(F(va), dv);
            GRef<f32>(va) = nv;
            GRef<f32>(a) = fpu::Add(F(a), nv);
            if ((GRef<u8>(P + 0x281) & 7) == 7) {
                const f32 q = dv * 0.25f;
                const u32 n1 = P + 0x26C + 4 * ((i + 1) % 3), n2 = P + 0x26C + 4 * ((i + 2) % 3);
                GRef<f32>(n1) = fpu::Add(F(n1), q);
                GRef<f32>(n2) = fpu::Add(F(n2), q);
            }
            f32 c = F(a);
            if (1.0f < c) {
                GRef<f32>(a) = 1.0f;
                c = 1.0f;
            }
            if (c < 0.0f) {
                GRef<f32>(a) = 0.0f;
                c = 0.0f;
            }
            c = fpu::Add(fpu::Sub(c, tgt) * F(0x0040DDE4), tgt);
            GRef<f32>(a) = c;
            if (1.0f < c) GRef<f32>(a) = 1.0f;
            if (F(a) < 0.0f) GRef<f32>(a) = 0.0f;
        }
    }
    for (u32 i = 0; i < 3; ++i) {
        const u8 c = GRef<u8>(P + 0x283 + i);
        if (c != 0) GRef<u8>(P + 0x283 + i) = static_cast<u8>(c - 1);
    }
}

// EE: 0x00126758
// Effect/trail flags +0x56 of player p.
// fpucheck: 1 add (R+0x78 - R+0x8C)
void func_00126758(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 R = AircraftRec(p);
    GRef<u16>(u + 0x56) = 0;
    const u32 flag = W(AircraftModels() + 0x18) + 4u * static_cast<u32>(p);
    if ((StatusFlags() & 0x10000000u) == 0)
        GRef<u32>(flag) |= 2u;
    else
        GRef<u32>(flag) &= 0xFFFFFFFDu;
    const f32 sp = F(P + 8) / F(R + 0xA4);
    const f32 th = F(P) / fpu::Sub(F(R + 0x78), F(R + 0x8C));
    if (F(0x0040DE84) < sp || sp < F(0x0040DE88) || th < F(0x0040DE8C)) GRef<u16>(u + 0x56) |= 1u;
    if (th < F(0x0040DE90)) GRef<u16>(u + 0x56) |= 4u;
    if (GRef<u8>(P + 0x2F3) == 2)
        GRef<u16>(u + 0x56) |= 0x48u;
    else
        GRef<u16>(u + 0x56) |= 8u;
    const u16 f = GRef<u16>(u + 0x56);
    GRef<u16>(u + 0x56) = static_cast<u16>(f | 0x30u);
    s8 st = GRef<s8>(P + 0x173);
    if (st == 6) {
        if (GRef<u8>(P + 0x289) == 0) {
            GRef<u8>(P + 0x289) = 0;
            GRef<s32>(P + 0x2CC) = -1;
        } else {
            GRef<u16>(u + 0x56) = static_cast<u16>(f | 0xB0u);
            st = GRef<s8>(P + 0x173);
            GRef<u8>(P + 0x289) = static_cast<u8>(GRef<u8>(P + 0x289) - 1);
        }
    }
    if (st == 8) {
        for (u32 i = 0; i < 2; ++i) {
            if (GRef<u8>(P + 0x289 + i) == 0) {
                GRef<s32>(P + 0x2CC + 4 * i) = -1;
                GRef<u8>(P + 0x289 + i) = 0;
            } else {
                GRef<u16>(u + 0x56) = static_cast<u16>(GRef<u16>(u + 0x56) | (0x200u << i));
                GRef<u8>(P + 0x289 + i) = static_cast<u8>(GRef<u8>(P + 0x289 + i) - 1);
            }
        }
    }
    if ((~W(u + 0x4C) & 0x1000000u) == 0)
        GRef<u16>(u + 0x56) &= 0xFFFDu;
    else
        GRef<u16>(u + 0x56) |= 2u;
}

// EE: 0x00126970
// Player effect emitters from the +0x56 bits (inventory note).
// fpucheck: 1 add (y - terrain height)
void UpdatePlayerTrails(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 rot = u + 0x10;
    guest::GuestFrame f(0x1B0);
    const u32 sp = f.Address();
    const u32 body = sp + 0x0;     // sp+0x1B0 -> frame base
    const u32 node = sp + 0x60;    // node matrix (sp+0x150 in the original layout)
    const u32 a = sp + 0x40, b = sp + 0x50, one = sp + 0xA0, sum_p = sp + 0xB0, sum_d = sp + 0xC0;
    sceVu0UnitMatrix(body);
    func_00102D28(body, body, rot);
    u16 fl = GRef<u16>(u + 0x56);
    bool to_engines = false;
    const s8 view = GRef<s8>(P + 0x149);
    if (!(Sub() == 0x11 && view != 0) && Sub() != 0x13 && view == 0) {
        GRef<s32>(u + 0x7C) = -1;
        GRef<s32>(u + 0x80) = -1;
        for (s32 i = 7; i >= 0; --i) GRef<s32>(P + 0x294 + 4u * static_cast<u32>(i)) = -1;
        to_engines = true;
    }
    if (!to_engines) {
        if ((fl & 1) == 0) {
            GRef<s32>(u + 0x7C) = -1;
            GRef<s32>(u + 0x80) = -1;
        } else {
            StoreVf0(one);
            func_00149EC8(node, p, 0x44, P);
            sceVu0ApplyMatrix(b, node, one);
            GRef<u32>(b + 0xC) = 0;
            StoreVf0(one);
            func_00149EC8(node, p, 0x45, P);
            sceVu0ApplyMatrix(a, node, one);
            GRef<u32>(a + 0xC) = 0;
            sceVu0ApplyMatrix(a, body, a);
            sceVu0AddVector(a, a, u);
            sceVu0ApplyMatrix(b, body, b);
            sceVu0AddVector(b, b, u);
            GRef<u32>(u + 0x80) = func_00185818(W(u + 0x80), a, u + 0x20);
            GRef<u32>(u + 0x7C) = func_00185818(W(u + 0x7C), b, u + 0x20);
            fl = GRef<u16>(u + 0x56);
        }
        if ((fl & 4) == 0) {
            for (s32 i = 7; i >= 0; --i) GRef<s32>(P + 0x294 + 4u * static_cast<u32>(i)) = -1;
        } else {
            s32 id = 0x46;
            for (u32 i = 0; i < 8; ++i, id += 2) {
                StoreVf0(one);
                sceVu0UnitMatrix(node);
                func_00149EC8(node, p, id, P);
                bool identity = true;
                for (u32 r = 0; r < 4 && identity; ++r)
                    for (u32 c = 0; c < 4; ++c)
                        if (F(node + 0x10 * r + 4 * c) != (r == c ? 1.0f : 0.0f)) {
                            identity = false;
                            break;
                        }
                if (identity) break;
                sceVu0ApplyMatrix(a, node, one);
                GRef<u32>(a + 0xC) = 0;
                sceVu0ApplyMatrix(a, body, a);
                sceVu0AddVector(a, a, u);
                StoreVf0(one);
                func_00149EC8(node, p, id + 1, P);
                sceVu0ApplyMatrix(b, node, one);
                GRef<u32>(b + 0xC) = 0;
                sceVu0ApplyMatrix(b, body, b);
                sceVu0AddVector(b, b, u);
                const u32 h = P + 0x294 + 4 * i;
                GRef<u32>(h) = func_00186928(W(h), a, b, rot);
            }
            fl = GRef<u16>(u + 0x56);
        }
    }
    if ((fl & 8) != 0) {
        const f32 hgt = func_0013E338(Terrain(), u);
        func_0019C278(fpu::Sub(F(u + 4), hgt), u, rot, u + 0x20, (fl & 0x40) != 0 ? 1 : 0);
    }
    const s32 engines = GRef<s8>(0x002A2520 + GRef<u8>(u + 0x69));
    if (engines >= 1) {
        ZeroQ(sum_p);
        ZeroQ(sum_d);
        for (s32 e = 0; e < engines; ++e) {
            // Node matrix (sp+0x140 in the original layout). It must not overlap `one` (sp + 0xA0):
            // the port's earlier sp + 0x70 put its translation row on `one`, which doubled the nozzle
            // offset and drew the engine flames away from the nozzles.
            const u32 m = sp + 0xD0;
            StoreVf0(one);
            func_00149EC8(m, p, e + 0x42, P);
            sceVu0ApplyMatrix(a, m, one);
            GRef<u32>(a + 0xC) = 0;
            sceVu0ApplyMatrix(a, body, a);
            sceVu0AddVector(a, a, u);
            sceVu0MulMatrix(m, body, m);
            func_00103290(b, m);
            const u32 h = P + 0x2C4 + 4u * static_cast<u32>(e);
            GRef<u32>(h) = func_001E43D8(F(P + 0x244), W(h), a, b, GRef<u8>(u + 0x69), 1);
            sceVu0AddVector(sum_p, sum_p, a);
            sceVu0AddVector(sum_d, sum_d, b);
        }
        if (engines >= 2) {
            sceVu0ScaleVector(sum_p, sum_p, 0.5f);
            sceVu0ScaleVector(sum_d, sum_d, 0.5f);
        }
        fl = GRef<u16>(u + 0x56);
        if ((fl & 0x20) != 0) {
            func_001E7CF8(F(P + 0x244), sum_p, sum_d, u + 0x20, GRef<u8>(u + 0x69));
            fl = GRef<u16>(u + 0x56);
        }
    } else {
        fl = GRef<u16>(u + 0x56);
    }
    if ((fl & 0x80) != 0) {
        GRef<u32>(P + 0x2CC) = func_001E9610(W(P + 0x2CC), u, rot, p);
        if ((W(Scene() + 0x1660) & 7) == 0) func_001EB380(u, rot, u + 0x20, p);
        fl = GRef<u16>(u + 0x56);
    } else if ((fl & 0x200) == 0) {
        GRef<s32>(P + 0x2CC) = -1;
    } else {
        GRef<u32>(P + 0x2CC) = func_00184638(W(P + 0x2CC), u, rot, -static_cast<s32>(GRef<u8>(u + 0x69)), GRef<u8>(u + 0x55));
        fl = GRef<u16>(u + 0x56);
    }
    if ((fl & 0x400) == 0) {
        GRef<s32>(P + 0x2D0) = -1;
    } else {
        GRef<u32>(P + 0x2D0) = func_00184638(W(P + 0x2D0), u, rot, GRef<u8>(u + 0x69), GRef<u8>(u + 0x55));
        fl = GRef<u16>(u + 0x56);
    }
    if ((fl & 2) != 0) GRef<u32>(u + 0x84) = func_0018AC00(W(u + 0x84), u, u + 0x20);  // a3 (angles) is not read
}

// EE: 0x001270B8
// Player engine sound record and the looped warning SEs (inventory note).
// fpucheck: 2 adds (throttle + afterburner * C, y - h)
void UpdatePlayerSound(s32 p) {
    const u32 u = Unit(p);
    const u32 pe = PadExt(p);
    const u32 P = W(u + 0x44);
    s32 off[2] = {0, 0};
    if ((~W(u + 0x4C) & 0x1000000u) == 0 && (~W(u + 0x4C) & 0x20000000u) == 0 && (StatusFlags() & 0x100000u) == 0 &&
        (W(P + 0x188) & 2u) == 0) {
        func_001279B0(p, 1);
        off[p] = 0;
    } else {
        func_001279B0(p, 0);
        off[p] = 1;
    }
    f32 ground;
    if ((StatusFlags() & 0x1000000u) != 0)
        ground = -960.0f;
    else if ((StatusFlags() & 0x2000000u) != 0 && GRef<s8>(P + 0x2F3) == -0x7F)
        ground = -95.5f;
    else
        ground = func_0013E338(Terrain(), u);
    const f32 y = F(u + 4);
    const u32 vel = u + 0x20;
    GetVectorLength(vel);
    const s32 d = static_cast<s32>(GRef<u8>(pe + 0x16)) - static_cast<s32>(GRef<u8>(pe + 0x17));
    const u16 sub = Sub();
    if (sub == 0x14 || sub == 0x19 || sub == 0x1B)
        GRef<u8>(0x00420620) = 1;
    else  // u8 view table 0x002A2538 (4-byte stride): the column of the engine layer volumes by view
        GRef<u8>(0x00420620) = GRef<u8>(0x002A2538 + 4u * static_cast<u32>(static_cast<s32>(GRef<s8>(P + 0x149))));
    GRef<f32>(0x00420624) = fpu::Add(F(P + 0x240), F(P + 0x244) * F(0x0040DE94));
    GRef<f32>(0x00420628) = GetVectorLength(vel);
    GRef<u32>(0x00420630) = 0;
    GRef<u32>(0x00420634) = W(P + 0x24C);
    GRef<u32>(0x00420638) = 0;
    GRef<f32>(0x0042062C) = fpu::Sub(y, ground);
    GRef<u32>(0x0042063C) = 0 < d ? 1u : 0u;
    GRef<u32>(0x00420640) = d < 0 ? 1u : 0u;
    sceVu0CopyVector(0x00420650, vel);
    sceVu0CopyVector(0x00420660, u + 0x10);
    if (32768.0f < F(P + 0x290))
        GRef<u32>(0x00420618) = 0;
    else
        GRef<s32>(0x00420618) = 0x8000 - fpu::FloatToInt(F(P + 0x290));
    const s8 t = GRef<s8>(P + 0x2F3);
    if (t == 2)
        GRef<u32>(0x00420614) = 1;
    else if (t == -0x80 || t == -0x7F)
        GRef<u32>(0x00420614) = 2;
    else
        GRef<u32>(0x00420614) = 0;
    GRef<s32>(0x00420610) = p;
    func_00216B80(0x00420610);
    s8 cd = GRef<s8>(P + 0x28B);
    if (Sub() != 0x11 && (W(u + 0x4C) & 0x1000000u) != 0 && (W(u + 0x50) & 0x800000u) != 0 && cd == 0) {
        PlayMenuSe(10, 0x11);
        GRef<u8>(P + 0x28B) = 0x3C;
        cd = 0x3C;
    }
    if (cd != 0) GRef<s8>(P + 0x28B) = static_cast<s8>(cd - 1);
    if (static_cast<u32>(Sub() - 0x19) < 2) func_002170E0(u, F(0x0040DE98) <= F(P + 0x130) ? 1 : 0, GRef<s8>(P + 0x149));
    if ((W(u + 0x4C) & 0x1000000u) == 0 || off[p] != 0 || (~StatusFlags() & 0x10000000u) == 0) {
        StopPlayerLoopSounds(p);
        return;
    }
    const u32 b1 = W(pe + 4);
    u32 sel = b1 & 1;
    if ((b1 & 2) != 0) {
        const bool alt = (W(P + 0x14C) & 0x8000u) != 0;
        if (GRef<u8>(P + (alt ? 0x15B : 0x15A)) == 0) {
            PlayMenuSe(10, 0x43);
            sel = W(pe + 4) & 1;
        }
    }
    if (sel != 0 && GRef<s16>(P + 0x158) == 0) PlayMenuSe(10, 0x43);
    u32 st;
    const u32 b0 = W(pe);
    if ((b0 & 1) == 0 || GRef<s16>(P + 0x158) == 0) {
        st = W(P + 0x350);
        if ((st & 7) != 0) {
            func_00219798(P + 0x354);
            st = W(P + 0x350) & 0xFFFFFFF8u;
            GRef<u32>(P + 0x350) = st;
        }
    } else {
        const s8 v = GRef<s8>(P + 0x149);
        const u32 bit = v == 0 ? 1u : v == 1 ? 2u : 4u;
        const u32 others = v == 0 ? 6u : v == 1 ? 5u : 3u;
        st = W(P + 0x350);
        if (v == 0 || v == 1 || v == 2) {
            if ((st & others) != 0) {
                func_00219798(P + 0x354);
                st = W(P + 0x350) & ~others;
                GRef<u32>(P + 0x350) = st;
            } else if ((~st & bit) != 0) {
                if (func_00219728(10, static_cast<s32>(0x13 + v), P + 0x354) == 1) {
                    st = W(P + 0x350) | bit;
                    GRef<u32>(P + 0x350) = st;
                } else {
                    st = W(P + 0x350);
                }
            }
        }
    }
    u32 hf = W(u + 0x50);
    const s32 kind_m = GRef<u8>(u + 0x55) != 1 ? 2 : 7;
    bool lock_beep = true;
    if ((hf & 0x3000000u) != 0) {
        if ((~st & 0x10) != 0) {
            func_00219728(10, kind_m, P + 0x39C);
            st = W(P + 0x350) | 0x10;
            GRef<u32>(P + 0x350) = st;
            hf = W(u + 0x50);
        } else {
            lock_beep = false;
        }
    } else if ((st & 0x10) != 0) {
        func_00219798(P + 0x39C);
        st = W(P + 0x350) & 0xFFFFFFEFu;
        GRef<u32>(P + 0x350) = st;
        hf = W(u + 0x50);
    }
    if (lock_beep && (hf & 0x3000000u) == 0 && (hf & 0x60000000u) != 0) {
        const u32 fc = W(Scene() + 0x165C);
        if (fc % 0x16 == 0 && (~st & 0x20) != 0) {
            func_00219728(10, kind_m, P + 0x3E4);
            st = W(P + 0x350) | 0x20;
            GRef<u32>(P + 0x350) = st;
        } else if (fc % 0x16 == 6 && (st & 0x20) != 0) {
            func_00219798(P + 0x3E4);
            st = W(P + 0x350) & 0xFFFFFFDFu;
            GRef<u32>(P + 0x350) = st;
        }
    } else if ((st & 0x20) != 0) {
        func_00219798(P + 0x3E4);
        st = W(P + 0x350) & 0xFFFFFFDFu;
        GRef<u32>(P + 0x350) = st;
    }
    u32 pf = W(P + 0x188);
    if ((pf & 0xF000u) == 0) {
        if ((st & 0x40) != 0) {
            func_00219798(P + 0x42C);
            pf = W(P + 0x188);
            st = W(P + 0x350) & 0xFFFFFFBFu;
            GRef<u32>(P + 0x350) = st;
        }
    } else if ((~st & 0x40) != 0) {
        func_00219728(10, GRef<u8>(u + 0x55) != 1 ? 0 : 5, P + 0x42C);
        pf = W(P + 0x188);
        st = W(P + 0x350) | 0x40;
        GRef<u32>(P + 0x350) = st;
    }
    if ((pf & 0xF00u) == 0) {
        if ((st & 0x80) != 0) {
            func_00219798(P + 0x474);
            GRef<u32>(P + 0x350) &= 0xFFFFFF7Fu;
        }
    } else if ((~st & 0x80) != 0) {
        func_00219728(10, GRef<u8>(u + 0x55) != 1 ? 1 : 6, P + 0x474);
        GRef<u32>(P + 0x350) |= 0x80u;
    }
}

// EE: 0x001278A0
void StopPlayerLoopSounds(s32 p) {
    const u32 P = W(Unit(p) + 0x44);
    u32 st = W(P + 0x350);
    if ((st & 7) != 0) {
        func_00219798(P + 0x354);
        st = W(P + 0x350) & 0xFFFFFFF8u;
        GRef<u32>(P + 0x350) = st;
    }
    if ((st & 0x10) != 0) {
        func_00219798(P + 0x39C);
        st = W(P + 0x350) & 0xFFFFFFEFu;
        GRef<u32>(P + 0x350) = st;
    }
    if ((st & 0x20) != 0) {
        func_00219798(P + 0x3E4);
        st = W(P + 0x350) & 0xFFFFFFDFu;
        GRef<u32>(P + 0x350) = st;
    }
    if ((st & 0x40) != 0) {
        func_00219798(P + 0x42C);
        st = W(P + 0x350) & 0xFFFFFFBFu;
        GRef<u32>(P + 0x350) = st;
    }
    if ((st & 0x80) != 0) {
        func_00219798(P + 0x474);
        GRef<u32>(P + 0x350) &= 0xFFFFFF7Fu;
    }
}

// EE: 0x00127BA8
// Speed limits by altitude.
// fpucheck: 13 adds
void func_00127BA8(u32 u, u32 P, u32 R) {
    const f32 y = F(u + 4), a = F(R + 0x44);
    f32 top, bot;
    if (y < a) {
        top = F(R + 0x50);
        bot = F(R + 0x48);
    } else {
        top = F(R + 0x54);
        bot = 0.0f;
    }
    GRef<f32>(P + 0x124) =
        fpu::Add(fpu::Sub(y, a) * fpu::Sub(top, F(R + 0x4C)) / fpu::Sub(bot, a), F(R + 0x4C));
    const f32 b = F(R + 0x60);
    if (y < b)
        GRef<f32>(P + 0x128) =
            fpu::Add(fpu::Sub(y, b) * fpu::Sub(F(R + 0x50), F(R + 0x64)) / fpu::Sub(F(R + 0x48), b), F(R + 0x64));
    else
        GRef<f32>(P + 0x128) = fpu::Add(y * fpu::Sub(F(R + 0x64), F(R + 0x58)) / b, F(R + 0x58));
    if (F(R + 0x60) <= F(u + 4))
        GRef<f32>(P + 0x12C) =
            fpu::Add(F(u + 4) * fpu::Sub(F(R + 0x64), F(R + 0x5C)) / F(R + 0x60), F(R + 0x5C));
    else
        GRef<u32>(P + 0x12C) = W(R + 0x64);
}

// EE: 0x00127CB0
// Throttle, afterburner and air brake.
// fpucheck: 7 adds
void UpdateThrottle(f32 target, u32 P) {
    f32 d = fpu::Sub(target, F(P + 0x240));
    f32 step = F(0x0040DE9C);
    if (d <= 0.0f) step = d;
    f32 s2 = F(0x0040DEA0);
    if (0.0f <= step) s2 = step;
    const f32 t = fpu::Add(s2, F(P + 0x240));
    GRef<f32>(P + 0x240) = t;
    if (1.0f < t) GRef<f32>(P + 0x240) = 1.0f;
    f32 thr = F(P + 0x240);
    if (thr < 0.0f) {
        GRef<f32>(P + 0x240) = 0.0f;
        thr = 0.0f;
    }
    f32 ab = 0.0f;
    if (static_cast<u32>(Sub() - 0x14) < 3) {
        GRef<f32>(P + 0x244) = 0.0f;
    } else if (F(0x0040DEA4) <= thr) {
        const u32 f = W(P + 0x188);
        GRef<u32>(P + 0x188) = (f & 0x100000u) == 0 ? (f | 0x300000u) : (f & 0xFFDFFFFFu);
        ab = F(P + 0x244);
        if (ab < 1.0f) {
            ab = fpu::Add(ab, F(0x0040DEA8));
            GRef<f32>(P + 0x244) = ab;
        }
    } else {
        const u32 f = W(P + 0x188);
        if ((f & 0x100000u) == 0)
            GRef<u32>(P + 0x188) = f & 0xFFBFFFFFu;
        else
            GRef<u32>(P + 0x188) = (f & 0xFFEFFFFFu) | 0x400000u;
        ab = F(P + 0x244);
        if (0.0f < ab) {
            ab = fpu::Sub(ab, F(0x0040DEAC));
            GRef<f32>(P + 0x244) = ab;
        }
    }
    f32 br = F(P + 0x24C);
    if (thr <= F(0x0040DEB0)) {
        if (br < 1.0f) {
            br = fpu::Add(br, F(0x0040DEB4));
            GRef<f32>(P + 0x24C) = br;
        }
    } else if (0.0f < br) {
        br = fpu::Sub(br, F(0x0040DEB8));
        GRef<f32>(P + 0x24C) = br;
    }
    if (1.0f < ab) {
        GRef<f32>(P + 0x244) = 1.0f;
        ab = 1.0f;
    }
    if (ab < 0.0f) GRef<f32>(P + 0x244) = 0.0f;
    if (1.0f < br) {
        GRef<f32>(P + 0x24C) = 1.0f;
        br = 1.0f;
    }
    if (br < 0.0f) GRef<f32>(P + 0x24C) = 0.0f;
}

// EE: 0x00127EC0
// Stall and lift factors.
// fpucheck: 8 adds
void func_00127EC0(u32 u, u32 P, u32 R) {
    const f32 cruise = F(P + 0x128), speed = F(P + 0x130);
    const f32 a = fpu::Sub(speed, fpu::Sub(cruise, F(R + 0x70))) / F(R + 0x70);
    GRef<f32>(P + 0x2DC) = a;
    if (1.0f < a) GRef<f32>(P + 0x2DC) = 1.0f;
    if (F(P + 0x2DC) < 0.0f) GRef<f32>(P + 0x2DC) = 0.0f;
    const f32 b = fpu::Sub(speed, cruise) / F(R + 0x74);
    GRef<f32>(P + 0x2E0) = b;
    if (1.0f < b) GRef<f32>(P + 0x2E0) = 1.0f;
    if (F(P + 0x2E0) < 0.0f) GRef<f32>(P + 0x2E0) = 0.0f;
    const f32 c = fpu::Sub(F(P + 0x12C), speed) / fpu::Sub(F(P + 0x12C), cruise);
    GRef<f32>(P + 0x2E4) = c;
    if (1.0f < c) GRef<f32>(P + 0x2E4) = 1.0f;
    if (F(P + 0x2E4) < 0.0f) GRef<f32>(P + 0x2E4) = 0.0f;
    const f32 h = func_0013E338(Terrain(), u);
    f32 g = fpu::Add(fpu::Sub(F(u + 4), h) * 0.00048828125f, 1.0f);
    if (1.0f < g) g = 1.0f;
    if (g < 0.0f) g = 0.0f;
    f32 l;
    if (F(P + 0x2E4) < g) {
        GRef<f32>(P + 0x2E4) = g;
        l = g;
    } else {
        l = F(P + 0x2E4);
    }
    GRef<f32>(P + 0x2E4) = fpu::Add(l * F(R + 0xB0), 1.0f) * F(P + 0x2E0);
    if (F(P + 0x2DC) < 1.0f || F(P + 0x2E0) < 1.0f)
        GRef<u32>(P + 0x188) |= 0x1000000u;
    else
        GRef<u32>(P + 0x188) &= 0xFEFFFFFFu;
}

// EE: 0x00128090
// Airspeed.
// fpucheck: 12 adds
void UpdateAirspeed(u32 u, u32 P, u32 R) {
    UpdateThrottle(fpu::Add(F(P + 0x114), 1.0f) * 0.5f, P);
    const f32 brake = fpu::Add(F(P + 0x24C), 1.0f);
    const f32 drag = F(R + 0x68) * brake;
    const f32 thrust = F(R + 0x6C) * brake * 2.5f;
    const f32 c = ee::cosf(F(u + 0x10));
    const f32 dc = drag * c;
    f32 k = c < 0.0f ? -c : fpu::Add(c, 1.0f) * 0.5f;
    f32 speed = F(P + 0x130);
    const f32 stall = F(P + 0x12C);
    f32 t;
    if (stall <= speed) {
        f32 r = fpu::Sub(speed, stall) / fpu::Sub(F(P + 0x124), stall);
        if (r < 0.0f) r = 0.0f;
        speed = fpu::Sub(speed, r * dc * k);
        GRef<f32>(P + 0x130) = speed;
        t = F(P + 0x114);
    } else {
        f32 r = fpu::Sub(stall, speed) / fpu::Sub(stall, fpu::Sub(F(P + 0x128), F(R + 0x70)));
        if (r < 0.0f) r = 0.0f;
        if (1.0f < r) r = 1.0f;
        if ((~W(P + 0x188) & 0x40000000u) == 0) {
            t = F(P + 0x114);
        } else {
            speed = fpu::Add(speed, r * thrust * c * k);
            GRef<f32>(P + 0x130) = speed;
            t = F(P + 0x114);
        }
    }
    if (0.0f < t)
        t = t * dc;
    else
        t = t * k * thrust * c;
    GRef<f32>(P + 0x130) = fpu::Add(speed, t * F(P + 0x2DC));
    const f32 s = ee::sinf(F(u + 0x10));
    const f32 v = fpu::Sub(F(P + 0x130), s * F(0x0040DEBC) * 5.0f * F(0x0040DEC0) * F(0x0040DEC0));
    GRef<f32>(P + 0x130) = v;
    if (v < 0.0f) GRef<f32>(P + 0x130) = 0.0f;
}

// EE: 0x001282B8
void UpdatePitchRate(u32 u, u32 P, u32 R) {
    (void)u;
    PitchRate(P, R, 0x0040DEC4, 0x0040DEC8);
}

// EE: 0x00128510
void UpdatePitchRateLanded(u32 u, u32 P, u32 R) {
    (void)u;
    PitchRate(P, R, 0x0040DECC, 0x0040DED0);
}

// EE: 0x00128768
// Roll rate and the bank-to-turn yaw rate.
// fpucheck: 21 static adds (two rate responses, bank wrap)
void UpdateRollAndYaw(u32 u, u32 P, u32 R) {
    const f32 roll = F(P + 0x118);
    const f32 t = roll * F(P + 0x2E4) * F(R + 0x78) * F(0x0040DED4);
    RateResponse(P + 4, t, F(R + 0x80), F(R + 0x84), F(0x0040DED8), F(0x0040DEDC));
    f32 z = F(u + 0x18);
    const f32 bank = roll * F(P + 0x2E4) * 45.0f * F(0x0040DEE0) / 180.0f;
    f32 d;
    if (z <= F(0x0040DEE4) && F(0x0040DEE8) <= z) {
        d = fpu::Sub(bank, z);
    } else {
        d = fpu::Sub(-bank, z);
        if (fpu::FloatToInt(fpu::Add(F(P + 0x11C), roll) * 10.0f) != 0) {
            d = fpu::Add(d, F(0x0040DEE0));
            if (F(0x0040DEE0) < d) d = fpu::Sub(d, F(0x0040DEEC));
        }
    }
    d = d / F(0x0040DEF0);
    d = d * ee::cosf(F(u + 0x10));
    if (1.0f < d) d = 1.0f;
    d = d * F(R + 0xA4);
    RateResponse(P + 8, d, F(R + 0xA8), F(R + 0xAC), F(0x0040DEF4), F(0x0040DEF8));
}

// EE: 0x00128AF0
// Yaw (rudder) rate in double precision (as 0x001282B8 with +0x2EC); result into state[1].
// fpucheck: 10 static adds
void UpdateYawRate(u32 u, u32 P, u32 R) {
    (void)u;
    const f32 s = ee::sinf(F(P + 0x120) * F(0x0040DEFC) * 0.5f) * F(P + 0x2E4);
    const double a = static_cast<double>(s * F(R + 0x90));
    double b = static_cast<double>(F(P + 0x2EC));
    if (b < 0.0) b = 0.0 - b;
    f32 t = static_cast<f32>(a * (1.0 - b));
    const f32 side = F(P + 0x2EC);
    if (t < 0.0f)
        t = fpu::Sub(t, s * F(R + 0xA0) * side);
    else
        t = fpu::Add(t, s * F(R + 0xA0) * side);
    const f32 v = RateResponse(P + 0x13C, t, F(R + 0x94), F(R + 0x98), F(0x0040DF00), F(0x0040DF04));
    GRef<f32>(P + 4) = fpu::Sub(v, F(R + 0xA0) * side);
}

// EE: 0x00128D40
// Yaw rate from the roll stick (flight mode 0).
// fpucheck: 7 static adds
void func_00128D40(u32 u, u32 P, u32 R) {
    (void)u;
    const f32 t = F(P + 0x118) * F(P + 0x2E4) * F(R + 0xA4);
    RateResponse(P + 8, t, F(R + 0xA8), F(R + 0xAC), F(0x0040DF08), F(0x0040DF0C));
}

// EE: 0x00128E80
// Velocity direction slip toward the nose (stall drop and side slip), then the climb penalty.
// fpucheck: 25 static adds
void func_00128E80(u32 u, u32 P, u32 R) {
    if ((~W(P + 0x188) & 0x40000000u) != 0) {
        guest::GuestFrame f(0xE0);
        const u32 ang = f.Address(0), m = f.Address(0x10), v = f.Address(0x50);
        GRef<u32>(ang) = 0;
        GRef<f32>(ang + 4) = -F(u + 0x14);
        GRef<u32>(ang + 8) = 0;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, ang);
        sceVu0ApplyMatrix(v, m, u + 0x20);
        f32 z = F(u + 0x18);
        if (z <= 0.0f) z = fpu::Add(z, F(0x0040DF10));
        const f32 c = ee::cosf(z);
        f32 k;
        if (z <= F(0x0040DF14))
            k = fpu::Sub(1.0f, c);
        else if (z <= F(0x0040DF18) || z <= F(0x0040DF1C))
            k = fpu::Add(c * F(0x0040DF20), 1.0f);
        else if (z <= F(0x0040DF24))
            k = fpu::Sub(1.0f, c);
        else
            k = 0.0f;
        const f32 s = fpu::Sub(1.0f, F(P + 0x2E0));
        if (k < s) k = s;
        const f32 g = fpu::Sub(F(0x0040DF28), F(u + 0x10)) / F(0x0040DF2C);
        const f32 t = fpu::Add(k * F(R + 0xC4) * g, fpu::Sub(1.0f, F(P + 0x2DC)) * F(R + 0xCC) * g);
        RateResponse(P + 0x140, t, F(R + 0xB4), F(R + 0xB8), F(0x0040DF30), F(0x0040DF34));
        const f32 t2 = ee::sinf(z) * F(R + 0xC8) * F(P + 0x2E0);
        const f32 y = RateResponse(P + 0x144, t2, F(R + 0xBC), F(R + 0xC0), F(0x0040DF38), F(0x0040DF3C));
        GRef<u32>(ang) = W(P + 0x140);
        GRef<f32>(ang + 4) = y;
        GRef<u32>(ang + 8) = 0;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, ang);
        sceVu0ApplyMatrix(v, m, v);
        GRef<u32>(ang) = 0;
        GRef<f32>(ang + 4) = F(u + 0x14);
        GRef<u32>(ang + 8) = 0;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, ang);
        sceVu0ApplyMatrix(u + 0x20, m, v);
    }
    if (F(P + 0x130) < F(P + 0x12C)) {
        f32 r = fpu::Sub(F(P + 0x130), F(P + 0x128)) / fpu::Sub(F(P + 0x12C), F(P + 0x128));
        if (r < 0.0f) r = 0.0f;
        const f32 d = fpu::Sub(1.0f, r) * F(0x0040DF40);
        GRef<f32>(P + 0x134) = d;
        GRef<f32>(u + 0x24) = fpu::Add(F(u + 0x24), d);
    } else {
        GRef<u32>(P + 0x134) = 0;
    }
}

// EE: 0x00129368
// Tutorial copy of the aircraft record (0xD0 bytes) with fixed limits.
// fpucheck: 2 adds (doubling of +0xC4 and +0xC8)
void func_00129368(u32 out, s32 p) {
    const u32 r = AircraftRec(p);
    for (u32 o = 0; o < 0x44; o += 4) GRef<u32>(out + o) = W(r + o);
    GRef<u32>(out + 0x60) = W(0x0040DF48);
    GRef<u32>(out + 0x64) = W(0x0040DF4C);
    GRef<u32>(out + 0x44) = W(0x0040DF50);
    GRef<u32>(out + 0x48) = W(0x0040DF54);
    GRef<u32>(out + 0x54) = W(0x0040DF44);
    GRef<u32>(out + 0x58) = W(0x0040DF58);
    GRef<u32>(out + 0x5C) = W(0x0040DF5C);
    GRef<u32>(out + 0x4C) = W(0x0040DF44);
    GRef<u32>(out + 0x50) = W(0x0040DF44);
    GRef<u32>(out + 0x68) = W(r + 0x68);
    GRef<u32>(out + 0x70) = W(0x0040DF60);
    GRef<u32>(out + 0x6C) = W(r + 0x6C);
    GRef<u32>(out + 0x74) = W(0x0040DF64);
    GRef<f32>(out + 0x78) = F(r + 0x78) * 0.5f;
    for (u32 o = 0x7C; o <= 0xA0; o += 4) GRef<u32>(out + o) = W(r + o);
    GRef<f32>(out + 0xA4) = F(r + 0xA4) * 0.5f;
    for (u32 o = 0xA8; o <= 0xC0; o += 4) GRef<u32>(out + o) = W(r + o);
    GRef<f32>(out + 0xC4) = fpu::Add(F(r + 0xC4), F(r + 0xC4));
    GRef<f32>(out + 0xC8) = fpu::Add(F(r + 0xC8), F(r + 0xC8));
    GRef<u32>(out + 0xCC) = W(r + 0xCC);
}

// EE: 0x00129500
// Player flight model step (docs/subsystems/flight.md): body-frame velocity, speed limits, stall
// factors, airspeed, angle of attack/slip, rates by flight mode (or the tutorial level hold),
// Euler integration with the loop flip and wraps, velocity back to world.
// fpucheck: 13 static adds
void UpdateFlightDynamics(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 pe = PadExt(p);
    guest::GuestFrame f(0x280);
    const u32 sp = f.Address();
    u32 R;
    if ((StatusFlags() & 0x10000000u) == 0) {
        R = AircraftRec(p);
    } else {
        R = sp;
        func_00129368(sp, p);
    }
    const u32 m = sp + 0x130, inv = sp + 0xF0, lv = sp + 0x180, ang = sp + 0x170;
    const u32 rot = u + 0x10, vel = u + 0x20;
    if ((GRef<u16>(pe + 0xE) & 1) == 0) {
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, rot);
        sceVu0InversMatrix(inv, m);
    } else {
        CopyQ(ang, rot);
        const f32 z = F(u + 0x18);
        if (F(0x0040DF68) < z || z < F(0x0040DF6C))
            GRef<u32>(ang + 8) = W(0x0040DF70);
        else
            GRef<u32>(ang + 8) = 0;
        sceVu0UnitMatrix(m);
        func_00102D28(m, m, ang);
        sceVu0InversMatrix(inv, m);
    }
    GRef<f32>(u + 0x24) = fpu::Sub(F(u + 0x24), F(P + 0x134));
    sceVu0ApplyMatrix(lv, inv, vel);
    GRef<u32>(P + 0x130) = W(lv + 8);
    func_00127BA8(u, P, R);
    func_00127EC0(u, P, R);
    UpdateAirspeed(u, P, R);
    if ((StatusFlags() & 0x10000000u) != 0) {
        const f32 lz = F(lv + 8);
        if (lz < 1.0f) {
            GRef<u32>(P + 0x2E8) = 0;
            GRef<u32>(P + 0x2EC) = 0;
        } else {
            GRef<f32>(P + 0x2E8) = F(lv + 4) / lz;
            GRef<f32>(P + 0x2EC) = -F(lv) / F(lv + 8);
        }
        GRef<u32>(lv + 8) = W(P + 0x130);
    } else {
        const f32 sp130 = F(P + 0x130);
        GRef<f32>(P + 0x2E8) = F(lv + 4) / F(lv + 8);
        GRef<f32>(P + 0x2EC) = -F(lv) / F(lv + 8);
        GRef<f32>(lv + 8) = sp130;
    }
    bool integrate = true;
    if ((GRef<u16>(pe + 0xE) & 1) != 0) {
        integrate = false;
        if ((GRef<u8>(P + 0x185) & 0x10) != 0) {
            GRef<u32>(P + 0x118) = 0;
            const f32 x = F(u + 0x10);
            if (0.0f < x) {
                const f32 v = -x / F(0x0040DF74);
                GRef<f32>(P + 0x11C) = v;
                if (v < -1.0f) GRef<f32>(P + 0x11C) = -1.0f;
            } else if (x < 0.0f) {
                const f32 v = -x / F(0x0040DF78);
                GRef<f32>(P + 0x11C) = v;
                if (1.0f < v) GRef<f32>(P + 0x11C) = 1.0f;
            }
        }
        UpdatePitchRateLanded(u, P, R);
        UpdateRollAndYaw(u, P, R);
        const f32 z = F(u + 0x18);
        if (F(0x0040DF7C) < z || z < F(0x0040DF80)) {
            GRef<f32>(u + 0x10) = fpu::Sub(F(u + 0x10), F(P));
            GRef<f32>(u + 0x14) = fpu::Sub(F(u + 0x14), F(P + 4));
        } else {
            GRef<f32>(u + 0x10) = fpu::Add(F(u + 0x10), F(P));
            GRef<f32>(u + 0x14) = fpu::Add(F(u + 0x14), F(P + 4));
        }
        GRef<f32>(u + 0x18) = fpu::Add(z, F(P + 8));
        const f32 x = F(u + 0x10);
        if (F(0x0040DF84) < x || x < F(0x0040DF88)) {
            const f32 pi = F(0x0040DF8C);
            const f32 nz = fpu::Sub(F(u + 0x18), pi);
            const f32 nx = fpu::Sub(pi, x);
            const f32 ny = fpu::Add(F(u + 0x14), pi);
            GRef<f32>(u + 0x18) = nz;
            GRef<f32>(u + 0x10) = nx;
            GRef<f32>(u + 0x14) = ny;
        }
        const f32 y = F(u + 0x14);
        if (F(0x0040DF90) < y)
            GRef<f32>(u + 0x14) = fpu::Sub(y, F(0x0040DF94));
        else if (y < F(0x0040DF98))
            GRef<f32>(u + 0x14) = fpu::Add(y, F(0x0040DF9C));
        const f32 zz = F(u + 0x18);
        if (F(0x0040DFA0) < zz)
            GRef<f32>(u + 0x18) = fpu::Sub(zz, F(0x0040DFA4));
        else if (zz < F(0x0040DFA8))
            GRef<f32>(u + 0x18) = fpu::Add(zz, F(0x0040DFAC));
    } else {
        if ((GRef<u8>(P + 0x185) & 0x10) != 0) {
            const f32 z = F(u + 0x18);
            if (0.0f < z) {
                const f32 v = -z / F(0x0040DFB0);
                GRef<f32>(P + 0x118) = v;
                if (v < -1.0f) GRef<f32>(P + 0x118) = -1.0f;
            } else if (z < F(0x00410660)) {
                const f32 v = -z / F(0x0040DFB4);
                GRef<f32>(P + 0x118) = v;
                if (1.0f < v) GRef<f32>(P + 0x118) = 1.0f;
            } else {
                GRef<f32>(P + 0x118) = 0.0f;
            }
            const f32 z2 = F(u + 0x18);
            const f32 lim = F(0x0040DFB8);
            if (z2 < lim && F(0x0040DFBC) < z2) {
                const f32 x = F(u + 0x10);
                if (0.0f < x) {
                    const f32 v = -x / lim;
                    GRef<f32>(P + 0x11C) = v;
                    if (v < -1.0f) GRef<f32>(P + 0x11C) = -1.0f;
                } else if (x < 0.0f) {
                    const f32 v = -x / lim;
                    GRef<f32>(P + 0x11C) = v;
                    if (1.0f < v) GRef<f32>(P + 0x11C) = 1.0f;
                } else {
                    GRef<f32>(P + 0x11C) = 0.0f;
                }
            }
        }
        UpdatePitchRate(u, P, R);
        UpdateYawRate(u, P, R);
        func_00128D40(u, P, R);
    }
    if (integrate) func_00102EF0(rot, rot, P);
    const u32 sa = sp + 0x190, sm = sp + 0x1A0;
    ZeroQ(sa);
    GRef<f32>(sa) = F(R + 0x88) * F(P + 0x2E8);
    GRef<f32>(sa + 4) = F(R + 0x9C) * F(P + 0x2EC);
    sceVu0UnitMatrix(sm);
    func_00102D28(sm, sm, sa);
    sceVu0ApplyMatrix(lv, sm, lv);
    sceVu0ApplyMatrix(vel, m, lv);
    func_00128E80(u, P, R);
}

// EE: 0x00129B58
// Flight model on the ground / carrier deck (+0x188 bit 0x20000000): taxi speed by throttle and
// brake, deck start position in mission 0xB, carrier catapult (status 0x2000000), steering.
// fpucheck: 15 static adds
void func_00129B58(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    const u32 pe = PadExt(p);
    guest::GuestFrame f(0x240);
    const u32 rc = f.Address();
    func_00129368(rc, p);
    if ((StatusFlags() & 0x2000000u) != 0)
        GRef<f32>(rc + 0x6C) = fpu::Add(F(rc + 0x6C), F(rc + 0x6C));
    else
        GRef<f32>(rc + 0x6C) = F(rc + 0x6C) * F(0x0040DFC0);
    const u32 m = rc + 0x130, ang = rc + 0x170, inv = rc + 0xF0, lv = rc + 0x180;
    sceVu0UnitMatrix(m);
    ZeroQ(ang);
    GRef<u32>(ang + 4) = W(u + 0x14);
    const u32 vel = u + 0x20;
    func_00102D28(m, m, ang);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(lv, inv, vel);
    GRef<u32>(P + 0x130) = W(lv + 8);
    const f32 k = F(0x0040DFC4);
    GRef<f32>(lv) = F(lv) * k;
    GRef<f32>(lv + 4) = F(lv + 4) * k;
    GRef<f32>(P + 0x2E0) = 1.0f;
    GRef<u32>(P + 0x188) &= 0xFEFFFFFFu;
    GRef<f32>(P + 0x2DC) = 1.0f;
    const u16 sub = Sub();
    if (sub == 0x16) {
        ZeroQ(vel);
        ZeroQ(P);
        GRef<u32>(P + 0x13C) = 0;
        GRef<u32>(P + 0x138) = 0;
        return;
    }
    if (sub == 0x19) return;
    bool normal = (StatusFlags() & 0x2000000u) == 0;
    if (!normal && sub == 0xB) {
        if (GRef<s32>(0x00410664) == 0 && F(lv + 8) == 0.0f) {
            GRef<u32>(u) = W(0x0040DFC8);
            GRef<f32>(u + 4) = -105.0f;
            GRef<u32>(u + 8) = W(0x0040DFCC);
            GRef<u32>(u + 0x14) = W(0x0040DFD0);
            GRef<s32>(0x00410664) = 1;
        }
        if (GRef<s32>(0x00410664) == 1) {
            func_0012A0E0(p);
            if ((~W(P + 0x188) & 0x20000000u) != 0) GRef<s32>(0x00410664) = 0;
            if ((W(PadState(0) + 8) & 4u) != 0) GRef<s32>(0x00410664) = 0;
            return;
        }
        normal = (StatusFlags() & 0x2000000u) == 0;
    }
    if (normal) {
        UpdateThrottle(F(P + 0x114), P);
        f32 t;
        if (Sub() == 0x15) {
            t = fpu::Sub(F(P + 0x114), F(0x0040DFD8));
            if (t < -1.0f) t = -1.0f;
        } else {
            t = F(P + 0x114);
        }
        const f32 g = 0.0f < t ? F(rc + 0x68) : F(rc + 0x6C);
        GRef<f32>(P + 0x130) = fpu::Add(F(P + 0x130), t * g);
        if (F(P + 0x130) < 0.0f) {
            GRef<f32>(P + 0x130) = 0.0f;
            GRef<f32>(lv + 8) = 0.0f;
        } else {
            GRef<u32>(lv + 8) = W(P + 0x130);
        }
        if (F(rc + 0x5C) < F(P + 0x130)) GRef<f32>(lv + 4) = fpu::Sub(F(lv + 4), F(0x0040DFDC));
    } else {
        f32 t = F(P + 0x114);
        UpdateThrottle(0.0f < t ? t : 0.0f, P);
        const f32 thr = F(P + 0x114);
        f32 g = 0.0f;
        if (0.0f < thr) g = fpu::Add(thr, thr) * F(rc + 0x68);
        const f32 v = fpu::Add(F(P + 0x130), fpu::Sub(g, F(rc + 0x6C)));
        GRef<f32>(P + 0x130) = v;
        if (v < 0.0f) GRef<f32>(P + 0x130) = 0.0f;
        GRef<u32>(lv + 8) = W(P + 0x130);
        if (F(rc + 0x5C) < F(P + 0x130)) GRef<f32>(lv + 4) = fpu::Sub(F(lv + 4), F(0x0040DFD4));
        if (0.0f < F(P + 0x130)) {
            GRef<u32>(P + 0x260) = 0;
            GRef<u32>(P + 0x26C) = 0;
        }
    }
    func_002576C8(p, 3, 0.0f, F(P + 0x130));
    const f32 steer = (GRef<u16>(pe + 0xE) & 1) == 0 ? F(P + 0x120) : F(P + 0x118);
    f32 s = F(P + 0x130);
    if (1.0f < s) s = 1.0f;
    const f32 tgt = steer * F(rc + 0x90) * s;
    if (0.0f < s) {
        GRef<f32>(P + 4) = RateResponse(P + 0x13C, tgt, F(rc + 0x94), F(rc + 0x98), F(0x0040DFE0), F(0x0040DFE4));
    } else {
        ZeroQ(P);
        GRef<u32>(P + 0x13C) = 0;
        GRef<u32>(P + 0x138) = 0;
    }
    func_00102EF0(u + 0x10, u + 0x10, P);
    sceVu0ApplyMatrix(vel, m, lv);
    if (Sub() == 0x1A) GRef<u32>(P + 0x24C) = 0;
}

// EE: 0x0012A0E0
// Carrier catapult / deck hold (subs 0x1A-0x1B or the mission 0xB start).
// fpucheck: 1 add
void func_0012A0E0(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    guest::GuestFrame f(0x210);
    const u32 rc = f.Address();
    func_00129368(rc, p);
    const u32 m = rc + 0x130, ang = rc + 0x170, inv = rc + 0xF0, lv = rc + 0x180;
    sceVu0UnitMatrix(m);
    ZeroQ(ang);
    GRef<u32>(ang + 4) = W(u + 0x14);
    func_00102D28(m, m, ang);
    sceVu0InversMatrix(inv, m);
    sceVu0ApplyMatrix(lv, inv, u + 0x20);
    const f32 k = F(0x0040DFE8);
    GRef<u32>(P + 0x130) = W(lv + 8);
    GRef<f32>(lv) = F(lv) * k;
    GRef<u32>(P + 0x188) &= 0xFEFFFFFFu;
    GRef<f32>(lv + 4) = F(lv + 4) * k;
    GRef<f32>(P + 0x2DC) = 1.0f;
    f32 t = F(P + 0x114);
    GRef<f32>(P + 0x2E0) = 1.0f;
    if (t <= 0.0f) t = 0.0f;
    UpdateThrottle(t, P);
    const f32 v = F(P + 0x130);
    if (v <= F(0x0040DFEC)) {
        if (1.0f <= F(P + 0x244)) {
            GRef<u32>(P + 0x130) = W(rc + 0x58);
            GRef<u32>(lv + 8) = W(rc + 0x58);
        } else {
            GRef<f32>(lv + 8) = v;
        }
    } else {
        f32 g = F(rc + 0x6C);
        if (0.0f < F(P + 0x114)) g = F(rc + 0x68);
        GRef<f32>(P + 0x130) = fpu::Add(v, F(P + 0x114) * g);
        f32 nv = F(P + 0x130);
        if (nv < 0.0f) {
            GRef<f32>(P + 0x130) = 0.0f;
            nv = 0.0f;
        }
        GRef<f32>(lv + 8) = nv;
    }
    func_002576C8(p, 3, 0.0f, F(P + 0x130));
    sceVu0ApplyMatrix(u + 0x20, m, lv);
    if (Sub() == 0x1A) GRef<u32>(P + 0x24C) = 0;
}

// EE: 0x0012A2D0
// Dead player aircraft: lockable flag cleared after a hard contact, falling, wreck tumble.
// fpucheck: 3 adds
void func_0012A2D0(s32 p) {
    const u32 u = Unit(p);
    const u32 P = W(u + 0x44);
    if (GRef<u16>(P + 0x2F6) >= 2 || (GRef<u16>(P + 0x2F4) & 0x1010) != 0) GRef<u32>(u + 0x4C) &= 0xDFFFFFFFu;
    if ((W(u + 0x4C) & 0x20000000u) == 0) {
        ZeroQ(u + 0x20);
        return;
    }
    GRef<f32>(u + 0x24) = fpu::Add(F(u + 0x24), F(0x0040DFF0));
    const u16 c = GRef<u16>(P + 0x2F4);
    if ((c & 0x400) != 0 && (~c & 0x800) != 0) GRef<f32>(P + 4) = fpu::Add(F(P + 4), F(0x0040DFF4));
    if ((~c & 0x400) != 0 && (c & 0x800) != 0) GRef<f32>(P + 4) = fpu::Add(F(P + 4), F(0x0040DFF8));
    func_00102EF0(u + 0x10, u + 0x10, P);
}

}  // namespace ac04::game
