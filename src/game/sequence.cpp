// In-engine sequences (slice S5i; docs/subsystems/mission-flow.md "In-engine sequences",
// rendering.md "Sequence character engine"): the sequence data (cuts, character models, animated
// sprites, still images), the per-frame keyframe player UpdateInEngineSequence and its camera, the
// sequence draw passes, the mid-mission scripted sequences of scene 2 subs 0xD-0xF and the still-
// image / animated-sprite overlays.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "game/flight_common.h"
#include "platform/log.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;
using namespace fl;

namespace {

u32 Eff() { return W(0x004106B4); }  // effect block
u32 S() { return g_FlightScene(); }   // flight scene object

// Marker for a packet word the original leaves as it is in the buffer (outside the u32 range, so no
// written value collides with it).
constexpr u64 kSeqKeep = 0x100000000ull;

void SeqCopyQ(u32 dst, u32 src) { CopyQ(dst, src); }

}  // namespace

// EE: 0x00106938
// Loads the still images of a sequence (part 3 of the sequence data, `data`: count, then offsets):
// `count` GIM texture records (0x28 bytes each, work heap, alignment 16) at set+0, each image placed
// at TBP 0x2500 and its CLUT at CBP 0x3E00 (CLUT record byte +2 = 1); the count at set+0x2808.
void func_00106938(u32 set, u32 data) {
    GRef<u32>(set + 0x2808) = W(data);
    GRef<u32>(set) = WorkHeapAlloc(W(data) * 0x28, 0x10);
    for (s32 i = 0; i < GRef<s32>(set + 0x2808); ++i) {
        const u32 tex = W(set) + static_cast<u32>(i) * 0x28;
        InitGimTexture(tex, data + W(data + 4 + static_cast<u32>(i) * 4));
        GRef<u16>(W(tex + 4)) = 0x2500;
        GRef<u8>(W(tex + 8) + 2) = 1;
        GRef<u16>(W(tex + 8)) = 0x3E00;
    }
}

// EE: 0x00106A20
// Clears the flags of the 256 still-image overlay slots (0x28 bytes from set+8) and forgets the
// last uploaded image (+0x280C = -1).
void func_00106A20(u32 set) {
    for (u32 k = 0; k < 0x100; ++k) GRef<u32>(set + 0x27E0 - k * 0x28) = 0;
    GRef<s32>(set + 0x280C) = -1;
}

// EE: 0x00106A50
// Shows still image `image` in overlay slot `slot` (both reset to 0 when out of range): colour
// `rgba`, centre (x, y) as GS sub-pixel coordinates ((v + 0x800) x 16), half sizes (image size x
// scale / 2, from the 16-bit integer parts), rotation `angle`; Q 1.0, flag bit 0 set.
void func_00106A50(u32 set, s32 slot, s32 image, u32 rgba, s32 x, s32 y, f32 sx, f32 sy, f32 angle) {
    if (!(slot < 0x100)) slot = 0;
    if (!(image < GRef<s32>(set + 0x2808))) image = 0;
    const u32 s = set + static_cast<u32>(slot) * 0x28 + 8;
    const u32 img = W(W(set) + static_cast<u32>(image) * 0x28 + 4);
    GRef<s32>(s + 0x24) = image;
    const f32 h = fpu::IntToFloat(GRef<u16>(img + 0xE)) * sy;
    const f32 w = fpu::IntToFloat(GRef<u16>(img + 0xC)) * sx;
    GRef<u32>(s + 8) = rgba;
    GRef<f32>(s + 0x20) = angle;
    GRef<u32>(s) = W(s) | 1;
    GRef<f32>(s + 0xC) = 1.0f;
    GRef<s32>(s + 0x10) = (static_cast<s16>(x) + 0x800) << 4;
    GRef<s32>(s + 0x14) = (static_cast<s16>(y) + 0x800) << 4;
    GRef<s32>(s + 0x18) = static_cast<s16>(fpu::FloatToInt(w)) / 2;
    GRef<s32>(s + 0x1C) = static_cast<s16>(fpu::FloatToInt(h)) / 2;
}

// EE: 0x00106B58
// Draws the active still-image overlays (slots 0..255) into bucket `bucket`: the four corners
// (+-half w, +-half h) rotated by -angle about z (VU0 matrix), the image's texture uploaded when it
// changes (+0x280C), then TEST / ZBUF (ZMSK) / ALPHA 0x44 / TEX1 / CLAMP_1 region, TEX0, and a
// 4-vertex textured strip (PRIM 0x154) in the slot colour; TEST 0x50000 and the main ZBUF restored.
void func_00106B58(u32 set, u32 list, s32 bucket) {
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address();
    const u32 corners = sp, ints = sp + 0x40, rot = sp + 0x80;
    u32 s = set + 8;
    for (s32 k = 0xFF; k >= 0; --k, s += 0x28) {
        if (((W(s) ^ 1) & 1) != 0) continue;
        const s32 hw = GRef<s32>(s + 0x18), hh = GRef<s32>(s + 0x1C);
        const f32 phw = fpu::IntToFloat(hw), nhw = fpu::IntToFloat(-hw), nhh = fpu::IntToFloat(-hh), phh = fpu::IntToFloat(hh);
        const u32 tex = W(set) + static_cast<u32>(GRef<s32>(s + 0x24)) * 0x28;
        const auto vec = [&](u32 k4, f32 x, f32 y) {
            GRef<f32>(corners + k4 * 0x10) = x;
            GRef<f32>(corners + k4 * 0x10 + 4) = y;
            GRef<u32>(corners + k4 * 0x10 + 8) = 0;
            GRef<f32>(corners + k4 * 0x10 + 0xC) = 1.0f;
        };
        vec(0, nhw, nhh);
        vec(1, phw, nhh);
        vec(2, nhw, phh);
        vec(3, phw, phh);
        sceVu0UnitMatrix(rot);
        sceVu0RotMatrixZ(rot, rot, -GRef<f32>(s + 0x20));
        for (u32 c = 0; c < 4; ++c) sceVu0ApplyMatrix(corners + c * 0x10, rot, corners + c * 0x10);
        for (u32 c = 0; c < 4; ++c) sceVu0FTOI4Vector(ints + c * 0x10, corners + c * 0x10);
        u32 p = BeginPacket();
        if (GRef<s32>(set + 0x280C) != GRef<s32>(s + 0x24)) {
            p = BuildTextureUpload(tex, p, 1, 0);
            GRef<s32>(set + 0x280C) = GRef<s32>(s + 0x24);
        }
        const u32 img = W(tex + 4);
        const u32 ctx = W(0x004104A8);
        const auto q = [&](u64 w0, u64 w1, u64 w2, u64 w3) {
            if (w0 != kSeqKeep) GRef<u32>(p) = static_cast<u32>(w0);
            if (w1 != kSeqKeep) GRef<u32>(p + 4) = static_cast<u32>(w1);
            if (w2 != kSeqKeep) GRef<u32>(p + 8) = static_cast<u32>(w2);
            if (w3 != kSeqKeep) GRef<u32>(p + 0xC) = static_cast<u32>(w3);
            p += 0x10;
        };
        q(0x10000006, 0, 0, 0x50000006);
        q(5, 0x10000000, 0xE, kSeqKeep);
        q(0x30003, kSeqKeep, 0x47, kSeqKeep);
        SeqCopyQ(p, ctx + 0xE0);
        GRef<u32>(p + 4) = 1;
        p += 0x10;
        q(0x44, 0, 0x42, 0);
        q(0x60, 0, 0x14, kSeqKeep);
        q((static_cast<u32>(GRef<u16>(img + 0xC) - 1) << 14) | 0xA, static_cast<u32>(GRef<u16>(img + 0xE) - 1) << 2, 8, kSeqKeep);
        q(0x10000002, 0, 0, 0x50000002);
        q(0x8001, 0x14000000, 0xF6, kSeqKeep);
        {
            const u32 clut = W(tex + 8);
            GRef<u32>(p) = static_cast<u32>(GRef<u16>(img)) | 0x80000000u | (static_cast<u32>(GRef<u8>(img + 3)) << 20) |
                           (static_cast<u32>(GRef<u8>(img + 2)) << 14) | 0x28000000u;
            if (clut == 0) {
                GRef<u32>(p + 4) = 6;
            } else {
                GRef<u32>(p + 4) = (static_cast<u32>(GRef<u16>(clut)) << 5) | 0x20000000u |
                                   (static_cast<u32>(GRef<u8>(clut + 2)) << 19) | 6u |
                                   (static_cast<u32>(GRef<u8>(clut + 3)) << 23);
            }
            p += 0x10;  // the second half of the REGLIST quadword is left as it is
        }
        q(0x10000007, 0, 0, 0x50000007);
        q(1, 0x24000000, 0x10, kSeqKeep);
        GRef<u64>(p) = 0x154;
        GRef<u64>(p + 8) = GRef<u64>(s + 8);
        p += 0x10;
        const u32 w16 = static_cast<u32>(GRef<u16>(img + 0xC)) << 4, h16 = static_cast<u32>(GRef<u16>(img + 0xE)) << 4;
        q(0x8004, 0x24000000, 0x43, kSeqKeep);
        const auto xy = [&](u32 c) {
            GRef<u32>(p) = (W(s + 0x10) + W(ints + c * 0x10)) | ((W(s + 0x14) + W(ints + c * 0x10 + 4)) << 16);
            GRef<u32>(p + 4) = 0;
            p += 8;
        };
        const auto uv = [&](u32 v) {
            GRef<u32>(p) = v;  // the upper word of UV is left as it is
            p += 8;
        };
        uv(0x80008);
        xy(0);
        uv(w16 | 0x80008u);
        xy(1);
        uv(((h16 + 8) << 16) | 8u);
        xy(2);
        uv((w16 + 8) | ((h16 + 8) << 16));
        xy(3);
        q(0x10000003, 0, 0x11000000, 0x50000003);
        q(0x8002, 0x10000000, 0xE, kSeqKeep);
        q(0x50000, kSeqKeep, 0x47, kSeqKeep);
        SeqCopyQ(p, ctx + 0xE0);
        EndPacket(list, bucket, p + 0x10);
    }
}

// EE: 0x00107168
// Parses a 'GMA' animated sprite set at `gma` into the 0x40-byte record `spr`: +0 header, +4 image
// (header +8), +8 frame table (header +0xC); returns 0 unless the header starts with "GMA"
// (0x004104C0). Then +0xC = a work-heap table of the frame pointers (image +0x10 + i x image +4,
// count at header +4) and +0x10 = frame table +0x10.
s32 func_00107168(u32 spr, u32 gma) {
    GRef<u32>(spr) = gma;
    GRef<u32>(spr + 4) = gma + W(gma + 8);
    GRef<u32>(spr + 8) = gma + W(gma + 0xC);
    if (ee::strcmp(gma, 0x004104C0) != 0) return 0;
    const u32 frames = WorkHeapAlloc(W(W(spr) + 4) << 2, 4);
    GRef<u32>(spr + 0xC) = frames;
    const s32 n = GRef<s32>(W(spr) + 4);
    for (s32 i = 0; i < n; ++i) {
        const u32 img = W(spr + 4);
        GRef<u32>(frames + static_cast<u32>(i) * 4) = img + static_cast<u32>(i) * W(img + 4) + 0x10;
    }
    GRef<u32>(spr + 0x10) = W(spr + 8) + 0x10;
    return 1;
}

// EE: 0x00107BD0
// Loads the animated sprite sets of a sequence (part 2: count, then offsets): 0x40-byte records
// (work heap, alignment 16) at set+0, count at set+4; each parsed by 0x00107168 and placed at
// TBP 0x2500 / CBP 0x2600.
void func_00107BD0(u32 set, u32 data) {
    GRef<u32>(set + 4) = W(data);
    GRef<u32>(set) = WorkHeapAlloc(W(data) << 6, 0x10);
    for (s32 i = 0; i < GRef<s32>(set + 4); ++i) {
        const u32 rec = W(set) + (static_cast<u32>(i) << 6);
        func_00107168(rec, data + W(data + 4 + static_cast<u32>(i) * 4));
        GRef<u16>(W(rec + 4)) = 0x2500;
        GRef<u16>(W(rec + 8)) = 0x2600;
    }
}

// EE: 0x00107C90
// Clears the flags of the 32 animated sprite slots (0x30 bytes from set+8).
void func_00107C90(u32 set) {
    for (u32 k = 0; k < 0x20; ++k) GRef<u32>(set + 0x5D8 - k * 0x30) = 0;
}

// EE: 0x00107CC0
// Shows animated sprite set `sprite` in slot `slot` (both reset to 0 when out of range): colour,
// centre ((v + 0x800) x 16), frame/time value `frame` (+0x20), half sizes from the set image's
// signed 16-bit width / height x scale / 2, rotation `angle` (+0x24); Q 1.0, flag bit 0 set.
void func_00107CC0(u32 set, s32 slot, s32 sprite, u32 rgba, s32 x, s32 y, u32 frame, f32 sx, f32 sy, f32 angle) {
    if (!(slot < 0x20)) slot = 0;
    if (!(sprite < GRef<s32>(set + 4))) sprite = 0;
    const u32 s = set + static_cast<u32>(slot) * 0x30 + 8;
    GRef<u32>(s) = W(s) | 1;
    GRef<u32>(s + 8) = rgba;
    GRef<f32>(s + 0xC) = 1.0f;
    GRef<s32>(s + 0x28) = sprite;
    GRef<s32>(s + 0x10) = (static_cast<s16>(x) + 0x800) << 4;
    GRef<s32>(s + 0x14) = (static_cast<s16>(y) + 0x800) << 4;
    const u32 img = W(W(set) + (static_cast<u32>(sprite) << 6) + 4);
    GRef<s32>(s + 0x18) = static_cast<s16>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(img + 0xC)) * sx)) / 2;
    GRef<f32>(s + 0x24) = angle;
    GRef<u32>(s + 0x20) = frame;
    GRef<s32>(s + 0x1C) = static_cast<s16>(fpu::FloatToInt(fpu::IntToFloat(GRef<s16>(img + 0xE)) * sy)) / 2;
}

// EE: 0x00107310
// Uploads frame `time` mod frame count of an animated sprite set into bucket `bucket`: for the image
// (frame data from the frame table) and then the CLUT, an A+D block (BITBLTBUF from the header's
// signed 16/8-bit fields, TRXPOS 0, TRXREG w / h, TRXDIR 0), an IMAGE GIF tag of ceil(size / 16)
// qwords and a DMA ref to the data with DIRECT; then TEXFLUSH. Unwritten words are kept.
void func_00107310(u32 spr, u32 list, s32 bucket, s32 time) {
    u32 p = BeginPacket();
    const s32 frames = GRef<s32>(W(spr) + 4);
    if (frames == 0) {
        // HYPOTHESIS: unreachable (a parsed set has frames); the original traps here.
        log::WriteOnce("seq:00107310-div0", log::Level::Warning, "game", "func_00107310: no frames (division trap on the PS2)");
        return;
    }
    const s32 frame = time % frames;
    const auto q = [&](u64 w0, u64 w1, u64 w2, u64 w3) {
        if (w0 != kSeqKeep) GRef<u32>(p) = static_cast<u32>(w0);
        if (w1 != kSeqKeep) GRef<u32>(p + 4) = static_cast<u32>(w1);
        if (w2 != kSeqKeep) GRef<u32>(p + 8) = static_cast<u32>(w2);
        if (w3 != kSeqKeep) GRef<u32>(p + 0xC) = static_cast<u32>(w3);
        p += 0x10;
    };
    const auto qwc = [](u32 rec) {
        const s32 v = GRef<s32>(rec + 4);
        return static_cast<u32>((v + 0xF >= 0 ? v + 0xF : v + 0x1E) >> 4);
    };
    const auto upload = [&](u32 rec, u32 data) {
        q(0x10000006, 0, 0, 0x50000006);
        q(4, 0x10000000, 0xE, kSeqKeep);
        q(0, static_cast<u32>(static_cast<s32>(GRef<s16>(rec)) | (static_cast<s32>(GRef<s8>(rec + 2)) << 16) |
                              (static_cast<s32>(GRef<s8>(rec + 3)) << 24)),
          0x50, kSeqKeep);
        q(0, 0, 0x51, kSeqKeep);
        q(static_cast<u32>(static_cast<s32>(GRef<s16>(rec + 0xC))), static_cast<u32>(static_cast<s32>(GRef<s16>(rec + 0xE))), 0x52, kSeqKeep);
        q(0, kSeqKeep, 0x53, kSeqKeep);
        q(qwc(rec), 0x08000000, 0, kSeqKeep);
        q(qwc(rec) | 0x30000000u, data, 0, qwc(rec) | 0x50000000u);
    };
    upload(W(spr + 4), W(W(spr + 0xC) + static_cast<u32>(frame) * 4));
    upload(W(spr + 8), W(spr + 0x10));
    q(0x10000002, 0, 0, 0x50000002);
    q(0x8001, 0x10000000, 0xE, kSeqKeep);
    q(kSeqKeep, kSeqKeep, 0x3F, kSeqKeep);
    EndPacket(list, bucket, p);
}

// EE: 0x00107DC8
// Draws the active animated sprites (slots 0..31): uploads the current frame (0x00107310), rotates
// the four corners by -angle (VU0), then the packet of 0x00106B58 with TEXFLUSH first, TEX0 always
// with the CLUT and UVs without the half-texel offset (0 / w x 16 / h x 16 << 16).
void func_00107DC8(u32 set, u32 list, s32 bucket) {
    guest::GuestFrame frame(0xC0);
    const u32 sp = frame.Address();
    const u32 corners = sp, ints = sp + 0x40, rot = sp + 0x80;
    for (s32 i = 0; i < 0x20; ++i) {
        const u32 s = set + static_cast<u32>(i) * 0x30 + 8;
        if (((W(s) ^ 1) & 1) != 0) continue;
        const u32 rec = W(set) + (W(s + 0x28) << 6);
        func_00107310(rec, list, bucket, GRef<s32>(s + 0x20));
        const s32 hw = GRef<s32>(s + 0x18), hh = GRef<s32>(s + 0x1C);
        const f32 phw = fpu::IntToFloat(hw), nhw = fpu::IntToFloat(-hw), nhh = fpu::IntToFloat(-hh), phh = fpu::IntToFloat(hh);
        const auto vec = [&](u32 k4, f32 x, f32 y) {
            GRef<f32>(corners + k4 * 0x10) = x;
            GRef<f32>(corners + k4 * 0x10 + 4) = y;
            GRef<u32>(corners + k4 * 0x10 + 8) = 0;
            GRef<f32>(corners + k4 * 0x10 + 0xC) = 1.0f;
        };
        vec(0, nhw, nhh);
        vec(1, phw, nhh);
        vec(2, nhw, phh);
        vec(3, phw, phh);
        sceVu0UnitMatrix(rot);
        sceVu0RotMatrixZ(rot, rot, -GRef<f32>(s + 0x24));
        for (u32 c = 0; c < 4; ++c) sceVu0ApplyMatrix(corners + c * 0x10, rot, corners + c * 0x10);
        for (u32 c = 0; c < 4; ++c) sceVu0FTOI4Vector(ints + c * 0x10, corners + c * 0x10);
        u32 p = BeginPacket();
        const u32 img = W(rec + 4), clut = W(rec + 8);
        const u32 ctx = W(0x004104A8);
        const auto q = [&](u64 w0, u64 w1, u64 w2, u64 w3) {
            if (w0 != kSeqKeep) GRef<u32>(p) = static_cast<u32>(w0);
            if (w1 != kSeqKeep) GRef<u32>(p + 4) = static_cast<u32>(w1);
            if (w2 != kSeqKeep) GRef<u32>(p + 8) = static_cast<u32>(w2);
            if (w3 != kSeqKeep) GRef<u32>(p + 0xC) = static_cast<u32>(w3);
            p += 0x10;
        };
        q(0x10000007, 0, 0, 0x50000007);
        q(6, 0x10000000, 0xE, kSeqKeep);
        q(kSeqKeep, kSeqKeep, 0x3F, kSeqKeep);
        q(0x30000, kSeqKeep, 0x47, kSeqKeep);
        SeqCopyQ(p, ctx + 0xE0);
        GRef<u32>(p + 4) = 1;
        p += 0x10;
        q(0x44, 0, 0x42, 0);
        q(0x60, 0, 0x14, kSeqKeep);
        const s32 w = GRef<s16>(img + 0xC), h = GRef<s16>(img + 0xE);
        q((static_cast<u32>(w - 1) << 14) | 0xA, static_cast<u32>(h - 1) << 2, 8, kSeqKeep);
        q(0x10000002, 0, 0, 0x50000002);
        q(0x8001, 0x14000000, 0xF6, kSeqKeep);
        GRef<u32>(p) = static_cast<u32>(static_cast<s32>(GRef<s16>(img))) | 0x80000000u |
                       (static_cast<u32>(static_cast<s32>(GRef<s8>(img + 3))) << 20) |
                       (static_cast<u32>(static_cast<s32>(GRef<s8>(img + 2))) << 14) | 0x28000000u;
        GRef<u32>(p + 4) = (static_cast<u32>(static_cast<s32>(GRef<s16>(clut))) << 5) | 0x20000000u |
                           (static_cast<u32>(static_cast<s32>(GRef<s8>(clut + 2))) << 19) | 6u |
                           (static_cast<u32>(static_cast<s32>(GRef<s8>(clut + 3))) << 23);
        p += 0x10;  // the second half of the REGLIST quadword is left as it is
        q(0x10000007, 0, 0, 0x50000007);
        q(1, 0x24000000, 0x10, kSeqKeep);
        GRef<u64>(p) = 0x154;
        GRef<u64>(p + 8) = GRef<u64>(s + 8);
        p += 0x10;
        q(0x8004, 0x24000000, 0x43, kSeqKeep);
        const u32 u1 = static_cast<u32>(w) << 4, v1 = static_cast<u32>(h) << 20;
        const auto xy = [&](u32 c) {
            GRef<u32>(p) = (W(s + 0x10) + W(ints + c * 0x10)) | ((W(s + 0x14) + W(ints + c * 0x10 + 4)) << 16);
            GRef<u32>(p + 4) = 0;
            p += 8;
        };
        const auto uv = [&](u32 v) {
            GRef<u32>(p) = v;  // the upper word of UV is left as it is
            p += 8;
        };
        uv(0);
        xy(0);
        uv(u1);
        xy(1);
        uv(v1);
        xy(2);
        uv(u1 | v1);
        xy(3);
        q(0x10000003, 0, 0x11000000, 0x50000003);
        q(0x8002, 0x10000000, 0xE, kSeqKeep);
        q(0x50000, kSeqKeep, 0x47, kSeqKeep);
        SeqCopyQ(p, ctx + 0xE0);
        EndPacket(list, bucket, p + 0x10);
    }
}

// EE: 0x0010C748
// Scripted-sequence step: the keyframe player (UpdateInEngineSequence 0x00177058), the unit
// frame update 0x0011D988 and the effects (0x00154DD0).
void func_0010C748() {
    UpdateInEngineSequence();
    func_0011D988();
    UpdateEffects();
}

// EE: 0x0010BA38
// Scene 2 sub 0x13 (special in-engine sequence: no-save intro, opening of mission 1, sequence after
// mission 18): terrain and world update (0x00139FE8, 0x00144CC0, 0x0013EC20, 0x0013EC38 with the
// current terrain entry's +0x44 / +0x50), the sequence step, then the draw lists.
void func_0010BA38() {
    func_00139FE8(GRef<u32>(0x00410694));
    func_00144CC0(GRef<u32>(0x0041069C));
    func_0013EC20(GRef<u32>(0x00410698));
    const u32 t = GRef<u32>(0x00410694);
    const u32 e = GRef<u32>(t) + GRef<u32>(t + 4) * 100;
    func_0013EC38(GRef<u32>(0x00410698), GRef<u32>(e + 0x44), GRef<u32>(e + 0x50));
    func_0010C748();
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010BA38, func_0010BA38);

// EE: 0x001373E8
// Copies the light direction matrix of a light set (+0x50, 4 rows) to `out`, xyz negated, w kept.
void func_001373E8(u32 set, u32 out) {
    for (u32 r = 0; r < 4; ++r) {
        GRef<f32>(out + r * 0x10) = -GRef<f32>(set + 0x50 + r * 0x10);
        GRef<f32>(out + r * 0x10 + 4) = -GRef<f32>(set + 0x54 + r * 0x10);
        GRef<f32>(out + r * 0x10 + 8) = -GRef<f32>(set + 0x58 + r * 0x10);
        GRef<f32>(out + r * 0x10 + 0xC) = GRef<f32>(set + 0x5C + r * 0x10);
    }
}

// EE: 0x001374A0
// Light colour matrix of a light set at half intensity: rows +0x90, +0xA0, +0xB0 and the ambient
// +0xD0 (xyz x 0.5, w 0) to `out`.
void func_001374A0(u32 set, u32 out) {
    const u32 src[4] = {0x90, 0xA0, 0xB0, 0xD0};
    for (u32 r = 0; r < 4; ++r) {
        GRef<f32>(out + r * 0x10) = GRef<f32>(set + src[r]) * 0.5f;
        GRef<f32>(out + r * 0x10 + 4) = GRef<f32>(set + src[r] + 4) * 0.5f;
        GRef<u32>(out + r * 0x10 + 0xC) = 0;
        GRef<f32>(out + r * 0x10 + 8) = GRef<f32>(set + src[r] + 8) * 0.5f;
    }
}

// EE: 0x0013EC08
// Stores (x, y, z, 0) at `v`.
void func_0013EC08(u32 v, f32 x, f32 y, f32 z) {
    GRef<f32>(v) = x;
    GRef<f32>(v + 4) = y;
    GRef<f32>(v + 8) = z;
    GRef<u32>(v + 0xC) = 0;
}

// EE: 0x0010B500
// Scene 2 subs 0xD-0xF: scripted mid-mission sequences of mission ids 4, 5, 10, 14, 15 by stage
// game[0x2C] (entry 0x002A16A0 + 8 x index: BGM id, length in frames). Sub 0xD: on the first frame
// stop the player's flight sound and the voices / streams (0x001279B0, 0x00219260, 0x0021AEE0,
// StopAllVoices, stream 1, 0x002559C8), and once the length has passed start the sequence
// (SetupInEngineSequence, timeline track 0x00259340, BGM, fade-in) and go to 0xE. Sub 0xF (the
// sequence ended): back to gameplay (0xA or 0xB), to the replay (0x10, replay skip flag cleared),
// to the results (RequestScene 5) or to sub 0xC. Every frame: terrain and world update, the
// sequence step while S+0x1688 = 1, the draw lists.
void func_0010B500() {
    const u32 game = g_GameState().addr;
    const u8 id = GRef<u8>(game + 0x27), st = GRef<u8>(game + 0x2C);
    u32 index = 0;
    if (id == 4 && st == 0) index = 0;
    else if (id == 5 && st == 0) index = 1;
    else if (id == 0xA && st == 0) index = 2;
    else if (id == 0xA && st == 1) index = 3;
    else if (id == 0xE && st == 0) index = 4;
    else if (id == 0xF && st == 0) index = 5;
    else if (id == 0xF) index = st == 1 ? 6u : 0u;
    const u32 entry = 0x002A16A0 + index * 8;
    const u16 sub = Ctx()->sub;
    if (sub == 0xD) {
        const u32 s = S();
        if (GRef<u32>(s + 0x1654) == 0) {
            GRef<u32>(s + 0x1688) = 0;
            GRef<f32>(s + 0x1670) = 1.0f;
            func_001279B0(0, 0);
            func_00219260();
            func_0021AEE0();
            StopAllVoices();
            func_0021FDF8(1);
            func_002559C8();
        }
        if (!(GRef<u32>(S() + 0x1654) < GRef<u32>(entry + 4))) {
            GRef<u32>(S() + 0x1688) = 1;
            SetupInEngineSequence();
            func_00259340(GRef<u8>(g_GameState().addr + 0x2C));
            func_0021FD40(0, static_cast<u16>(GRef<u32>(entry)));
            func_0021F190(0.0f, 0, 0xFF, 0xFF, 1);
            RequestSubState(0xE);
        }
    } else if (sub == 0xF) {
        if ((id == 4 && st == 0) || (id == 0xA && st == 1)) {
            RequestSubState(0xB);
            func_001279B0(0, 1);
        } else if (id == 0xA && st == 0) {
            RequestSubState(0xA);
            func_001279B0(0, 1);
        } else if ((id == 5 && st == 0) || (id == 0xE && st == 0)) {
            GRef<u32>(S() + 0x1668) = GRef<u32>(S() + 0x1668) & 0xFFFEFFFFu;
            RequestSubState(0x10);
        } else if (id == 0xF && st == 0) {
            RequestScene(5);
        } else if (id == 0xF && st == 1) {
            RequestSubState(0xC);
        }
    }
    func_00139FE8(GRef<u32>(0x00410694));
    func_00144CC0(GRef<u32>(0x0041069C));
    func_0013EC20(GRef<u32>(0x00410698));
    const u32 t = GRef<u32>(0x00410694);
    const u32 e = GRef<u32>(t) + GRef<u32>(t + 4) * 100;
    func_0013EC38(GRef<u32>(0x00410698), GRef<u32>(e + 0x44), GRef<u32>(e + 0x50));
    if (static_cast<u32>(Ctx()->sub) - 0xD < 3 && GRef<u32>(S() + 0x1688) == 1) func_0010C748();
    BuildFlightDrawLists();
}
AC04_GUEST_FUNCTION(0x0010B500, func_0010B500);

// EE: 0x00176BB8
// Starts cut W(seq+0x110) of the in-engine sequence (state W(0x00410B18), parts W(0x00410B1C),
// models W(0x00410B20)): unless +0x119 bit 2, rebuilds the draw-env template (0x002212A0 with
// +0x118) and clears the page table; seq+0x11C = the cut; for each '"' (0x22) record of the cut,
// creates a character object (0x00220C40) from model (v & 0x1F) into seq+0x10 [slot +1], binds the
// second model ((v >> 10) & 0x1F, none for 0x1F), stores C 0x0040EB84 at +0xEC and starts motion
// ((v >> 5) & 0x1F) + 1. Then for every unit: unit 0's position into seq+0; aircraft (kind 0) lose
// their targets (+0x7C, +0x80, aircraft +0x2C4, +0x2C8 = -1), kind 1 units too (+0x7C, +0x80).
void func_00176BB8() {
    u32 seq = GRef<u32>(0x00410B18);
    if ((GRef<u8>(seq + 0x119) & 4) == 0) {
        func_002212A0(GRef<u8>(seq + 0x118));
        func_002254A0();
        seq = GRef<u32>(0x00410B18);
    }
    const u32 parts = GRef<u32>(0x00410B1C);
    const u32 cut = parts + W(parts + 8 + W(seq + 0x110) * 4);
    GRef<u32>(seq + 0x11C) = cut;
    for (s32 i = 0; i < GRef<s16>(cut + 4); ++i) {
        const u32 rec = cut + W(cut + 0x14 + static_cast<u32>(i) * 4);
        if (GRef<u8>(rec) != 0x22) continue;
        const u16 v = GRef<u16>(rec + 2);
        const u32 models = GRef<u32>(0x00410B20);
        const u32 m1 = models + W(models + 8 + (v & 0x1Fu) * 4);
        const u32 m2 = (v & 0x7C00) == 0x7C00 ? 0u : models + W(models + 8 + ((static_cast<u32>(v) >> 8) & 0x7C));
        const u32 obj = func_00220C40(m1);
        const u32 slot = GRef<u32>(0x00410B18) + 0x10 + static_cast<u32>(GRef<u8>(rec + 1)) * 4;
        GRef<u32>(slot) = obj;
        func_00220BF0(W(slot), m2);
        func_00220C38(W(GRef<u32>(0x00410B18) + 0x10 + static_cast<u32>(GRef<u8>(rec + 1)) * 4), GRef<f32>(0x0040EB84));
        func_00221FA0(W(GRef<u32>(0x00410B18) + 0x10 + static_cast<u32>(GRef<u8>(rec + 1)) * 4),
                      static_cast<s32>((GRef<u16>(rec + 2) >> 5) & 0x1F) + 1);
    }
    for (s32 i = 0; i < GRef<s32>(GRef<u32>(0x00410640)); ++i) {
        const u32 unit = W(0x002A1F70 + static_cast<u32>(i) * 4);
        if (i == 0) sceVu0CopyVector(GRef<u32>(0x00410B18), unit);
        const u8 kind = GRef<u8>(unit + 0x54);
        if (kind == 0) {
            GRef<s32>(unit + 0x7C) = -1;
            GRef<s32>(unit + 0x80) = -1;
            const u32 ac = W(unit + 0x44);
            GRef<s32>(ac + 0x2C8) = -1;
            GRef<s32>(ac + 0x2C4) = -1;
        } else if (kind == 1) {
            GRef<s32>(unit + 0x7C) = -1;
            GRef<s32>(unit + 0x80) = -1;
        }
    }
}

// EE: 0x00176DD0
// SetupInEngineSequence: sequence game[0x2C] of the mission data container S+0x1708 has 4 parts:
// cuts (W(0x00410B1C)), model table (W(0x00410B20)), animated sprites (0x00107BD0) and still
// images (0x00106938). Allocates the 0x150-byte state W(0x00410B18) (flags +0x119: bit 0 set, bit 2
// when the model table is empty; +0x118 = 0x60) and its buffers (+0x130 8, +0x120 0x608,
// +0x124 0x2810, +0x128 0x800, +0x134 4, +0x138 0x40, +0x144 0x10, +0x14C 0x18, +0x148 0x60,
// +0x13C 0x20, +0x140 0x80 bytes), starts the character engine when there are models, resets the
// effects, fades in and starts BGM 0x30 / 0x33 / 0x32 for mission ids 0x23 / 0x24 / 0x25.
void SetupInEngineSequence() {
    guest::GuestFrame frame(0x10);
    const u32 r = frame.Address();
    InitContainerReader(r, GRef<u32>(S() + 0x1708));
    InitContainerReader(r, GetContainerItem(r, GRef<u8>(g_GameState().addr + 0x2C)));
    const u32 cuts = GetContainerItem(r, 0);
    const u32 models = GetContainerItem(r, 1);
    const u32 sprites = GetContainerItem(r, 2);
    const u32 images = GetContainerItem(r, 3);
    GRef<u32>(0x00410B20) = models;
    const u32 seq = WorkHeapAlloc(0x150, 0x10);
    GRef<u32>(0x00410B18) = seq;
    GRef<u8>(seq + 0x119) = 0;
    GRef<u32>(seq + 0x110) = 0;
    GRef<u32>(seq + 0x114) = 0;
    GRef<u32>(seq + 0x12C) = 0;
    const auto st = []() { return GRef<u32>(0x00410B18); };
    if (W(GRef<u32>(0x00410B20) + 4) == 0) GRef<u8>(st() + 0x119) = static_cast<u8>(GRef<u8>(st() + 0x119) | 4);
    GRef<u8>(st() + 0x119) = static_cast<u8>(GRef<u8>(st() + 0x119) | 1);
    GRef<u8>(st() + 0x11A) = 0;
    GRef<u32>(st() + 0x130) = WorkHeapAlloc(8, 1);
    if ((GRef<u8>(st() + 0x119) & 4) == 0) InitSequenceModelEngine();
    {
        const u32 b = WorkHeapAlloc(0x608, 0x10);
        GRef<u32>(st() + 0x120) = b;
        func_00107BD0(b, sprites);
    }
    {
        const u32 b = WorkHeapAlloc(0x2810, 0x10);
        GRef<u32>(st() + 0x124) = b;
        func_00106938(b, images);
    }
    GRef<u8>(st() + 0x118) = 0x60;
    GRef<u32>(st() + 0x128) = WorkHeapAlloc(0x800, 0x10);
    GRef<u32>(st() + 0x134) = WorkHeapAlloc(4, 1);
    GRef<u32>(st() + 0x138) = WorkHeapAlloc(0x40, 0x10);
    GRef<u32>(st() + 0x144) = WorkHeapAlloc(0x10, 1);
    GRef<u32>(st() + 0x14C) = WorkHeapAlloc(0x18, 4);
    GRef<u32>(st() + 0x148) = WorkHeapAlloc(0x60, 0x10);
    GRef<u32>(st() + 0x13C) = WorkHeapAlloc(0x20, 1);
    GRef<u32>(st() + 0x140) = WorkHeapAlloc(0x80, 0x10);
    ResetEffects();
    GRef<u32>(0x00410B1C) = cuts;
    func_0021F190(0.0f, 0, 0xFF, 0xFF, 1);
    const u8 id = GRef<u8>(g_GameState().addr + 0x27);
    if (id == 0x23) func_0021FD40(0, 0x30);
    else if (id == 0x24) func_0021FD40(0, 0x33);
    else if (id == 0x25) func_0021FD40(0, 0x32);
}

// EE: 0x00177058
// UpdateInEngineSequence: the baked keyframe player of the in-engine sequences (docs/subsystems/
// mission-flow.md). Cut c = W(seq+0x110), frame f = W(seq+0x114), cut header W(seq+0x11C) (+4 track
// count, +6 length, +8 origin, +0x14 track offsets). At a cut start (+0x119 bit 0): effects reset,
// the per-cut slot tables cleared, the cut started (0x00176BB8). Every frame: the VU0 skinning
// program uploaded (with models), units 0/1 reset for drawing, the overlay slots cleared, then each
// track's record for frame f applied (positions are stored with y / z negated and the cut origin
// added): 0x10 camera (position, yaw, FOV -> 240 / tan(fov / 2)), 0x11 look-at, 0x20-0x24 unit and
// character transforms (Euler angles via a VU0 matrix and 0x00103290) with animation channels, 0x40
// / 0x41 unit flags, 0x42 lights, 0x43 / 0x48 effect markers, 0x44-0x47 / 0x49 effects, 0x70 / 0x71
// overlays, 0x80-0x84 fades and colours. Then the world origin, unit hierarchies and lights; the
// frame advances once the first-frame wait (+0x11A, set by 0x00353C2C & 0x24) is over; at the end of
// the last cut, or 32 frames after Start, the sequence ends: sub 0xE -> 0xF, else by mission id
// (0x23 menu, 0x24 -> mission 0xF, 0x25 -> mission 1). The camera is set by 0x00178DF8.
// fpucheck: verified 48 (15 position placements of 1 add + 2 sub, inlined per track case in the original; 2 roll wraps; 1 add inside the unsigned conversion of track 0x83)
void UpdateInEngineSequence() {
    guest::GuestFrame frame(0x200);
    const u32 sp = frame.Address();
    const u32 cam = sp, look = sp + 0x10, ang = sp + 0x20, mat = sp + 0x30, lpos = sp + 0x70, ldir = sp + 0xF0;
    const u32 lflag = sp + 0x170, pv = sp + 0x180, dv = sp + 0x190, mat2 = sp + 0x1A0, epos = sp + 0x1E0, evel = sp + 0x1F0;
    const auto seqp = []() { return GRef<u32>(0x00410B18); };
    f32 fovd = 512.0f, roll = 0.0f;
    GRef<u32>(seqp() + 0x12C) = 0;
    for (u32 k = 0; k < 8; ++k) GRef<u8>(lflag + k) = 0;
    if ((GRef<u8>(seqp() + 0x119) & 1) != 0) {
        ResetEffects();
        for (u32 k = 0; k < 8; ++k) GRef<u8>(W(seqp() + 0x130) + k) = 0;
        for (u32 k = 0; k < 0x10; ++k) GRef<u8>(W(seqp() + 0x144) + k) = 0;
        for (u32 k = 0; k < 6; ++k) GRef<s32>(W(seqp() + 0x14C) + k * 4) = -1;
        for (u32 k = 0; k < 8; ++k) GRef<s32>(W(seqp() + 0x13C) + k * 4) = -1;
        func_00176BB8();
        GRef<u8>(seqp() + 0x119) = static_cast<u8>(GRef<u8>(seqp() + 0x119) & 0xFE);
        const u32 s = seqp();
        if ((W(s + 0x114) & 2) != 0) {
            const s16 len = GRef<s16>(W(s + 0x11C) + 6);
            GRef<u8>(s + 0x119) = static_cast<u8>(GRef<u8>(s + 0x119) & 0xFD);
            GRef<s32>(s + 0x114) = len - 1;
        }
        for (u32 k = 0; k < 8; ++k) GRef<s32>(0x00420730 + k * 4) = -1;
    }
    for (u32 k = 0; k < 4; ++k) GRef<u8>(W(seqp() + 0x134) + k) = 0;
    const u32 cut = W(seqp() + 0x11C);
    if ((GRef<u8>(seqp() + 0x119) & 4) == 0) UploadVu0Microcode();
    for (s32 i = 0; i < GRef<s32>(GRef<u32>(0x00410640)); ++i) {
        const u32 unit = W(0x002A1F70 + static_cast<u32>(i) * 4);
        GRef<u16>(unit + 0x56) = 0;
        GRef<u32>(unit + 0x4C) = W(unit + 0x4C) & 0xEFFFFFFFu & 0xDFFFFFFFu & 0xFFFFFEFFu & 0xFFFFFDFFu;
        if (!(i < 2)) continue;
        const u32 ac = W(unit + 0x44);
        GRef<f32>(ac + 0x258) = 0.5f;
        GRef<f32>(ac + 0x25C) = GRef<f32>(0x0040EB88);
        GRef<u32>(ac + 0x244) = 0;
        GRef<f32>(ac + 0x250) = 0.5f;
        if (i == 0) {
            sceVu0SubVector(unit + 0x20, unit, seqp());
            sceVu0CopyVector(seqp(), unit);
        }
        GRef<u8>(unit + 0x69) = GRef<u8>(g_GameState().addr + static_cast<u32>(i) * 0x10);
        for (s32 j = 0; j < 0x80; ++j) func_00149C40(0.5f, GRef<u32>(0x004106A4), i, j);
        func_0014AA38(GRef<u32>(0x004106A8), i, 0, 0, 1);
        func_0014AA38(GRef<u32>(0x004106A8), i, 0, 1, 1);
        for (s32 k = 0; k < GRef<s8>(ac + 0x175); ++k) func_0014AA38(GRef<u32>(0x004106A8), i, 1, k, 1);
    }
    for (u32 k = 0; k < 0x20; ++k) GRef<u32>(seqp() + 0x10C - k * 4) = 0;
    func_00107C90(W(seqp() + 0x120));
    func_00106A20(W(seqp() + 0x124));
    {
        const u32 e = Eff() + 0x60000;
        GRef<u8>(e + 0x2450) = 0;
        GRef<u32>(e + 0x2444) = 0;
        GRef<u32>(e + 0x2440) = 0;
        GRef<u32>(e + 0x2448) = 0;
        GRef<u8>(e + 0x2451) = 0;
        GRef<u32>(e + 0x244C) = 0xC0C0C0;
    }
    const auto oy = [cut]() { return GRef<f32>(cut + 0xC); };
    const auto oz = [cut]() { return GRef<f32>(cut + 0x10); };
    const auto frm = [seqp]() { return GRef<s32>(seqp() + 0x114); };
    // Position (x, -y, -z) + origin (x + ox, -y - oy, -z - oz) of a record entry at `src` +4..+0xC.
    const auto place = [&](u32 dst, u32 src) {
        GRef<f32>(dst) = fpu::Add(GRef<f32>(src + 4), GRef<f32>(cut + 8));
        GRef<f32>(dst + 4) = fpu::Sub(-GRef<f32>(src + 8), oy());
        GRef<f32>(dst + 8) = fpu::Sub(-GRef<f32>(src + 0xC), oz());
    };
    // Euler angles (+0x10..+0x18, degrees, y / z negated) of an entry into `ang` through matrix m.
    const auto euler = [&](u32 src, f32 k, u32 m) {
        GRef<f32>(ang) = GRef<f32>(src + 0x10) * k / 180.0f;
        GRef<f32>(ang + 4) = -GRef<f32>(src + 0x14) * k / 180.0f;
        GRef<u32>(ang + 0xC) = 0;
        GRef<f32>(ang + 8) = -GRef<f32>(src + 0x18) * k / 180.0f;
        sceVu0UnitMatrix(m);
        sceVu0RotMatrixX(m, m, GRef<f32>(ang));
        sceVu0RotMatrixY(m, m, GRef<f32>(ang + 4));
        sceVu0RotMatrixZ(m, m, GRef<f32>(ang + 8));
        func_00103290(ang, m);
    };
    // Unit position from an entry: (x, -y, -z, 1); unit_origin adds the cut origin in place.
    const auto unit_pos = [&](u32 unit, u32 src) {
        GRef<f32>(unit) = GRef<f32>(src + 4);
        GRef<f32>(unit + 4) = -GRef<f32>(src + 8);
        GRef<f32>(unit + 0xC) = 1.0f;
        GRef<f32>(unit + 8) = -GRef<f32>(src + 0xC);
    };
    const auto unit_origin = [&](u32 unit) {
        GRef<f32>(unit) = fpu::Add(GRef<f32>(unit), GRef<f32>(cut + 8));
        GRef<f32>(unit + 4) = fpu::Sub(GRef<f32>(unit + 4), oy());
        GRef<f32>(unit + 8) = fpu::Sub(GRef<f32>(unit + 8), oz());
    };
    const auto unit_of = [](u32 rec, u32 b) { return W(0x002A1F70 + static_cast<u32>(GRef<u8>(rec + b)) * 4); };
    for (s32 t = 0; t < GRef<s16>(cut + 4); ++t) {
        const u32 rec = cut + W(cut + 0x14 + static_cast<u32>(t) * 4);
        const u8 type = GRef<u8>(rec);
        const u32 k = GRef<u8>(rec + 1);
        const u32 fr = static_cast<u32>(frm());
        switch (type) {
        case 0x10: {
            const u32 e = rec + 4 + fr * 0x18;
            const f32 pi = GRef<f32>(0x0040EB8C);
            GRef<f32>(cam) = GRef<f32>(e + 4);
            GRef<f32>(cam + 4) = -GRef<f32>(e + 8);
            GRef<f32>(cam + 8) = -GRef<f32>(e + 0xC);
            const f32 tn = ee::tanf(GRef<f32>(e + 0x14) * 0.5f * pi / 180.0f);
            fovd = 240.0f / tn;
            roll = -GRef<f32>(e + 0x10) * pi / 180.0f;
            if (pi < roll) roll = fpu::Sub(roll, GRef<f32>(0x0040EB90));
            if (roll < GRef<f32>(0x0040EB94)) roll = fpu::Add(roll, GRef<f32>(0x0040EB98));
            GRef<f32>(cam) = fpu::Add(GRef<f32>(cam), GRef<f32>(cut + 8));
            GRef<f32>(cam + 4) = fpu::Sub(GRef<f32>(cam + 4), oy());
            GRef<f32>(cam + 8) = fpu::Sub(GRef<f32>(cam + 8), oz());
            break;
        }
        case 0x11: {
            const u32 e = rec + 4 + fr * 0x10;
            GRef<f32>(look) = GRef<f32>(e + 4);
            GRef<f32>(look + 4) = -GRef<f32>(e + 8);
            GRef<f32>(look + 8) = -GRef<f32>(e + 0xC);
            GRef<f32>(look) = fpu::Add(GRef<f32>(look), GRef<f32>(cut + 8));
            GRef<f32>(look + 4) = fpu::Sub(GRef<f32>(look + 4), oy());
            GRef<f32>(look + 8) = fpu::Sub(GRef<f32>(look + 8), oz());
            break;
        }
        case 0x20: {
            const u32 unit = unit_of(rec, 1);
            const u32 e = rec + 4 + fr * 0x1C;
            if (GRef<s16>(e) != frm() || GRef<f32>(e + 8) < GRef<f32>(0x0040EB9C)) break;
            unit_pos(unit, e);
            euler(e, GRef<f32>(0x0040EBA0), mat);
            sceVu0CopyVector(unit + 0x10, ang);
            func_00179780(unit + 0x10, unit + 0x10);
            unit_origin(unit);
            if (GRef<u8>(unit + 0x69) == 0x39)
                func_00145D08(fpu::IntToFloat(frm() % 600) / 600.0f, GRef<u32>(0x004106A0), GRef<u8>(rec + 1), 1);
            GRef<u32>(unit + 0x4C) = W(unit + 0x4C) | 0x10000000u | 0x20000300u;
            break;
        }
        case 0x21: {
            const u32 unit = unit_of(rec, 1);
            const u32 e = rec + 4 + fr * 0x24;
            if (GRef<s16>(e) != frm() || GRef<f32>(e + 8) < GRef<f32>(0x0040EBA4)) break;
            unit_pos(unit, e);
            const f32 kc = GRef<f32>(0x0040EBAC);
            euler(e, GRef<f32>(0x0040EBA8), mat);
            sceVu0CopyVector(unit + 0x10, ang);
            unit_origin(unit);
            func_00149C40(GRef<f32>(e + 0x1C) * kc, GRef<u32>(0x004106A4), GRef<u8>(rec + 1), 0xF);
            func_00149C40(GRef<f32>(e + 0x20) * kc, GRef<u32>(0x004106A4), GRef<u8>(rec + 1), 0x13);
            func_00149C40(fpu::IntToFloat(GRef<u8>(e + 2)) * kc, GRef<u32>(0x004106A4), GRef<u8>(rec + 1), 0x2E);
            GRef<u32>(unit + 0x4C) = W(unit + 0x4C) | 0x10000000u | 0x20000300u;
            break;
        }
        case 0x22: {
            const u32 unit = unit_of(rec, 1);
            const u32 e = rec + 4 + fr * 0x1C;
            if (GRef<s16>(e) != frm() || GRef<f32>(e + 8) < GRef<f32>(0x0040EBB0)) break;
            unit_pos(unit, e);
            euler(e, GRef<f32>(0x0040EBB4), mat);
            sceVu0CopyVector(unit + 0x10, ang);
            unit_origin(unit);
            const u32 s = seqp();
            GRef<u32>(s + 0x90 + k * 4) = 1;
            func_00222800(W(s + 0x10 + k * 4), unit + 0x10, unit, 1);
            func_00223518(W(seqp() + 0x10 + k * 4), fpu::IntToFloat(frm()));
            func_001373E8(GRef<u32>(0x00410688), mat2);
            func_00220A40(W(seqp() + 0x10 + k * 4), mat2);
            func_001374A0(GRef<u32>(0x00410688), mat2);
            func_00220A98(W(seqp() + 0x10 + k * 4), mat2);
            break;
        }
        case 0x23: {
            if (!(k < 6)) break;
            const u32 e = rec + 4 + fr * 0x1C;
            const u32 unit = unit_of(rec, 2);
            if (GRef<s16>(e) != frm()) break;
            unit_pos(unit, e);
            unit_origin(unit);
            euler(e, GRef<f32>(0x0040EBB8), mat2);
            sceVu0CopyVector(unit + 0x10, ang);
            GRef<u32>(unit + 0x4C) = W(unit + 0x4C) | 0x10000000u | 0x20000100u;
            if (GRef<u8>(e + 2) == 1) {
                if (GRef<s32>(W(seqp() + 0x14C) + k * 4) == -1) sceVu0CopyVector(W(seqp() + 0x148) + k * 0x10, unit);
                if (GRef<u8>(e + 3) == 0) GRef<u8>(e + 3) = 1;  // written into the sequence data, as the original
                sceVu0SubVector(dv, unit, W(seqp() + 0x148) + k * 0x10);
                const u32 h = func_00200B00(W(W(seqp() + 0x14C) + k * 4), unit, dv, GRef<u8>(e + 3), 0xD2);
                GRef<u32>(W(seqp() + 0x14C) + k * 4) = h;
                sceVu0CopyVector(W(seqp() + 0x148) + k * 0x10, unit);
            } else {
                GRef<s32>(W(seqp() + 0x14C) + k * 4) = -1;
            }
            break;
        }
        case 0x24: {
            const u32 e = rec + 4 + fr * 0x28;
            const u32 unit = unit_of(rec, 1);
            if (GRef<s16>(e) != frm()) break;
            unit_pos(unit, e);
            unit_origin(unit);
            euler(e, GRef<f32>(0x0040EBBC), mat2);
            sceVu0CopyVector(unit + 0x10, ang);
            func_00145D08(GRef<f32>(e + 0x1C), GRef<u32>(0x004106A0), GRef<u8>(rec + 1), 1);
            func_00145D08(GRef<f32>(e + 0x20), GRef<u32>(0x004106A0), GRef<u8>(rec + 1), 2);
            func_00145D08(GRef<f32>(e + 0x24), GRef<u32>(0x004106A0), GRef<u8>(rec + 1), 3);
            GRef<u32>(unit + 0x4C) = W(unit + 0x4C) | 0x10000000u | 0x20000300u;
            break;
        }
        case 0x40: {
            const u32 unit = unit_of(rec, 1);
            const u32 e = rec + 4 + fr * 4;
            if (GRef<s16>(e) != frm()) break;
            if ((GRef<u8>(e + 2) & 1) != 0) GRef<u16>(unit + 0x56) = static_cast<u16>(GRef<u16>(unit + 0x56) | 1);
            else GRef<u16>(unit + 0x56) = static_cast<u16>(GRef<u16>(unit + 0x56) & 0xFFFE);
            break;
        }
        case 0x41: {
            const u32 unit = unit_of(rec, 1);
            const u32 e = rec + 4 + fr * 8;
            const u16 f0 = static_cast<u16>(GRef<u16>(unit + 0x56) | 0x10);
            GRef<u16>(unit + 0x56) = f0;
            if (GRef<u8>(e + 2) != 0) GRef<u16>(unit + 0x56) = static_cast<u16>(f0 | 0x20);
            GRef<f32>(W(unit + 0x44) + 0x244) = GRef<f32>(e + 4);
            break;
        }
        case 0x42: {
            const u32 e = rec + 4 + fr * 0x1C;
            if (!(k < 8)) break;
            place(lpos + k * 0x10, e);
            GRef<f32>(lpos + k * 0x10 + 0xC) = 1.0f;
            GRef<f32>(ldir + k * 0x10) = GRef<f32>(e + 0x10);
            GRef<f32>(ldir + k * 0x10 + 4) = -GRef<f32>(e + 0x14);
            GRef<f32>(ldir + k * 0x10 + 8) = -GRef<f32>(e + 0x18);
            GRef<u32>(ldir + k * 0x10 + 0xC) = 0;
            GRef<u8>(lflag + k) = GRef<u8>(e + 2);
            break;
        }
        case 0x43:
        case 0x48: {
            const u32 e = rec + 4 + fr * 0x18;
            const u32 m = W(seqp() + 0x128) + W(seqp() + 0x12C) * 0x40;
            place(m, e);
            GRef<u32>(m + 0x30) = type == 0x43 ? 0x46FFFFFFu : 0u;
            GRef<f32>(m + 0x24) = type == 0x43 ? 3.0f : 0.0f;
            GRef<f32>(m + 0x1C) = 1.0f;
            GRef<f32>(m + 0xC) = 1.0f;
            GRef<u32>(m + 0x20) = 0;
            GRef<u32>(m + 0x28) = 0;
            GRef<u32>(m + 0x2C) = 0;
            GRef<u32>(m + 0x10) = 0;
            GRef<u32>(m + 0x14) = 0;
            GRef<u32>(m + 0x18) = 0;
            GRef<f32>(m + 0x34) = GRef<f32>(e + 0x10);
            GRef<f32>(m + 0x38) = GRef<f32>(e + 0x14);
            GRef<u8>(m + 0x3D) = type == 0x43 ? 4 : 0x14;
            GRef<u8>(m + 0x3C) = GRef<u8>(e + 2);
            GRef<u8>(m + 0x3F) = 0;
            GRef<u8>(m + 0x3E) = 0;
            const s32 n = GRef<s32>(seqp() + 0x12C);
            if (n < 0x1F) GRef<s32>(seqp() + 0x12C) = n + 1;
            break;
        }
        case 0x44: {
            if (!(k < 8)) break;
            const u32 e = rec + 4 + fr * 0x20;
            const u32 flag = W(seqp() + 0x130) + k;
            if (GRef<u8>(e + 3) == 1) {
                if (GRef<u8>(flag) == 1) break;
                place(epos, e);
                GRef<f32>(epos + 0xC) = 1.0f;
                GRef<f32>(evel) = GRef<f32>(e + 0x10);
                GRef<f32>(evel + 4) = -GRef<f32>(e + 0x14);
                GRef<u32>(evel + 0xC) = 0;
                GRef<f32>(evel + 8) = -GRef<f32>(e + 0x18);
                const f32 a = GRef<u8>(e + 2) == 2 ? 1.0f : GRef<f32>(0x0040EBC4);
                func_001C9768(epos, evel, a, GRef<f32>(e + 0x1C));
                GRef<u8>(W(seqp() + 0x130) + k) = 1;
            } else if (GRef<u8>(flag) == 1) {
                GRef<u8>(flag) = 0;
            }
            break;
        }
        case 0x45: {
            if (!(k < 4)) break;
            const u32 e = rec + 4 + fr * 0x10;
            if (GRef<u8>(e + 2) == 0) {
                GRef<u8>(W(seqp() + 0x134) + k) = 0;
                break;
            }
            place(pv, e);
            GRef<f32>(pv + 0xC) = 1.0f;
            if (GRef<s8>(W(seqp() + 0x134) + k) == 0) {
                sceVu0CopyVector(W(seqp() + 0x138) + k * 0x10, pv);
                GRef<u8>(W(seqp() + 0x134) + k) = 1;
            }
            sceVu0SubVector(dv, W(seqp() + 0x138) + k * 0x10, pv);
            func_001CD360(pv, dv);
            sceVu0CopyVector(W(seqp() + 0x138) + k * 0x10, pv);
            break;
        }
        case 0x46: {
            if (!(k < 0x10)) break;
            const u32 e = rec + 4 + fr * 0x10;
            if (GRef<u8>(e + 2) != 1) {
                GRef<u8>(W(seqp() + 0x144) + k) = 0;
                break;
            }
            if (GRef<s8>(W(seqp() + 0x144) + k) == 1) break;
            GRef<u64>(evel) = GRef<u64>(0x00405110);
            GRef<u64>(evel + 8) = GRef<u64>(0x00405118);
            place(epos, e);
            GRef<f32>(epos + 0xC) = 1.0f;
            func_001E9540(epos, evel, GRef<u8>(e + 3));
            GRef<u8>(W(seqp() + 0x144) + k) = 1;
            break;
        }
        case 0x47: {
            if (!(k < 8)) break;
            const u32 e = rec + 4 + fr * 0x10;
            place(pv, e);
            GRef<f32>(pv + 0xC) = 1.0f;
            if (GRef<u8>(e + 2) == 1) {
                if (GRef<s32>(W(seqp() + 0x13C) + k * 4) == -1) sceVu0CopyVector(W(seqp() + 0x140) + k * 0x10, pv);
                sceVu0SubVector(dv, pv, W(seqp() + 0x140) + k * 0x10);
                const u32 h = func_0020A3A0(W(W(seqp() + 0x13C) + k * 4), pv, dv, 0x78);
                GRef<u32>(W(seqp() + 0x13C) + k * 4) = h;
                sceVu0CopyVector(W(seqp() + 0x140) + k * 0x10, pv);
            } else {
                GRef<s32>(W(seqp() + 0x13C) + k * 4) = -1;
            }
            break;
        }
        case 0x49: {
            const u32 e = rec + 4 + fr * 0x1C;
            place(pv, e);
            GRef<f32>(pv + 0xC) = 1.0f;
            euler(e, GRef<f32>(0x0040EBC0), mat2);
            if (GRef<u8>(e + 2) != 0) func_001CF2E8(pv, ang);
            break;
        }
        case 0x70: {
            const u32 e = rec + 4 + fr * 0x18;
            if (GRef<s16>(e) != frm()) break;
            func_00106A50(W(seqp() + 0x124), GRef<u8>(rec + 1), GRef<u8>(rec + 2), W(e + 4), GRef<s16>(e + 8),
                          GRef<s16>(e + 0xA), GRef<f32>(e + 0xC), GRef<f32>(e + 0x10), GRef<f32>(e + 0x14));
            break;
        }
        case 0x71: {
            const u32 e = rec + 4 + fr * 0x18;
            if (GRef<s16>(e) != frm()) break;
            func_00107CC0(W(seqp() + 0x120), GRef<u8>(rec + 1), GRef<u8>(rec + 2), W(e + 4), GRef<s16>(e + 8),
                          GRef<s16>(e + 0xA), GRef<u8>(e + 2), GRef<f32>(e + 0xC), GRef<f32>(e + 0x10), GRef<f32>(e + 0x14));
            break;
        }
        case 0x80:
        case 0x81: {
            const u32 e = rec + 4 + fr * 4;
            if (GRef<s16>(e) != frm()) break;
            GRef<f32>(Eff() + (type == 0x80 ? 0x62444u : 0x62440u)) = fpu::IntToFloat(GRef<u8>(e + 2)) * 0.0078125f;
            break;
        }
        case 0x82: {
            const u32 e = rec + 4 + fr * 4;
            if (GRef<s16>(e) != frm()) break;
            GRef<u8>(W(0x004104A8) + 0x7B) = static_cast<u8>(0x80 - GRef<u8>(e + 2));
            GRef<u16>(W(0x004104A8) + 0x8C) = static_cast<u16>(GRef<u16>(W(0x004104A8) + 0x8C) | 0x2000);
            break;
        }
        case 0x83: {
            const u32 e = rec + 4 + fr * 8;
            if (GRef<s16>(e) != frm()) break;
            const u32 v = W(e + 4);
            const f32 a = fpu::UnsignedToFloat(v >> 24);
            GRef<u32>(Eff() + 0x6244C) = v & 0xFFFFFF;
            GRef<f32>(Eff() + 0x62448) = a * 0.0078125f;
            break;
        }
        case 0x84: {
            const u32 e = rec + 4 + fr * 8;
            if (GRef<s16>(e) != frm()) break;
            const u32 b = Eff() + 0x60000;
            const s32 n = GRef<s8>(b + 0x2450);
            if (!(n < 8)) break;
            GRef<u32>(b + static_cast<u32>(n) * 8 + 0x2454) = W(e + 4);
            GRef<u8>(Eff() + static_cast<u32>(GRef<s8>(b + 0x2450)) * 8 + 0x62458) = GRef<u8>(e + 2);
            GRef<u8>(Eff() + 0x62450) = static_cast<u8>(GRef<u8>(Eff() + 0x62450) + 1);
            break;
        }
        default:
            break;
        }
    }
    func_0013EC08(GRef<u32>(0x00410698), GRef<f32>(cut + 8), GRef<f32>(cut + 0xC), GRef<f32>(cut + 0x10));
    UpdateUnitModelHierarchies(GRef<u32>(0x004106A0));
    for (u32 k = 0; k < 8; ++k) {
        if (GRef<u8>(lflag + k) != 1) continue;
        GRef<u32>(0x00420730 + k * 4) = func_001FF6C0(W(0x00420730 + k * 4), lpos + k * 0x10, ldir + k * 0x10, 0x32);
    }
    u32 s = seqp();
    if (GRef<u8>(s + 0x11A) == 0 && (W(0x00353C2C) & 0x24) != 0) GRef<u8>(s + 0x11A) = 1;
    if (GRef<u8>(s + 0x11A) == 1) {
        const s32 f = GRef<s32>(s + 0x114);
        GRef<u32>(S() + 0x1670) = 0;
        GRef<s32>(s + 0x114) = f + 1;
    }
    bool end = false;
    if (!(GRef<s32>(s + 0x114) < GRef<s16>(cut + 6))) {
        const s32 c = GRef<s32>(s + 0x110) + 1;
        GRef<s32>(s + 0x110) = c;
        GRef<u8>(s + 0x119) = static_cast<u8>(GRef<u8>(s + 0x119) | 1);
        end = c == GRef<s32>(GRef<u32>(0x00410B1C) + 4);
        GRef<u32>(seqp() + 0x114) = 0;
    }
    s = seqp();
    if ((Ctx()->pad_buttons[0][1] & 0x40) != 0 && (GRef<u8>(s + 0x119) & 8) == 0) {
        GRef<u8>(s + 0x119) = static_cast<u8>(GRef<u8>(s + 0x119) | 8);
        GRef<u8>(seqp() + 0x11B) = 0;
    }
    s = seqp();
    if ((GRef<u8>(s + 0x119) & 8) != 0) {
        const u8 n = GRef<u8>(s + 0x11B);
        if (n < 0x1F) GRef<u8>(s + 0x11B) = static_cast<u8>(n + 1);
        else end = true;
    }
    if (end) {
        if (Ctx()->sub == 0xE) {
            RequestSubState(0xF);
            func_0021F190(0.5f, 0, 0, 0, 2);
        } else {
            const u8 id = GRef<u8>(g_GameState().addr + 0x27);
            if (id == 0x23) {
                EnterMenu(0);
            } else if (id == 0x24) {
                GRef<u8>(g_GameState().addr + 0x27) = 0xF;
                GRef<u8>(g_GameState().addr + 0x2A) = 0;
                RequestScene(2);
                GRef<u32>(GRef<u32>(0x00410E90)) = 0;
                func_0021F190(1.0f, 0, 0, 0, 2);
            } else if (id == 0x25) {
                GRef<u8>(g_GameState().addr) = 2;
                GRef<u8>(g_GameState().addr + 0x27) = 1;
                GRef<u8>(g_GameState().addr + 0x2A) = 0;
                RequestScene(2);
                GRef<u32>(GRef<u32>(0x00410E90)) = 0;
                func_0021F190(1.0f, 0, 0, 0, 2);
            }
        }
        GRef<u32>(seqp() + 0x110) = 0;
        GRef<u8>(Eff() + 0x62451) = 1;
    }
    func_00178DF8(cam, look, fovd, roll);
}

// EE: 0x00178DF8
// Sequence camera: view 0 of the flight scene (S()) set up as the only view (+0x1650 = 1, flags
// 0x80000001, +4 = 5, +7 = 0x43 when unit 0 has +0x4C bit 0x20000000, else 0), projection 640 x 480
// (360 when main-context flag 0x10000) at distance K 0x0040EBC8, viewport the draw area; eye `cam`,
// target zero, look-at `look`, roll -> +0xC, distance `fovd` (capped at 4096) into both cameras;
// solve and camera matrices. Unless the sequence has no models (+0x119 bit 2): the character
// engine view matrices (camera +0x20, +0x60) and its fog (colour toward the camera, terrain
// +0xA8 / +0xA4).
void func_00178DF8(u32 cam, u32 look, f32 fovd, f32 roll) {
    guest::GuestFrame frame(0x10);
    const u32 zero = frame.Address();
    ZeroQ(zero);
    GRef<u8>(g_FlightScene() + 0x1650) = 1;
    const u32 v = g_FlightScene();
    GRef<u8>(v + 4) = 5;
    const u32 c0 = v + 0xB0;
    GRef<u8>(v + 5) = 0;
    GRef<u8>(v + 6) = 0;
    GRef<u32>(v + 0) = 0x80000001;
    const u32 c1 = v + 0x270;
    GRef<u8>(v + 7) = (W(W(0x002A1F70) + 0x4C) & 0x20000000) != 0 ? 0x43 : 0;
    // Original: 640 x 480, or 640 x 360 in 16:9 (Vert-: the 4:3 width with the top and bottom cut).
    f32 width = 640.0f;
    f32 height = (Ctx()->flags & 0x10000) != 0 ? 360.0f : 480.0f;
    // port: ultrawide flight presents the window aspect, so the in-engine sequence camera must widen
    // with it or it is stretched. With the 4:3 field of view kept it is 640 x aspect/(4/3) by 480;
    // otherwise the original 16:9 view widened like the flight cameras.
    if (port::Display().ultrawide) {
        if (port::Display().keep_4x3_fov) {
            width = 640.0f * (port::Display().window_aspect / (4.0f / 3.0f));
            height = 480.0f;
        } else {
            width = 640.0f * port::Display().projection_x_scale;
        }
    }
    SetCameraProjection(width, height, GRef<f32>(0x0040EBC8), 1.0f, GRef<f32>(0x0040EBCC), c0);
    SetCameraViewport(1.0f, GRef<f32>(0x0040EBD0), c0, 0, 0, static_cast<s16>(Ctx()->draw_w),
                      static_cast<s16>(Ctx()->draw_h));
    func_00102838(c1, c0);
    func_00102990(c1, c0);
    func_0010D5E8(v, 0);
    func_0010D5F0(v, cam, zero);
    func_0010D680(v, look);
    GRef<f32>(c1 + 0x168) = fovd;
    GRef<f32>(c0 + 0x168) = fovd;
    GRef<f32>(v + 0xC) = roll;
    ZeroQ(v + 0x40);
    if (4096.0f < GRef<f32>(c0 + 0x168)) {
        GRef<f32>(c0 + 0x168) = 4096.0f;
        GRef<f32>(c1 + 0x168) = 4096.0f;
    }
    func_0010D760(v);
    UpdateCameraMatrices(c1);
    UpdateCameraMatrices(c0);
    if ((GRef<u8>(GRef<u32>(0x00410B18) + 0x119) & 4) != 0) return;
    func_00220948(c0 + 0x20);
    func_00220958(c0 + 0x60);
    const u32 colour = static_cast<u32>(func_001100A0(c0 + 0x10, GRef<f32>(GRef<u32>(0x00410694) + 0x8C), GRef<f32>(c0 + 4)));
    const u32 t = GRef<u32>(0x00410694);
    func_00220B00(colour, GRef<f32>(t + 0xA8), GRef<f32>(t + 0xA4));
}

// EE: 0x00179008
// Sequence draw (world pass): the effects of the flight clock (0x001558C8), then in bucket 0 a
// ZBUF_1 write with ZMSK set (the qword at main context +0xE0; the GIF tag upper REGS word is not
// written, as the original), the effect markers of this frame (seq+0x128, count +0x12C, 0x001E0A98),
// the ZBUF_1 restore in bucket 0x3FF, then in `chars` bucket 0 ALPHA_2 = 0x44, TEST_2 = 0x70000,
// ZBUF_2 = ZBUF with ZMSK clear, and the character models of the slots placed this frame
// (seq+0x90[i] == 1: 0x002206C0, 0x00220228).
void func_00179008(u32 cam, u32 chars, u32 list) {
    func_001558C8(cam, list, GRef<u32>(g_FlightScene() + 0x1660));
    const auto zbuf = [list](s32 bucket, bool mask) {
        const u32 p = BeginPacket();
        GRef<u32>(p + 4) = 0;
        GRef<u32>(p) = 0x10000002;
        GRef<u32>(p + 8) = 0;
        GRef<u32>(p + 0xC) = 0x50000002;
        GRef<u32>(p + 0x10) = 0x8001;
        GRef<u32>(p + 0x14) = 0x10000000;
        GRef<u32>(p + 0x18) = 0xE;
        CopyQ(p + 0x20, W(0x004104A8) + 0xE0);
        if (mask) GRef<u32>(p + 0x24) = 1;
        EndPacket(list, bucket, p + 0x30);
    };
    zbuf(0, true);
    for (s32 i = 0; i < GRef<s32>(GRef<u32>(0x00410B18) + 0x12C); ++i)
        func_001E0A98(cam, list, W(GRef<u32>(0x00410B18) + 0x128) + static_cast<u32>(i) * 0x40);
    zbuf(0x3FF, false);
    const u32 p = AllocBucketPacket(chars, 0, 4, 0, 0x50000004);
    GRef<u64>(p) = 0x1000400000008003ull;
    GRef<u64>(p + 8) = 0xE;
    GRef<u64>(p + 0x10) = 0x44;
    GRef<u64>(p + 0x18) = 0x43;
    GRef<u64>(p + 0x20) = 0x70000;
    GRef<u64>(p + 0x28) = 0x48;
    GRef<u64>(p + 0x38) = 0x4F;
    GRef<u64>(p + 0x30) = GRef<u64>(W(0x004104A8) + 0xE0) & 0xFFFFFFFEFFFFFFFFull;
    for (u32 i = 0; i < 0x20; ++i) {
        if (W(GRef<u32>(0x00410B18) + 0x90 + i * 4) != 1) continue;
        func_002206C0(W(GRef<u32>(0x00410B18) + 0x10 + i * 4));
        func_00220228(W(GRef<u32>(0x00410B18) + 0x10 + i * 4), chars, list);
    }
}

// EE: 0x00179298
// Sequence draw (screen pass), bucket 0xF: the sky-colour overlay (Eff+0x62448 level, colour
// +0x6244C), the two fades (+0x62440 mode 0, +0x62444 mode 1), the animated-sprite and image
// overlays; then a set-up packet (FRAME_2 from main context +0x150 with FBMSK 0xFFFFFF, +0x180,
// ZBUF from +0x160 with ZMSK, XYOFFSET_2 = 0, TEST_2 = 0x70000; the words the original leaves
// unwritten stay unwritten) and, per queued depth wall (s8 Eff+0x62450, entries {f32 depth,
// u8 alpha} at +0x62454 + 8i), a row of 64-pixel sprites over the whole screen at the projected
// depth (z = (s32)(z_ndc * 16) >> 4), followed by 0x00112450 when any; finally the skip fade-out
// (seq+0x11B / 30, 1.0 from 30 on) while Start-skip is running.
void func_00179298(u32 cam, u32 list) {
    const u32 e = Eff() + 0x60000;
    func_001120E8(GRef<f32>(e + 0x2448), list, 0xF, cam, 2, W(e + 0x244C));
    DrawScreenFade(GRef<f32>(Eff() + 0x62440), list, 0xF, 0, cam);
    DrawScreenFade(GRef<f32>(Eff() + 0x62444), list, 0xF, 1, cam);
    func_00107DC8(W(GRef<u32>(0x00410B18) + 0x120), list, 0xF);
    func_00106B58(W(GRef<u32>(0x00410B18) + 0x124), list, 0xF);
    const s32 h = GRef<s16>(cam + 0x18C);
    const s32 cols = (h + 0x3F) / 0x40;
    {
        const u32 p = BeginPacket();
        GRef<u32>(p + 4) = 0;
        GRef<u32>(p) = 0x10000006;
        GRef<u32>(p + 8) = 0;
        GRef<u32>(p + 0xC) = 0x50000006;
        GRef<u32>(p + 0x10) = 0x8005;
        GRef<u32>(p + 0x14) = 0x10000000;
        GRef<u32>(p + 0x18) = 0xE;
        CopyQ(p + 0x20, W(0x004104A8) + 0x150);
        GRef<u32>(p + 0x24) = 0xFFFFFF;
        CopyQ(p + 0x30, W(0x004104A8) + 0x180);
        CopyQ(p + 0x40, W(0x004104A8) + 0x160);
        GRef<u32>(p + 0x44) = 1;
        GRef<u32>(p + 0x58) = 0x19;
        GRef<u32>(p + 0x50) = 0;
        GRef<u32>(p + 0x54) = 0;
        GRef<u32>(p + 0x60) = 0x70000;
        GRef<u32>(p + 0x68) = 0x48;
        EndPacket(list, 0xF, p + 0x70);
    }
    for (s32 i = 0; i < GRef<s8>(Eff() + 0x62450); ++i) {
        guest::GuestFrame frame(0x10);
        const u32 v = frame.Address();
        const u32 ent = Eff() + 0x62454 + static_cast<u32>(i) * 8;
        ZeroQ(v);  // memset(sp, 0, 16)
        GRef<f32>(v + 0xC) = 1.0f;
        GRef<f32>(v + 8) = GRef<f32>(ent);
        sceVu0ApplyMatrix(v, cam + 0x60, v);
        sceVu0ScaleVector(v, v, 1.0f / GRef<f32>(v + 0xC));
        const s32 z = fpu::FloatToInt(GRef<f32>(v + 8) * 16.0f);
        const u32 p = BeginPacket();
        const u32 qw = static_cast<u32>(cols + 3);
        GRef<u32>(p + 4) = 0;
        GRef<u32>(p) = qw | 0x10000000;
        GRef<u32>(p + 8) = 0;
        GRef<u32>(p + 0xC) = qw | 0x50000000;
        GRef<u32>(p + 0x10) = 1;
        GRef<u32>(p + 0x18) = 0x10;
        GRef<u32>(p + 0x14) = 0x24000000;
        GRef<u64>(p + 0x20) = 0x206;
        GRef<f32>(p + 0x2C) = 1.0f;
        GRef<u32>(p + 0x28) = (static_cast<u32>(GRef<u8>(ent + 4)) << 24) | 0x808080;
        GRef<u32>(p + 0x34) = 0x24000000;
        GRef<u32>(p + 0x38) = 0x44;
        GRef<u32>(p + 0x30) = static_cast<u32>(cols) | 0x8000;
        u32 q = p + 0x40;
        u32 x = 0;
        for (s32 c = 0; c < cols; ++c) {
            GRef<u32>(q) = x;
            GRef<u32>(q + 4) = static_cast<u32>(z >> 4);
            GRef<u32>(q + 8) = (x + 0x400) | (static_cast<u32>(GRef<u16>(cam + 0x18E)) << 20);
            GRef<u32>(q + 0xC) = static_cast<u32>(z >> 4);
            x += 0x400;
            q += 0x10;
        }
        EndPacket(list, 0xF, (q + 0xF) & 0xFFFFFFF0u);
    }
    if (0 < GRef<s8>(Eff() + 0x62450)) func_00112450(list, 0xF, cam, W(0x00410B14));
    const u32 s = GRef<u32>(0x00410B18);
    if ((GRef<u8>(s + 0x119) & 8) == 0) return;
    const f32 t = GRef<u8>(s + 0x11B) < 0x1E ? fpu::IntToFloat(GRef<u8>(s + 0x11B)) / 30.0f : 1.0f;
    DrawScreenFade(t, list, 0xF, 0, cam);
}

// EE: 0x001796E0
// Dead (no reference): out = v rotated about X, Y, Z by the components of a zeroed local and then
// offset by another zeroed local (both never written, as the original): a copy of v through VU0.
void func_001796E0(u32 out, u32 v) {
    guest::GuestFrame frame(0x60);
    const u32 m = frame.Address(), ang = m + 0x40, ofs = m + 0x50;
    for (u32 o = 0; o < 0x20; o += 4) GRef<u32>(ang + o) = 0;
    sceVu0UnitMatrix(m);
    sceVu0RotMatrixX(m, m, GRef<f32>(ang));
    sceVu0RotMatrixY(m, m, GRef<f32>(ang + 4));
    sceVu0RotMatrixZ(m, m, GRef<f32>(ang + 8));
    sceVu0ApplyMatrix(out, m, v);
    sceVu0AddVector(out, out, ofs);
}

// EE: 0x00179780
// Euler angles `in` -> rotation matrix (0x00102D28 on a unit matrix) -> rotated about X, Y, Z by
// the components of a zeroed local that is never written (as the original) -> angles `out`.
void func_00179780(u32 out, u32 in) {
    guest::GuestFrame frame(0x50);
    const u32 m = frame.Address();
    const u32 zero = m + 0x40;
    ZeroQ(zero);
    sceVu0UnitMatrix(m);
    func_00102D28(m, m, in);
    sceVu0RotMatrixX(m, m, GRef<f32>(zero));
    sceVu0RotMatrixY(m, m, GRef<f32>(zero + 4));
    sceVu0RotMatrixZ(m, m, GRef<f32>(zero + 8));
    func_00103290(out, m);
}

}  // namespace ac04::game
