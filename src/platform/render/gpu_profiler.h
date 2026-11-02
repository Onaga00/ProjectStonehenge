// GPU time of the presented frames, from timestamp queries (docs/port/renderer.md §3).
// - Always: two timestamps per frame (start and end of its command buffer): the frame's GPU time.
// - Detailed (--gpu-profile): one more timestamp at the end of each section of the frame's work
//   (GsBackend::Record, CopyPresent, the output pass). The GPU time from the previous timestamp to
//   this one is the section's: what the section's commands added, including the wait of the
//   barrier in front of them for the earlier work. Timestamps are written at the bottom of the
//   pipe: they wait for nothing and add no barrier.
// Results are read when the frame's fence has been waited on (no stall), so the figures of a
// frame arrive kFramesInFlight frames later.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include "platform/render/vk_context.h"

namespace ac04::render {

enum class GpuSection : std::uint8_t {
    kTextureWork,   // memory updates, GPU decodes and uploads of the frame's textures
    kDraws,         // GS render passes (draws, depth fills, their load / store)
    kFrameTexture,  // frame buffer read as a texture: the copy of the area a draw reads
    kField,         // field copy of the draw buffer into the image to present
    kDstRegion,     // DATE / shader-blend region copies
    kPalette,       // GPU-drawn palettes
    kSync,          // view syncs (another format of the same memory converted into a target)
    kConvert,       // frame buffer converted through the GS memory layout for a draw
    kDepthCopy,     // Z buffer copied into another one
    kAntiAlias,     // FXAA / SMAA of the flight views
    kPresentCopy,   // presented image copied into the native target
    kMovie,         // movie upscaling stage
    kOutput,        // output pass and overlays (includes the wait for the swapchain image)
    kCount
};

class GpuProfiler {
public:
    // `detailed`: per-section timestamps. Without timestamp support on the queue every call is a no-op.
    void Init(VkContext& ctx, int frames, bool detailed);
    void Shutdown();
    bool Detailed() const { return pool_ != VK_NULL_HANDLE && detailed_; }

    // At the start of frame slot `frame`'s command buffer, after its fence was waited on: takes the
    // slot's previous results, resets its queries and writes the frame's first timestamp.
    void BeginFrame(VkCommandBuffer cmd, int frame);
    // End of a section (detailed only). For kDraws: `tag` names the pass's target (GsBackend:
    // FBP | FBW << 9 | layout class << 15) and `draws` counts its draw calls.
    void Mark(VkCommandBuffer cmd, GpuSection s, std::uint32_t tag = 0, std::uint32_t draws = 0) {
        if (!Detailed() || recording_ < 0) return;
        Write(cmd, s, tag, draws);
    }
    // End of the frame: the last section. `drawn`: the frame recorded GS draws (not only the
    // image of an earlier frame presented again); the sections are averaged over those frames.
    void EndFrame(VkCommandBuffer cmd, GpuSection last, bool drawn);
    // Takes the results still pending (after vkDeviceWaitIdle).
    void CollectAll();

    struct Totals {
        std::uint64_t frames = 0;  // every presented frame
        double total_ms = 0.0;
        std::uint64_t drawn_frames = 0;  // frames with GS draws: the sections are theirs
        double drawn_ms = 0.0;
        double max_ms = 0.0;
        std::array<double, static_cast<std::size_t>(GpuSection::kCount)> ms{};
        std::array<std::uint64_t, static_cast<std::size_t>(GpuSection::kCount)> count{};
        std::uint64_t dropped_marks = 0;  // marks over the per-frame budget (their time goes to the next one)
        // kDraws by the pass's target tag.
        struct Passes {
            double ms = 0.0;
            std::uint64_t passes = 0, draws = 0;
        };
        std::map<std::uint32_t, Passes> draws_by_target;
    };
    const Totals& Session() const { return totals_; }
    double LastFrameMs() const { return last_ms_; }
    static const char* Name(GpuSection s);

private:
    static constexpr std::uint32_t kMaxQueries = 2048;  // per frame slot
    void Write(VkCommandBuffer cmd, GpuSection s, std::uint32_t tag, std::uint32_t draws);
    void Collect(int frame);

    VkContext* ctx_ = nullptr;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    bool detailed_ = false;
    double ns_per_tick_ = 1.0;
    std::uint64_t mask_ = ~0ull;
    int recording_ = -1;  // slot of the command buffer being recorded
    struct Slot {
        std::uint32_t used = 0;             // queries written
        bool drawn = false;
        std::vector<GpuSection> sections;  // section ended by query i (i >= 1)
        std::vector<std::pair<std::uint32_t, std::uint32_t>> tags;  // its tag and draw count
    };
    std::vector<Slot> slots_;
    std::vector<std::uint64_t> results_;
    Totals totals_;
    double last_ms_ = 0.0;
};

}  // namespace ac04::render
