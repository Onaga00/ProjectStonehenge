// Renderer front end for the skeleton (ADR-003, ADR-006).
// - A native render target (the presented draw buffer, 512x448 or 640x448, x internal scale) that
//   the GS renderer's presented image is copied into; until the first frame a test pattern
//   (development builds) or black (release builds).
// - The output pass scales it into the window at the display aspect with the selected filter.
// - An overlay hook draws after the output pass (debug overlay, milestone D).
#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/render/gs/gs_state.h"
#include "platform/render/gpu_profiler.h"
#include "platform/render/gs/gs_vulkan.h"
#include "platform/render/movie_upscaler.h"
#include "platform/render/vk_context.h"
#include "platform/settings.h"

struct SDL_Window;
struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace ac04::render {

class Renderer {
public:
    using OverlayHook = std::function<void(VkCommandBuffer)>;

    ~Renderer();

    // `validation`: request the Vulkan validation layer (development builds; used when installed).
    // `pipeline_cache_file`: the pipeline cache kept between sessions (pipeline_cache.h; empty =
    // none), loaded here and saved by Shutdown.
    bool Init(SDL_Window* window, const platform::PresentationSettings& presentation, bool validation,
              const std::filesystem::path& pipeline_cache_file, std::string& error);
    void Shutdown();

    void ApplySettings(const platform::PresentationSettings& presentation);
    void OnWindowResized() { swapchain_dirty_ = true; }
    // Window size in pixels for the swapchain, given by the main thread when the renderer runs on
    // the render thread (SDL window queries stay on the main thread). Until then the renderer asks
    // SDL itself.
    void SetWindowPixelSize(int w, int h) {
        window_w_ = w;
        window_h_ = h;
    }

    // Draws and presents one frame. Returns false only on a fatal device error.
    bool DrawFrame();

    void SetOverlayHook(OverlayHook hook) { overlay_hook_ = std::move(hook); }
    // --gpu-profile: GPU time per section of the frame (gpu_profiler.h), logged at shutdown. Before Init.
    void SetGpuProfile(bool detailed) { gpu_profile_ = detailed; }

    VkContext& Context() { return ctx_; }
    VkRenderPass OutputRenderPass() const { return render_pass_; }
    uint32_t SwapchainImageCount() const { return static_cast<uint32_t>(ctx_.swapchain_images.size()); }
    int InternalScale() const { return internal_scale_; }

    // GS data of completed game steps (in order) with their source marks (same order, may be
    // shorter); processed now, drawn with the next frame.
    // Processes the steps' GIF data; the emptied step buffers go to `pool` (kept for reuse).
    void SubmitGsSteps(std::vector<std::vector<uint8_t>>& steps, std::vector<std::vector<gs::SourceMark>>& marks,
                       std::vector<std::vector<uint8_t>>& pool);
    // Frame interpolation: the blended copy of the newest step (FrameInterpolator::Build, with its
    // step serial), drawn with the next frame. The first copy of a step is processed and its draws
    // kept; a later copy of the same step that differs only in vertex values reuses them with the
    // new vertices (GsState::ReplayVertices) and adds no texture work: the textures and the GPU's
    // copy of local memory are as the first copy left them, and repeating its memory updates would
    // leave them so. Otherwise the copy is processed again.
    void SubmitBlendedStep(const std::vector<uint8_t>& gif, const std::vector<gs::SourceMark>& marks,
                           std::uint64_t step_serial);
    // Off: submitted steps update GS registers and local memory but produce no draws (--fast-until:
    // steps that are never shown).
    void SetGsDrawsEnabled(bool on) { gs_state_.SetDrawsEnabled(on); }
    // Where GS textures are decoded (docs/port/renderer.md §2): on the GPU (default), on the CPU
    // (--cpu-texture-decode), or both with the results compared (--verify-texture-decode). Set
    // before the first SubmitGsSteps.
    void SetTextureDecode(gs::TextureDecode mode) {
        gs_state_.SetTextureDecode(mode);
        gs_backend_.SetVerifyDecode(mode == gs::TextureDecode::kBoth);
    }
    // GS local memory (4 MiB, gs_memory.h layout) for development dumps (--dump-vram).
    const std::uint8_t* GsLocalMemory() const { return gs_state_.Memory().Data(); }
    // Loads a 4 MiB GS memory image (--play-gsdump).
    void LoadGsLocalMemory(const std::uint8_t* image) { gs_state_.Memory().LoadImage(image); }

    // Display registers of the last sceGsPutDispEnv (DISPFB2, DISPLAY2), taken with the GS steps.
    void SetDisplay(uint64_t dispfb, uint64_t display) { gs_backend_.SetDisplay(dispfb, display); }

    // Movie upscaling stage (ADR-006 §5). The factor is the internal scale; 1 = passthrough (the
    // decoder side path is not needed). Development builds can set it for the stage alone
    // (--movie-upscale), with the GS still drawn at 1x, to inspect the stage output.
    int MovieUpscaleFactor() const { return movie_factor_; }
    void SetMovieUpscaleFactor(int factor) {
        movie_factor_ = factor < 1 ? 1 : factor;
        movie_factor_forced_ = true;
    }
    // A new decoded picture from the side path (row-major RGBA8; `rgba` is taken) with the game's
    // image buffer it was written to (guest address and size). Upscaled once, with the next frame,
    // when the factor is above 1; at internal scale > 1 the GS draws of that buffer sample it.
    void SubmitMoviePicture(uint64_t serial, uint32_t guest_addr, uint32_t guest_bytes, int width, int height,
                            std::vector<uint8_t>& rgba);
    // Reads back the last upscaled picture (RGBA8). False when there is none. Waits idle.
    bool CaptureMovieUpscaled(std::vector<uint8_t>& rgba, int& width, int& height);
    // Native (pre-output-pass) image size in host pixels: the draw buffer size the game uses x the
    // internal scale.
    int NativeWidth() const { return native_width_; }
    // Internal scale of the native image (NativeWidth() = presented draw buffer width x this).
    int NativeScale() const { return native_scale_; }
    int NativeHeight() const { return native_height_; }
    VkImage NativeImage() const { return native_image_; }
    // Reads back the native image (RGBA8, rows top to bottom) for the native screenshot. Waits idle.
    bool CaptureNative(std::vector<uint8_t>& rgba, int& width, int& height);

    // Display aspect of the game image (4:3, or 16:9 when the game's SCREEN RATIO flag is set).
    void SetDisplayAspect(int w, int h);

    // CPU cost of the renderer (debug overlay; averages logged at shutdown): GS processing of the
    // submitted steps since the last frame, recording of the frame, and the texture work.
    struct Stats {
        double gs_ms = 0.0;      // SubmitGsSteps since the previous DrawFrame
        double record_ms = 0.0;  // GsBackend::Record of the last frame
        double wait_ms = 0.0;    // waiting for the GPU to finish the frame before last (its fence)
        gs::GsBackend::TextureStats textures;
        gs::GsBackend::PassStats passes;  // render passes, barriers and frame-buffer copies of the last frame
    };
    const Stats& LastStats() const { return last_stats_; }

private:
    static constexpr int kFramesInFlight = 2;

    bool RecreateSwapchain(std::string& error);
    void DestroySwapchainResources();
    bool CreateNativeTarget(std::string& error, int width = 0, int height = 0);
    bool ResizeNativeTarget(int width, int height);
    bool CreatePipeline(std::string& error);
    bool UploadTestPattern(std::string& error);
    bool ReadbackImage(VkImage image, int width, int height, std::vector<uint8_t>& rgba);
    void LoadPipelineCache();
    void SavePipelineCache();
    void LogGpuTime() const;

    SDL_Window* window_ = nullptr;
    int window_w_ = -1, window_h_ = -1;  // SetWindowPixelSize; -1 = ask SDL
    VkContext ctx_;
    std::filesystem::path pipeline_cache_file_;
    std::size_t pipeline_cache_loaded_bytes_ = 0;  // saved again only when the size changed
    platform::PresentationSettings presentation_;
    int internal_scale_ = 1;
    int aspect_w_ = 4, aspect_h_ = 3;
    bool swapchain_dirty_ = false;
    bool minimised_ = false;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkImage native_image_ = VK_NULL_HANDLE;
    VmaAllocation native_alloc_ = nullptr;
    VkImageView native_view_ = VK_NULL_HANDLE;
    int native_width_ = 0, native_height_ = 0;  // host pixels (native size x native_scale_)
    int native_scale_ = 1;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffers_[kFramesInFlight]{};
    VkSemaphore image_available_[kFramesInFlight]{};
    VkFence in_flight_[kFramesInFlight]{};
    std::vector<VkSemaphore> render_finished_;  // one per swapchain image
    int frame_ = 0;

    OverlayHook overlay_hook_;
    Stats last_stats_;
    double pending_gs_ms_ = 0.0;
    // Session sums for the shutdown summary (frames with GS draws only).
    std::uint64_t stat_frames_ = 0;
    double stat_gs_ms_ = 0.0, stat_record_ms_ = 0.0, stat_wait_ms_ = 0.0;
    std::uint64_t stat_presented_ = 0;
    std::uint64_t stat_upload_bytes_ = 0, stat_uploads_ = 0, stat_decodes_ = 0, stat_memory_updates_ = 0,
                  stat_textures_ = 0;
    std::uint64_t stat_render_passes_ = 0, stat_barriers_ = 0, stat_copies_ = 0, stat_copy_pixels_ = 0;
    std::uint64_t stat_draws_ = 0, stat_pipeline_binds_ = 0, stat_push_constants_ = 0, stat_texture_binds_ = 0,
                  stat_depth_state_sets_ = 0;
    std::array<std::uint64_t, 7> stat_kind_copies_{}, stat_kind_pixels_{};
    std::uint64_t stat_blend_processed_ = 0, stat_blend_replayed_ = 0;

    GpuProfiler gpu_profiler_;
    bool gpu_profile_ = false;

    gs::GsState gs_state_;
    gs::GsBackend gs_backend_;
    bool gs_ready_ = false;
    gs::DrawList gs_pending_;
    // SubmitBlendedStep: the draws of the step's processed copy, and the list handed to gs_pending_.
    bool blended_valid_ = false;
    std::uint64_t blended_step_ = 0;
    gs::DrawList blended_draws_, blended_copy_;
    std::vector<gs::TextureOp> gs_ops_;  // texture work of the pending draws (all of it reaches Record, in order)
    int native_request_w_ = 0, native_request_h_ = 0;
    float present_scale_[3] = {1.0f, 1.0f, 1.0f};  // field-copy colour scale (brightness option)

    // Movie upscaling stage
    std::unique_ptr<MovieUpscaler> movie_upscaler_;
    bool movie_upscaler_ready_ = false;
    std::atomic<int> movie_factor_{1};  // read by the main thread (render thread: render_thread.h)
    uint64_t movie_serial_ = 0, movie_done_serial_ = 0;
    uint32_t movie_guest_addr_ = 0, movie_guest_bytes_ = 0;
    int movie_width_ = 0, movie_height_ = 0;
    bool movie_factor_forced_ = false;  // --movie-upscale
    std::vector<uint8_t> movie_rgba_;
};

}  // namespace ac04::render
