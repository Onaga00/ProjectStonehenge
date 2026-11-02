// GS draw state keys the game is known to use (gs_vulkan.cpp PipelineKey: blend enable, Vulkan
// blend factors and op, colour write mask, depth compare op, depth write). Their pipelines (SplitKey:
// without the depth state, and for blended keys without the alpha write, set per draw since
// 2026-10-30) are built by warm-up threads at startup (GsBackend::StartPipelineWarmUp), so a new GPU
// driver compiles them before the first 3D frame instead of inside frames (docs/port/validation.md §2ai). These are the renderer's own
// draw states, not game data.
//
// Collected 2026-10-25 with tools/research/pipeline_keys.py from the session logs of every
// tools/research/full.json job (front end, VS mode, shop, hangar routes, opening, missions 1-18)
// and the User's mission 1 input log. A key missing here is still built when first drawn (logged
// at shutdown as "built during frames"): add it here.
#pragma once

#include <cstdint>

namespace ac04::render::gs {

constexpr std::uint64_t kGsPipelineKeys[] = {
    0x0470011F,
    0x0470101F,
    0x04800002,
    0x04F00002,
    0x04F0011F,
    0x04F0101F,
    0x0670011F,
    0x0670101F,
    0x06F00111,
    0x06F0011F,
    0x06F0101F,
    0x07300002,
    0x07700002,
    0x07700111,
    0x0770011F,
    0x0770101F,
    0x07800002,
    0x07F00002,
    0x07F0001F,
    0x07F00101,
    0x07F00111,
    0x07F0011F,
    0x07F0101F,
    0x07F1011F,
    0x0C700002,
    0x0CF00002,
    0x0CF0011F,
    0x0CF0101F,
    0x0EF00002,
    0x0EF00111,
    0x0EF0011F,
    0x0EF00F01,
    0x0EF0101F,
    0x0FF00002,
};

}  // namespace ac04::render::gs
