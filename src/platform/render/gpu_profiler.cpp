#include "platform/render/gpu_profiler.h"

#include <algorithm>
#include <iterator>

namespace ac04::render {

void GpuProfiler::Init(VkContext& ctx, int frames, bool detailed) {
    ctx_ = &ctx;
    detailed_ = detailed;
    std::uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical_device, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(ctx.physical_device, &n, families.data());
    const std::uint32_t bits = ctx.queue_family < n ? families[ctx.queue_family].timestampValidBits : 0;
    if (bits == 0) return;  // no timestamps on this queue: everything stays a no-op
    mask_ = bits >= 64 ? ~0ull : (1ull << bits) - 1;
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physical_device, &props);
    ns_per_tick_ = props.limits.timestampPeriod;
    VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kMaxQueries * static_cast<std::uint32_t>(frames);
    if (vkCreateQueryPool(ctx.device, &qi, nullptr, &pool_) != VK_SUCCESS) {
        pool_ = VK_NULL_HANDLE;
        return;
    }
    slots_.assign(static_cast<std::size_t>(frames), Slot{});
    for (auto& s : slots_) {
        s.sections.reserve(detailed ? 512 : 2);
        s.tags.reserve(detailed ? 512 : 2);
    }
    results_.resize(kMaxQueries);
}

void GpuProfiler::Shutdown() {
    if (pool_) vkDestroyQueryPool(ctx_->device, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    slots_.clear();
    recording_ = -1;
}

const char* GpuProfiler::Name(GpuSection s) {
    static const char* const kNames[] = {"texture work",  "draws",          "frame as texture",
                                         "field copy",    "DATE / shader-blend regions",
                                         "GPU palettes",  "view syncs",     "conversions",
                                         "depth copies",  "anti-aliasing",  "present copy",
                                         "movie upscaling", "output pass"};
    static_assert(std::size(kNames) == static_cast<std::size_t>(GpuSection::kCount));
    const auto i = static_cast<std::size_t>(s);
    return i < std::size(kNames) ? kNames[i] : "?";
}

void GpuProfiler::Write(VkCommandBuffer cmd, GpuSection s, std::uint32_t tag, std::uint32_t draws) {
    Slot& slot = slots_[static_cast<std::size_t>(recording_)];
    // The last query is kept for EndFrame.
    if (slot.used + 1 >= kMaxQueries) {
        ++totals_.dropped_marks;
        return;
    }
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_,
                        static_cast<std::uint32_t>(recording_) * kMaxQueries + slot.used);
    ++slot.used;
    slot.sections.push_back(s);
    slot.tags.emplace_back(tag, draws);
}

void GpuProfiler::BeginFrame(VkCommandBuffer cmd, int frame) {
    if (!pool_) return;
    Collect(frame);
    Slot& slot = slots_[static_cast<std::size_t>(frame)];
    vkCmdResetQueryPool(cmd, pool_, static_cast<std::uint32_t>(frame) * kMaxQueries, kMaxQueries);
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, static_cast<std::uint32_t>(frame) * kMaxQueries);
    slot.used = 1;
    slot.sections.clear();
    slot.sections.push_back(GpuSection::kCount);  // query 0 ends no section
    slot.tags.clear();
    slot.tags.emplace_back(0, 0);
    recording_ = frame;
}

void GpuProfiler::EndFrame(VkCommandBuffer cmd, GpuSection last, bool drawn) {
    if (!pool_ || recording_ < 0) return;
    Slot& slot = slots_[static_cast<std::size_t>(recording_)];
    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_,
                        static_cast<std::uint32_t>(recording_) * kMaxQueries + slot.used);
    ++slot.used;
    slot.sections.push_back(last);
    slot.tags.emplace_back(0, 0);
    slot.drawn = drawn;
    recording_ = -1;
}

void GpuProfiler::Collect(int frame) {
    Slot& slot = slots_[static_cast<std::size_t>(frame)];
    if (slot.used < 2) return;
    const VkResult r = vkGetQueryPoolResults(ctx_->device, pool_, static_cast<std::uint32_t>(frame) * kMaxQueries, slot.used,
                                             slot.used * sizeof(std::uint64_t), results_.data(), sizeof(std::uint64_t),
                                             VK_QUERY_RESULT_64_BIT);
    const std::uint32_t used = slot.used;
    slot.used = 0;
    if (r != VK_SUCCESS) return;  // not available (should not happen after the fence): frame skipped
    const double ms_per_tick = ns_per_tick_ / 1e6;
    auto delta = [&](std::uint32_t a, std::uint32_t b) {
        const std::uint64_t d = (results_[b] - results_[a]) & mask_;
        return d > mask_ / 2 ? 0.0 : static_cast<double>(d) * ms_per_tick;  // out of order: none
    };
    const double total = delta(0, used - 1);
    ++totals_.frames;
    totals_.total_ms += total;
    totals_.max_ms = std::max(totals_.max_ms, total);
    last_ms_ = total;
    if (!slot.drawn) return;
    ++totals_.drawn_frames;
    totals_.drawn_ms += total;
    if (!detailed_) return;
    for (std::uint32_t i = 1; i < used; ++i) {
        const auto k = static_cast<std::size_t>(slot.sections[i]);
        if (k >= totals_.ms.size()) continue;
        const double ms = delta(i - 1, i);
        totals_.ms[k] += ms;
        ++totals_.count[k];
        if (slot.sections[i] == GpuSection::kDraws) {
            Totals::Passes& p = totals_.draws_by_target[slot.tags[i].first];
            p.ms += ms;
            ++p.passes;
            p.draws += slot.tags[i].second;
        }
    }
}

void GpuProfiler::CollectAll() {
    for (int i = 0; i < static_cast<int>(slots_.size()); ++i) {
        if (i != recording_) Collect(i);
    }
}

}  // namespace ac04::render
