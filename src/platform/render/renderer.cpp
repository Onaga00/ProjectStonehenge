#include "platform/render/renderer.h"

#include <SDL3/SDL.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "output_frag_spv.h"
#include "output_vert_spv.h"
#include "platform/log.h"
#include "platform/render/output_layout.h"
#include "platform/render/pipeline_cache.h"
#include "platform/render/test_pattern.h"

namespace ac04::render {
namespace {

struct OutputParams {
    float source_size[2];
    float output_size[2];
    int filter_mode;
    int pad[3];
    float color_scale[4];  // std430: vec4 at offset 32
};
static_assert(sizeof(OutputParams) == 48, "output push constant layout");

VkShaderModule MakeShader(VkDevice device, const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &m);
    return m;
}

gs::GsBackend::AntiAlias ToBackend(platform::AntiAliasing a) {
    switch (a) {
        case platform::AntiAliasing::Fxaa: return gs::GsBackend::AntiAlias::kFxaa;
        case platform::AntiAliasing::Smaa: return gs::GsBackend::AntiAlias::kSmaa;
        default: return gs::GsBackend::AntiAlias::kOff;
    }
}

}  // namespace

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(SDL_Window* window, const platform::PresentationSettings& presentation, bool validation,
                    const std::filesystem::path& pipeline_cache_file, std::string& error) {
    window_ = window;
    presentation_ = presentation;
    internal_scale_ = std::clamp(presentation.internal_scale, 1, platform::kMaxInternalScale);
    movie_factor_ = internal_scale_;  // movie upscaling stage (ADR-006 §5): passthrough at 1x
    if (!ctx_.Init(window, AC04_DEV_BUILD != 0 && validation, error)) return false;
    AC04_LOG_INFO("renderer", "GPU: %s, Vulkan %s, driver %s", ctx_.gpu.name.c_str(),
                  ctx_.gpu.api_version.c_str(), ctx_.gpu.driver.c_str());
    pipeline_cache_file_ = pipeline_cache_file;
    LoadPipelineCache();

    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = ctx_.queue_family;
    vkCreateCommandPool(ctx_.device, &pci, nullptr, &command_pool_);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = command_pool_;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = kFramesInFlight;
    vkAllocateCommandBuffers(ctx_.device, &cai, command_buffers_);
    for (int i = 0; i < kFramesInFlight; ++i) {
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(ctx_.device, &sci, nullptr, &image_available_[i]);
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(ctx_.device, &fci, nullptr, &in_flight_[i]);
    }

    if (!CreateNativeTarget(error)) return false;
    if (!UploadTestPattern(error)) return false;
    if (!RecreateSwapchain(error) && !minimised_) return false;
    if (!CreatePipeline(error)) return false;
    // GS renderer (ADR-003): draws the game's GS packets; until the first frame the test pattern shows.
    std::string gs_error;
    gpu_profiler_.Init(ctx_, kFramesInFlight, gpu_profile_);
    gs_backend_.SetProfiler(&gpu_profiler_);
    gs_ready_ = gs_backend_.Init(ctx_, gs_error);
    if (!gs_ready_) AC04_LOG_ERROR("renderer", "GS renderer unavailable: %s", gs_error.c_str());
    gs_backend_.SetScale(internal_scale_);
    gs_backend_.SetAntiAlias(ToBackend(presentation.antialiasing));
    gs_state_.SetFrameQuery([this](uint32_t tbp) { return gs_backend_.IsFrameBuffer(tbp); });
    gs_state_.SetTextureDecode(gs::TextureDecode::kGpu);  // docs/port/renderer.md §2; SetTextureDecode changes it
    // Movie upscaling stage (ADR-006 §5); without it movies are drawn from the GS upload only.
    std::string movie_error;
    movie_upscaler_ = CreateFsr1MovieUpscaler();
    movie_upscaler_ready_ = movie_upscaler_->Init(ctx_, movie_error);
    if (!movie_upscaler_ready_) AC04_LOG_ERROR("renderer", "movie upscaling unavailable: %s", movie_error.c_str());
    return true;
}

void Renderer::SubmitGsSteps(std::vector<std::vector<uint8_t>>& steps,
                             std::vector<std::vector<gs::SourceMark>>& marks,
                             std::vector<std::vector<uint8_t>>& pool) {
    const auto start = std::chrono::steady_clock::now();
    if (!steps.empty()) {
        // A processed step ends the reuse of a blended copy; its draws (and textures) are let go.
        blended_valid_ = false;
        blended_draws_.clear();
    }
    for (size_t i = 0; i < steps.size(); ++i) {
        const std::vector<uint8_t>& s = steps[i];
        gs_state_.ProcessGif(s.data(), s.size(), i < marks.size() ? &marks[i] : nullptr);
        gs_state_.TakeDraws(gs_pending_);
        auto ops = gs_state_.TakeTextureOps();
        if (gs_ready_) gs_ops_.insert(gs_ops_.end(), std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
    }
    for (std::vector<uint8_t>& s : steps) {
        if (pool.size() >= 4) break;
        s.clear();
        pool.push_back(std::move(s));
    }
    steps.clear();
    marks.clear();
    pending_gs_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Renderer::SubmitBlendedStep(const std::vector<uint8_t>& gif, const std::vector<gs::SourceMark>& marks,
                                 std::uint64_t step_serial) {
    const auto start = std::chrono::steady_clock::now();
    if (blended_valid_ && blended_step_ == step_serial &&
        gs_state_.ReplayVertices(gif.data(), gif.size(), blended_draws_, blended_copy_.vertices)) {
        blended_copy_.draws = blended_draws_.draws;
        gs_pending_.Append(blended_copy_);
        ++stat_blend_replayed_;
    } else {
        gs_state_.ProcessGifTraced(gif.data(), gif.size(), &marks);
        blended_draws_.clear();
        gs_state_.TakeDraws(blended_draws_);
        auto ops = gs_state_.TakeTextureOps();
        if (gs_ready_) gs_ops_.insert(gs_ops_.end(), std::make_move_iterator(ops.begin()), std::make_move_iterator(ops.end()));
        blended_valid_ = gs_state_.Traced();
        blended_step_ = step_serial;
        blended_copy_.draws = blended_draws_.draws;
        blended_copy_.vertices = blended_draws_.vertices;
        gs_pending_.Append(blended_copy_);
        ++stat_blend_processed_;
    }
    pending_gs_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void Renderer::SubmitMoviePicture(uint64_t serial, uint32_t guest_addr, uint32_t guest_bytes, int width, int height,
                                  std::vector<uint8_t>& rgba) {
    if (serial == movie_serial_ || rgba.empty()) return;
    movie_serial_ = serial;
    movie_guest_addr_ = guest_addr;
    movie_guest_bytes_ = guest_bytes;
    movie_width_ = width;
    movie_height_ = height;
    movie_rgba_.swap(rgba);
}

bool Renderer::CaptureMovieUpscaled(std::vector<uint8_t>& rgba, int& width, int& height) {
    if (!movie_upscaler_ready_ || movie_done_serial_ == 0 || !movie_upscaler_->OutputImage()) return false;
    vkDeviceWaitIdle(ctx_.device);
    width = movie_upscaler_->OutputWidth();
    height = movie_upscaler_->OutputHeight();
    if (!ReadbackImage(movie_upscaler_->OutputImage(), width, height, rgba)) return false;
    for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 0xFF;
    return true;
}

bool Renderer::CaptureNative(std::vector<uint8_t>& rgba, int& width, int& height) {
    vkDeviceWaitIdle(ctx_.device);
    width = native_width_;
    height = native_height_;
    if (!ReadbackImage(native_image_, width, height, rgba)) return false;
    // The image as presented: the field copy's colour scale (brightness option), opaque.
    for (size_t i = 0; i < rgba.size(); i += 4) {
        for (int c = 0; c < 3; ++c) rgba[i + c] = static_cast<uint8_t>(std::min(255.0f, std::floor(rgba[i + c] * present_scale_[c] + 0.001f)));
        rgba[i + 3] = 0xFF;
    }
    return true;
}

// Copies a colour image in SHADER_READ_ONLY_OPTIMAL layout to host memory (RGBA8, rows top to
// bottom). The device must be idle.
bool Renderer::ReadbackImage(VkImage image, int width, int height, std::vector<uint8_t>& rgba) {
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = nullptr;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(ctx_.allocator, &bci, &aci, &buffer, &alloc, &info) != VK_SUCCESS) return false;
    VkCommandBuffer cmd = command_buffers_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx_.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.queue);
    vmaInvalidateAllocation(ctx_.allocator, alloc, 0, VK_WHOLE_SIZE);
    rgba.assign(static_cast<const uint8_t*>(info.pMappedData), static_cast<const uint8_t*>(info.pMappedData) + bytes);
    vmaDestroyBuffer(ctx_.allocator, buffer, alloc);
    return true;
}
bool Renderer::ResizeNativeTarget(int width, int height) {
    vkDeviceWaitIdle(ctx_.device);
    if (native_view_) vkDestroyImageView(ctx_.device, native_view_, nullptr);
    if (native_image_) vmaDestroyImage(ctx_.allocator, native_image_, native_alloc_);
    native_view_ = VK_NULL_HANDLE;
    native_image_ = VK_NULL_HANDLE;
    if (sampler_) vkDestroySampler(ctx_.device, sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    std::string error;
    if (!CreateNativeTarget(error, width, height) || !UploadTestPattern(error)) {
        AC04_LOG_ERROR("renderer", "%s", error.c_str());
        return false;
    }
    VkDescriptorImageInfo dii{sampler_, native_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = descriptor_set_;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);
    AC04_LOG_INFO("renderer", "native image %dx%d", width, height);
    return true;
}

bool Renderer::CreateNativeTarget(std::string& error, int width, int height) {
    native_scale_ = internal_scale_;
    native_width_ = (width > 0 ? width : kNativeWidth) * internal_scale_;
    native_height_ = (height > 0 ? height : kNativeHeight) * internal_scale_;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {static_cast<uint32_t>(native_width_), static_cast<uint32_t>(native_height_), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VkResult r = vmaCreateImage(ctx_.allocator, &ici, &aci, &native_image_, &native_alloc_, nullptr);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create the native render target: ") + VkResultName(r);
        return false;
    }
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = native_image_;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(ctx_.device, &vci, nullptr, &native_view_);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.maxLod = 0.0f;
    vkCreateSampler(ctx_.device, &sci, nullptr, &sampler_);
    return true;
}

bool Renderer::UploadTestPattern(std::string& error) {
    // What the window shows until the GS renderer has an image: the test pattern in development
    // builds, black in release builds.
#if AC04_DEV_BUILD
    const std::vector<uint8_t> pixels = MakeTestPattern(native_width_, native_height_);
#else
    std::vector<uint8_t> pixels(static_cast<size_t>(native_width_) * native_height_ * 4, 0);
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 0xFF;
#endif
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = pixels.size();
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VkBuffer staging = VK_NULL_HANDLE;
    VmaAllocation staging_alloc = nullptr;
    VmaAllocationInfo info{};
    VkResult r = vmaCreateBuffer(ctx_.allocator, &bci, &aci, &staging, &staging_alloc, &info);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create a staging buffer: ") + VkResultName(r);
        return false;
    }
    std::memcpy(info.pMappedData, pixels.data(), pixels.size());
    vmaFlushAllocation(ctx_.allocator, staging_alloc, 0, VK_WHOLE_SIZE);

    VkCommandBuffer cmd = command_buffers_[0];
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = native_image_;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &b);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {static_cast<uint32_t>(native_width_), static_cast<uint32_t>(native_height_), 1};
    vkCmdCopyBufferToImage(cmd, staging, native_image_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &b);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx_.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx_.queue);
    vmaDestroyBuffer(ctx_.allocator, staging, staging_alloc);
    return true;
}

bool Renderer::CreatePipeline(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &binding;
    vkCreateDescriptorSetLayout(ctx_.device, &dli, nullptr, &set_layout_);

    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(OutputParams)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx_.device, &pli, nullptr, &pipeline_layout_);

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(ctx_.device, &dpi, nullptr, &descriptor_pool_);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = descriptor_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    vkAllocateDescriptorSets(ctx_.device, &dai, &descriptor_set_);
    VkDescriptorImageInfo dii{sampler_, native_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = descriptor_set_;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &dii;
    vkUpdateDescriptorSets(ctx_.device, 1, &w, 0, nullptr);

    VkShaderModule vs = MakeShader(ctx_.device, output_vert_spv, sizeof(output_vert_spv));
    VkShaderModule fs = MakeShader(ctx_.device, output_frag_spv, sizeof(output_frag_spv));
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_VERTEX_BIT, vs, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2;
    ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gpi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gpi.stageCount = 2;
    gpi.pStages = stages;
    gpi.pVertexInputState = &vi;
    gpi.pInputAssemblyState = &ia;
    gpi.pViewportState = &vp;
    gpi.pRasterizationState = &rs;
    gpi.pMultisampleState = &ms;
    gpi.pColorBlendState = &cb;
    gpi.pDynamicState = &ds;
    gpi.layout = pipeline_layout_;
    gpi.renderPass = render_pass_;
    VkResult r = vkCreateGraphicsPipelines(ctx_.device, ctx_.pipeline_cache, 1, &gpi, nullptr, &pipeline_);
    vkDestroyShaderModule(ctx_.device, vs, nullptr);
    vkDestroyShaderModule(ctx_.device, fs, nullptr);
    if (r != VK_SUCCESS) {
        error = std::string("cannot create the output pipeline: ") + VkResultName(r);
        return false;
    }
    return true;
}

bool Renderer::RecreateSwapchain(std::string& error) {
    int w = window_w_, h = window_h_;
    if (w < 0 || h < 0) SDL_GetWindowSizeInPixels(window_, &w, &h);
    vkDeviceWaitIdle(ctx_.device);
    const VkFormat old_format = ctx_.swapchain_format;
    DestroySwapchainResources();
    if (!ctx_.CreateSwapchain(w, h, presentation_.vsync, error)) {
        minimised_ = error.empty();
        return false;
    }
    minimised_ = false;

    if (!render_pass_ || old_format != ctx_.swapchain_format) {
        if (render_pass_) vkDestroyRenderPass(ctx_.device, render_pass_, nullptr);
        VkAttachmentDescription att{};
        att.format = ctx_.swapchain_format;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;  // clears the bars to black
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rpi.attachmentCount = 1;
        rpi.pAttachments = &att;
        rpi.subpassCount = 1;
        rpi.pSubpasses = &sub;
        rpi.dependencyCount = 1;
        rpi.pDependencies = &dep;
        vkCreateRenderPass(ctx_.device, &rpi, nullptr, &render_pass_);
        if (pipeline_) {
            vkDestroyPipeline(ctx_.device, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
            vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
            vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);
            vkDestroyDescriptorSetLayout(ctx_.device, set_layout_, nullptr);
            if (!CreatePipeline(error)) return false;
        }
    }

    framebuffers_.resize(ctx_.swapchain_views.size());
    render_finished_.resize(ctx_.swapchain_views.size());
    for (size_t i = 0; i < ctx_.swapchain_views.size(); ++i) {
        VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fci.renderPass = render_pass_;
        fci.attachmentCount = 1;
        fci.pAttachments = &ctx_.swapchain_views[i];
        fci.width = ctx_.swapchain_extent.width;
        fci.height = ctx_.swapchain_extent.height;
        fci.layers = 1;
        vkCreateFramebuffer(ctx_.device, &fci, nullptr, &framebuffers_[i]);
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(ctx_.device, &sci, nullptr, &render_finished_[i]);
    }
    swapchain_dirty_ = false;
    return true;
}

void Renderer::DestroySwapchainResources() {
    for (VkFramebuffer fb : framebuffers_) vkDestroyFramebuffer(ctx_.device, fb, nullptr);
    framebuffers_.clear();
    for (VkSemaphore s : render_finished_) vkDestroySemaphore(ctx_.device, s, nullptr);
    render_finished_.clear();
}

void Renderer::SetDisplayAspect(int w, int h) {
    if (w == aspect_w_ && h == aspect_h_) return;
    aspect_w_ = w;
    aspect_h_ = h;
    AC04_LOG_INFO("renderer", "display aspect %d:%d", w, h);
}

void Renderer::ApplySettings(const platform::PresentationSettings& presentation) {
    const bool vsync_changed = presentation.vsync != presentation_.vsync;
    gs_backend_.SetAntiAlias(ToBackend(presentation.antialiasing));
    presentation_ = presentation;
    if (vsync_changed) swapchain_dirty_ = true;
    const int scale = std::clamp(presentation.internal_scale, 1, platform::kMaxInternalScale);
    if (scale != internal_scale_) {
        // Takes effect with the next frame: the GS targets are rebuilt and the native image resized.
        internal_scale_ = scale;
        if (!movie_factor_forced_) movie_factor_ = scale;
        if (gs_ready_) gs_backend_.SetScale(scale);
    }
}

bool Renderer::DrawFrame() {
    std::string error;
    if (swapchain_dirty_ || minimised_) {
        if (!RecreateSwapchain(error)) {
            if (minimised_) return true;  // nothing to draw into while minimised
            AC04_LOG_ERROR("renderer", "%s", error.c_str());
            return false;
        }
    }

    if (native_request_w_ > 0 && (native_request_w_ * internal_scale_ != native_width_ ||
                                  native_request_h_ * internal_scale_ != native_height_ || native_scale_ != internal_scale_)) {
        ResizeNativeTarget(native_request_w_, native_request_h_);
    }
    {
        const auto wait_start = std::chrono::steady_clock::now();
        vkWaitForFences(ctx_.device, 1, &in_flight_[frame_], VK_TRUE, UINT64_MAX);
        last_stats_.wait_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wait_start).count();
        stat_wait_ms_ += last_stats_.wait_ms;
        ++stat_presented_;
    }
    uint32_t image = 0;
    VkResult r = vkAcquireNextImageKHR(ctx_.device, ctx_.swapchain, UINT64_MAX, image_available_[frame_],
                                       VK_NULL_HANDLE, &image);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchain_dirty_ = true;
        return true;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        AC04_LOG_ERROR("renderer", "vkAcquireNextImageKHR: %s", VkResultName(r));
        return r != VK_ERROR_DEVICE_LOST;
    }
    vkResetFences(ctx_.device, 1, &in_flight_[frame_]);

    VkCommandBuffer cmd = command_buffers_[frame_];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    gpu_profiler_.BeginFrame(cmd, frame_);

    // Movie upscaling stage (ADR-006 §5): once per new picture, only above 1x. At internal scale
    // N > 1 the GS backend samples its output for draws whose texture came from the movie image
    // buffer (DrawCommand::texture_guest_source, MovieTexelOrigin).
    if (movie_upscaler_ready_ && movie_factor_ > 1 && movie_serial_ != movie_done_serial_ && !movie_rgba_.empty()) {
        if (movie_upscaler_->Upscale(cmd, frame_, movie_rgba_, movie_width_, movie_height_, movie_factor_))
            movie_done_serial_ = movie_serial_;
        gpu_profiler_.Mark(cmd, GpuSection::kMovie);
    }
    {
        // At internal scale > 1 the GS draws from the movie image buffer sample the upscaled picture.
        gs::GsBackend::MovieSource ms;
        if (internal_scale_ > 1 && movie_factor_ > 1 && movie_upscaler_ready_ && movie_done_serial_ != 0 &&
            movie_upscaler_->OutputView()) {
            ms.guest_addr = movie_guest_addr_;
            ms.guest_bytes = movie_guest_bytes_;
            ms.width = movie_width_;
            ms.height = movie_height_;
            ms.view = movie_upscaler_->OutputView();
        }
        gs_backend_.SetMovieSource(ms);
    }

    const bool drawn = gs_ready_ && (!gs_pending_.empty() || !gs_ops_.empty());
    if (drawn) {
        const auto start = std::chrono::steady_clock::now();
        gs_backend_.Record(cmd, frame_, gs_pending_, gs_ops_);
        gs_pending_.clear();
        gs_ops_.clear();
        last_stats_.gs_ms = pending_gs_ms_;
        last_stats_.record_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        last_stats_.textures = gs_backend_.LastTextureStats();
        pending_gs_ms_ = 0.0;
        ++stat_frames_;
        stat_gs_ms_ += last_stats_.gs_ms;
        stat_record_ms_ += last_stats_.record_ms;
        stat_upload_bytes_ += last_stats_.textures.upload_bytes;
        stat_uploads_ += last_stats_.textures.uploads;
        stat_decodes_ += last_stats_.textures.decodes;
        stat_memory_updates_ += last_stats_.textures.memory_updates;
        stat_textures_ += last_stats_.textures.textures;
        int w = 0, h = 0;
        gs_backend_.PresentSize(w, h);
        if (w > 0 && h >= 16) {  // ignore the first frames before the draw buffer is cleared
            native_request_w_ = w;
            native_request_h_ = h;
            gs_backend_.CopyPresent(cmd, native_image_, native_width_, native_height_);
            gs_backend_.PresentScale(present_scale_);
            gpu_profiler_.Mark(cmd, GpuSection::kPresentCopy);
        }
        last_stats_.passes = gs_backend_.LastPassStats();  // after CopyPresent: its barriers count too
        stat_render_passes_ += last_stats_.passes.render_passes;
        stat_barriers_ += last_stats_.passes.barriers;
        stat_draws_ += last_stats_.passes.draws;
        stat_pipeline_binds_ += last_stats_.passes.pipeline_binds;
        stat_push_constants_ += last_stats_.passes.push_constants;
        stat_texture_binds_ += last_stats_.passes.texture_binds;
        stat_depth_state_sets_ += last_stats_.passes.depth_state_sets;
        stat_copies_ += last_stats_.passes.copies;
        stat_copy_pixels_ += last_stats_.passes.copy_pixels;
        for (std::size_t k = 0; k < stat_kind_copies_.size(); ++k) {
            stat_kind_copies_[k] += last_stats_.passes.kind_copies[k];
            stat_kind_pixels_[k] += last_stats_.passes.kind_pixels[k];
        }
    }

    VkClearValue clear{};
    clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = render_pass_;
    rbi.framebuffer = framebuffers_[image];
    rbi.renderArea.extent = ctx_.swapchain_extent;
    rbi.clearValueCount = 1;
    rbi.pClearValues = &clear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    const Rect rect = FitAspect(static_cast<int>(ctx_.swapchain_extent.width),
                                static_cast<int>(ctx_.swapchain_extent.height), aspect_w_, aspect_h_);
    if (rect.width > 0 && rect.height > 0) {
        VkViewport viewport{static_cast<float>(rect.x), static_cast<float>(rect.y),
                            static_cast<float>(rect.width), static_cast<float>(rect.height), 0.0f, 1.0f};
        VkRect2D scissor{{rect.x, rect.y}, {static_cast<uint32_t>(rect.width), static_cast<uint32_t>(rect.height)}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout_, 0, 1,
                                &descriptor_set_, 0, nullptr);
        OutputParams params{{static_cast<float>(native_width_), static_cast<float>(native_height_)},
                            {static_cast<float>(rect.width), static_cast<float>(rect.height)},
                            presentation_.filter == platform::ScaleFilter::Sharp ? 0 : 1,
                            {0, 0, 0},
                            {present_scale_[0], present_scale_[1], present_scale_[2], 1.0f}};
        vkCmdPushConstants(cmd, pipeline_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(params), &params);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    if (overlay_hook_) {
        // The overlay covers the whole window, not only the game rectangle.
        VkViewport full{0.0f, 0.0f, static_cast<float>(ctx_.swapchain_extent.width),
                        static_cast<float>(ctx_.swapchain_extent.height), 0.0f, 1.0f};
        VkRect2D full_scissor{{0, 0}, ctx_.swapchain_extent};
        vkCmdSetViewport(cmd, 0, 1, &full);
        vkCmdSetScissor(cmd, 0, 1, &full_scissor);
        overlay_hook_(cmd);
    }
    vkCmdEndRenderPass(cmd);
    gpu_profiler_.EndFrame(cmd, GpuSection::kOutput, drawn);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &image_available_[frame_];
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &render_finished_[image];
    r = vkQueueSubmit(ctx_.queue, 1, &si, in_flight_[frame_]);
    if (r != VK_SUCCESS) {
        AC04_LOG_ERROR("renderer", "vkQueueSubmit: %s", VkResultName(r));
        return r != VK_ERROR_DEVICE_LOST;
    }

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &render_finished_[image];
    pi.swapchainCount = 1;
    pi.pSwapchains = &ctx_.swapchain;
    pi.pImageIndices = &image;
    r = vkQueuePresentKHR(ctx_.queue, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapchain_dirty_ = true;
    else if (r != VK_SUCCESS) AC04_LOG_ERROR("renderer", "vkQueuePresentKHR: %s", VkResultName(r));

    frame_ = (frame_ + 1) % kFramesInFlight;
    return true;
}

void Renderer::LogGpuTime() const {
    const GpuProfiler::Totals& t = gpu_profiler_.Session();
    if (t.frames == 0) return;
    const double n = static_cast<double>(std::max<std::uint64_t>(t.drawn_frames, 1));
    AC04_LOG_INFO("renderer",
                  "GPU time per frame (timestamps): %.2f ms on average over %llu frames with GS draws (%.2f ms over all "
                  "%llu presented), slowest %.2f ms",
                  t.drawn_ms / n, static_cast<unsigned long long>(t.drawn_frames),
                  t.total_ms / static_cast<double>(t.frames), static_cast<unsigned long long>(t.frames), t.max_ms);
    if (!gpu_profiler_.Detailed() || t.drawn_frames == 0) return;
    // Sections by time, largest first: ms per frame (sections per frame).
    std::array<std::size_t, static_cast<std::size_t>(GpuSection::kCount)> order{};
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return t.ms[a] > t.ms[b]; });
    std::string parts;
    for (std::size_t k : order) {
        if (t.count[k] == 0) continue;
        char part[112];
        std::snprintf(part, sizeof(part), "%s%s %.2f ms (%.1f)", parts.empty() ? "" : ", ",
                      GpuProfiler::Name(static_cast<GpuSection>(k)), t.ms[k] / n, static_cast<double>(t.count[k]) / n);
        parts += part;
    }
    AC04_LOG_INFO("renderer", "GPU time per frame with GS draws by section (ms, sections per frame): %s", parts.c_str());
    // The draws by target, largest first (at most 8): FBP, FBW, layout; ms, passes and draw calls per frame.
    std::vector<std::pair<std::uint32_t, GpuProfiler::Totals::Passes>> targets(t.draws_by_target.begin(), t.draws_by_target.end());
    std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) { return a.second.ms > b.second.ms; });
    std::string by_target;
    for (std::size_t i = 0; i < targets.size() && i < 8; ++i) {
        const std::uint32_t tag = targets[i].first;
        const std::uint32_t psm = tag >> 15;
        char part[128];
        std::snprintf(part, sizeof(part), "%sFBP 0x%X FBW %u %s %.2f ms (%.1f passes, %.1f draws)", by_target.empty() ? "" : ", ",
                      tag & 0x1FF, (tag >> 9) & 0x3F, psm == 0 ? "32-bit" : psm == 0x2 ? "CT16" : psm == 0xA ? "CT16S" : "16-bit",
                      targets[i].second.ms / n, static_cast<double>(targets[i].second.passes) / n,
                      static_cast<double>(targets[i].second.draws) / n);
        by_target += part;
    }
    if (!by_target.empty()) AC04_LOG_INFO("renderer", "GPU time of the draws by target (per frame with GS draws): %s", by_target.c_str());
    if (t.dropped_marks > 0)
        AC04_LOG_WARN("renderer", "GPU profile: %llu section marks over the per-frame budget (counted in the next section)",
                      static_cast<unsigned long long>(t.dropped_marks));
}

void Renderer::Shutdown() {
    if (!ctx_.device) return;
    if (stat_frames_ > 0) {
        const double n = static_cast<double>(stat_frames_);
        AC04_LOG_INFO("renderer",
                      "%llu frames drawn; per frame on average: GS processing %.2f ms, recording %.2f ms, %.1f textures "
                      "drawn, %.2f decoded on the GPU, %.2f uploaded from the CPU, %.2f GPU memory updates, %.0f KiB "
                      "uploaded",
                      static_cast<unsigned long long>(stat_frames_), stat_gs_ms_ / n, stat_record_ms_ / n,
                      static_cast<double>(stat_textures_) / n, static_cast<double>(stat_decodes_) / n,
                      static_cast<double>(stat_uploads_) / n, static_cast<double>(stat_memory_updates_) / n,
                      static_cast<double>(stat_upload_bytes_) / n / 1024.0);
        AC04_LOG_INFO("renderer",
                      "GPU work per frame on average: %.1f render passes, %.1f pipeline barriers, %.1f frame-buffer "
                      "copies (%.2f Mpixels)",
                      static_cast<double>(stat_render_passes_) / n, static_cast<double>(stat_barriers_) / n,
                      static_cast<double>(stat_copies_) / n, static_cast<double>(stat_copy_pixels_) / n / 1e6);
        AC04_LOG_INFO("renderer",
                      "draw state per frame on average: %.1f draw calls, %.1f pipeline binds, %.1f depth state changes, "
                      "%.1f parameter updates, %.1f texture binds",
                      static_cast<double>(stat_draws_) / n, static_cast<double>(stat_pipeline_binds_) / n,
                      static_cast<double>(stat_depth_state_sets_) / n, static_cast<double>(stat_push_constants_) / n,
                      static_cast<double>(stat_texture_binds_) / n);
        std::string kinds;
        for (std::size_t k = 0; k < stat_kind_copies_.size(); ++k) {
            if (stat_kind_copies_[k] == 0) continue;
            char part[96];
            std::snprintf(part, sizeof(part), "%s%s %.1f (%.2f Mpixels)", kinds.empty() ? "" : ", ",
                          gs::GsBackend::CopyKindName(static_cast<int>(k)), static_cast<double>(stat_kind_copies_[k]) / n,
                          static_cast<double>(stat_kind_pixels_[k]) / n / 1e6);
            kinds += part;
        }
        if (!kinds.empty()) AC04_LOG_INFO("renderer", "frame-buffer copies per frame by kind: %s", kinds.c_str());
        if (stat_blend_processed_ + stat_blend_replayed_ > 0)
            AC04_LOG_INFO("renderer", "blended copies: %llu processed, %llu reused from the copy before (new vertices)",
                          static_cast<unsigned long long>(stat_blend_processed_),
                          static_cast<unsigned long long>(stat_blend_replayed_));
        AC04_LOG_INFO("renderer", "%llu frames presented; waiting for the GPU %.2f ms per frame on average",
                      static_cast<unsigned long long>(stat_presented_),
                      stat_wait_ms_ / static_cast<double>(stat_presented_));
    }
    if (gs_backend_.VerifiedDecodes() > 0) {
        const auto bad = static_cast<unsigned long long>(gs_backend_.VerifyMismatches());
        const auto all = static_cast<unsigned long long>(gs_backend_.VerifiedDecodes());
        if (bad == 0) AC04_LOG_INFO("renderer", "texture decode check: %llu GPU decodes equal to the CPU decode", all);
        else AC04_LOG_WARN("renderer", "texture decode check: %llu of %llu GPU decodes differ from the CPU decode", bad, all);
    }
    vkDeviceWaitIdle(ctx_.device);
    gpu_profiler_.CollectAll();
    LogGpuTime();
    gpu_profiler_.Shutdown();
    DestroySwapchainResources();
    if (movie_upscaler_) movie_upscaler_->Shutdown();
    movie_upscaler_ready_ = false;
    gs_backend_.Shutdown();
    if (pipeline_) vkDestroyPipeline(ctx_.device, pipeline_, nullptr);
    if (pipeline_layout_) vkDestroyPipelineLayout(ctx_.device, pipeline_layout_, nullptr);
    if (descriptor_pool_) vkDestroyDescriptorPool(ctx_.device, descriptor_pool_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(ctx_.device, set_layout_, nullptr);
    if (render_pass_) vkDestroyRenderPass(ctx_.device, render_pass_, nullptr);
    if (sampler_) vkDestroySampler(ctx_.device, sampler_, nullptr);
    if (native_view_) vkDestroyImageView(ctx_.device, native_view_, nullptr);
    if (native_image_) vmaDestroyImage(ctx_.allocator, native_image_, native_alloc_);
    for (int i = 0; i < kFramesInFlight; ++i) {
        if (image_available_[i]) vkDestroySemaphore(ctx_.device, image_available_[i], nullptr);
        if (in_flight_[i]) vkDestroyFence(ctx_.device, in_flight_[i], nullptr);
    }
    if (command_pool_) vkDestroyCommandPool(ctx_.device, command_pool_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipeline_layout_ = VK_NULL_HANDLE;
    descriptor_pool_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    render_pass_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    native_view_ = VK_NULL_HANDLE;
    native_image_ = VK_NULL_HANDLE;
    command_pool_ = VK_NULL_HANDLE;
    SavePipelineCache();
    ctx_.Shutdown();
}

void Renderer::LoadPipelineCache() {
    std::vector<uint8_t> bytes;
    if (!pipeline_cache_file_.empty() && ReadPipelineCacheFile(pipeline_cache_file_, bytes)) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(ctx_.physical_device, &props);
        if (!PipelineCacheHeaderMatches(bytes.data(), bytes.size(), props.vendorID, props.deviceID,
                                        props.pipelineCacheUUID)) {
            // Another GPU or driver (or a damaged file): start empty, as with a rejected settings.json.
            AC04_LOG_INFO("renderer", "pipeline cache made for another GPU or driver: deleted, rebuilt this session");
            std::error_code ec;
            std::filesystem::remove(pipeline_cache_file_, ec);
            bytes.clear();
        }
    }
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    ci.initialDataSize = bytes.size();
    ci.pInitialData = bytes.empty() ? nullptr : bytes.data();
    if (vkCreatePipelineCache(ctx_.device, &ci, nullptr, &ctx_.pipeline_cache) != VK_SUCCESS && !bytes.empty()) {
        AC04_LOG_ERROR("renderer", "the driver rejected the pipeline cache file: starting with an empty cache");
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        bytes.clear();
        if (vkCreatePipelineCache(ctx_.device, &ci, nullptr, &ctx_.pipeline_cache) != VK_SUCCESS)
            ctx_.pipeline_cache = VK_NULL_HANDLE;
    }
    pipeline_cache_loaded_bytes_ = bytes.size();
}

void Renderer::SavePipelineCache() {
    if (!ctx_.pipeline_cache) return;
    std::size_t size = 0;
    std::vector<uint8_t> bytes;
    if (!pipeline_cache_file_.empty() &&
        vkGetPipelineCacheData(ctx_.device, ctx_.pipeline_cache, &size, nullptr) == VK_SUCCESS && size > 0 &&
        size != pipeline_cache_loaded_bytes_ && size <= kMaxPipelineCacheBytes) {
        bytes.resize(size);
        if (vkGetPipelineCacheData(ctx_.device, ctx_.pipeline_cache, &size, bytes.data()) == VK_SUCCESS) {
            bytes.resize(size);
            std::string error;
            if (!WritePipelineCacheFile(pipeline_cache_file_, bytes, error))
                AC04_LOG_ERROR("renderer", "cannot save the pipeline cache: %s", error.c_str());
        }
    }
    vkDestroyPipelineCache(ctx_.device, ctx_.pipeline_cache, nullptr);
    ctx_.pipeline_cache = VK_NULL_HANDLE;
}

}  // namespace ac04::render
