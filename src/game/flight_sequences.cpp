// Scene 2 sequence frames (slice S5j): the replay, mission-end, post-mission, game-over and pause
// sub-state frames, the carrier landing / takeoff sequences and their retry restart, the unit and
// player snapshot helpers, and the flight record kept for the results (docs/subsystems/mission-flow.md).
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

// The world update every scene 2 frame handler starts with: terrain, sea, world object lists and
// the world objects of the current terrain entry.
void UpdateWorld() {
    func_00139FE8(Terrain());
    func_00144CC0(SeaObj());
    func_0013EC20(WorldObjs());
    const u32 t = Terrain();
    const u32 e = W(t) + W(t + 4) * 100;
    func_0013EC38(WorldObjs(), W(e + 0x44), W(e + 0x50));
}

// The sound listener of view camera 0 (Scene() + 0xB0) for player v.
void Listener(s32 v) {
    const u32 cam = Scene() + 0xB0;
    func_00219330(cam);
    func_0021AF18(v, cam);
}

void ListenerObject(s32 v) {
    const u32 obj = W(UnitList() + 4u * static_cast<u32>(v) + 0x4C);
    GRef<u32>(obj + 0x10) = Scene() + 0xB0;
    GRef<u32>(obj + 0x14) = Scene() + 0xC0;
    func_0021C700(obj);
}

}  // namespace

// EE: 0x0010B930
// Subs 0x10-0x12 (replay / demo playback): world update, replay frame (0x0010C6F8), replay view and
// camera (0x0010D570), HUD and sound passes, the listener on the camera of the watched player
// (game[0x2D], 0 when not below the player count), the radio noise when status flag 0x10000000,
// then the draw lists.
void func_0010B930() {
    UpdateWorld();
    func_0010C6F8();
    func_0010D570();
    func_0011DC20();
    func_0014D4D0();
    s32 v = 0;
    if (Game(0x2D) < Game(0x25)) v = Game(0x2D);
    ListenerObject(v);
    func_00219330(Scene() + 0xB0);
    func_0021AF18(v, Scene() + 0xB0);
    if ((StatusFlags() & 0x10000000u) != 0) func_00216D88(Scene() + 0xB0, 0);
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010B930, func_0010B930);

// EE: 0x0010BAB8
// Subs 0x14-0x18 (mission-end sequence): world update, simulation and end sequence (0x0010C508),
// view cameras, player 0 sound, sound passes, listener of view 0, radio noise, draw lists.
void func_0010BAB8() {
    UpdateWorld();
    func_0010C508();
    UpdateViewCameras();
    UpdatePlayerSound(0);
    func_002166A8();
    func_0011DC20();
    func_0014D4D0();
    Listener(0);
    ListenerObject(0);
    func_00216D88(Scene() + 0xB0, 0);
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010BAB8, func_0010BAB8);

// EE: 0x0010BB98
// Subs 0x19-0x1B (post-mission sequence): as 0x0010BAB8 with 0x0010C638 and without the radio
// noise.
void func_0010BB98() {
    UpdateWorld();
    func_0010C638();
    UpdateViewCameras();
    UpdatePlayerSound(0);
    func_002166A8();
    func_0011DC20();
    func_0014D4D0();
    Listener(0);
    ListenerObject(0);
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010BB98, func_0010BB98);

// EE: 0x0010BC68
// Sub 0x1C (game-over menu): menu update (0x0017E200), player loop sounds stopped, view cameras,
// draw lists.
void func_0010BC68() {
    func_0017E200();
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
        StopPlayerLoopSounds(p);
        func_001279B0(p, 0);
    }
    UpdateViewCameras();
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010BC68, func_0010BC68);

// EE: 0x0010BCD8
// Sub 0x1D (pause menu): world update, unit states, the player held at the stored position
// (0x0017A920), menu update (0x0017E450), radio, view cameras, player 0 sound, sound pass, draw
// lists.
void func_0010BCD8() {
    UpdateWorld();
    UpdateUnitStates();
    func_0017A920();
    func_0017E450();
    func_00251050();
    UpdateViewCameras();
    UpdatePlayerSound(0);
    func_002166A8();
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010BCD8, func_0010BCD8);

// EE: 0x0010C508
// Mission-end simulation (subs 0x14-0x18), unless main-context flag 0x100: replay recording on in
// subs 0x14 / 0x15, and in sub 0x16 for the first 120 frames (+0x1654), else off; unit states,
// player passes, effects, HUDs (with the empty per-player hook 0x0016DA68), radio, rules, frame
// record when recording, frame counter +0x165C. Then always the end sequence (0x0017A1B8).
void func_0010C508() {
    MainContext* ctx = Ctx();
    if ((~ctx->flags & 0x100u) != 0) {
        const u16 sub = ctx->sub;
        bool rec = static_cast<u32>(sub - 0x14) < 2;
        if (!rec && sub == 0x16 && W(Scene() + 0x1654) < 0x78) rec = true;
        if (rec)
            GRef<u32>(Scene() + 0x1668) |= 1u;
        else
            GRef<u32>(Scene() + 0x1668) &= 0xFFFFFFFEu;
        UpdateUnitStates();
        func_0011D300();
        func_0011D988();
        UpdateEffects();
        for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
            UpdateHud(p);
            func_0016DA68();
        }
        func_00251050();
        func_00257890();
        func_00155AE8();
        func_0016DB40();
        if ((W(Scene() + 0x1668) & 1u) != 0) RecordReplayFrame();
        GRef<s32>(Scene() + 0x165C) += 1;
    }
    func_0017A1B8();
}

// EE: 0x0010C638
// Post-mission simulation (subs 0x19-0x1B), unless main-context flag 0x100: as 0x0010C508 without
// replay recording; then always the post-mission sequence (0x0017AC68).
void func_0010C638() {
    if ((~Ctx()->flags & 0x100u) != 0) {
        UpdateUnitStates();
        func_0011D300();
        func_0011D988();
        UpdateEffects();
        for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
            UpdateHud(p);
            func_0016DA68();
        }
        func_00251050();
        func_00257890();
        func_00155AE8();
        func_0016DB40();
        GRef<s32>(Scene() + 0x165C) += 1;
    }
    func_0017AC68();
}

// EE: 0x0010C6F8
// Replay frame: playback (0x0016E2D0), unit model hierarchies, and in sub 0x11 projectiles and
// effects.
void func_0010C6F8() {
    UpdateReplayPlayback();
    UpdateUnitModelHierarchies(UnitModels());
    if (Ctx()->sub != 0x11) return;
    AgeProjectiles();
    UpdateProjectiles();
    UpdateEffects();
}

// EE: 0x0010D470
// Replay view: one view (+0x1650 = 1, flags 0x80000001, +4 = 4, player +6 = game[0x2D] when below
// the player count, else 0), projection 640 (K 0x0040DADC in 16:9) x 480 at distance K 0x0040DAE0,
// near K 0x0040DAE4, viewport the draw area (far K 0x0040DAE8), camera copy.
void func_0010D470() {
    GRef<u8>(Scene() + 0x1650) = 1;
    const u32 v = Scene();
    GRef<u8>(v + 4) = 4;
    const u32 c0 = v + 0xB0;
    GRef<u8>(v + 5) = 0;
    GRef<u32>(v + 0) = 0x80000001;
    const u8 p = Game(0x2D);
    const u32 c1 = v + 0x270;
    GRef<u8>(v + 6) = p < Game(0x25) ? p : 0;
    GRef<u8>(v + 7) = 0;
    f32 width = 640.0f;
    if ((Ctx()->flags & 0x10000u) != 0) width = F(0x0040DADC);
    // port: match the live flight cameras when the host presents an ultrawide window.
    if ((Ctx()->flags & 0x10000u) != 0) width = port::UltrawideProjectionWidth(640.0f, width);
    SetCameraProjection(width, 480.0f, F(0x0040DAE0), 1.0f, F(0x0040DAE4), c0);
    SetCameraViewport(1.0f, F(0x0040DAE8), c0, 0, 0, static_cast<s16>(Ctx()->draw_w), static_cast<s16>(Ctx()->draw_h));
    func_00102838(c1, c0);
    func_00102990(c1, c0);
}

// EE: 0x0010D570
void func_0010D570() {
    func_0010D470();
    func_00176060();
}

// EE: 0x0016DA68
// Empty per-player hook.
void func_0016DA68() {}

// EE: 0x0017A920
// Holds player unit 0 at the position stored in the mission status (+0, +8, +0xC, +0x14 copied;
// the other words kept), velocity and aircraft state +0 cleared, aircraft +0x188 |= 0x800000,
// +0x138..+0x144 cleared.
void func_0017A920() {
    const u32 s = Status();
    const u32 u = Unit(0);
    const u32 ac = W(u + 0x44);
    GRef<f32>(u) = F(s);
    GRef<f32>(u + 8) = F(s + 8);
    GRef<u32>(ac + 0x188) = W(ac + 0x188) | 0x800000u;
    GRef<f32>(u + 0xC) = F(s + 0xC);
    GRef<f32>(u + 0x14) = F(s + 0x14);
    ZeroQ(u + 0x20);
    ZeroQ(ac);
    GRef<u32>(ac + 0x144) = 0;
    GRef<u32>(ac + 0x138) = 0;
    GRef<u32>(ac + 0x13C) = 0;
    GRef<u32>(ac + 0x140) = 0;
}

// EE: 0x0017E200
// Game-over menu (table 0x002B8430).
void func_0017E200() { func_0017E4E8(0x002B8430); }

// EE: 0x0017E450
// Pause menu (table 0x002B8450).
void func_0017E450() { func_0017E4E8(0x002B8450); }

// EE: 0x0017D838
void func_0017D838(u32 hud, u32 v) { GRef<u32>(hud + 0x4C) = v; }

// EE: 0x0017D890
void func_0017D890(u32 hud, u16 v) { GRef<u16>(hud + 0x5A) = v; }

// EE: 0x0017D898
// Sets the camera index byte +0x59 when +0x58 is set.
void func_0017D898(u32 hud, u32 v) {
    if (GRef<u8>(hud + 0x58) != 0) GRef<u8>(hud + 0x59) = static_cast<u8>(v);
}

namespace {

bool StartHeld() { return (W(CtxAddr() + 0x218) & 0x800u) != 0; }  // pad state 0 +8, bit 0x800
f32 Fade() { return F(Scene() + 0x1670); }
u32 Frames() { return W(Scene() + 0x1654); }

}  // namespace

// EE: 0x0017A1B8
// Carrier landing sequence, scene 2 subs 0x14-0x18 (unit 0 = player, retry allowed in mission 3
// with status flag 4). 0x14 approach: Start -> 0x18 (retry) or the pause 0x1D; radio 2 at frame 2;
// sub 0x15 from frame 180. 0x17 success / 0x18 skip: on entry centre message 9, look-back camera
// (aircraft +0x30), menu SE 10/0x45, music fade (0x0021F190(10, 0, 0, 0, 2)); fade out at frame 180;
// at full fade 0x17 replays (sub 0x10, scene flag 0x10000 cleared) and 0x18 goes to scene 5, then
// the radio stops (0x002559C8); a dead player gives sub 0x16 with a fade in. 0x15 landing: Start as
// 0x14; timer status +0x38 counts down; on entry centre message 0 and scene flag 0x100; touchdown
// (aircraft +0x130 below K 0x0040EBFC): radio 0xC and sub 0x17 (retry) / 0x16; more than 32768
// from the rules position in x or z: status flag 0x40000000 and 0x16; dead or timer 0: 0x16; radio
// 0xE at timer 1800. 0x16 failure: HUD label 0xFF; a unit just destroyed requests 0x16 again;
// alive with time left: out of the area (flag 0x40000000) -> radio 0xF, message 1, fade at 240, then
// sub 0x14; else message 2, fade at 360, then replay (sub 0x10) and the radio stops; dead or no
// time: message 1, fade at 240, then the hp restored from *0x004117DC when not positive, flags
// 0x31000000 and sub 0x14. Every frame the replay HUD shows the timer +0x1660.
// fpucheck: 4 add (area test)
void func_0017A1B8() {
    const u32 u = Unit(0);
    const u32 ac = W(u + 0x44);
    s32 retry = 0;
    if (Game(0x27) == 3) retry = (StatusFlags() & 4u) != 0 ? 1 : 0;
    MainContext* ctx = Ctx();
    if (ctx->sub == 0x14) {
        if (StartHeld()) RequestSubState(retry == 1 ? 0x18 : 0x1D);
        if (Frames() == 2) func_0017B1E0(2);
        if (!(Frames() < 0xB4)) RequestSubState(0x15);
    }
    if (static_cast<u32>(ctx->sub - 0x17) < 2) {
        if (Frames() == 0) {
            ShowCenterMessage(0, 9, 0);
            func_0010D748(Scene(), ac + 0x30);
            PlayMenuSe(10, 0x45);
            func_0021F190(10.0f, 0, 0, 0, 2);
        }
        if (Frames() == 0xB4 && Fade() == 0.0f) func_0010A130(1.0f, 3.0f, 0);
        if (Fade() == 1.0f) {
            if (ctx->sub == 0x17) {
                GRef<u32>(Scene() + 0x1668) &= 0xFFFEFFFFu;
                RequestSubState(0x10);
            } else if (ctx->sub == 0x18) {
                RequestScene(5);
            }
            func_002559C8();
        }
        if ((~W(u + 0x4C) & 0x1000000u) != 0) {
            RequestSubState(0x16);
            if (Fade() != 0.0f) func_0010A130(0.0f, 0.5f, 0);
        }
    }
    if (ctx->sub == 0x15) {
        if (StartHeld()) RequestSubState(retry == 1 ? 0x18 : 0x1D);
        if (W(Status() + 0x38) != 0) GRef<u32>(Status() + 0x38) = W(Status() + 0x38) - 1;
        if (Frames() == 0) {
            ShowCenterMessage(0, 0, 0);
            GRef<u32>(Scene() + 0x1668) |= 0x100u;
        }
        if (F(ac + 0x130) < F(0x0040EBFC)) {
            func_0017B1E0(0xC);
            RequestSubState(retry == 1 ? 0x17 : 0x16);
        }
        const u32 r = Rules();
        bool out = F(u) < fpu::Add(F(r), -32768.0f);
        if (!out) out = fpu::Add(F(r), 32768.0f) < F(u);
        if (!out) out = F(u + 8) < fpu::Add(F(r + 8), -32768.0f);
        if (!out) out = fpu::Add(F(r + 8), 32768.0f) < F(u + 8);
        if (out) {
            GRef<u32>(Status() + 0x20) = StatusFlags() | 0x40000000u;
            RequestSubState(0x16);
        }
        if ((~W(u + 0x4C) & 0x1000000u) != 0 || W(Status() + 0x38) == 0) RequestSubState(0x16);
        if (W(Status() + 0x38) == 0x708) func_0017B1E0(0xE);
    }
    if (ctx->sub == 0x16) {
        if (Frames() == 0) func_0017D848(W(0x00410B40), 0xFF, 0);
        const u32 f = W(u + 0x4C);
        if ((f & 0x200000u) != 0) {
            RequestSubState(0x16);
            return;
        }
        if ((~f & 0x1000000u) == 0 && W(Status() + 0x38) != 0) {
            if ((StatusFlags() & 0x40000000u) != 0) {
                if (Frames() == 0) {
                    func_0017B1E0(0xF);
                    ShowCenterMessage(0, 1, 0);
                }
                if (Frames() == 0xF0) func_0010A130(1.0f, 1.0f, 0);
                if (Fade() == 1.0f) RequestSubState(0x14);
            } else {
                if (Frames() == 0) ShowCenterMessage(0, 2, 0);
                if (Frames() == 0x168) func_0010A130(1.0f, 1.0f, 0);
                if (Fade() == 1.0f) {
                    GRef<u32>(Scene() + 0x1668) &= 0xFFFEFFFFu;
                    RequestSubState(0x10);
                    func_002559C8();
                }
            }
        } else {
            if (Frames() == 0) ShowCenterMessage(0, 1, 0);
            if (Frames() == 0xF0) func_0010A130(1.0f, 1.0f, 0);
            if (Fade() == 1.0f) {
                if (!(0 < GRef<s16>(u + 0x5E))) GRef<u16>(u + 0x5E) = GRef<u16>(0x004117DC);
                GRef<u32>(u + 0x4C) = W(u + 0x4C) | 0x31000000u;
                RequestSubState(0x14);
            }
        }
    }
    func_0017D838(W(0x00410B40), W(Scene() + 0x1660));
}

// EE: 0x0017A980
// Puts the player back for another try: weapons and reload reset (0x0011EC48, 0x00124740), flags
// 0x31000000, airborne counter 0, hp (saved *0x004117DC in game mode 5, else the model default
// 0x0011E878), +0x50 / aircraft +0x2D4 cleared, targets -1, gear up (0x001211B0 -> clearance h),
// aircraft +0x188 |= 0x800000. With status flag 0x2000000 (carrier start): flag 0x4000000 follows
// bit 0 of the frame counter +0x165C; position rules x + 90 - 1 (or x - 30 - 1), y + h, z + K
// 0x0040EC00 (or + 300), yaw K 0x0040EC04 (or K 0x0040EC08). Otherwise rules x, y - 8, z + 4608,
// angles 0. Status +0 / +0x10 = position / angles, velocity and aircraft motion cleared, speed
// aircraft +0x134 = K 0x0040EC0C (carrier) / 0x0040EC10, inputs 0 and 0.5.
// fpucheck: carrier path 4 (1 add or sub, 1 sub, 2 add), else 1 sub, 1 add
void func_0017A980() {
    const u32 u = Unit(0);
    const u32 ac = W(u + 0x44);
    func_0011EC48(ac, GRef<s8>(ac + 0x172));
    func_00124740(0, ac);
    GRef<u32>(u + 0x4C) = W(u + 0x4C) | 0x31000000u;
    GRef<u32>(ac + 0x27C) = 0;
    u16 hp = GRef<u16>(0x004117DC);
    if (Game(0x28) != 5) hp = static_cast<u16>(func_0011E878(GRef<u8>(u + 0x69)));
    GRef<u16>(u + 0x5E) = hp;
    GRef<u32>(u + 0x50) = 0;
    GRef<u32>(ac + 0x2D4) = 0;
    GRef<s32>(ac + 0x2C8) = -1;
    GRef<s32>(ac + 0x2C4) = -1;
    const f32 h = func_001211B0(0);
    GRef<u32>(ac + 0x188) = W(ac + 0x188) | 0x800000u;
    const u32 st = Status();
    const u32 r = Rules();
    if ((W(st + 0x20) & 0x2000000u) != 0) {
        const u32 f = W(st + 0x20);
        GRef<u32>(st + 0x20) = (W(Scene() + 0x165C) & 1u) != 0 ? (f | 0x4000000u) : (f & 0xFBFFFFFFu);
        f32 x, dz, yaw;
        if ((W(st + 0x20) & 0x4000000u) != 0) {
            x = fpu::Add(F(r), 90.0f);
            dz = F(0x0040EC00);
            yaw = F(0x0040EC04);
        } else {
            x = fpu::Sub(F(r), 30.0f);
            dz = 300.0f;
            yaw = F(0x0040EC08);
        }
        GRef<f32>(u) = fpu::Sub(x, 1.0f);
        GRef<f32>(u + 4) = fpu::Add(F(r + 4), h);
        GRef<f32>(u + 0x14) = yaw;
        GRef<f32>(u + 8) = fpu::Add(F(r + 8), dz);
        GRef<u32>(u + 0x10) = 0;
        GRef<f32>(u + 0xC) = 1.0f;
        GRef<u32>(u + 0x18) = 0;
    } else {
        GRef<f32>(u) = F(r);
        GRef<f32>(u + 4) = fpu::Sub(F(r + 4), 8.0f);
        GRef<f32>(u + 0xC) = 1.0f;
        GRef<f32>(u + 8) = fpu::Add(F(r + 8), 4608.0f);
        GRef<u32>(u + 0x10) = 0;
        GRef<u32>(u + 0x14) = 0;
        GRef<u32>(u + 0x18) = 0;
    }
    GRef<u32>(u + 0x1C) = 0;
    CopyQ(st, u);
    CopyQ(Status() + 0x10, u + 0x10);
    ZeroQ(u + 0x20);
    ZeroQ(ac);
    GRef<u32>(ac + 0x138) = 0;
    GRef<u32>(ac + 0x13C) = 0;
    GRef<u32>(ac + 0x140) = 0;
    GRef<u32>(ac + 0x144) = 0;
    GRef<f32>(ac + 0x134) = (StatusFlags() & 0x2000000u) != 0 ? F(0x0040EC0C) : F(0x0040EC10);
    GRef<u32>(ac + 0x240) = 0;
    GRef<f32>(ac + 0x254) = 0.5f;
    GRef<u32>(ac + 0x244) = 0;
    GRef<u32>(ac + 0x248) = 0;
    GRef<u32>(ac + 0x24C) = 0;
    GRef<f32>(ac + 0x250) = 0.5f;
    GRef<f32>(ac + 0x258) = 0.5f;
}

// EE: 0x0017AC68
// Carrier takeoff sequence, subs 0x19-0x1B. 0x19: fade in on entry, radio 0x10 at frame 2, Start
// from frame 2 -> 0x1B with the timer cleared, sub 0x1A from frame 180. 0x1A: Start as 0x19; timer
// counts down; on entry scene flag 0x100, look-back camera (aircraft +0x30), centre message 3;
// airborne (aircraft +0x188 bit 0x20000000, all wheels down, clear) counter +0x27C += K 0x0040EC14
// (1/60), above 5 (5 s in the air) -> 0x1B; wheels down: reset; still on the deck (+0x130 == 0):
// radio 0x12 at frame 600 and 0x13 at 1200; dead or no time -> 0x1B. 0x1B: HUD label 0xFF and fade in on entry; a unit just destroyed requests 0x1B again;
// dead: message 4, fade out at 180, then the player is put back (0x0017A980) and sub 0x19; out of
// time: fade out at once, then sub 0xB and message 7; success: gear state 3 -> 4, radio 0x11, fade
// at 180, then sub 0xB and message 7. The replay HUD shows the timer.
// fpucheck: 1 add
void func_0017AC68() {
    const u32 u = Unit(0);
    const u32 ac = W(u + 0x44);
    MainContext* ctx = Ctx();
    if (ctx->sub == 0x19) {
        if (Frames() == 0 && Fade() != 0.0f) func_0010A130(0.0f, 0.5f, 0);
        if (Frames() == 2) func_0017B1E0(0x10);
        if (!(Frames() < 2) && StartHeld()) {
            GRef<u32>(Status() + 0x38) = 0;
            RequestSubState(0x1B);
        }
        if (!(Frames() < 0xB4)) RequestSubState(0x1A);
    }
    if (ctx->sub == 0x1A) {
        if (StartHeld()) {
            GRef<u32>(Status() + 0x38) = 0;
            RequestSubState(0x1B);
        }
        if (W(Status() + 0x38) != 0) GRef<u32>(Status() + 0x38) = W(Status() + 0x38) - 1;
        if (Frames() == 0) {
            GRef<u32>(Scene() + 0x1668) |= 0x100u;
            func_0010D748(Scene(), ac + 0x30);
            ShowCenterMessage(0, 3, 0);
        }
        if ((~W(ac + 0x188) & 0x20000000u) == 0) {
            GRef<u32>(ac + 0x27C) = 0;
        } else {
            const f32 v = fpu::Add(F(ac + 0x27C), F(0x0040EC14));
            GRef<f32>(ac + 0x27C) = v;
            if (5.0f < v) RequestSubState(0x1B);
        }
        if (F(ac + 0x130) == 0.0f) {
            if (Frames() == 0x258) func_0017B1E0(0x12);
            if (Frames() == 0x4B0) func_0017B1E0(0x13);
        }
        if ((~W(u + 0x4C) & 0x1000000u) != 0) RequestSubState(0x1B);
        if (W(Status() + 0x38) == 0) RequestSubState(0x1B);
    }
    if (ctx->sub == 0x1B) {
        if (Frames() == 0) {
            func_0017D848(W(0x00410B40), 0xFF, 0);
            if (Fade() != 0.0f) func_0010A130(0.0f, 0.5f, 0);
        }
        const u32 f = W(u + 0x4C);
        if ((f & 0x200000u) != 0) {
            RequestSubState(0x1B);
            return;
        }
        if ((~f & 0x1000000u) != 0) {
            if (Frames() == 0) ShowCenterMessage(0, 4, 0);
            if (Frames() == 0xB4) func_0010A130(1.0f, 2.0f, 0);
            if (Fade() == 1.0f) {
                func_0017A980();
                RequestSubState(0x19);
            }
        } else if (W(Status() + 0x38) == 0) {
            if (Frames() == 0) func_0010A130(1.0f, 2.0f, 0);
            if (Fade() == 1.0f) {
                RequestSubState(0xB);
                ShowCenterMessage(0, 7, 0);
            }
        } else {
            if (GRef<u8>(ac + 0x280) == 3) GRef<u8>(ac + 0x280) = 4;
            if (Frames() == 0) func_0017B1E0(0x11);
            if (Frames() == 0xB4) func_0010A130(1.0f, 2.0f, 0);
            if (Fade() == 1.0f) {
                RequestSubState(0xB);
                ShowCenterMessage(0, 7, 0);
            }
        }
    }
    func_0017D838(W(0x00410B40), W(Scene() + 0x1660));
}

// EE: 0x0011D888
// Clears every unit's target references (+0x7C, +0x80, +0x84 = -1); for units with +0x54 == 0 (aircraft)
// also the aircraft lock tables: +0x294..+0x2B0 (8 words) = -1, and for k = 0, 1: +0x2C4 / +0x2B4 /
// +0x2CC = -1, +0x2BC = 0, byte +0x289 = 0.
void func_0011D888() {
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i);
        const u8 kind = GRef<u8>(u + 0x54);
        GRef<s32>(u + 0x7C) = -1;
        GRef<s32>(u + 0x84) = -1;
        GRef<s32>(u + 0x80) = -1;
        if (kind != 0) continue;
        const u32 ac = W(u + 0x44);
        for (s32 k = 7; k >= 0; --k) GRef<s32>(ac + 0x294 + static_cast<u32>(k) * 4) = -1;
        for (u32 k = 0; k < 2; ++k) {
            GRef<s32>(ac + 0x2C4 + k * 4) = -1;
            GRef<s32>(ac + 0x2B4 + k * 4) = -1;
            GRef<s32>(ac + 0x2CC + k * 4) = -1;
            GRef<u32>(ac + 0x2BC + k * 4) = 0;
            GRef<u8>(ac + 0x289 + k) = 0;
        }
    }
}

// EE: 0x0011E5A8
// Explosion effect id of a unit kind: table 0x002A1F10[kind], or for kind 0xFF 0x81 (class < 2) /
// 0xB5.
u32 func_0011E5A8(u32 kind, u32 cls) {
    if (kind == 0xFF) return cls < 2 ? 0x81u : 0xB5u;
    return W(0x002A1F10 + kind * 4);
}

// EE: 0x001211B0
// Mid-air start of unit i: gear up at once (+0x280 = 3, +0x14B = 0xB4, gear channels +0x260..+0x268
// = 0.5 and +0x26C..+0x274 = 0, model channels 0x2B-0x2D set, 0x00148308), then the ground clearance
// from the three gear points (0x00149EC8 for joints 0x1B-0x1D applied to (0, 0, 0, 1)): p1.y + (p0.y -
// p1.y) x (0 - p1.z) / (p0.z - p1.z); +0x281 |= 7, +0x188 |= 0x20000000.
// fpucheck: 3 sub, 1 add
f32 func_001211B0(s32 i) {
    guest::GuestFrame frame(0x70);
    const u32 m = frame.Address(), pts = m + 0x40;
    const u32 ac = W(Unit(i) + 0x44);
    GRef<u8>(ac + 0x280) = 3;
    GRef<u8>(ac + 0x14B) = 0xB4;
    GRef<f32>(ac + 0x268) = 0.5f;
    GRef<f32>(ac + 0x260) = 0.5f;
    GRef<f32>(ac + 0x264) = 0.5f;
    GRef<u32>(ac + 0x26C) = 0;
    GRef<u32>(ac + 0x270) = 0;
    GRef<u32>(ac + 0x274) = 0;
    for (s32 k = 0; k < 3; ++k) func_00149C40(F(ac + 0x260 + static_cast<u32>(k) * 4), AircraftModels(), i, 0x2B + k);
    func_00148308(AircraftModels(), 0);
    for (s32 k = 0; k < 3; ++k) {
        const u32 p = pts + static_cast<u32>(k) * 0x10;
        StoreVf0(p);
        func_00149EC8(m, 0, 0x1B + k, ac);
        sceVu0ApplyMatrix(p, m, p);
    }
    const f32 y1 = F(pts + 0x14), z1 = F(pts + 0x18);
    const f32 dy = fpu::Sub(F(pts + 4), y1);
    const f32 nz = fpu::Sub(0.0f, z1);
    const f32 dz = fpu::Sub(F(pts + 8), z1);
    GRef<u8>(ac + 0x281) = static_cast<u8>(GRef<u8>(ac + 0x281) | 7);
    GRef<u32>(ac + 0x188) = W(ac + 0x188) | 0x20000000u;
    return fpu::Add(dy * nz / dz, y1);
}

// EE: 0x00135E10
// Replay start snapshot of every unit into its record 0x002A2660[i]: position (+0 -> 0), rotation
// (+0x10 -> 0xC), velocity (+0x20 -> 0x18), flags +0x48/+0x4C/+0x50 -> 0x24, +0x56 -> 0x30, +0x5A ->
// 0x32, target references +0x7C/+0x80/+0x84 -> 0x34, HP +0x70/+0x74 -> 0x40.
void func_00135E10() {
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 u = Unit(i), s = W(0x002A2660 + static_cast<u32>(i) * 4);
        GRef<f32>(s) = F(u);
        GRef<u32>(s + 0x24) = W(u + 0x48);
        GRef<f32>(s + 4) = F(u + 4);
        GRef<u32>(s + 0x28) = W(u + 0x4C);
        GRef<f32>(s + 8) = F(u + 8);
        GRef<u32>(s + 0x2C) = W(u + 0x50);
        GRef<f32>(s + 0xC) = F(u + 0x10);
        GRef<f32>(s + 0x10) = F(u + 0x14);
        GRef<f32>(s + 0x14) = F(u + 0x18);
        GRef<f32>(s + 0x18) = F(u + 0x20);
        GRef<f32>(s + 0x1C) = F(u + 0x24);
        GRef<f32>(s + 0x20) = F(u + 0x28);
        GRef<u8>(s + 0x32) = GRef<u8>(u + 0x5A);
        GRef<u32>(s + 0x34) = W(u + 0x7C);
        GRef<u16>(s + 0x30) = GRef<u16>(u + 0x56);
        GRef<u32>(s + 0x38) = W(u + 0x80);
        GRef<f32>(s + 0x40) = F(u + 0x70);
        GRef<u32>(s + 0x3C) = W(u + 0x84);
        GRef<f32>(s + 0x44) = F(u + 0x74);
    }
}

// EE: 0x00135EF8
// Restores every unit from its replay start snapshot (the reverse of 0x00135E10).
void func_00135EF8() {
    for (s32 i = 0; i < UnitCount(); ++i) {
        const u32 s = W(0x002A2660 + static_cast<u32>(i) * 4), u = Unit(i);
        GRef<f32>(u) = F(s);
        GRef<u32>(u + 0x48) = W(s + 0x24);
        GRef<f32>(u + 4) = F(s + 4);
        GRef<u32>(u + 0x4C) = W(s + 0x28);
        GRef<f32>(u + 8) = F(s + 8);
        GRef<u32>(u + 0x50) = W(s + 0x2C);
        GRef<f32>(u + 0x10) = F(s + 0xC);
        GRef<f32>(u + 0x14) = F(s + 0x10);
        GRef<f32>(u + 0x18) = F(s + 0x14);
        GRef<f32>(u + 0x20) = F(s + 0x18);
        GRef<f32>(u + 0x24) = F(s + 0x1C);
        GRef<f32>(u + 0x28) = F(s + 0x20);
        GRef<u8>(u + 0x5A) = GRef<u8>(s + 0x32);
        GRef<u32>(u + 0x7C) = W(s + 0x34);
        GRef<u16>(u + 0x56) = GRef<u16>(s + 0x30);
        GRef<u32>(u + 0x80) = W(s + 0x38);
        GRef<f32>(u + 0x70) = F(s + 0x40);
        GRef<u32>(u + 0x84) = W(s + 0x3C);
        GRef<f32>(u + 0x74) = F(s + 0x44);
    }
}

namespace {

// Aircraft animation inputs stored as u8 x 255 in the player snapshot (offset in the aircraft,
// byte in the snapshot), in the original order.
struct Channel {
    u32 ac;
    u32 snap;
};
constexpr Channel kInputs[8] = {{0x240, 4}, {0x250, 5}, {0x258, 6}, {0x254, 7},
                                {0x248, 8}, {0x24C, 9}, {0x25C, 0xA}, {0x244, 0xB}};
constexpr Channel kGear[3] = {{0x260, 0xD}, {0x264, 0xE}, {0x268, 0xF}};

}  // namespace

// EE: 0x00135FE0
// Replay start snapshot of every player aircraft into its record *0x00410678[p]: view mode +0x149,
// the animation inputs and gear channels as (s32)(v x 255) bytes, gear state +0x280, the lock
// tables +0x294..+0x2B3 -> 0x10, and for k = 0, 1: +0x2C4 -> 0x40, +0x2B4 -> 0x30, +0x2CC -> 0x48,
// +0x2BC -> 0x38, byte +0x289 -> 1.
void func_00135FE0() {
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
        const u32 s = W(0x00410678 + static_cast<u32>(p) * 4);
        const u32 ac = W(Unit(p) + 0x44);
        GRef<u8>(s) = GRef<u8>(ac + 0x149);
        for (const Channel& c : kInputs) GRef<u8>(s + c.snap) = static_cast<u8>(fpu::FloatToInt(F(ac + c.ac) * 255.0f));
        GRef<u8>(s + 0xC) = GRef<u8>(ac + 0x280);
        for (const Channel& c : kGear) GRef<u8>(s + c.snap) = static_cast<u8>(fpu::FloatToInt(F(ac + c.ac) * 255.0f));
        for (s32 k = 7; k >= 0; --k) {
            const u32 o = static_cast<u32>(7 - k) * 4;
            GRef<u32>(s + 0x10 + o) = W(ac + 0x294 + o);
        }
        for (u32 k = 0; k < 2; ++k) {
            GRef<u32>(s + 0x40 + k * 4) = W(ac + 0x2C4 + k * 4);
            GRef<u32>(s + 0x30 + k * 4) = W(ac + 0x2B4 + k * 4);
            GRef<u32>(s + 0x48 + k * 4) = W(ac + 0x2CC + k * 4);
            GRef<u32>(s + 0x38 + k * 4) = W(ac + 0x2BC + k * 4);
            GRef<u8>(s + 1 + k) = GRef<u8>(ac + 0x289 + k);
        }
    }
}

// EE: 0x00136240
// Restores every player aircraft from its replay start snapshot: the inputs as byte / 255, the
// moving parts re-run (UpdateAircraftMovingParts(p, 1)) before the gear state and channels are
// restored, then the lock tables (the reverse of 0x00135FE0).
void func_00136240() {
    for (s32 p = 0; p < static_cast<s32>(Game(0x25)); ++p) {
        const u32 s = W(0x00410678 + static_cast<u32>(p) * 4);
        const u32 ac = W(Unit(p) + 0x44);
        GRef<u8>(ac + 0x149) = GRef<u8>(s);
        for (const Channel& c : kInputs) GRef<f32>(ac + c.ac) = fpu::IntToFloat(GRef<u8>(s + c.snap)) / 255.0f;
        UpdateAircraftMovingParts(p, 1);
        GRef<u8>(ac + 0x280) = GRef<u8>(s + 0xC);
        for (const Channel& c : kGear) GRef<f32>(ac + c.ac) = fpu::IntToFloat(GRef<u8>(s + c.snap)) / 255.0f;
        for (u32 o = 0; o < 0x20; o += 4) GRef<u32>(ac + 0x294 + o) = W(s + 0x10 + o);
        for (u32 k = 0; k < 2; ++k) {
            GRef<u32>(ac + 0x2C4 + k * 4) = W(s + 0x40 + k * 4);
            GRef<u32>(ac + 0x2B4 + k * 4) = W(s + 0x30 + k * 4);
            GRef<u32>(ac + 0x2CC + k * 4) = W(s + 0x48 + k * 4);
            GRef<u32>(ac + 0x2BC + k * 4) = W(s + 0x38 + k * 4);
            GRef<u8>(ac + 0x289 + k) = GRef<u8>(s + 1 + k);
        }
    }
}

// EE: 0x0024CDD0
// In-flight result recording, each simulation step of subs 0xA / 0xB (result record R =
// *0x00411578, +0xE tracked unit count, +8 per-unit records of 0xD0 bytes, +0xF mode, +0xC last
// minute; status S): for every tracked unit destroyed this frame (+0x4C bit 0x200000; units >= 2
// only when +0x64), up to 16 entries (count rec+5) of 0xC bytes at rec+0x10: time S+0x28, position
// / 8 as s16 (x +4, y +6, z +8), cause +0xA (u+0x63: 0 -> 1, 1 -> 2, else 3; in mode 2 unit 0 -> 2,
// unit 1 -> 1). Then once a minute (S+0x28 % 60 == 0 and a new minute): a block in the stream
// *0x00411584 (block start -> +4): {1, minute (u16)}, per tracked active unit (+0x4C bits 0x10000000
// and 0x2000, +0x64): kinds 2 / 3 a 4-byte record {0x11, -, index, landed (bit 0x1000000)}, others a
// 0x10-byte record {0x10, -, index, rotation x / y / z x K 0x00410450 (bytes), position / 8 (s16 x3),
// velocity direction x 127 (bytes), flag}; a 0 terminator; the stream pointer moves 2 bytes past it.
void func_0024CDD0() {
    u32 R = W(0x00411578);
    for (s32 i = 0; i < GRef<u8>(R + 0xE); ++i, R = W(0x00411578)) {
        const u32 u = Unit(i);
        const u32 rec = W(R + 8) + static_cast<u32>(i) * 0xD0;
        if ((W(u + 0x4C) & 0x200000u) == 0) continue;
        if (!(i < 2) && GRef<u8>(u + 0x64) == 0) continue;
        if (!(GRef<u8>(rec + 5) < 0x10)) continue;
        const u32 e = rec + 0x10 + static_cast<u32>(GRef<u8>(rec + 5)) * 0xC;
        GRef<u32>(e) = W(Status() + 0x28);
        const u8 cause = GRef<u8>(u + 0x63);
        GRef<u8>(e + 0xA) = cause == 0 ? 1 : cause == 1 ? 2 : 3;
        if (GRef<u8>(W(0x00411578) + 0xF) == 2) {
            if (i == 0) GRef<u8>(e + 0xA) = 2;
            else if (i == 1) GRef<u8>(e + 0xA) = 1;
        }
        GRef<s16>(e + 8) = static_cast<s16>(fpu::FloatToInt(F(u + 8) * 0.125f));
        GRef<s16>(e + 4) = static_cast<s16>(fpu::FloatToInt(F(u) * 0.125f));
        GRef<s16>(e + 6) = static_cast<s16>(fpu::FloatToInt(F(u + 4) * 0.125f));
        GRef<u8>(rec + 5) = static_cast<u8>(GRef<u8>(rec + 5) + 1);
    }
    const u32 frame = W(Status() + 0x28);
    if (frame % 60 != 0) return;
    if (GRef<s16>(R + 0xC) == static_cast<s32>(frame) / 60) return;
    GRef<u16>(R + 0xC) = static_cast<u16>(frame / 60);
    const u32 P = W(0x00411584);
    u32 s = W(P);
    GRef<u32>(P + 4) = s;
    GRef<u8>(s) = 1;
    GRef<u16>(s + 2) = static_cast<u16>(W(Status() + 0x28) / 60);
    s += 4;
    u32 s1 = s + 2;
    for (s32 i = 0; i < GRef<u8>(W(0x00411578) + 0xE); ++i) {
        const u32 u = Unit(i);
        const u32 flags = W(u + 0x4C);
        if ((~flags & 0x10000000u) != 0 || (~flags & 0x2000u) != 0 || GRef<u8>(u + 0x64) == 0) continue;
        if (static_cast<u32>(GRef<u8>(u + 0x6A) - 2) < 2) {
            GRef<u8>(s + 2) = static_cast<u8>(i);
            GRef<u8>(s) = 0x11;
            GRef<u8>(s1 + 1) = 0;
            if ((W(u + 0x4C) & 0x1000000u) != 0) GRef<u8>(s1 + 1) = 1;
            s += 4;
            s1 = s + 2;
            continue;
        }
        GRef<u8>(s + 2) = static_cast<u8>(i);
        GRef<u8>(s) = 0x10;
        const f32 k = F(0x00410450);
        GRef<u8>(s1 + 1) = static_cast<u8>(fpu::FloatToInt(F(u + 0x10) * k));
        GRef<s16>(s1 + 8) = static_cast<s16>(fpu::FloatToInt(F(u + 8) * 0.125f));
        GRef<s16>(s1 + 4) = static_cast<s16>(fpu::FloatToInt(F(u) * 0.125f));
        GRef<s16>(s1 + 6) = static_cast<s16>(fpu::FloatToInt(F(u + 4) * 0.125f));
        GRef<u8>(s1 + 2) = static_cast<u8>(fpu::FloatToInt(F(u + 0x14) * k));
        GRef<u8>(s1 + 0xD) = 0;
        GRef<u8>(s1 + 3) = static_cast<u8>(fpu::FloatToInt(F(u + 0x18) * k));
        if ((W(u + 0x4C) & 0x1000000u) != 0) GRef<u8>(s1 + 0xD) = 1;
        s += 0x10;
        guest::GuestFrame vf(0x10);
        const u32 v = vf.Address();
        const f32 len = GetVectorLength(u + 0x20);
        sceVu0ScaleVector(v, u + 0x20, 1.0f / len * 127.0f);
        GRef<u8>(s1 + 0xA) = static_cast<u8>(fpu::FloatToInt(F(v)));
        GRef<u8>(s1 + 0xB) = static_cast<u8>(fpu::FloatToInt(F(v + 4)));
        GRef<u8>(s1 + 0xC) = static_cast<u8>(fpu::FloatToInt(F(v + 8)));
        s1 = s + 2;
    }
    GRef<u8>(s) = 0;
    GRef<u32>(W(0x00411584)) = s1;
}

// EE: 0x0024D1C8
// Closes the flight record of the watched player (unit 1 when game[0x2D] == 1, else unit 0): ten
// 0x16-byte blocks in the stream *0x00411584, each {1, minute + 1 + i (u16), 0x10, -, unit, rotation
// x / y / z x K 0x00410454 (bytes), position / 8 (s16 x3), velocity direction x 127 (bytes), flag
// (bit 0x1000000), 0} (R+0xC advanced per block), an 0xFF terminator (the pointer 2 bytes past);
// R+0x18 |= 1 when status flag 4; R+0 / +0x10 / +0x14 = status +0x28 / +0x2C / +0x30, R+0x1C..+0x34 =
// rules +0x18 / +0x40 / +0x3C / +0x38 / +0x50 / +0x4C / +0x48.
void func_0024D1C8() {
    const u32 p = Game(0x2D) == 1 ? 1u : 0u;
    const u32 u = Unit(static_cast<s32>(p));
    u32 s = W(W(0x00411584) + 0);
    const f32 k = F(0x00410454);
    for (s32 i = 0; i < 10; ++i) {
        const u32 b = s;
        GRef<u8>(b) = 1;
        s += 4;
        const u32 minute = W(Status() + 0x28) / 60;
        GRef<u8>(b + 4) = 0x10;
        GRef<u8>(b + 6) = static_cast<u8>(p);
        GRef<u8>(b + 7) = static_cast<u8>(fpu::FloatToInt(F(u + 0x10) * k));
        GRef<s16>(b + 0xA) = static_cast<s16>(fpu::FloatToInt(F(u) * 0.125f));
        GRef<s16>(b + 0xC) = static_cast<s16>(fpu::FloatToInt(F(u + 4) * 0.125f));
        GRef<s16>(b + 0xE) = static_cast<s16>(fpu::FloatToInt(F(u + 8) * 0.125f));
        GRef<u8>(b + 8) = static_cast<u8>(fpu::FloatToInt(F(u + 0x14) * k));
        GRef<u8>(b + 0x13) = 0;
        GRef<u8>(b + 9) = static_cast<u8>(fpu::FloatToInt(F(u + 0x18) * k));
        GRef<u16>(b + 2) = static_cast<u16>(minute + 1 + static_cast<u32>(i));
        if ((W(u + 0x4C) & 0x1000000u) != 0) GRef<u8>(b + 0x13) = 1;
        const f32 len = GetVectorLength(u + 0x20);
        s += 0x12;
        guest::GuestFrame vf(0x10);
        const u32 v = vf.Address();
        sceVu0ScaleVector(v, u + 0x20, 1.0f / len * 127.0f);
        GRef<u8>(b + 0x10) = static_cast<u8>(fpu::FloatToInt(F(v)));
        GRef<u8>(b + 0x11) = static_cast<u8>(fpu::FloatToInt(F(v + 4)));
        GRef<u8>(b + 0x12) = static_cast<u8>(fpu::FloatToInt(F(v + 8)));
        GRef<u8>(b + 0x14) = 0;
        const u32 R = W(0x00411578);
        GRef<u16>(R + 0xC) = static_cast<u16>(GRef<u16>(R + 0xC) + 1);
    }
    GRef<u8>(s) = 0xFF;
    s += 2;
    GRef<u32>(W(0x00411584)) = s;
    if ((StatusFlags() & 4u) != 0) GRef<u8>(W(0x00411578) + 0x18) = static_cast<u8>(GRef<u8>(W(0x00411578) + 0x18) | 1);
    const u32 R = W(0x00411578), st = Status(), ru = Rules();
    GRef<u32>(R) = W(st + 0x28);
    GRef<u32>(R + 0x10) = W(st + 0x2C);
    GRef<u32>(R + 0x14) = W(st + 0x30);
    GRef<u32>(R + 0x1C) = W(ru + 0x18);
    GRef<u32>(R + 0x20) = W(ru + 0x40);
    GRef<u32>(R + 0x24) = W(ru + 0x3C);
    GRef<u32>(R + 0x28) = W(ru + 0x38);
    GRef<u32>(R + 0x2C) = W(ru + 0x50);
    GRef<u32>(R + 0x30) = W(ru + 0x4C);
    GRef<u32>(R + 0x34) = W(ru + 0x48);
}

}  // namespace ac04::game
