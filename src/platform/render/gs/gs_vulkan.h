// Vulkan backend of the GS renderer (ADR-003): replays the draw commands of GsState into render
// targets (one RGBA8 image per frame buffer address, each with a D32 depth image for the GS Z
// buffer drawn with it), with one image per decoded texture (decoded on the GPU from a copy of GS
// local memory, or uploaded as decoded on the CPU: docs/port/renderer.md §2), and provides the image to
// present (ADR-006 §4: the draw buffer as it stands when the field copy reads it).
//
// Internal resolution (docs/port/renderer.md §1): each target has a scale factor (the
// internal scale N for frame-sized buffers, 1 for the small off-screen targets of the shadow and
// glow chains). A target holds its native GS region (native_w x native_h pixels) at scale x that
// size. Everything that addresses target pixels goes through the target's scale (viewport,
// scissor, depth fills, DATE copies, frame-as-texture reads); textures from local memory and
// format conversions stay native. At N = 1 every rule is the identity.
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <initializer_list>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "platform/render/gpu_profiler.h"
#include "platform/render/gs/gs_state.h"
#include "platform/render/vk_context.h"

struct VmaAllocation_T;
typedef VmaAllocation_T* VmaAllocation;

namespace ac04::render::gs {

class GsBackend {
public:
    static constexpr int kFrames = 2;  // frames in flight (as the renderer)

    GsBackend();
    ~GsBackend();
    bool Init(VkContext& ctx, std::string& error);
    void Shutdown();

    // Internal scale N (1-8). Changing it rebuilds the targets at the new scale (their colour is
    // resampled, depth cleared). Waits idle.
    void SetScale(int n);
    int Scale() const { return scale_; }

    // Movie upscaling stage output (ADR-006 §5) for draws whose texture came from the movie image
    // buffer: the game's image buffer (guest address, size, picture height) and the upscaled picture
    // (view in SHADER_READ_ONLY layout, picture size in native pixels). Used only at N > 1;
    // `view` null = not available (the game's own upload is drawn).
    struct MovieSource {
        std::uint32_t guest_addr = 0, guest_bytes = 0;
        int width = 0, height = 0;
        VkImageView view = VK_NULL_HANDLE;
    };
    void SetMovieSource(const MovieSource& m) { movie_ = m; }

    // True when a frame buffer at block pointer `tbp` has been drawn (texture reads come from it).
    bool IsFrameBuffer(std::uint32_t tbp) const;

    // Records the texture work and then the draws into `cmd` (outside any render pass); `frame`
    // selects the per-frame buffers. Every op of `ops` must reach Record once, in stream order (the
    // GPU copy of local memory follows them); draws may only use textures whose decode op came in
    // this or an earlier call, or that carry their CPU-decoded rgba.
    void Record(VkCommandBuffer cmd, int frame, const DrawList& list, const std::vector<TextureOp>& ops);

    // --verify-texture-decode (development builds): every GPU decode is read back and compared with
    // the CPU decode the texture carries (GsState in TextureDecode::kBoth); differences are logged
    // (the first few in detail) and counted in the shutdown summary.
    void SetVerifyDecode(bool on) { verify_ = on; }

    // Display registers of the last sceGsPutDispEnv (DISPFB2, DISPLAY2). The image to present is the
    // source of the field copy (ADR-006 §4): the first draw that reads a frame buffer as a texture
    // and writes into the display buffer at DISPFB's FBP. Other frame-as-texture reads (the flight
    // sky's work pages, glow, heat haze) are not the field copy. Before any DISPFB is known the first
    // frame-as-texture read is taken, as before.
    void SetDisplay(std::uint64_t dispfb, std::uint64_t display) {
        dispfb_ = dispfb;
        display_ = display;
    }
    // Native size of the image to present (0 when nothing has been drawn yet).
    void PresentSize(int& w, int& h) const;
    // Copies the image to present into `dst` (layout SHADER_READ_ONLY before and after), which is
    // `dst_w` x `dst_h` pixels: the native size x N. A source at another scale is resampled.
    void CopyPresent(VkCommandBuffer cmd, VkImage dst, int dst_w, int dst_h);
    // Colour scale of the field copy that read the presented image (GS MODULATE with the copy's
    // vertex colour / 128; 1.0 without a copy): the game's brightness option (ADR-006 §4).
    void PresentScale(float out[3]) const {
        for (int c = 0; c < 3; ++c) out[c] = present_scale_[c];
    }

    // Texture statistics of the last Record (debug overlay, session summary).
    struct TextureStats {
        std::uint32_t textures = 0;        // distinct decoded textures drawn
        std::uint32_t decodes = 0;         // textures decoded on the GPU
        std::uint32_t uploads = 0;         // textures uploaded as decoded on the CPU
        std::uint32_t memory_updates = 0;  // copies into the GPU's local memory (one before each decode that needs one)
        std::uint64_t upload_bytes = 0;    // bytes copied to the GPU (memory blocks, CPU-decoded texels, palettes)
        std::uint32_t images = 0;          // texture images held (in use and free)
        std::uint64_t image_bytes = 0;     // their size
    };
    const TextureStats& LastTextureStats() const { return texture_stats_; }
    // GPU work structure of the last frame (Record and CopyPresent; debug overlay, session summary):
    // what breaks the GPU's work into small dependent pieces (validation.md §2al).
    struct PassStats {
        std::uint32_t render_passes = 0;  // GS render passes begun (draw targets, depth copies, anti-aliasing)
        std::uint32_t barriers = 0;       // pipeline barriers (each waits for the earlier work of its source stages)
        std::uint32_t copies = 0;         // frame-buffer copies: frame read as a texture, field copy, DATE /
                                          // shader-blend regions, GPU palettes, view syncs, conversions, anti-aliasing
        std::uint64_t copy_pixels = 0;    // host pixels those copies wrote
        // State changes inside the GS render passes (gpu_profiler.h: the draws' GPU time).
        std::uint32_t draws = 0;           // draw calls
        std::uint32_t pipeline_binds = 0;  // vkCmdBindPipeline
        std::uint32_t push_constants = 0;  // vkCmdPushConstants of the draw parameters
        std::uint32_t texture_binds = 0;   // vkCmdBindDescriptorSets of the draw's texture
        std::uint32_t depth_state_sets = 0;  // dynamic depth compare / depth write changes
        // The same by kind (CopyKind).
        std::array<std::uint32_t, 7> kind_copies{};
        std::array<std::uint64_t, 7> kind_pixels{};
    };
    enum CopyKind { kCopyFrameTexture, kCopyField, kCopyDstRegion, kCopyPalette, kCopySync, kCopyConvert, kCopyAntiAlias };
    static const char* CopyKindName(int k);
    const PassStats& LastPassStats() const { return pass_stats_; }
    // --verify-texture-decode results so far: textures compared, and those that differed.
    std::uint64_t VerifiedDecodes() const { return verify_count_; }
    std::uint64_t VerifyMismatches() const { return verify_mismatches_; }

    // Optional anti-aliasing (presentation.antialiasing, gs_antialias.cpp): at each flight view's
    // anti-aliasing point (DrawCommand::aa_point, after the view's 3D, before its HUD) the view's
    // rectangle of the frame buffer is anti-aliased in place with FXAA 3.11 or SMAA 1x. RGB only:
    // the GS alpha in the frame buffer is kept.
    enum class AntiAlias { kOff, kFxaa, kSmaa };
    void SetAntiAlias(AntiAlias mode) { aa_mode_ = mode; }
    // GPU time per section (gpu_profiler.h): Record marks the end of each section it records.
    void SetProfiler(GpuProfiler* p) { profiler_ = p; }

private:
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        int width = 0, height = 0;
    };
    struct Target {
        Image img;
        Image depth;  // the GS Z buffer drawn with this frame buffer (D32: Z / 2^bits of the Z format)
        VkFramebuffer fb = VK_NULL_HANDLE;
        std::uint32_t fbp = 0, fbw = 0;
        std::uint32_t psm = 0;  // layout class: 0 (CT32/CT24), kPsmCt16 or kPsmCt16S
        std::uint64_t last_write = 0;  // draw clock of the last draw into it (the newest data wins)
        int used_height = 0;  // native rows drawn
        int scale = 1;        // host pixels per GS pixel
        int native_w = 0, native_h = 0;  // GS region held (img is native_w*scale x native_h*scale)
    };
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation alloc = nullptr;
        void* mapped = nullptr;
        std::size_t size = 0;
    };

    // Frame buffer shown when no field copy was drawn (PresentSize and CopyPresent use the same one).
    const Target* DisplayTarget() const;
    bool CreateImage(Image& img, int w, int h, VkImageUsageFlags usage, VkFormat format = VK_FORMAT_R8G8B8A8_UNORM);
    void DestroyImage(Image& img);
    // A host-visible buffer of at least `size` bytes (write-combined; `readback`: cached for CPU reads).
    bool EnsureBuffer(Buffer& b, std::size_t size, VkBufferUsageFlags usage, bool readback = false);
    void DestroyBuffer(Buffer& b);
    void Transition(VkCommandBuffer cmd, Image& img, VkImageLayout layout);
    // Several images (at most 4) to new layouts with one barrier; images already there are skipped.
    void Transitions(VkCommandBuffer cmd, std::initializer_list<std::pair<Image*, VkImageLayout>> images);
    // vkCmdPipelineBarrier, counted in pass_stats_.
    void Barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst, std::uint32_t buffer_count,
                 const VkBufferMemoryBarrier* buffers, std::uint32_t image_count, const VkImageMemoryBarrier* images);
    // One frame-buffer copy of w x h host pixels, counted in pass_stats_.
    void CountCopy(CopyKind kind, std::uint64_t w, std::uint64_t h) {
        ++pass_stats_.copies;
        pass_stats_.copy_pixels += w * h;
        ++pass_stats_.kind_copies[kind];
        pass_stats_.kind_pixels[kind] += w * h;
    }
    // The target for (fbp, fbw, psm), created or grown so that it holds at least `rows` native rows.
    Target* GetTarget(VkCommandBuffer cmd, int frame, std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm,
                      int rows = 0);
    // Scale of a new target `native_w` pixels wide (TargetScale policy, renderer.md §1.4.5).
    int TargetScale(int native_w) const;
    // `t` rebuilt at 1x from its scaled image (each native pixel from its centre host pixel).
    void DownscaleTarget(VkCommandBuffer cmd, int frame, Target& t);
    bool CreateTargetImages(Target& t, int frame, VkCommandBuffer cmd);
    // Destroys `img` once the frame slot `frame` comes round again (in-flight commands may use it).
    void Retire(int frame, Image& img);
    // feedback_, date_ and present_ hold at least w x h host pixels.
    bool EnsureScratch(int frame, VkCommandBuffer cmd, int w, int h);
    // Texture images (docs/port/renderer.md §2.3). An image for `t` (from the free pool when one of a
    // fitting size is free again), with its set-1 descriptor set; null when it cannot be created.
    struct TexImage;
    TexImage* AcquireTexImage(const std::shared_ptr<const DecodedTexture>& t);
    // Images of textures nobody holds any more go to the free pool; the pool is trimmed to its budget.
    void ReleaseUnusedTextures();
    void DestroyTexImage(TexImage& t);
    bool InitTextures(std::string& error);
    // Step 1 of Record: the texture work of `ops` (GPU memory updates and decodes, in order) and the
    // uploads of CPU-decoded textures that `draws` use for the first time.
    void RecordTextureWork(VkCommandBuffer cmd, int frame, const std::vector<DrawCommand>& draws,
                           const std::vector<TextureOp>& ops);
    // --verify-texture-decode: compares the read-back decodes of the frame slot's last use.
    void CheckVerifyReadback(int frame);
    // Builds the texture of draw `d` (TEX0 format, address, width) from target `src`, which holds
    // that memory in another format or width, into feedback_ (gs_conv.comp).
    // `box`: the texels [u0, u1) x [v0, v1) the draw can read (only those are converted).
    void Convert(VkCommandBuffer cmd, int frame, Target& src, const DrawCommand& d, const std::array<int, 4>& box);
    // One conversion (gs_conv.comp) of `src` into feedback_: texels [u0, u1) x [v0, v1) of the
    // buffer (psm, tbp, tbw), with TEXA (TA0 bits 0-7, AEM 15, TA1 16-23) and an optional CLUT.
    struct ConvRequest {
        std::uint32_t psm = 0, tbp = 0, tbw = 0, texa = 0;
        std::int32_t u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        const std::array<std::uint32_t, 256>* clut = nullptr;
    };
    bool ConvertCore(VkCommandBuffer cmd, int frame, Target& src, const ConvRequest& r);
    // The newest other view of `t`'s memory (same base page) drawn after `t`, or null.
    Target* NewerView(const Target& t);
    // Before a draw into `t`: converts `newer` into it (at native resolution, then scaled into t).
    void Sync(VkCommandBuffer cmd, int frame, Target& t, Target& newer);
    bool InitConvert(std::string& error);
    bool InitDepthCopy(std::string& error);
    // Anti-aliasing (gs_antialias.cpp): pipelines and the SMAA lookup textures; working images the
    // size of the target being processed (recreated when that changes, after a device wait).
    bool InitAntiAlias(std::string& error);
    void ShutdownAntiAlias();
    bool EnsureAntiAliasImages(int w, int h);
    void ApplyAntiAlias(VkCommandBuffer cmd, Target& t, const DrawCommand& d);
    // GS draw pipelines: GetPipeline (Record, main thread) returns the pipeline of a key, built
    // during the frame only when the warm-up has not built it (CreatePipeline is thread-safe).
    VkPipeline GetPipeline(std::uint64_t key);
    VkPipeline CreatePipeline(std::uint64_t key) const;
    void StartPipelineWarmUp();
    void StopPipelineWarmUp();
    void LogOnce(const char* key, const char* text);

    VkContext* ctx_ = nullptr;
    VkRenderPass pass_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;      // set 0: feedback, DATE copy, movie
    VkDescriptorSetLayout tex_set_layout_ = VK_NULL_HANDLE;  // set 1: the draw's texture
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet sets_[kFrames]{};  // per frame: set 0 (rewritten per Record)
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkSampler linear_sampler_ = VK_NULL_HANDLE;  // the upscaled movie picture
    VkShaderModule vs_ = VK_NULL_HANDLE, fs_ = VK_NULL_HANDLE;
    VkShaderModule fs_single_ = VK_NULL_HANDLE;  // gs.frag without the second blend output
    // Pipeline warm-up (gs_pipeline_keys.h): threads started by Init build the keys the game is
    // known to use, so that a new driver compiles them before the first 3D frame instead of inside
    // frames (validation.md §2ai). pipelines_ and warming_ are guarded by pipelines_mutex_.
    std::unordered_map<std::uint64_t, VkPipeline> pipelines_;
    std::unordered_set<std::uint64_t> warming_;  // keys a warm-up thread is building now
    std::mutex pipelines_mutex_;
    std::condition_variable pipelines_cv_;
    std::vector<std::thread> warm_threads_;
    std::atomic<std::size_t> warm_next_{0};
    std::atomic<int> warm_running_{0};
    std::atomic<bool> warm_stop_{false};
    std::chrono::steady_clock::time_point warm_start_;
    // Pipelines built during frames this session (summary logged at Shutdown): their keys, total
    // and slowest time.
    std::vector<std::uint64_t> frame_pipeline_keys_;
    double frame_pipeline_ms_ = 0.0, frame_pipeline_ms_max_ = 0.0;

    // Texture images, one per decoded texture (by DecodedTexture::serial), kept while GsState's cache
    // or a draw still holds the texture. Sizes are rounded up to kTexAlign so that freed images fit
    // later textures; a freed image waits in free_textures_ until the frames in flight that may read
    // it are done (record_count_), and the pool is trimmed to kFreeTextureBytes.
    struct TexImage {
        Image img;
        VkDescriptorSet set = VK_NULL_HANDLE;  // set 1: img with the nearest sampler
        VkDescriptorPool pool = VK_NULL_HANDLE;
        std::weak_ptr<const DecodedTexture> texture;
        std::uint64_t free_after = 0;  // reusable from this Record on (free pool only)
    };
    std::unordered_map<std::uint64_t, TexImage> textures_;
    std::multimap<std::pair<int, int>, TexImage> free_textures_;
    std::uint64_t free_texture_bytes_ = 0;
    std::vector<VkDescriptorPool> tex_pools_;  // set-1 sets, kTexSetsPerPool each, freed one by one
    TexImage dummy_texture_;                   // 1 x 1, bound for draws without a texture image
    std::uint64_t record_count_ = 0;
    TextureStats texture_stats_;
    PassStats pass_stats_;
    GpuProfiler* profiler_ = nullptr;
    void Mark(VkCommandBuffer cmd, GpuSection s, std::uint32_t tag = 0, std::uint32_t draws = 0) {
        if (profiler_) profiler_->Mark(cmd, s, tag, draws);
    }
    // GPU decode (gs_decode.comp): the GPU's copy of GS local memory, the pipeline, per-frame
    // descriptor pools (one set per decoded rectangle's texture) and palette buffers.
    Buffer gs_memory_;  // device local, 4 MiB
    VkDescriptorSetLayout decode_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout decode_layout_ = VK_NULL_HANDLE;
    VkPipeline decode_pipeline_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> decode_pools_[kFrames];
    Buffer decode_palettes_[kFrames];
    // --verify-texture-decode: per frame slot, the GPU decodes read back and what to compare them with.
    bool verify_ = false;
    struct VerifyItem {
        std::shared_ptr<const DecodedTexture> texture;
        std::size_t offset = 0;  // of its rectangles, tightly packed in order, in verify_buffer_
    };
    std::vector<VerifyItem> verify_items_[kFrames];
    Buffer verify_buffer_[kFrames];
    std::uint64_t verify_count_ = 0, verify_mismatches_ = 0;
    Image feedback_;
    // The frame-buffer region feedback_ holds (host pixels [x0, x1) x [y0, y1), at the same position as
    // in the source): reused by later draws that read inside it while the source image is not drawn
    // into (last_write unchanged). Cleared at each Record and when feedback_ is used otherwise.
    struct FeedbackCopy {
        VkImage image = VK_NULL_HANDLE;
        std::uint64_t write = 0;
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };
    FeedbackCopy feedback_copy_;
    Image date_;  // destination alpha test: copy of the target region a DATE draw covers
    Image clut_img_;  // 16x16 palette drawn on the GPU, copied before the draw that uses it (flag 29)
    Image present_;
    bool present_valid_ = false;
    int present_w_ = 0, present_h_ = 0;  // native
    int present_src_scale_ = 1;           // scale of the target present_ was copied from
    float present_scale_[3] = {1.0f, 1.0f, 1.0f};
    std::uint32_t present_fbp_ = 0;
    std::uint64_t dispfb_ = 0, display_ = 0;
    Buffer staging_[kFrames];
    Buffer vertices_[kFrames];
    std::map<std::uint64_t, Target> targets_;
    std::uint64_t write_clock_ = 0;
    int scale_ = 1;
    int max_image_dim_ = 16384;
    MovieSource movie_;
    std::vector<Image> retired_[kFrames];
    std::vector<VkFramebuffer> retired_fb_[kFrames];
    // Per-frame working lists of Record (texture placement, uploads, vertices), kept so their
    // memory is reused from frame to frame.
    struct RecordScratch;
    std::unique_ptr<RecordScratch> scratch_;
    // Format conversion (gs_conv.comp): layout tables, per-frame descriptor pools and CLUT buffers.
    static constexpr std::uint32_t kMaxConversions = 512;  // per frame
    VkDescriptorSetLayout conv_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout conv_layout_ = VK_NULL_HANDLE;
    VkPipeline conv_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool conv_pool_[kFrames]{};
    Buffer conv_tables_;
    Buffer conv_clut_[kFrames];
    std::uint32_t conv_count_ = 0;
    // Z buffers seen in draws (ZBUF.ZBP -> target key and the ZBUF value): a later draw whose FRAME
    // points at a Z buffer (the game clears Z by drawing into it as a colour buffer) fills the
    // depth image of that target instead.
    std::map<std::uint32_t, std::pair<std::uint64_t, std::uint64_t>> zbuf_owner_;
    // Depth copies (gs_depth_copy.frag): a Z buffer drawn into another Z buffer through a sprite.
    VkDescriptorSetLayout depth_copy_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout depth_copy_layout_ = VK_NULL_HANDLE;
    VkPipeline depth_copy_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool depth_copy_pool_[kFrames]{};
    static constexpr std::uint32_t kMaxDepthCopies = 16;  // per frame
    std::uint32_t depth_copy_count_ = 0;
    // Frame buffers whose memory is drawn both in the 32-bit layout and in a 16-bit one: their views
    // reinterpret each other's bits, so they are held at 1x (renderer.md §1, row 7).
    std::set<std::uint32_t> native_fbps_;
    // Anti-aliasing state (gs_antialias.cpp).
    AntiAlias aa_mode_ = AntiAlias::kOff;
    bool aa_ready_ = false;
    Image aa_color_, aa_edges_, aa_weights_, aa_out_, aa_area_, aa_search_;
    VkRenderPass aa_pass_rg_ = VK_NULL_HANDLE, aa_pass_rgba_ = VK_NULL_HANDLE;
    VkFramebuffer aa_fb_edges_ = VK_NULL_HANDLE, aa_fb_weights_ = VK_NULL_HANDLE, aa_fb_out_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout aa_set_layout_ = VK_NULL_HANDLE;
    VkPipelineLayout aa_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool aa_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet aa_sets_[4]{};      // SMAA edges, SMAA weights, SMAA blend, FXAA
    VkPipeline aa_pipelines_[4]{};
    int aa_w_ = 0, aa_h_ = 0;
};

}  // namespace ac04::render::gs
