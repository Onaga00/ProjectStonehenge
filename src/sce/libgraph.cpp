// libgraph, libdma, libpkt and libvu0 entry points the game calls.
// GS state lives where the SDK keeps it (sceGsGetGParam block 0x003FAAA0 in .data), so translated
// code and RAM comparisons see the same values as on the PS2. DMA chains are walked and counted
// (milestone C); nothing is drawn yet.
#include <chrono>
#include <cmath>
#include <cstring>

#include <vector>

#include "common/guest_ref.h"
#include "common/port_hooks.h"
#include "platform/log.h"
#include "platform/render/vu1/vif1.h"
#include "sce/ee_hw.h"
#include "sce/hw_registers.h"
#include "sce/runtime.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

using ac04::guest::GRef;

namespace {

// sceGsGParam: {s16 interlace, s16 omode, s16 ffmode, s16 version, u32 sync handler state, u32 id}
constexpr u32 kGParam = 0x003FAAA0u;
constexpr u32 kDmaChanTable = 0x003FAAD0u;  // 10 channel register addresses (.data)

s16& GParam(u32 index) { return GRef<s16>(kGParam + 2 * index); }

// GS CSR.FIELD as sceGsSyncV reads it (field of the vblank just started).
u32 g_csr_field = 0;

u32 ChannelIndex(u32 chan) {
    for (u32 i = 0; i < 10; ++i) {
        if (GRef<u32>(kDmaChanTable + 4 * i) == chan) return i;
    }
    return 10;
}

// Walks a source-chain DMA list (tag modes cnt/next/ref/refs/refe/call/ret/end) and returns the
// number of qwords it transfers. Stops on end tags, a bad tag or a runaway chain. `on_tag` gets the
// address of each tag (for TTE), `on_data` each data block (guest address, qwords).
template <typename OnTag, typename OnData>
u64 WalkChain(u32 tag_addr, bool& ok, OnTag on_tag, OnData on_data) {
    u64 qwords = 0;
    u32 call_stack[2] = {};
    u32 asp = 0;
    ok = true;
    for (u32 n = 0; n < 0x100000; ++n) {
        const u64 tag = GRef<u64>(tag_addr);
        const u32 qwc = static_cast<u32>(tag & 0xFFFF);
        const u32 id = static_cast<u32>(tag >> 28) & 7;
        const u32 addr = static_cast<u32>(tag >> 32) & 0x7FFFFFF0u;
        qwords += qwc;
        on_tag(tag_addr);
        switch (id) {
            case 0:  // refe
                on_data(addr, qwc);
                return qwords;
            case 1:  // cnt
                on_data(tag_addr + 16, qwc);
                tag_addr = tag_addr + 16 + qwc * 16;
                break;
            case 2:  // next
                on_data(tag_addr + 16, qwc);
                tag_addr = addr;
                break;
            case 3:  // ref
            case 4:  // refs
                on_data(addr, qwc);
                tag_addr += 16;
                break;
            case 5:  // call
                on_data(tag_addr + 16, qwc);
                if (asp >= 2) {
                    ok = false;
                    return qwords;
                }
                call_stack[asp++] = tag_addr + 16 + qwc * 16;
                tag_addr = addr;
                break;
            case 6:  // ret
                on_data(tag_addr + 16, qwc);
                if (asp == 0) return qwords;
                tag_addr = call_stack[--asp];
                break;
            case 7:  // end
                on_data(tag_addr + 16, qwc);
                return qwords;
        }
    }
    ok = false;
    return qwords;
}

// VIF1 (DIRECT data, UNPACK into VU1 memory, microcode uploads and the VU1 programs' XGKICK
// output): one persistent instance, since VU1 memory and the resident program outlive a send.
ac04::render::vu1::Vif1& Vif1Unit(std::vector<u8>& gif) {
    static ac04::render::vu1::Vif1 vif(gif);
    vif.SetOutput(gif);
    auto& dma = ac04::sce::Dma();
    vif.SetRunLog(dma.record_runs ? &dma.gs_step_runs : nullptr);
    return vif;
}

}  // namespace

// ---- libgraph -------------------------------------------------------------------------------

// PS2 (0x002616E8): mode 0 resets the GS (CSR.RESET), stores interlace/omode/ffmode and the GS
// revision in the GParam block, masks GS interrupts and calls SetGsCrt; mode 1 flushes (CSR.FLUSH);
// mode 5 only re-sets the CRT. Returns nothing useful (v0 undefined); the game ignores it.
s32 sceGsResetGraph(s16 mode, s16 inter, s16 omode, s16 ffmode) {
    if (mode == 0 || mode == 5) {
        GParam(0) = inter;
        GParam(1) = omode;
        GParam(2) = static_cast<s16>(ffmode != 0 ? 1 : 0);
        GParam(3) = 0x1B;  // HYPOTHESIS: CSR.REV of the GS in PCSX2 (check in the RAM diff)
        if (mode == 0) {
            GRef<u32>(kGParam + 8) = 0;
            GRef<u32>(kGParam + 12) = 0;
        }
    }
    return 0;
}

// PS2 (0x00261888): resets VIF1/GIF paths and the GIF FIFO. Nothing to do on the host.
void sceGsResetPath() {}

// PS2 (0x002618F0): fills sceGsDispEnv {pmode, smode2, dispfb, display, bgcolor} from GParam:
// PMODE 0x66; SMODE2 = 2 (progressive), 3 (interlaced field), 1 (interlaced frame); DISPFB.FBW =
// (w+63)/64, PSM; DISPLAY: MAGH = (w+0x9FF)/w - 1, DW = MAGH'*w - 1, DX = dx*MAGH' + 0x27C (NTSC) /
// 0x290 (PAL), DY = dy + 0x19/0x32 (NTSC) or 0x24/0x48 (PAL), DH = h-1 (h*2-1 interlaced frame).
void sceGsSetDefDispEnv(u32 disp, s16 psm, s16 w, s16 h, s16 dx, s16 dy) {
    const s16 inter = GParam(0);
    const s16 omode = GParam(1);
    const s16 ffmode = GParam(2);
    GRef<u64>(disp) = 0x66;
    GRef<u64>(disp + 8) = inter == 0 ? 2 : (ffmode == 0 ? 1 : 3);
    GRef<u64>(disp + 16) = (static_cast<u64>(static_cast<s64>(psm)) & 0xF) << 15 |
                           (static_cast<u64>(static_cast<s64>((w + 0x3F) >> 6)) & 0x3F) << 9;
    if (omode != 2 && omode != 3) {
        // The SDK prints "sceGsDefDispEnv:Not support displaymode for %d!!" and leaves DISPLAY.
        AC04_LOG_WARN("sce", "sceGsSetDefDispEnv: unsupported display mode %d", omode);
        GRef<u64>(disp + 32) = 0;
        return;
    }
    const s32 iw = w, ih = h, idx = dx, idy = dy;
    const s32 mag = (iw + 0x9FF) / iw;
    const s32 xoff = omode == 2 ? 0x27C : 0x290;
    s32 y;
    s32 dh;
    if (inter != 1) {
        y = idy + (omode == 2 ? 0x19 : 0x24);
        dh = ih - 1;
    } else {
        y = idy + (omode == 2 ? 0x32 : 0x48);
        dh = ffmode == 0 ? ih - 1 : ih * 2 - 1;
    }
    GRef<u64>(disp + 24) = static_cast<u64>(static_cast<s64>(mag - 1)) << 23 |
                           static_cast<u64>(static_cast<s64>(mag * iw - 1)) << 32 |
                           (static_cast<u64>(static_cast<s64>(idx * mag + xoff)) & 0xFFF) |
                           static_cast<u64>(static_cast<s64>(dh)) << 44 |
                           (static_cast<u64>(static_cast<s64>(y)) & 0xFFF) << 12;
    GRef<u64>(disp + 32) = 0;
}

// PS2 (0x00261B60): writes PMODE, SMODE2, DISPFB2, DISPLAY2, BGCOLOR (or circuit 1 for GS
// revision 1). The port records the values for the renderer.
void sceGsPutDispEnv(u32 disp) {
    auto& d = ac04::sce::Dma().dispenv;
    for (u32 i = 0; i < 5; ++i) d[i] = GRef<u64>(disp + 8 * i);
}

namespace {

// One vblank passes: the step's GS data goes to the renderer, the IOP stand-ins advance, the game
// thread parks until the vblank, timer 0 advances. Returns CSR.FIELD.
u32 PassVBlank() {
    auto& rt = ac04::sce::GameRuntime::Get();
    auto& dma = ac04::sce::Dma();
    dma.frame_qwords_gif = dma.step_qwords_gif;
    dma.frame_qwords_vif1 = dma.step_qwords_vif1;
    dma.step_qwords_gif = 0;
    dma.step_qwords_vif1 = 0;
    // The GS data of the step goes to the renderer (taken by the host while the game is parked).
    if (dma.capture_active) {
        dma.capture_active = false;
        if (ac04::sce::Host().on_dma_capture)
            ac04::sce::Host().on_dma_capture(rt.VBlankCount(), dma.capture_vif1, dma.gs_step);
        dma.capture_vif1.clear();
    }
    dma.gs_steps.push_back(std::move(dma.gs_step));
    dma.gs_step.clear();
    if (!dma.gs_step_pool.empty()) {
        dma.gs_step = std::move(dma.gs_step_pool.back());
        dma.gs_step_pool.pop_back();
        dma.gs_step.clear();
    }
    dma.gs_steps_marks.push_back(std::move(dma.gs_step_marks));
    dma.gs_step_marks.clear();
    dma.gs_steps_runs.push_back(std::move(dma.gs_step_runs));
    dma.gs_step_runs.Clear();
    if (!dma.gs_runs_pool.empty()) {
        dma.gs_step_runs = std::move(dma.gs_runs_pool.back());
        dma.gs_runs_pool.pop_back();
        dma.gs_step_runs.Clear();
    }
    if (ac04::sce::Host().on_vblank) ac04::sce::Host().on_vblank();
    g_csr_field = rt.WaitVBlank();
    ac04::sce::EeRegisters::Get().OnVBlank(rt.VBlankCount());
    if (ac04::sce::Host().dma_capture_at != 0 && rt.VBlankCount() == ac04::sce::Host().dma_capture_at)
        dma.capture_active = true;
    return g_csr_field;
}

}  // namespace

// PS2 (0x00261C20): waits for the start of the next vblank and returns CSR.FIELD for interlaced
// modes (1 otherwise). Port: parks the game thread until the host releases the next 59.94 Hz tick
// (ADR-002); the field alternates per tick.
s32 sceGsSyncV(s32 /*mode*/) {
    PassVBlank();
    return GParam(0) == 1 ? static_cast<s32>(g_csr_field) : 1;
}

namespace ac04::sce {

// The game busy-waits on EE timer 0 (MovieFrameSync): each poll lets one H-blank pass; at the end
// of the field the vblank passes (the game thread parks until the next 59.94 Hz tick).
void EeHwWaitTimer() {
    if (!EeRegisters::Get().AdvanceLine()) PassVBlank();
}

}  // namespace ac04::sce

// PS2 (0x00261CB8): waits until the VIF1/GIF paths are idle (mode 0) and returns 0. Transfers
// complete at once in the port.
s32 sceGsSyncPath(s32 /*mode*/, u16 /*timeout*/) { return 0; }

s32 sceGsSetDefLoadImage(u32, s16, s16, s16, s16, s16, s16, s16) {
    AC04_SCE_STUB("sceGsSetDefLoadImage");
    return -1;
}

s32 sceGsExecLoadImage(u32, u32) {
    AC04_SCE_STUB("sceGsExecLoadImage");
    return -1;
}

// ---- libdma ---------------------------------------------------------------------------------

// PS2 (0x00262370): returns the register block address of channel `id` (table 0x003FAAD0), 0 for
// an id above 9.
u32 sceDmaGetChan(s32 id) {
    if (static_cast<u32>(id) >= 10) return 0;
    return GRef<u32>(kDmaChanTable + 4u * static_cast<u32>(id));
}

// PS2 (0x00262398): mode 1 resets the DMAC (all channel registers, D_CTRL enabled, priorities).
// Port: clears the channel registers kept in EeRegisters and enables the DMAC.
s32 sceDmaReset(s32 mode) {
    if (mode == 1) {
        auto& regs = ac04::sce::EeRegisters::Get();
        for (u32 ch = 0; ch < 10; ++ch) {
            const u32 base = GRef<u32>(kDmaChanTable + 4 * ch);
            if (base == 0) continue;
            for (u32 off = 0; off < 0x90; off += 0x10) regs.SetWord(base + off, 0);
        }
        regs.SetWord(0x1000E000, 1);  // D_CTRL.DMAE
        regs.SetWord(0x1000E010, 0);  // D_STAT
        regs.SetWord(0x1000E020, 0);  // D_PCR
        regs.SetWord(0x1000E030, 0);  // D_SQWC
    }
    return 0;
}

// PS2 (0x00262C10): mode 1 returns the busy bit (CHCR.STR) of `chan`; any other mode waits until
// the channel is idle (polling at most `timeout` times, 0x1000000 when 0; on expiry it prints a
// message and clears STR) and returns 0. Port: every transfer completes when it is started, so the
// channel is idle; a STR left set is cleared as the timeout path does.
s32 sceDmaSync(u32 chan, s32 mode, s32 timeout) {
    (void)timeout;
    const u32 chcr = ac04::sce::EeDmaReadChcr(chan);
    if (mode == 1) return static_cast<s32>((chcr >> 8) & 1u);
    if ((chcr & 0x100u) != 0) ac04::sce::EeDmaWriteChcr(chan, chcr & ~0x100u);
    return 0;
}

// PS2 (0x002626C0): starts a source-chain transfer on `chan` from tag address `tag` (waits for the
// channel to be idle first). Port: walks the chain and counts packets/qwords per channel; the
// transfer completes at once.
s32 sceDmaSend(u32 chan, u32 tag) {
    auto& dma = ac04::sce::Dma();
    const u32 ch = ChannelIndex(chan);
    if (ch >= 10) return -1;
    bool ok = true;
    u64 qw = 0;
    std::vector<u8>& gif = dma.gs_step;
    if (ch == 1) {
        // Host time of the send (VIF1 and the VU1 HLE), for the performance figures.
        const auto start = std::chrono::steady_clock::now();
        struct AddTime {
            u64& total;
            std::chrono::steady_clock::time_point start;
            ~AddTime() {
                total += static_cast<u64>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
            }
        } add_time{dma.vif1_ns, start};
        // VIF1: with CHCR.TTE the upper 64 bits of each tag go to the VIF as two VIF codes.
        const bool tte = (ac04::sce::EeDmaReadChcr(chan) & 0x40) != 0;
        ac04::render::vu1::Vif1& vif = Vif1Unit(gif);
        qw = WalkChain(
            tag & 0x7FFFFFF0u, ok,
            [&](u32 t) {
                // Anti-aliasing point (port_hooks.h): a flight view's overlay table starts here.
                for (const ac04::port::RenderMarks::Point& m : ac04::port::Render().sending) {
                    if (((m.tag ^ t) & 0x0FFFFFF0u) != 0) continue;  // any RAM alias
                    ac04::render::gs::SourceMark mark;
                    mark.offset = static_cast<u32>(gif.size());
                    mark.kind = ac04::render::gs::SourceMark::kAntiAlias;
                    mark.rect[0] = m.x0;
                    mark.rect[1] = m.y0;
                    mark.rect[2] = m.x1;
                    mark.rect[3] = m.y1;
                    dma.gs_step_marks.push_back(mark);
                }
                if (tte) {
                    if (dma.capture_active) {
                        for (u32 o : {8u, 12u}) {
                            dma.capture_vif1.push_back(t + o);
                            dma.capture_vif1.push_back(GRef<u32>(t + o));
                        }
                    }
                    vif.Word(GRef<u32>(t + 8), t + 8);
                    vif.Word(GRef<u32>(t + 12), t + 12);
                }
            },
            [&](u32 addr, u32 qwc) {
                if (dma.capture_active) {
                    for (u32 i = 0; i < qwc * 4; ++i) {
                        dma.capture_vif1.push_back(addr + 4 * i);
                        dma.capture_vif1.push_back(GRef<u32>(addr + 4 * i));
                    }
                }
                if (const u8* p = ac04::guest::Mem().Translate(addr, qwc * 16)) {
                    vif.Words(p, qwc * 4, addr);
                } else {
                    for (u32 i = 0; i < qwc * 4; ++i) vif.Word(GRef<u32>(addr + i * 4), addr + i * 4);
                }
            });
    } else if (ch == 2) {
        // GIF (PATH3): the data is GIF packets.
        qw = WalkChain(
            tag & 0x7FFFFFF0u, ok, [](u32) {},
            [&](u32 addr, u32 qwc) {
                const u8* p = ac04::guest::Mem().Translate(addr, qwc * 16);
                if (p == nullptr) return;
                // Movie upscaling side path: mark data read from the movie image buffer.
                const ac04::sce::MoviePicture& mp = dma.movie_picture;
                if (ac04::sce::Host().movie_pictures && mp.serial != 0 && addr >= mp.guest_addr &&
                    addr - mp.guest_addr < mp.guest_bytes) {
                    dma.gs_step_marks.push_back({static_cast<u32>(gif.size()), qwc * 16, addr});
                }
                gif.insert(gif.end(), p, p + qwc * 16);
            });
    } else {
        qw = WalkChain(tag & 0x7FFFFFF0u, ok, [](u32) {}, [](u32, u32) {});
    }
    if (!ok) {
        ++dma.bad_chains;
        ac04::log::WriteOnce("dma:badchain", ac04::log::Level::Warning, "dma",
                             "DMA chain on channel %u did not end cleanly (counted only)", ch);
    }
    dma.sends[ch] += 1;
    dma.qwords[ch] += qw;
    if (ch == 2) dma.step_qwords_gif += qw;
    if (ch == 1) dma.step_qwords_vif1 += qw;
    return 0;
}

// ---- libpkt (not reached on the boot path) --------------------------------------------------
void sceVif1PkInit(u32, u32) { AC04_SCE_STUB("sceVif1PkInit"); }
void sceVif1PkReset(u32) { AC04_SCE_STUB("sceVif1PkReset"); }
u32 sceVif1PkTerminate(u32) {
    AC04_SCE_STUB("sceVif1PkTerminate");
    return 0;
}
u32 sceVif1PkSize(u32) {
    AC04_SCE_STUB("sceVif1PkSize");
    return 0;
}
void sceVif1PkEnd(u32, u32) { AC04_SCE_STUB("sceVif1PkEnd"); }

// ---- libvu0 (matrix and vector functions: libvu0.cpp) -----------------------------------------

// PS2 (0x00263BF0, hypothesis name): resets VIF0. Nothing to do on the host.
void sceVu0ResetVif0() {}
