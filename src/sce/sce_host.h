// Host side of the SDK layer: what src/sce needs from the platform (disc image, pads, IOP
// stand-ins) and the state it exposes back (DMA/GS counters for the renderer and the overlay).
// main.cpp fills SceHost before starting the game thread. The host only touches this state while
// the game thread is parked at a vsync (ADR-002 lockstep), so no locking is needed.
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "common/types.h"
#include "platform/input.h"
#include "platform/iop/iop_services.h"
#include "platform/iso9660.h"
#include "platform/memcard/folder_card.h"
#include "platform/render/gs/gs_source_mark.h"
#include "platform/render/vu1/vif1.h"

namespace ac04::sce {

struct SceHost {
    const platform::Iso9660* iso = nullptr;
    std::function<platform::PadState(int port)> read_pad;
    std::vector<std::unique_ptr<platform::iop::RpcService>> iop_services;
    // Memory card in slot 1 (port 0); null = no card. Slot 2 is always empty.
    std::unique_ptr<platform::memcard::FolderCard> memcard;
    // Called on the game thread once per vblank (sceGsSyncV), before the wait: advances the IOP
    // stand-ins that are clocked by game steps (SOUND mixer).
    std::function<void()> on_vblank;
    // Development capture (--dump-dma-at): the step that starts at vblank `dma_capture_at` records
    // every VIF1 word with its guest address (0 = from a tag) and its GIF output; at the step's end
    // on_dma_capture receives (vblank, vif1 pairs, gif bytes) on the game thread.
    u64 dma_capture_at = 0;
    std::function<void(u64, const std::vector<u32>&, const std::vector<u8>&)> on_dma_capture;
    // Movie upscaling side path (ADR-006 §5): set by the host when the upscaling stage runs
    // (internal scale > 1). sceMpegGetPicture then also hands each decoded picture to
    // DmaStats::movie_picture, and GIF transfers from its image buffer get source marks.
    bool movie_pictures = false;
    // Vibration (scePadSetActDirect): per port the small motor (0 off, 1 on) and the large motor
    // (0-255) as the game last set them. The host passes them to the gamepad between steps.
    std::array<std::array<u8, 2>, 2> pad_motors{};
};

// A decoded movie picture handed to the renderer (presentation only; never read back by the game).
struct MoviePicture {
    u64 serial = 0;        // increases with every decoded picture; 0 = none yet
    u32 guest_addr = 0;    // the game's image buffer: 16-pixel columns, each `height` rows, column after column
    u32 guest_bytes = 0;
    s32 width = 0, height = 0;
    std::vector<u8> rgba;  // the same picture, row-major RGBA8 (alpha as the guest buffer holds it)
};

// A block of GIF data of a step that the DMA read from guest memory (only blocks inside the movie
// image buffer are marked).
using GsSourceMark = render::gs::SourceMark;

// Packet intake statistics (milestone C: packets are counted, not drawn).
struct DmaStats {
    std::array<u64, 10> sends{};    // sceDmaSend calls per channel
    std::array<u64, 10> qwords{};   // qwords walked per channel (all time)
    u64 frame_qwords_gif = 0;       // GIF (ch 2) qwords in the last completed step
    u64 frame_qwords_vif1 = 0;      // VIF1 (ch 1) qwords in the last completed step
    u64 step_qwords_gif = 0;        // accumulating in the current step
    u64 step_qwords_vif1 = 0;
    u64 bad_chains = 0;             // chains with an unknown tag or that did not end
    // Host time spent in the VIF1 sends (sceDmaSend channel 1: VIF unpacking and the VU1 HLE
    // programs), all time; the host reads it around each tick (performance figures only).
    u64 vif1_ns = 0;
    // Last values written by sceGsPutDispEnv (PMODE, SMODE2, DISPFB, DISPLAY, BGCOLOR).
    std::array<u64, 5> dispenv{};
    // GIF data (PATH2 from VIF1 DIRECT, PATH3) of the current step, and of the completed steps not
    // yet taken by the renderer (in order).
    std::vector<u8> gs_step;
    std::vector<std::vector<u8>> gs_steps;
    // Step buffers the renderer has finished with, kept with their capacity for the next steps
    // (a hangar step is about 3 MB; regrowing it every step was a measurable cost).
    std::vector<std::vector<u8>> gs_step_pool;
    // Source marks of gs_step / gs_steps (same order), and the latest movie picture (side path).
    std::vector<GsSourceMark> gs_step_marks;
    std::vector<std::vector<GsSourceMark>> gs_steps_marks;
    MoviePicture movie_picture;
    // Frame interpolation (platform/render/frame_interpolator.h): while the host sets record_runs,
    // the VU1 program runs of the current step and of the completed steps (same order as gs_steps).
    bool record_runs = false;
    render::vu1::RunLog gs_step_runs;
    std::vector<render::vu1::RunLog> gs_steps_runs;
    // Run logs the host has finished with, kept with their capacity (a flight step records about
    // 4 MB; a fresh log every step cost more than the VU1 programs it records).
    std::vector<render::vu1::RunLog> gs_runs_pool;
    // --dump-dma-at: capture of the current step (pairs guest address, word).
    bool capture_active = false;
    std::vector<u32> capture_vif1;
};

SceHost& Host();
DmaStats& Dma();

// 2 MiB of IOP RAM: sceCdReadIOPm reads land here (stream rings); the EE never reads it.
std::vector<u8>& IopRam();

// Unimplemented or stubbed SDK entry: logged once per name.
void LogUnimplemented(const char* name);
#define AC04_SCE_STUB(name) ::ac04::sce::LogUnimplemented(name)

}  // namespace ac04::sce
