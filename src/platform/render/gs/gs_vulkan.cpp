#include "platform/render/gs/gs_vulkan.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "gs_conv_comp_spv.h"
#include "gs_decode_comp_spv.h"
#include "gs_depth_copy_frag_spv.h"
#include "gs_frag_spv.h"
#include "gs_vert_spv.h"
#include "platform/log.h"
#include "platform/render/gs/gs_pipeline_keys.h"
#include "platform/render/movie_texel_map.h"

namespace ac04::render::gs {
namespace {

// Texture images (GsBackend::TexImage): sizes rounded up to kTexAlign, freed images kept for reuse
// up to kFreeTextureBytes, set-1 descriptor sets in pools of kTexSetsPerPool.
constexpr int kTexAlign = 32;
constexpr std::uint64_t kFreeTextureBytes = std::uint64_t{128} << 20;
constexpr std::uint32_t kTexSetsPerPool = 1024;
constexpr std::uint32_t kDecodeSetsPerPool = 256;  // gs_decode.comp sets, per frame, pools added as needed
constexpr int kTargetHeight = 1024;  // native rows of the scratch images (conversions, feedback copies)
constexpr int kFeedbackWidth = 2048;
constexpr int kMaxScale = 8;

struct Vertex {
    float x, y, z;  // z: GS Z / 2^bits of the Z buffer format (the depth image value)
    std::uint8_t r, g, b, a;
    float s, t, q;
    float f;  // GS fog coefficient F (0-255): 255 keeps the colour, 0 gives FOGCOL
};
static_assert(sizeof(Vertex) == 32, "vertex layout");

constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

// GS Z of a vertex as the depth image value: clamped to the range of the Z format (ZBUF.PSM: Z32,
// Z24, Z16/Z16S) and divided by its size. D32 holds Z24 and Z16 exactly; Z32 loses the low bits.
float DepthValue(std::uint32_t z, std::uint64_t zbuf) {
    const std::uint32_t psm = static_cast<std::uint32_t>((zbuf >> 24) & 0xF);
    if (psm == 0) return static_cast<float>(static_cast<double>(z) / 4294967296.0);
    const std::uint32_t bits = psm == 1 ? 24 : 16;
    const std::uint32_t max = (1u << bits) - 1;
    return static_cast<float>(static_cast<double>(std::min(z, max)) / static_cast<double>(1u << bits));
}

// GS depth test (TEST.ZTE, ZTST: never, always, >=, >) and depth write (ZBUF.ZMSK) as pipeline key
// bits.
std::uint64_t DepthKey(std::uint64_t test, std::uint64_t zbuf) {
    std::uint64_t op = VK_COMPARE_OP_ALWAYS;
    if (((test >> 16) & 1) != 0) {
        switch ((test >> 17) & 3) {
            case 0: op = VK_COMPARE_OP_NEVER; break;
            case 1: op = VK_COMPARE_OP_ALWAYS; break;
            case 2: op = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
            default: op = VK_COMPARE_OP_GREATER; break;
        }
    }
    const std::uint64_t write = ((zbuf >> 32) & 1) == 0 ? 1 : 0;
    return op << 24 | write << 27;
}

// Push constants (std430 layout of gs.frag's Params block).
struct Params {
    float target_size[2];
    float pad0[2];
    std::int32_t rect[4];
    std::int32_t origin[2];
    std::int32_t tex_size[2];
    std::int32_t region[4];
    std::int32_t flags;
    std::int32_t aref;
    float fix;
    std::int32_t fogcol;  // FOGCOL: R bits 0-7, G 8-15, B 16-23
    // TEX1 for the filter choice: MMIN bits 0-2, decoded mip levels 3-5, LCM 6, L 7-8, K 9-20
    // (signed, 1/16), MMAG 21.
    std::int32_t tex1;
    std::int32_t scale;      // host pixels per GS pixel of the target drawn into
    std::int32_t tex_scale;  // host texels per GS texel of the frame-buffer copy sampled (flag 16)
    std::int32_t alpha;      // ALPHA bits 0-7 (A, B, C, D) for the blend done in the shader (flag 28)
};
static_assert(sizeof(Params) == 96, "push constant layout");

std::uint64_t Bits(std::uint64_t v, int lo, int n) { return (v >> lo) & ((1ull << n) - 1); }

// Blend state for the GS equation (A - B) * C + D, A/B/D in {Cs, Cd, 0}, C in {As, Ad, FIX}.
struct BlendState {
    bool enable = false;
    VkBlendFactor src = VK_BLEND_FACTOR_ONE, dst = VK_BLEND_FACTOR_ZERO;
    VkBlendOp op = VK_BLEND_OP_ADD;
    bool supported = true;
    bool src_over_128 = false;  // the shader outputs Cs / 128 (C = Ad: see MapBlend)
};

BlendState MapBlend(std::uint64_t alpha) {
    const std::uint32_t a = static_cast<std::uint32_t>(Bits(alpha, 0, 2)), b = static_cast<std::uint32_t>(Bits(alpha, 2, 2));
    const std::uint32_t c = static_cast<std::uint32_t>(Bits(alpha, 4, 2));
    const std::uint32_t d = static_cast<std::uint32_t>(Bits(alpha, 6, 2));
    BlendState s;
    s.enable = true;
    if (a == b) {
        if (d == 0) s.enable = false;
        else s.src = VK_BLEND_FACTOR_ZERO, s.dst = d == 1 ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ZERO;
        return s;
    }
    const std::uint32_t k = a << 4 | b << 2 | d;
    if (c == 1) {
        // C = Ad. Targets store the GS alpha byte as a / 255, so DST_ALPHA is Ad / 255 instead of
        // Ad / 128. Cs * Ad + Cd (the menu filters' ALPHA 0x58) is exact when the shader outputs
        // Cs / 128 instead of Cs / 255, as long as Cs <= 128 (UNORM outputs clamp to 1; the menu
        // filter colours stay below 0x80). Other equations with Ad are not mapped.
        if (k == 0x09) {
            s.src = VK_BLEND_FACTOR_DST_ALPHA;
            s.dst = VK_BLEND_FACTOR_ONE;
            s.src_over_128 = true;
            return s;
        }
        s.supported = false;
        s.src = VK_BLEND_FACTOR_DST_ALPHA;
        s.dst = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        return s;
    }
    switch (k) {
        case 0x05: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; break;  // (Cs-Cd)C+Cd
        case 0x09: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ONE; break;                   // Cs*C+Cd
        case 0x0A: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ZERO; break;                  // Cs*C
        case 0x21: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ONE; s.op = VK_BLEND_OP_REVERSE_SUBTRACT; break;  // Cd-Cs*C
        case 0x10: s.src = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; break;  // (Cd-Cs)C+Cs
        case 0x18: s.src = VK_BLEND_FACTOR_ONE; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; break;                   // Cd*C+Cs
        case 0x25: s.src = VK_BLEND_FACTOR_ZERO; s.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; break;        // Cd-Cd*C
        // The equations without a D term or with D = the other operand of the difference: linear in
        // Cs and Cd with factors C, 1 - C, 1 or 0 (negative results clamp to 0 as with COLCLAMP 1).
        case 0x06: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; s.op = VK_BLEND_OP_SUBTRACT; break;          // (Cs-Cd)C
        case 0x12: s.src = VK_BLEND_FACTOR_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; s.op = VK_BLEND_OP_REVERSE_SUBTRACT; break;  // (Cd-Cs)C
        case 0x1A: s.src = VK_BLEND_FACTOR_ZERO; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; break;                                             // Cd*C
        case 0x20: s.src = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR; s.dst = VK_BLEND_FACTOR_ZERO; break;                                   // Cs-Cs*C
        case 0x22:                                                                                                                      // -Cs*C
        case 0x26: s.src = VK_BLEND_FACTOR_ZERO; s.dst = VK_BLEND_FACTOR_ZERO; break;                                                   // -Cd*C
        case 0x24: s.src = VK_BLEND_FACTOR_ONE; s.dst = VK_BLEND_FACTOR_SRC1_COLOR; s.op = VK_BLEND_OP_SUBTRACT; break;                 // Cs-Cd*C
        default:
            s.supported = false;
            s.src = VK_BLEND_FACTOR_SRC1_COLOR;
            s.dst = VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
            break;
    }
    return s;
}

// Target layout class of a frame format: CT24 shares the CT32 layout; the 16-bit formats differ.
std::uint32_t LayoutClass(std::uint32_t psm) { return psm == kPsmCt16 || psm == kPsmCt16S ? psm : 0; }

std::uint64_t TargetKey(std::uint32_t fbp, std::uint32_t fbw, std::uint32_t psm) {
    return static_cast<std::uint64_t>(fbp) << 16 | static_cast<std::uint64_t>(fbw) << 8 | LayoutClass(psm);
}

// Push constants of gs_conv.comp.
struct ConvParams {
    std::int32_t src_psm, src_fbp, src_fbw;
    std::int32_t dst_psm, dst_tbp, dst_tbw;
    std::int32_t origin[2], size[2], src_size[2];
    std::int32_t texa;
    std::int32_t src_scale;  // the source target's scale: its native pixel (x, y) is read at (x, y) * scale + scale / 2
};
static_assert(sizeof(ConvParams) == 56, "conversion push constants");

// Page tables of gs_conv.comp (same order as its kFwd*/kInv* offsets), from GsMemory::PixelAddress
// within one page at block 0.
std::vector<std::uint32_t> BuildConvTables() {
    std::vector<std::uint32_t> t;
    auto fwd = [&](std::uint32_t psm, std::uint32_t pw, std::uint32_t ph, bool nibbles, std::uint32_t unit_shift) {
        for (std::uint32_t y = 0; y < ph; ++y) {
            for (std::uint32_t x = 0; x < pw; ++x) {
                std::uint32_t nib = 0;
                const std::uint32_t a = GsMemory::PixelAddress(psm, 0, pw / 64, x, y, &nib);
                t.push_back(nibbles ? a * 2 + nib : a >> unit_shift);
            }
        }
    };
    fwd(kPsmCt32, 64, 32, false, 2);
    fwd(kPsmCt16, 64, 64, false, 1);
    fwd(kPsmCt16S, 64, 64, false, 1);
    fwd(kPsmT8, 128, 64, false, 0);
    fwd(kPsmT4, 128, 128, true, 0);
    auto inv = [&](std::uint32_t psm, std::uint32_t ph, std::uint32_t unit_shift, std::uint32_t units) {
        const std::size_t base = t.size();
        t.resize(base + units, 0);
        for (std::uint32_t y = 0; y < ph; ++y) {
            for (std::uint32_t x = 0; x < 64; ++x) {
                const std::uint32_t a = GsMemory::PixelAddress(psm, 0, 1, x, y) >> unit_shift;
                t[base + a] = x | y << 8;
            }
        }
    };
    inv(kPsmCt32, 32, 2, 2048);
    inv(kPsmCt16, 64, 1, 4096);
    inv(kPsmCt16S, 64, 1, 4096);
    return t;
}

// Push constants of gs_decode.comp.
struct DecodeParams {
    std::int32_t psm, bp, bw, su, sv, w, h, dx, dy, texa;
};
static_assert(sizeof(DecodeParams) == 40, "decode push constants");

std::uint64_t PipelineKey(const BlendState& b, std::uint32_t write_mask) {
    return static_cast<std::uint64_t>(b.enable) | static_cast<std::uint64_t>(b.src) << 1 |
           static_cast<std::uint64_t>(b.dst) << 8 | static_cast<std::uint64_t>(b.op) << 15 |
           static_cast<std::uint64_t>(write_mask) << 20;
}

// A draw's full state key (PipelineKey | DepthKey) split into the pipeline it binds and the state
// set per draw (ADR-009, validation.md §2ao): the game alternates the depth test, the depth write
// and the alpha write on almost every draw, and a pipeline switch costs GPU time. Depth compare and
// depth write are dynamic state (VK_EXT_extended_dynamic_state). A blended pipeline always writes
// A and blends it with the blend constant (srcAlpha CONSTANT_ALPHA, dstAlpha ONE_MINUS_CONSTANT_ALPHA):
// constant 1 writes As as before (As * 1 + Ad * 0), constant 0 keeps the stored alpha
// (As * 0 + Ad * 1), as a colour write mask without A did. Unblended pipelines keep A in their mask.
struct DrawState {
    std::uint64_t pipeline_key = 0;
    VkCompareOp depth_op = VK_COMPARE_OP_ALWAYS;
    bool depth_write = false;
    float alpha_constant = 1.0f;  // blended pipelines only
};

DrawState SplitKey(std::uint64_t key) {
    DrawState s;
    s.depth_op = static_cast<VkCompareOp>((key >> 24) & 7);
    s.depth_write = ((key >> 27) & 1) != 0;
    s.pipeline_key = key & ~(0xFull << 24);
    if ((key & 1) != 0) {
        s.alpha_constant = ((key >> 23) & 1) != 0 ? 1.0f : 0.0f;
        s.pipeline_key |= 1ull << 23;
    }
    return s;
}

// Pipeline stages and accesses of an image in each layout, as this backend uses the layouts: the
// scope of the barriers that move images between them (GsBackend::Transitions).
struct LayoutUse {
    VkPipelineStageFlags stages;
    VkAccessFlags reads, writes;
};

LayoutUse UseOf(VkImageLayout layout) {
    constexpr VkPipelineStageFlags kTests =
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    constexpr VkPipelineStageFlags kShaders = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return {VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0, 0};
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:  // render passes load, draw and clear (vkCmdClearAttachments)
            return {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return {kTests, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL:  // sampled by gs_depth_copy.frag
            return {kTests | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT, 0};
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:  // sampled by the draws, conversions and anti-aliasing
            return {kShaders, VK_ACCESS_SHADER_READ_BIT, 0};
        case VK_IMAGE_LAYOUT_GENERAL:  // storage images of gs_conv.comp and gs_decode.comp
            return {kShaders, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT};
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return {VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0};
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL: return {VK_PIPELINE_STAGE_TRANSFER_BIT, 0, VK_ACCESS_TRANSFER_WRITE_BIT};
        default:
            return {VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_MEMORY_WRITE_BIT};
    }
}

}  // namespace

struct GsBackend::RecordScratch {
    // Texture work (RecordTextureWork): copies into the GPU's local memory (runs of blocks) and, in
    // stream order, the steps: the copies before a decode, then the decode.
    struct Step {
        std::size_t first_copy = 0, copies = 0;
        const DecodedTexture* decode = nullptr;
        TexImage* image = nullptr;
        std::size_t palette = 0;  // byte offset in decode_palettes_
    };
    std::vector<VkBufferCopy> copies;
    std::vector<Step> steps;
    std::vector<std::pair<const TextureOp*, std::size_t>> memory;   // memory ops and their staging offsets
    struct Upload {
        const DecodedTexture* texture = nullptr;  // decoded on the CPU, uploaded whole
        TexImage* image = nullptr;
        std::size_t offset = 0;  // in the staging buffer
    };
    std::vector<Upload> uploads;
    std::vector<VkImageMemoryBarrier> barriers;
    std::vector<Vertex> verts;
    std::vector<std::pair<std::size_t, std::size_t>> ranges;  // first, count per draw
    std::vector<std::array<float, 4>> boxes;                   // x0, y0, x1, y1 of each draw's vertices
};

const char* GsBackend::CopyKindName(int k) {
    static const char* const kNames[] = {"frame as texture", "field copy", "DATE / shader-blend regions", "GPU palettes",
                                         "view syncs", "conversions", "anti-aliasing"};
    return k >= 0 && k < 7 ? kNames[k] : "?";
}

GsBackend::GsBackend() = default;
GsBackend::~GsBackend() { Shutdown(); }

void GsBackend::LogOnce(const char* key, const char* text) {
    const std::string k = std::string("gsvk:") + key;
    log::WriteOnce(k.c_str(), log::Level::Warning, "renderer", "%s", text);
}

bool GsBackend::CreateImage(Image& img, int w, int h, VkImageUsageFlags usage, VkFormat format) {
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = format;
    ici.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = usage;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateImage(ctx_->allocator, &ici, &aci, &img.image, &img.alloc, nullptr) != VK_SUCCESS) return false;
    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    img.aspect = format == kDepthFormat ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    vci.subresourceRange = {img.aspect, 0, 1, 0, 1};
    vkCreateImageView(ctx_->device, &vci, nullptr, &img.view);
    img.width = w;
    img.height = h;
    img.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    return true;
}

void GsBackend::DestroyImage(Image& img) {
    if (img.view) vkDestroyImageView(ctx_->device, img.view, nullptr);
    if (img.image) vmaDestroyImage(ctx_->allocator, img.image, img.alloc);
    img = Image{};
}

bool GsBackend::EnsureBuffer(Buffer& b, std::size_t size, VkBufferUsageFlags usage, bool readback) {
    if (b.buffer && b.size >= size) return true;
    DestroyBuffer(b);
    std::size_t n = 1 << 16;
    while (n < size) n <<= 1;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = n;
    bci.usage = usage;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO;
    aci.flags = (readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT : VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
    VmaAllocationInfo info{};
    if (vmaCreateBuffer(ctx_->allocator, &bci, &aci, &b.buffer, &b.alloc, &info) != VK_SUCCESS) return false;
    b.mapped = info.pMappedData;
    b.size = n;
    return true;
}

void GsBackend::DestroyBuffer(Buffer& b) {
    if (b.buffer) vmaDestroyBuffer(ctx_->allocator, b.buffer, b.alloc);
    b = Buffer{};
}

void GsBackend::Transition(VkCommandBuffer cmd, Image& img, VkImageLayout layout) { Transitions(cmd, {{&img, layout}}); }

void GsBackend::Transitions(VkCommandBuffer cmd, std::initializer_list<std::pair<Image*, VkImageLayout>> images) {
    // One barrier for all of them. It waits only for the stages that used each image in its old
    // layout and makes only their writes visible, to the stages of the new layout (validation.md
    // §2al: the earlier ALL_COMMANDS barriers drained the whole GPU at every transition).
    VkImageMemoryBarrier b[4];
    std::uint32_t n = 0;
    VkPipelineStageFlags src = 0, dst = 0;
    for (const auto& [img, layout] : images) {
        if (img->layout == layout || n == 4) continue;
        const LayoutUse from = UseOf(img->layout), to = UseOf(layout);
        VkImageMemoryBarrier& x = b[n++];
        x = VkImageMemoryBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        x.image = img->image;
        x.subresourceRange = {img->aspect, 0, 1, 0, 1};
        x.oldLayout = img->layout;
        x.newLayout = layout;
        x.srcAccessMask = from.writes;
        x.dstAccessMask = to.reads | to.writes;
        src |= from.stages;
        dst |= to.stages;
        img->layout = layout;
    }
    if (n > 0) Barrier(cmd, src, dst, 0, nullptr, n, b);
}

void GsBackend::Barrier(VkCommandBuffer cmd, VkPipelineStageFlags src, VkPipelineStageFlags dst,
                        std::uint32_t buffer_count, const VkBufferMemoryBarrier* buffers, std::uint32_t image_count,
                        const VkImageMemoryBarrier* images) {
    vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, buffer_count, buffers, image_count, images);
    ++pass_stats_.barriers;
}

bool GsBackend::Init(VkContext& ctx, std::string& error) {
    ctx_ = &ctx;
    if (!ctx.dual_src_blend) {
        error = "the GPU does not support dual-source blending (needed for the GS blend equation)";
        return false;
    }
    VkAttachmentDescription att[2]{};
    att[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    att[1] = att[0];
    att[1].format = kDepthFormat;
    att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    sub.pDepthStencilAttachment = &dref;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 2;
    rpi.pAttachments = att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    vkCreateRenderPass(ctx.device, &rpi, nullptr, &pass_);

    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(ctx.physical_device, &props);
    max_image_dim_ = static_cast<int>(props.limits.maxImageDimension2D);
    // Set 0: feedback, DATE copy, movie and the GPU-drawn palette (bindings 1-4); set 1: the draw's
    // texture (binding 0).
    VkDescriptorSetLayoutBinding bindings[4]{};
    for (int i = 0; i < 4; ++i) {
        bindings[i].binding = static_cast<uint32_t>(i + 1);
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 4;
    dli.pBindings = bindings;
    vkCreateDescriptorSetLayout(ctx.device, &dli, nullptr, &set_layout_);
    bindings[0].binding = 0;
    dli.bindingCount = 1;
    vkCreateDescriptorSetLayout(ctx.device, &dli, nullptr, &tex_set_layout_);
    const VkDescriptorSetLayout set_layouts[2] = {set_layout_, tex_set_layout_};
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Params)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 2;
    pli.pSetLayouts = set_layouts;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx.device, &pli, nullptr, &layout_);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(ctx.device, &sci, nullptr, &sampler_);
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    vkCreateSampler(ctx.device, &sci, nullptr, &linear_sampler_);

    const VkImageUsageFlags tex_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (!CreateImage(feedback_, kFeedbackWidth, kTargetHeight, tex_usage | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT) ||
        !CreateImage(date_, kFeedbackWidth, kTargetHeight, tex_usage) || !CreateImage(clut_img_, 16, 16, tex_usage) ||
        !CreateImage(present_, kFeedbackWidth, kTargetHeight, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        error = "cannot create the GS renderer images";
        return false;
    }
    // One set per frame in flight: Record rewrites its frame's set (the images can be replaced
    // between frames: scratch growth, the movie stage's output).
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4 * kFrames};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kFrames;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    vkCreateDescriptorPool(ctx.device, &dpi, nullptr, &pool_);
    const VkDescriptorSetLayout layouts[kFrames] = {set_layout_, set_layout_};
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = kFrames;
    dai.pSetLayouts = layouts;
    vkAllocateDescriptorSets(ctx.device, &dai, sets_);
    if (!InitTextures(error)) return false;

    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(gs_vert_spv);
    smi.pCode = gs_vert_spv;
    vkCreateShaderModule(ctx.device, &smi, nullptr, &vs_);
    smi.codeSize = sizeof(gs_frag_spv);
    smi.pCode = gs_frag_spv;
    vkCreateShaderModule(ctx.device, &smi, nullptr, &fs_);

    // Initial layouts: images read by the shader start out cleared and readable.
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = ctx.queue_family;
    vkCreateCommandPool(ctx.device, &cpi, nullptr, &pool);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(ctx.device, &cai, &cmd);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    for (Image* img : {&dummy_texture_.img, &feedback_, &date_, &present_}) {
        Transition(cmd, *img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(cmd, img->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    }
    // The GPU's local memory starts as GsMemory does: zero.
    vkCmdFillBuffer(cmd, gs_memory_.buffer, 0, GsMemory::kSize, 0);
    Transition(cmd, dummy_texture_.img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, date_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, present_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(ctx.queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(ctx.queue);
    vkDestroyCommandPool(ctx.device, pool, nullptr);
    if (!InitConvert(error)) return false;
    if (!InitDepthCopy(error)) return false;
    // Anti-aliasing is optional: without it the setting has no effect.
    std::string aa_error;
    if (!InitAntiAlias(aa_error)) AC04_LOG_ERROR("renderer", "anti-aliasing unavailable: %s", aa_error.c_str());
    StartPipelineWarmUp();
    return true;
}

bool GsBackend::InitDepthCopy(std::string& error) {
    VkDescriptorSetLayoutBinding b{};
    b.binding = 0;
    b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b.descriptorCount = 1;
    b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &b;
    vkCreateDescriptorSetLayout(ctx_->device, &dli, nullptr, &depth_copy_set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 24};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &depth_copy_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx_->device, &pli, nullptr, &depth_copy_layout_);
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(gs_depth_copy_frag_spv);
    smi.pCode = gs_depth_copy_frag_spv;
    VkShaderModule fs = VK_NULL_HANDLE;
    vkCreateShaderModule(ctx_->device, &smi, nullptr, &fs);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[4] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                               {1, 0, VK_FORMAT_R8G8B8A8_UINT, 12},
                                               {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 16},
                                               {3, 0, VK_FORMAT_R32_SFLOAT, 28}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = va;
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
    VkPipelineColorBlendAttachmentState cba{};  // colour not written
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;
    dss.depthCompareOp = VK_COMPARE_OP_ALWAYS;
    dss.depthWriteEnable = VK_TRUE;
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
    gpi.pDepthStencilState = &dss;
    gpi.pDynamicState = &ds;
    gpi.layout = depth_copy_layout_;
    gpi.renderPass = pass_;
    const VkResult r = vkCreateGraphicsPipelines(ctx_->device, ctx_->pipeline_cache, 1, &gpi, nullptr, &depth_copy_pipeline_);
    vkDestroyShaderModule(ctx_->device, fs, nullptr);
    if (r != VK_SUCCESS) {
        error = "cannot create the GS depth copy pipeline";
        return false;
    }
    const VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxDepthCopies};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kMaxDepthCopies;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    for (int f = 0; f < kFrames; ++f) vkCreateDescriptorPool(ctx_->device, &dpi, nullptr, &depth_copy_pool_[f]);
    return true;
}

bool GsBackend::InitConvert(std::string& error) {
    VkDescriptorSetLayoutBinding b[4]{};
    const VkDescriptorType types[4] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for (int i = 0; i < 4; ++i) {
        b[i].binding = static_cast<uint32_t>(i);
        b[i].descriptorType = types[i];
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 4;
    dli.pBindings = b;
    vkCreateDescriptorSetLayout(ctx_->device, &dli, nullptr, &conv_set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ConvParams)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &conv_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx_->device, &pli, nullptr, &conv_layout_);
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(gs_conv_comp_spv);
    smi.pCode = gs_conv_comp_spv;
    VkShaderModule cs = VK_NULL_HANDLE;
    vkCreateShaderModule(ctx_->device, &smi, nullptr, &cs);
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, cs, "main", nullptr};
    cpi.layout = conv_layout_;
    const VkResult r = vkCreateComputePipelines(ctx_->device, ctx_->pipeline_cache, 1, &cpi, nullptr, &conv_pipeline_);
    vkDestroyShaderModule(ctx_->device, cs, nullptr);
    if (r != VK_SUCCESS) {
        error = "cannot create the GS format conversion pipeline";
        return false;
    }
    const VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxConversions},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * kMaxConversions},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kMaxConversions}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = kMaxConversions;
    dpi.poolSizeCount = 3;
    dpi.pPoolSizes = ps;
    for (int f = 0; f < kFrames; ++f) {
        vkCreateDescriptorPool(ctx_->device, &dpi, nullptr, &conv_pool_[f]);
        if (!EnsureBuffer(conv_clut_[f], kMaxConversions * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
            error = "cannot create the GS conversion buffers";
            return false;
        }
    }
    const std::vector<std::uint32_t> tables = BuildConvTables();
    if (!EnsureBuffer(conv_tables_, tables.size() * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) {
        error = "cannot create the GS conversion buffers";
        return false;
    }
    std::memcpy(conv_tables_.mapped, tables.data(), tables.size() * 4);
    vmaFlushAllocation(ctx_->allocator, conv_tables_.alloc, 0, VK_WHOLE_SIZE);
    return true;
}

bool GsBackend::InitTextures(std::string& error) {
    // The GPU's copy of GS local memory (filled with zero in Init's first commands).
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = GsMemory::kSize;
    bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    if (vmaCreateBuffer(ctx_->allocator, &bci, &aci, &gs_memory_.buffer, &gs_memory_.alloc, nullptr) != VK_SUCCESS) {
        error = "cannot create the GPU copy of GS local memory";
        return false;
    }
    gs_memory_.size = GsMemory::kSize;
    // gs_decode.comp: local memory, layout tables, palette, the texture image.
    VkDescriptorSetLayoutBinding b[4]{};
    const VkDescriptorType types[4] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                       VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE};
    for (int i = 0; i < 4; ++i) {
        b[i].binding = static_cast<uint32_t>(i);
        b[i].descriptorType = types[i];
        b[i].descriptorCount = 1;
        b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 4;
    dli.pBindings = b;
    vkCreateDescriptorSetLayout(ctx_->device, &dli, nullptr, &decode_set_layout_);
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(DecodeParams)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &decode_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    vkCreatePipelineLayout(ctx_->device, &pli, nullptr, &decode_layout_);
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smi.codeSize = sizeof(gs_decode_comp_spv);
    smi.pCode = gs_decode_comp_spv;
    VkShaderModule cs = VK_NULL_HANDLE;
    vkCreateShaderModule(ctx_->device, &smi, nullptr, &cs);
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, cs, "main", nullptr};
    cpi.layout = decode_layout_;
    const VkResult r = vkCreateComputePipelines(ctx_->device, ctx_->pipeline_cache, 1, &cpi, nullptr, &decode_pipeline_);
    vkDestroyShaderModule(ctx_->device, cs, nullptr);
    if (r != VK_SUCCESS) {
        error = "cannot create the GS texture decode pipeline";
        return false;
    }
    // The texture bound for draws that sample none (set 1 must be valid in every draw).
    if (!CreateImage(dummy_texture_.img, 1, 1, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
        error = "cannot create the GS renderer images";
        return false;
    }
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTexSetsPerPool};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    dpi.maxSets = kTexSetsPerPool;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    vkCreateDescriptorPool(ctx_->device, &dpi, nullptr, &pool);
    tex_pools_.push_back(pool);
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = pool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &tex_set_layout_;
    vkAllocateDescriptorSets(ctx_->device, &dai, &dummy_texture_.set);
    dummy_texture_.pool = pool;
    VkDescriptorImageInfo info{sampler_, dummy_texture_.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = dummy_texture_.set;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(ctx_->device, 1, &w, 0, nullptr);
    return true;
}

GsBackend::TexImage* GsBackend::AcquireTexImage(const std::shared_ptr<const DecodedTexture>& t) {
    const int w = (static_cast<int>(t->width) + kTexAlign - 1) / kTexAlign * kTexAlign;
    const int h = (static_cast<int>(t->height) + kTexAlign - 1) / kTexAlign * kTexAlign;
    if (w > max_image_dim_ || h > max_image_dim_) {
        LogOnce("tex-big", "texture larger than the device's image size limit (skipped)");
        return nullptr;
    }
    TexImage img;
    bool reused = false;
    const auto range = free_textures_.equal_range({w, h});
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second.free_after > record_count_) continue;
        img = std::move(it->second);
        free_textures_.erase(it);
        free_texture_bytes_ -= static_cast<std::uint64_t>(w) * h * 4;
        reused = true;
        break;
    }
    if (!reused) {
        if (!CreateImage(img.img, w, h,
                         VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                             VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
            LogOnce("tex-image", "cannot create a texture image (draws skipped)");
            return nullptr;
        }
        // Set 1 for this image: from the newest pool, a new pool when that one is full.
        VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorSetCount = 1;
        dai.pSetLayouts = &tex_set_layout_;
        dai.descriptorPool = tex_pools_.back();
        if (vkAllocateDescriptorSets(ctx_->device, &dai, &img.set) != VK_SUCCESS) {
            VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kTexSetsPerPool};
            VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            dpi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            dpi.maxSets = kTexSetsPerPool;
            dpi.poolSizeCount = 1;
            dpi.pPoolSizes = &ps;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(ctx_->device, &dpi, nullptr, &pool) != VK_SUCCESS) {
                DestroyImage(img.img);
                LogOnce("tex-set", "cannot create a texture descriptor set (draws skipped)");
                return nullptr;
            }
            tex_pools_.push_back(pool);
            dai.descriptorPool = pool;
            if (vkAllocateDescriptorSets(ctx_->device, &dai, &img.set) != VK_SUCCESS) {
                DestroyImage(img.img);
                LogOnce("tex-set", "cannot create a texture descriptor set (draws skipped)");
                return nullptr;
            }
        }
        img.pool = dai.descriptorPool;
        VkDescriptorImageInfo info{sampler_, img.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet wr{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wr.dstSet = img.set;
        wr.descriptorCount = 1;
        wr.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr.pImageInfo = &info;
        vkUpdateDescriptorSets(ctx_->device, 1, &wr, 0, nullptr);
    }
    img.img.layout = VK_IMAGE_LAYOUT_UNDEFINED;  // earlier contents are not needed
    img.texture = t;
    img.free_after = 0;
    return &textures_.insert_or_assign(t->serial, std::move(img)).first->second;
}

void GsBackend::DestroyTexImage(TexImage& t) {
    if (t.set) vkFreeDescriptorSets(ctx_->device, t.pool, 1, &t.set);
    t.set = VK_NULL_HANDLE;
    DestroyImage(t.img);
}

void GsBackend::ReleaseUnusedTextures() {
    for (auto it = textures_.begin(); it != textures_.end();) {
        if (!it->second.texture.expired()) {
            ++it;
            continue;
        }
        TexImage t = std::move(it->second);
        it = textures_.erase(it);
        t.texture.reset();
        // The frames in flight may still sample it.
        t.free_after = record_count_ + kFrames;
        free_texture_bytes_ += static_cast<std::uint64_t>(t.img.width) * t.img.height * 4;
        const std::pair<int, int> size{t.img.width, t.img.height};
        free_textures_.emplace(size, std::move(t));
    }
    for (auto it = free_textures_.begin(); free_texture_bytes_ > kFreeTextureBytes && it != free_textures_.end();) {
        if (it->second.free_after > record_count_) {
            ++it;
            continue;
        }
        free_texture_bytes_ -= static_cast<std::uint64_t>(it->second.img.width) * it->second.img.height * 4;
        DestroyTexImage(it->second);
        it = free_textures_.erase(it);
    }
}

void GsBackend::CheckVerifyReadback(int frame) {
    auto& items = verify_items_[frame];
    if (items.empty()) return;
    vmaInvalidateAllocation(ctx_->allocator, verify_buffer_[frame].alloc, 0, VK_WHOLE_SIZE);
    const auto* gpu = static_cast<const std::uint8_t*>(verify_buffer_[frame].mapped);
    for (const VerifyItem& item : items) {
        const DecodedTexture& t = *item.texture;
        ++verify_count_;
        std::size_t offset = item.offset;
        bool same = true;
        std::uint32_t bad_x = 0, bad_y = 0, gpu_px = 0, cpu_px = 0;
        for (const DecodeRect& r : t.rects) {
            for (std::uint32_t y = 0; y < r.h && same; ++y) {
                const std::uint8_t* g = gpu + offset + static_cast<std::size_t>(y) * r.w * 4;
                const std::uint8_t* c = t.rgba.data() + (static_cast<std::size_t>(r.dy + y) * t.width + r.dx) * 4;
                if (std::memcmp(g, c, static_cast<std::size_t>(r.w) * 4) == 0) continue;
                for (std::uint32_t x = 0; x < r.w; ++x) {
                    if (std::memcmp(g + x * 4, c + x * 4, 4) == 0) continue;
                    bad_x = r.dx + x;
                    bad_y = r.dy + y;
                    std::memcpy(&gpu_px, g + x * 4, 4);
                    std::memcpy(&cpu_px, c + x * 4, 4);
                    break;
                }
                same = false;
            }
            offset += static_cast<std::size_t>(r.w) * r.h * 4;
        }
        if (same) continue;
        ++verify_mismatches_;
        if (verify_mismatches_ <= 8) {
            const DecodeRect& r0 = t.rects.front();
            AC04_LOG_WARN("renderer",
                          "texture decode differs (GPU vs CPU): PSM 0x%02X TBP 0x%X TBW %u, %ux%u at (%u,%u), %u mip "
                          "levels; texel (%u,%u) of the image: GPU %08X, CPU %08X",
                          t.psm, r0.bp, r0.bw, r0.w, r0.h, r0.su, r0.sv, t.levels, bad_x, bad_y, gpu_px, cpu_px);
        }
    }
    items.clear();
}

void GsBackend::RecordTextureWork(VkCommandBuffer cmd, int frame, const std::vector<DrawCommand>& draws,
                                  const std::vector<TextureOp>& ops) {
    CheckVerifyReadback(frame);
    ReleaseUnusedTextures();
    TextureStats stats;
    auto& copies = scratch_->copies;
    auto& steps = scratch_->steps;
    auto& memory = scratch_->memory;
    auto& uploads = scratch_->uploads;
    copies.clear();
    steps.clear();
    memory.clear();
    uploads.clear();
    // 1. Plan, in stream order: memory updates go into the GPU copy just before the next decode
    // (a decode op whose texture nobody holds any more is dropped, its memory update kept).
    std::size_t staging_bytes = 0, palettes = 0;
    std::size_t pending_first = 0;
    for (const TextureOp& op : ops) {
        if (!op.decode) {
            memory.emplace_back(&op, staging_bytes);
            for (std::size_t i = 0; i < op.blocks.size(); ++i) {
                const VkDeviceSize src = staging_bytes + i * 256, dst = static_cast<VkDeviceSize>(op.blocks[i]) * 256;
                if (copies.size() > pending_first && copies.back().srcOffset + copies.back().size == src &&
                    copies.back().dstOffset + copies.back().size == dst) {
                    copies.back().size += 256;
                } else {
                    copies.push_back(VkBufferCopy{src, dst, 256});
                }
            }
            staging_bytes += op.bytes.size();
            continue;
        }
        if (op.decode.use_count() == 1) continue;  // only this op holds it: never drawn
        TexImage* image = AcquireTexImage(op.decode);
        if (!image) continue;
        RecordScratch::Step s;
        s.first_copy = pending_first;
        s.copies = copies.size() - pending_first;
        pending_first = copies.size();
        s.decode = op.decode.get();
        s.image = image;
        s.palette = palettes * 1024;
        ++palettes;
        steps.push_back(s);
    }
    if (copies.size() > pending_first) {
        RecordScratch::Step s;
        s.first_copy = pending_first;
        s.copies = copies.size() - pending_first;
        steps.push_back(s);
    }
    // Textures decoded on the CPU that have no image yet are uploaded; a texture with neither has
    // lost its decode op (not expected) and is not drawn.
    for (const DrawCommand& d : draws) {
        const DecodedTexture* t = d.texture.get();
        if (!t || t->width == 0 || t->height == 0 || textures_.count(t->serial) != 0) continue;
        if (t->rgba.empty()) {
            LogOnce("tex-lost", "texture without a decode (draws skipped)");
            continue;
        }
        TexImage* image = AcquireTexImage(d.texture);
        if (!image) continue;
        uploads.push_back({t, image, staging_bytes});
        staging_bytes += t->rgba.size();
    }
    {
        std::unordered_set<std::uint64_t> seen;
        for (const DrawCommand& d : draws) {
            if (d.texture && seen.insert(d.texture->serial).second) ++stats.textures;
        }
    }
    // 2. Host data: memory blocks and CPU texels in the staging buffer, palettes.
    if (staging_bytes > 0) {
        if (!EnsureBuffer(staging_[frame], staging_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT)) return;
        auto* staging = static_cast<std::uint8_t*>(staging_[frame].mapped);
        for (const auto& [op, offset] : memory) std::memcpy(staging + offset, op->bytes.data(), op->bytes.size());
        for (const auto& u : uploads) std::memcpy(staging + u.offset, u.texture->rgba.data(), u.texture->rgba.size());
        vmaFlushAllocation(ctx_->allocator, staging_[frame].alloc, 0, VK_WHOLE_SIZE);
    }
    if (palettes > 0) {
        if (!EnsureBuffer(decode_palettes_[frame], palettes * 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)) return;
        auto* out = static_cast<std::uint8_t*>(decode_palettes_[frame].mapped);
        for (const RecordScratch::Step& s : steps) {
            if (!s.decode) continue;
            if (s.decode->palette) std::memcpy(out + s.palette, s.decode->palette->data(), 1024);
            else std::memset(out + s.palette, 0, 1024);
        }
        vmaFlushAllocation(ctx_->allocator, decode_palettes_[frame].alloc, 0, VK_WHOLE_SIZE);
    }
    stats.upload_bytes = staging_bytes + palettes * 1024;
    // 3. Commands. The images to write: decode targets to GENERAL, upload targets to TRANSFER_DST
    // (earlier contents discarded).
    auto& barriers = scratch_->barriers;
    barriers.clear();
    auto image_barrier = [&](Image& img, VkImageLayout layout, VkAccessFlags src, VkAccessFlags dst) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.oldLayout = img.layout;
        b.newLayout = layout;
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        barriers.push_back(b);
        img.layout = layout;
    };
    auto flush_barriers = [&](VkPipelineStageFlags src, VkPipelineStageFlags dst) {
        if (barriers.empty()) return;
        Barrier(cmd, src, dst, 0, nullptr, static_cast<uint32_t>(barriers.size()), barriers.data());
        barriers.clear();
    };
    for (const RecordScratch::Step& s : steps) {
        if (s.image) image_barrier(s.image->img, VK_IMAGE_LAYOUT_GENERAL, 0, VK_ACCESS_SHADER_WRITE_BIT);
    }
    for (const auto& u : uploads) image_barrier(u.image->img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
    flush_barriers(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT);
    for (const auto& u : uploads) {
        VkBufferImageCopy c{};
        c.bufferOffset = u.offset;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {u.texture->width, u.texture->height, 1};
        vkCmdCopyBufferToImage(cmd, staging_[frame].buffer, u.image->img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        ++stats.uploads;
    }
    if (!steps.empty()) {
        for (auto& pool : decode_pools_[frame]) vkResetDescriptorPool(ctx_->device, pool, 0);
        std::size_t pool_index = 0;
        auto alloc_set = [&]() -> VkDescriptorSet {
            VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            dai.descriptorSetCount = 1;
            dai.pSetLayouts = &decode_set_layout_;
            VkDescriptorSet set = VK_NULL_HANDLE;
            while (true) {
                if (pool_index == decode_pools_[frame].size()) {
                    const VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3 * kDecodeSetsPerPool},
                                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, kDecodeSetsPerPool}};
                    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                    dpi.maxSets = kDecodeSetsPerPool;
                    dpi.poolSizeCount = 2;
                    dpi.pPoolSizes = ps;
                    VkDescriptorPool pool = VK_NULL_HANDLE;
                    if (vkCreateDescriptorPool(ctx_->device, &dpi, nullptr, &pool) != VK_SUCCESS) return VK_NULL_HANDLE;
                    decode_pools_[frame].push_back(pool);
                }
                dai.descriptorPool = decode_pools_[frame][pool_index];
                if (vkAllocateDescriptorSets(ctx_->device, &dai, &set) == VK_SUCCESS) return set;
                ++pool_index;
            }
        };
        bool bound = false;
        auto memory_barrier = [&](VkPipelineStageFlags src_stage, VkAccessFlags src, VkPipelineStageFlags dst_stage,
                                  VkAccessFlags dst) {
            VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer = gs_memory_.buffer;
            b.size = VK_WHOLE_SIZE;
            b.srcAccessMask = src;
            b.dstAccessMask = dst;
            Barrier(cmd, src_stage, dst_stage, 1, &b, 0, nullptr);
        };
        for (const RecordScratch::Step& s : steps) {
            if (s.copies > 0) {
                // Earlier decodes (this frame's and the last frame's) read the old contents first.
                memory_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               VK_ACCESS_TRANSFER_WRITE_BIT);
                vkCmdCopyBuffer(cmd, staging_[frame].buffer, gs_memory_.buffer, static_cast<uint32_t>(s.copies),
                                copies.data() + s.first_copy);
                memory_barrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                               VK_ACCESS_SHADER_READ_BIT);
                ++stats.memory_updates;
            }
            if (!s.decode) continue;
            const VkDescriptorSet set = alloc_set();
            if (!set) {
                LogOnce("decode-set", "cannot allocate a texture decode descriptor set (texture not decoded)");
                continue;
            }
            VkDescriptorBufferInfo mem_info{gs_memory_.buffer, 0, VK_WHOLE_SIZE};
            VkDescriptorBufferInfo tab_info{conv_tables_.buffer, 0, VK_WHOLE_SIZE};
            VkDescriptorBufferInfo pal_info{decode_palettes_[frame].buffer, s.palette, 1024};
            VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, s.image->img.view, VK_IMAGE_LAYOUT_GENERAL};
            VkWriteDescriptorSet w[4]{};
            for (int i = 0; i < 4; ++i) {
                w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                w[i].dstSet = set;
                w[i].dstBinding = static_cast<uint32_t>(i);
                w[i].descriptorCount = 1;
                w[i].descriptorType = i < 3 ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            }
            w[0].pBufferInfo = &mem_info;
            w[1].pBufferInfo = &tab_info;
            w[2].pBufferInfo = &pal_info;
            w[3].pImageInfo = &dst_info;
            vkUpdateDescriptorSets(ctx_->device, 4, w, 0, nullptr);
            if (!bound) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, decode_pipeline_);
                bound = true;
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, decode_layout_, 0, 1, &set, 0, nullptr);
            for (const DecodeRect& r : s.decode->rects) {
                if (r.w == 0 || r.h == 0) continue;
                const DecodeParams dp{static_cast<std::int32_t>(s.decode->psm), static_cast<std::int32_t>(r.bp),
                                      static_cast<std::int32_t>(r.bw), static_cast<std::int32_t>(r.su),
                                      static_cast<std::int32_t>(r.sv), static_cast<std::int32_t>(r.w),
                                      static_cast<std::int32_t>(r.h), static_cast<std::int32_t>(r.dx),
                                      static_cast<std::int32_t>(r.dy), static_cast<std::int32_t>(s.decode->texa)};
                vkCmdPushConstants(cmd, decode_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(dp), &dp);
                vkCmdDispatch(cmd, (r.w + 7) / 8, (r.h + 7) / 8, 1);
            }
            ++stats.decodes;
        }
        // --verify-texture-decode: the decoded rectangles read back for the comparison with the CPU
        // decode when this frame slot comes round again.
        if (verify_) {
            std::size_t bytes = 0;
            for (const RecordScratch::Step& s : steps) {
                if (!s.decode || s.decode->rgba.empty()) continue;
                for (const DecodeRect& r : s.decode->rects) bytes += static_cast<std::size_t>(r.w) * r.h * 4;
            }
            if (bytes > 0 && EnsureBuffer(verify_buffer_[frame], bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true)) {
                for (const RecordScratch::Step& s : steps) {
                    if (s.image && !s.decode->rgba.empty())
                        image_barrier(s.image->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_WRITE_BIT,
                                      VK_ACCESS_TRANSFER_READ_BIT);
                }
                flush_barriers(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
                std::size_t offset = 0;
                for (const RecordScratch::Step& s : steps) {
                    if (!s.image || s.decode->rgba.empty()) continue;
                    // The op still holds the texture: find its shared_ptr through the image's weak_ptr.
                    verify_items_[frame].push_back({s.image->texture.lock(), offset});
                    for (const DecodeRect& r : s.decode->rects) {
                        VkBufferImageCopy c{};
                        c.bufferOffset = offset;
                        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        c.imageOffset = {static_cast<int32_t>(r.dx), static_cast<int32_t>(r.dy), 0};
                        c.imageExtent = {r.w, r.h, 1};
                        if (r.w > 0 && r.h > 0)
                            vkCmdCopyImageToBuffer(cmd, s.image->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                   verify_buffer_[frame].buffer, 1, &c);
                        offset += static_cast<std::size_t>(r.w) * r.h * 4;
                    }
                }
            }
        }
    }
    // 4. Everything written becomes shader-readable.
    for (const RecordScratch::Step& s : steps) {
        if (s.image && s.image->img.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
            image_barrier(s.image->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    }
    for (const auto& u : uploads)
        image_barrier(u.image->img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    flush_barriers(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    stats.images = static_cast<std::uint32_t>(textures_.size() + free_textures_.size());
    for (const auto& [serial, t] : textures_) stats.image_bytes += static_cast<std::uint64_t>(t.img.width) * t.img.height * 4;
    stats.image_bytes += free_texture_bytes_;
    texture_stats_ = stats;
}

bool GsBackend::ConvertCore(VkCommandBuffer cmd, int frame, Target& src, const ConvRequest& r) {
    if (r.u1 <= r.u0 || r.v1 <= r.v0) return false;
    if (conv_count_ >= kMaxConversions) {
        LogOnce("conv-full", "too many frame-buffer format conversions in one frame (skipped)");
        return false;
    }
    const std::uint32_t slot = conv_count_++;
    std::uint32_t clut[256]{};
    if (r.clut) std::memcpy(clut, r.clut->data(), sizeof(clut));
    std::memcpy(static_cast<std::uint8_t*>(conv_clut_[frame].mapped) + slot * 1024, clut, sizeof(clut));
    vmaFlushAllocation(ctx_->allocator, conv_clut_[frame].alloc, slot * 1024, 1024);
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = conv_pool_[frame];
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &conv_set_layout_;
    if (vkAllocateDescriptorSets(ctx_->device, &dai, &set) != VK_SUCCESS) return false;
    VkDescriptorImageInfo src_info{sampler_, src.img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo tab_info{conv_tables_.buffer, 0, VK_WHOLE_SIZE};
    VkDescriptorBufferInfo clut_info{conv_clut_[frame].buffer, slot * 1024, 1024};
    VkDescriptorImageInfo dst_info{VK_NULL_HANDLE, feedback_.view, VK_IMAGE_LAYOUT_GENERAL};
    VkWriteDescriptorSet w[4]{};
    for (int i = 0; i < 4; ++i) {
        w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[i].dstSet = set;
        w[i].dstBinding = static_cast<uint32_t>(i);
        w[i].descriptorCount = 1;
    }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &src_info;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[1].pBufferInfo = &tab_info;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[2].pBufferInfo = &clut_info;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    w[3].pImageInfo = &dst_info;
    vkUpdateDescriptorSets(ctx_->device, 4, w, 0, nullptr);

    ConvParams cp{};
    cp.src_psm = static_cast<std::int32_t>(src.psm);
    cp.src_fbp = static_cast<std::int32_t>(src.fbp);
    cp.src_fbw = static_cast<std::int32_t>(src.fbw);
    cp.dst_psm = static_cast<std::int32_t>(r.psm);
    cp.dst_tbp = static_cast<std::int32_t>(r.tbp);
    cp.dst_tbw = static_cast<std::int32_t>(r.tbw);
    cp.origin[0] = r.u0;
    cp.origin[1] = r.v0;
    cp.size[0] = r.u1 - r.u0;
    cp.size[1] = r.v1 - r.v0;
    cp.src_size[0] = src.native_w;
    cp.src_size[1] = src.native_h;
    cp.texa = static_cast<std::int32_t>(r.texa);
    cp.src_scale = src.scale;

    Transitions(cmd, {{&src.img, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {&feedback_, VK_IMAGE_LAYOUT_GENERAL}});
    feedback_copy_ = FeedbackCopy{};  // feedback_ no longer holds a frame-buffer copy
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, conv_pipeline_);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, conv_layout_, 0, 1, &set, 0, nullptr);
    vkCmdPushConstants(cmd, conv_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cp), &cp);
    vkCmdDispatch(cmd, static_cast<uint32_t>((cp.size[0] + 7) / 8), static_cast<uint32_t>((cp.size[1] + 7) / 8), 1);
    CountCopy(kCopyConvert, static_cast<std::uint64_t>(cp.size[0]), static_cast<std::uint64_t>(cp.size[1]));
    // The source stays sampled until it is drawn into again (begin_pass).
    Transition(cmd, feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

void GsBackend::Convert(VkCommandBuffer cmd, int frame, Target& src, const DrawCommand& d, const std::array<int, 4>& box) {
    // Texels to convert: the ones the draw can read (Record's read_box), within the feedback image.
    ConvRequest r;
    r.u0 = std::clamp(box[0], 0, kFeedbackWidth);
    r.v0 = std::clamp(box[1], 0, kTargetHeight);
    r.u1 = std::clamp(box[2], r.u0, kFeedbackWidth);
    r.v1 = std::clamp(box[3], r.v0, kTargetHeight);
    r.psm = static_cast<std::uint32_t>(Bits(d.tex0, 20, 6));
    r.tbp = static_cast<std::uint32_t>(Bits(d.tex0, 0, 14));
    r.tbw = static_cast<std::uint32_t>(Bits(d.tex0, 14, 6));
    r.texa = static_cast<std::uint32_t>(Bits(d.texa, 0, 8) | Bits(d.texa, 15, 1) << 15 | Bits(d.texa, 32, 8) << 16);
    r.clut = d.frame_clut.get();
    if (ConvertCore(cmd, frame, src, r)) Mark(cmd, GpuSection::kConvert);
}

GsBackend::Target* GsBackend::NewerView(const Target& t) {
    // The GS has one local memory: targets that share a base page (the same frame buffer drawn
    // with another format or width) are views of the same bytes. The newest of them, when newer
    // than `t`, holds what `t` must show.
    Target* newest = nullptr;
    for (auto& [key, u] : targets_) {
        if (&u == &t || u.fbp != t.fbp || u.last_write <= t.last_write) continue;
        if (!newest || u.last_write > newest->last_write) newest = &u;
    }
    return newest;
}

void GsBackend::Sync(VkCommandBuffer cmd, int frame, Target& t, Target& newer) {
    // `newer` converted into `t`'s format over the rows of `t` that it covers.
    Target* newest = &newer;
    // Bytes the newer view has drawn, as rows of `t`.
    const std::int64_t src_bpp = newest->psm == 0 ? 4 : 2, dst_bpp = t.psm == 0 ? 4 : 2;
    const std::int64_t bytes = static_cast<std::int64_t>(std::max<std::uint32_t>(newest->fbw, 1)) * 64 *
                               std::max(newest->used_height, 1) * src_bpp;
    const std::int64_t row = static_cast<std::int64_t>(std::max<std::uint32_t>(t.fbw, 1)) * 64 * dst_bpp;
    // A scaled target holds only the rows drawn so far: grow it to the rows the newer view covers.
    const int rows = static_cast<int>(std::min<std::int64_t>((bytes + row - 1) / row, kTargetHeight));
    if (rows > t.native_h) GetTarget(cmd, frame, t.fbp, t.fbw, t.psm == 0 ? kPsmCt32 : t.psm, rows);
    ConvRequest r;
    r.u1 = std::min(t.native_w, kFeedbackWidth);
    r.v1 = static_cast<std::int32_t>(std::min<std::int64_t>((bytes + row - 1) / row, std::min(t.native_h, kTargetHeight)));
    r.psm = t.psm == 0 ? kPsmCt32 : t.psm;
    r.tbp = t.fbp * 32;
    r.tbw = t.fbw;
    r.texa = 0x80u << 16;  // 16-bit views: alpha bit -> 0x80, as gs.frag stores it (flag 24)
    if (!ConvertCore(cmd, frame, *newest, r)) return;
    // The conversion is native; a scaled target gets each converted pixel as a block of
    // scale x scale host pixels (nearest).
    Transitions(cmd, {{&feedback_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, {&t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
    if (t.scale == 1) {
        VkImageCopy c{};
        c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.dstSubresource = c.srcSubresource;
        c.extent = {static_cast<uint32_t>(r.u1), static_cast<uint32_t>(r.v1), 1};
        vkCmdCopyImage(cmd, feedback_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.img.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        CountCopy(kCopySync, c.extent.width, c.extent.height);
    } else {
        VkImageBlit b{};
        b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        b.dstSubresource = b.srcSubresource;
        b.srcOffsets[1] = {r.u1, r.v1, 1};
        b.dstOffsets[1] = {r.u1 * t.scale, r.v1 * t.scale, 1};
        vkCmdBlitImage(cmd, feedback_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.img.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
        CountCopy(kCopySync, static_cast<std::uint64_t>(r.u1) * t.scale, static_cast<std::uint64_t>(r.v1) * t.scale);
    }
    Transitions(cmd, {{&feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}, {&t.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}});
    // used_height stays the drawn height: it sizes the presented image (ADR-006 Â§4: 448 lines).
    t.last_write = newest->last_write;
    Mark(cmd, GpuSection::kSync);
}

void GsBackend::Shutdown() {
    if (!ctx_ || !ctx_->device) return;
    vkDeviceWaitIdle(ctx_->device);
    ShutdownAntiAlias();
    StopPipelineWarmUp();
    if (!pipelines_.empty()) {
        // One summary per session: the keys not in gs_pipeline_keys.h were built inside frames.
        std::vector<std::uint64_t> keys;
        for (const auto& [key, p] : pipelines_) keys.push_back(key);
        std::sort(keys.begin(), keys.end());
        std::sort(frame_pipeline_keys_.begin(), frame_pipeline_keys_.end());
        auto list = [](const std::vector<std::uint64_t>& v) {
            std::string out;
            char hex[24];
            for (std::uint64_t k : v) {
                std::snprintf(hex, sizeof(hex), "%s%llx", out.empty() ? "" : " ", static_cast<unsigned long long>(k));
                out += hex;
            }
            return out;
        };
        AC04_LOG_INFO("renderer", "GS pipelines: %zu in use; keys: %s", keys.size(), list(keys).c_str());
        if (!frame_pipeline_keys_.empty())
            AC04_LOG_INFO("renderer", "GS pipelines: %zu built during frames (%.1f ms in total, slowest %.1f ms): %s",
                          frame_pipeline_keys_.size(), frame_pipeline_ms_, frame_pipeline_ms_max_,
                          list(frame_pipeline_keys_).c_str());
    }
    for (auto& [key, p] : pipelines_) vkDestroyPipeline(ctx_->device, p, nullptr);
    pipelines_.clear();
    for (auto& [key, t] : targets_) {
        if (t.fb) vkDestroyFramebuffer(ctx_->device, t.fb, nullptr);
        DestroyImage(t.img);
        DestroyImage(t.depth);
    }
    targets_.clear();
    for (int f = 0; f < kFrames; ++f) {
        for (Image& img : retired_[f]) DestroyImage(img);
        for (VkFramebuffer fb : retired_fb_[f]) vkDestroyFramebuffer(ctx_->device, fb, nullptr);
        retired_[f].clear();
        retired_fb_[f].clear();
        if (conv_pool_[f]) vkDestroyDescriptorPool(ctx_->device, conv_pool_[f], nullptr);
        conv_pool_[f] = VK_NULL_HANDLE;
        DestroyBuffer(conv_clut_[f]);
    }
    DestroyBuffer(conv_tables_);
    if (depth_copy_pipeline_) vkDestroyPipeline(ctx_->device, depth_copy_pipeline_, nullptr);
    if (depth_copy_layout_) vkDestroyPipelineLayout(ctx_->device, depth_copy_layout_, nullptr);
    if (depth_copy_set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, depth_copy_set_layout_, nullptr);
    for (int f = 0; f < kFrames; ++f) {
        if (depth_copy_pool_[f]) vkDestroyDescriptorPool(ctx_->device, depth_copy_pool_[f], nullptr);
        depth_copy_pool_[f] = VK_NULL_HANDLE;
    }
    depth_copy_pipeline_ = VK_NULL_HANDLE;
    depth_copy_layout_ = VK_NULL_HANDLE;
    depth_copy_set_layout_ = VK_NULL_HANDLE;
    if (conv_pipeline_) vkDestroyPipeline(ctx_->device, conv_pipeline_, nullptr);
    if (conv_layout_) vkDestroyPipelineLayout(ctx_->device, conv_layout_, nullptr);
    if (conv_set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, conv_set_layout_, nullptr);
    conv_pipeline_ = VK_NULL_HANDLE;
    conv_layout_ = VK_NULL_HANDLE;
    conv_set_layout_ = VK_NULL_HANDLE;
    for (auto& [serial, t] : textures_) DestroyTexImage(t);
    textures_.clear();
    for (auto& [size, t] : free_textures_) DestroyTexImage(t);
    free_textures_.clear();
    free_texture_bytes_ = 0;
    DestroyTexImage(dummy_texture_);
    for (VkDescriptorPool p : tex_pools_) vkDestroyDescriptorPool(ctx_->device, p, nullptr);
    tex_pools_.clear();
    for (int f = 0; f < kFrames; ++f) {
        for (VkDescriptorPool p : decode_pools_[f]) vkDestroyDescriptorPool(ctx_->device, p, nullptr);
        decode_pools_[f].clear();
        DestroyBuffer(decode_palettes_[f]);
        DestroyBuffer(verify_buffer_[f]);
        verify_items_[f].clear();
    }
    DestroyBuffer(gs_memory_);
    if (decode_pipeline_) vkDestroyPipeline(ctx_->device, decode_pipeline_, nullptr);
    if (decode_layout_) vkDestroyPipelineLayout(ctx_->device, decode_layout_, nullptr);
    if (decode_set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, decode_set_layout_, nullptr);
    decode_pipeline_ = VK_NULL_HANDLE;
    decode_layout_ = VK_NULL_HANDLE;
    decode_set_layout_ = VK_NULL_HANDLE;
    DestroyImage(feedback_);
    DestroyImage(date_);
    DestroyImage(clut_img_);
    DestroyImage(present_);
    for (int i = 0; i < kFrames; ++i) {
        DestroyBuffer(staging_[i]);
        DestroyBuffer(vertices_[i]);
    }
    if (vs_) vkDestroyShaderModule(ctx_->device, vs_, nullptr);
    if (fs_) vkDestroyShaderModule(ctx_->device, fs_, nullptr);
    if (sampler_) vkDestroySampler(ctx_->device, sampler_, nullptr);
    if (linear_sampler_) vkDestroySampler(ctx_->device, linear_sampler_, nullptr);
    linear_sampler_ = VK_NULL_HANDLE;
    if (pool_) vkDestroyDescriptorPool(ctx_->device, pool_, nullptr);
    if (layout_) vkDestroyPipelineLayout(ctx_->device, layout_, nullptr);
    if (set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, set_layout_, nullptr);
    if (tex_set_layout_) vkDestroyDescriptorSetLayout(ctx_->device, tex_set_layout_, nullptr);
    tex_set_layout_ = VK_NULL_HANDLE;
    if (pass_) vkDestroyRenderPass(ctx_->device, pass_, nullptr);
    vs_ = fs_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    set_layout_ = VK_NULL_HANDLE;
    pass_ = VK_NULL_HANDLE;
    ctx_ = nullptr;
}

bool GsBackend::IsFrameBuffer(std::uint32_t tbp) const {
    for (const auto& [key, t] : targets_) {
        if (t.fbp * 32 == tbp) return true;
    }
    return false;
}

int GsBackend::TargetScale(int native_w) const {
    // Frame-sized buffers (512/640 wide: draw, display, sky work pages) at N; the small off-screen
    // targets of the shadow and glow chains (64-256 wide) at 1x, which keeps their blur kernels and
    // the PS2 look (renderer.md Â§1.4, fixes 5 and 6). Bounded by the device's image size
    // limit for the tallest buffer (kTargetHeight rows).
    if (native_w < 512) return 1;
    return std::max(1, std::min(scale_, max_image_dim_ / std::max(native_w, kTargetHeight)));
}

void GsBackend::Retire(int frame, Image& img) {
    if (img.image) retired_[frame].push_back(img);
    img = Image{};
}

bool GsBackend::CreateTargetImages(Target& t, int frame, VkCommandBuffer cmd) {
    const int w = t.native_w * t.scale, h = t.native_h * t.scale;
    if (!CreateImage(t.img, w, h,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)) {
        LogOnce("target", "cannot create a render target");
        return false;
    }
    if (!CreateImage(t.depth, w, h,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                         VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                     kDepthFormat)) {
        LogOnce("target", "cannot create a render target");
        Retire(frame, t.img);
        return false;
    }
    const VkImageView views[2] = {t.img.view, t.depth.view};
    VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = pass_;
    fci.attachmentCount = 2;
    fci.pAttachments = views;
    fci.width = static_cast<uint32_t>(w);
    fci.height = static_cast<uint32_t>(h);
    fci.layers = 1;
    vkCreateFramebuffer(ctx_->device, &fci, nullptr, &t.fb);
    VkClearColorValue zero{};
    VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    Transition(cmd, t.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdClearColorImage(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    VkClearDepthStencilValue zero_depth{};
    VkImageSubresourceRange drange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    Transition(cmd, t.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    vkCmdClearDepthStencilImage(cmd, t.depth.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero_depth, 1, &drange);
    // The clears complete before anything else writes the images (a grown or downscaled target's
    // copy, validation.md §2al).
    Transitions(cmd, {{&t.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, {&t.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}});
    return true;
}

GsBackend::Target* GsBackend::GetTarget(VkCommandBuffer cmd, int frame, std::uint32_t fbp, std::uint32_t fbw,
                                        std::uint32_t psm, int rows) {
    // Native rows: unscaled targets hold kTargetHeight rows as at 1x; scaled ones only the rows
    // drawn so far (at most kTargetHeight, as at 1x), rounded up to 64 (a 512x512 buffer at 8x is
    // already 64 MiB of colour), and grow when a draw needs more.
    rows = std::clamp(rows, 0, kTargetHeight);
    const std::uint64_t key = TargetKey(fbp, fbw, psm);
    auto it = targets_.find(key);
    if (it != targets_.end()) {
        Target& t = it->second;
        if (rows <= t.native_h || t.scale == 1) return &t;
        // Grow: a new pair of images with the old contents copied in.
        Target grown = t;
        grown.native_h = (rows + 63) & ~63;
        grown.img = Image{};
        grown.depth = Image{};
        grown.fb = VK_NULL_HANDLE;
        vkDeviceWaitIdle(ctx_->device);  // the other frame in flight may still read the old images
        if (!CreateTargetImages(grown, frame, cmd)) return &t;
        const VkExtent3D old_extent{static_cast<uint32_t>(t.img.width), static_cast<uint32_t>(t.img.height), 1};
        for (int pass = 0; pass < 2; ++pass) {
            Image& from = pass == 0 ? t.img : t.depth;
            Image& to = pass == 0 ? grown.img : grown.depth;
            VkImageCopy c{};
            c.srcSubresource = {from.aspect, 0, 0, 1};
            c.dstSubresource = c.srcSubresource;
            c.extent = old_extent;
            Transitions(cmd, {{&from, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, {&to, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
            vkCmdCopyImage(cmd, from.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, to.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
        }
        Transition(cmd, grown.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        Transition(cmd, grown.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        Retire(frame, t.img);
        Retire(frame, t.depth);
        retired_fb_[frame].push_back(t.fb);
        t = std::move(grown);
        return &t;
    }
    // A frame buffer drawn both in the 32-bit layout and in a 16-bit one (the flight's work page at
    // FBP 0x180: CT32, then CT16S over the same bytes, then CT32 again; the explosions sample it)
    // moves bits between pixels with each view change, which a scaled image cannot follow: all its
    // views are held at 1x (renderer.md §1, row 7; validation.md §2y).
    for (auto& [k, u] : targets_) {
        if (u.fbp != fbp || u.psm == LayoutClass(psm)) continue;
        if (native_fbps_.insert(fbp).second && (u.scale > 1 || TargetScale(std::max<int>(64, static_cast<int>(fbw) * 64)) > 1))
            AC04_LOG_INFO("renderer", "frame buffer FBP 0x%X drawn in 32- and 16-bit layouts: kept at 1x", fbp);
        break;
    }
    if (native_fbps_.count(fbp) != 0) {
        for (auto& [k, u] : targets_) {
            if (u.fbp == fbp && u.scale > 1) DownscaleTarget(cmd, frame, u);
        }
    }
    Target t;
    t.fbp = fbp;
    t.fbw = fbw;
    t.psm = LayoutClass(psm);
    t.native_w = std::max<int>(64, static_cast<int>(fbw) * 64);
    t.scale = native_fbps_.count(fbp) != 0 ? 1 : TargetScale(t.native_w);
    t.native_h = t.scale == 1 ? kTargetHeight : std::max(64, (rows + 63) & ~63);
    if (!CreateTargetImages(t, frame, cmd)) return nullptr;
    Transition(cmd, t.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Transition(cmd, t.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    return &targets_.emplace(key, std::move(t)).first->second;
}

void GsBackend::DownscaleTarget(VkCommandBuffer cmd, int frame, Target& t) {
    Target n = t;
    n.scale = 1;
    n.native_h = kTargetHeight;
    n.img = Image{};
    n.depth = Image{};
    n.fb = VK_NULL_HANDLE;
    if (!CreateTargetImages(n, frame, cmd)) return;
    // Colour only: the depth image starts cleared (these are 2D work buffers).
    VkImageBlit b{};
    b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    b.dstSubresource = b.srcSubresource;
    b.srcOffsets[1] = {t.img.width, t.img.height, 1};
    b.dstOffsets[1] = {t.native_w, t.native_h, 1};
    Transitions(cmd, {{&t.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL}, {&n.img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
    vkCmdBlitImage(cmd, t.img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, n.img.image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
    Transition(cmd, n.img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    Transition(cmd, n.depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    Retire(frame, t.img);
    Retire(frame, t.depth);
    retired_fb_[frame].push_back(t.fb);
    t = std::move(n);
}

bool GsBackend::EnsureScratch(int frame, VkCommandBuffer cmd, int w, int h) {
    // feedback_ (frame-buffer copies and native conversions), date_ (DATE region copies) and
    // present_ (the field copy's source) hold a whole target at its scale.
    w = std::max(w, kFeedbackWidth);
    h = std::max(h, kTargetHeight);
    if (feedback_.width >= w && feedback_.height >= h) return true;
    w = std::max(w, feedback_.width);
    h = std::max(h, feedback_.height);
    vkDeviceWaitIdle(ctx_->device);
    const VkImageUsageFlags tex_usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    Image* images[3] = {&feedback_, &date_, &present_};
    const VkImageUsageFlags usage[3] = {
        tex_usage | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, tex_usage,
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT};
    for (int i = 0; i < 3; ++i) {
        Retire(frame, *images[i]);
        if (!CreateImage(*images[i], w, h, usage[i])) {
            LogOnce("scratch", "cannot create the GS renderer images for this internal scale");
            return false;
        }
        VkClearColorValue zero{};
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        Transition(cmd, *images[i], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        vkCmdClearColorImage(cmd, images[i]->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
    }
    Transition(cmd, feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, date_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    Transition(cmd, present_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    present_valid_ = false;
    feedback_copy_ = FeedbackCopy{};
    return true;
}

void GsBackend::SetScale(int n) {
    n = std::clamp(n, 1, kMaxScale);
    if (n == scale_) return;
    scale_ = n;
    if (!ctx_ || !ctx_->device) return;
    // The targets are rebuilt at the new scale when next drawn; the game redraws every buffer each
    // frame, so only feedback effects show one frame without their history.
    vkDeviceWaitIdle(ctx_->device);
    for (auto& [key, t] : targets_) {
        if (t.fb) vkDestroyFramebuffer(ctx_->device, t.fb, nullptr);
        DestroyImage(t.img);
        DestroyImage(t.depth);
    }
    targets_.clear();
    present_valid_ = false;
    AC04_LOG_INFO("renderer", "internal scale %dx", scale_);
}

VkPipeline GsBackend::GetPipeline(std::uint64_t key) {
    {
        std::unique_lock<std::mutex> lock(pipelines_mutex_);
        // A key a warm-up thread is building now is waited for (cheaper than building it twice).
        pipelines_cv_.wait(lock, [&] { return warming_.count(key) == 0; });
        if (auto it = pipelines_.find(key); it != pipelines_.end()) return it->second;
    }
    const auto start = std::chrono::steady_clock::now();
    VkPipeline p = CreatePipeline(key);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (p == VK_NULL_HANDLE) LogOnce("pipeline", "cannot create a GS pipeline");
    std::lock_guard<std::mutex> lock(pipelines_mutex_);
    if (auto it = pipelines_.find(key); it != pipelines_.end()) {
        // Built by a warm-up thread meanwhile.
        if (p) vkDestroyPipeline(ctx_->device, p, nullptr);
        return it->second;
    }
    frame_pipeline_keys_.push_back(key);
    frame_pipeline_ms_ += ms;
    frame_pipeline_ms_max_ = std::max(frame_pipeline_ms_max_, ms);
    pipelines_[key] = p;
    return p;
}

VkPipeline GsBackend::CreatePipeline(std::uint64_t key) const {
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs_, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs_, "main", nullptr};
    VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va[4] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                               {1, 0, VK_FORMAT_R8G8B8A8_UINT, 12},
                                               {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 16},
                                               {3, 0, VK_FORMAT_R32_SFLOAT, 28}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = va;
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
    cba.blendEnable = (key & 1) ? VK_TRUE : VK_FALSE;
    cba.srcColorBlendFactor = static_cast<VkBlendFactor>((key >> 1) & 0x7F);
    cba.dstColorBlendFactor = static_cast<VkBlendFactor>((key >> 8) & 0x7F);
    cba.colorBlendOp = static_cast<VkBlendOp>((key >> 15) & 0x1F);
    // The GS writes As unblended; blended pipelines take A through the blend constant (SplitKey).
    cba.srcAlphaBlendFactor = cba.blendEnable ? VK_BLEND_FACTOR_CONSTANT_ALPHA : VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = cba.blendEnable ? VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA : VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
    cba.colorWriteMask = static_cast<VkColorComponentFlags>((key >> 20) & 0xF);
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkPipelineDepthStencilStateCreateInfo dss{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    dss.depthTestEnable = VK_TRUE;  // compare op and depth write: dynamic (SplitKey)
    VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP_EXT,
                            VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE_EXT, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = static_cast<uint32_t>(std::size(dyn));
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
    gpi.pDepthStencilState = &dss;
    gpi.pDynamicState = &ds;
    gpi.layout = layout_;
    gpi.renderPass = pass_;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(ctx_->device, ctx_->pipeline_cache, 1, &gpi, nullptr, &p) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return p;
}

void GsBackend::StartPipelineWarmUp() {
    // Up to 4 threads, leaving cores for the game and the main thread; the driver compiles each
    // pipeline on the calling thread.
    const unsigned cores = std::thread::hardware_concurrency();
    const int threads = static_cast<int>(std::clamp(cores / 2, 1u, 4u));
    warm_next_ = 0;
    warm_stop_ = false;
    warm_running_ = threads;
    warm_start_ = std::chrono::steady_clock::now();
    for (int i = 0; i < threads; ++i) {
        warm_threads_.emplace_back([this] {
            for (;;) {
                const std::size_t next = warm_next_++;
                if (warm_stop_ || next >= std::size(kGsPipelineKeys)) break;
                const std::uint64_t key = SplitKey(kGsPipelineKeys[next]).pipeline_key;
                {
                    std::lock_guard<std::mutex> lock(pipelines_mutex_);
                    if (pipelines_.count(key) != 0 || warming_.count(key) != 0) continue;
                    warming_.insert(key);
                }
                const VkPipeline p = CreatePipeline(key);
                {
                    // A failed key is left out: GetPipeline tries again and logs the failure.
                    std::lock_guard<std::mutex> lock(pipelines_mutex_);
                    if (p) pipelines_[key] = p;
                    warming_.erase(key);
                }
                pipelines_cv_.notify_all();
            }
            if (--warm_running_ == 0 && !warm_stop_) {
                const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - warm_start_).count();
                std::size_t built = 0;
                {
                    std::lock_guard<std::mutex> lock(pipelines_mutex_);
                    built = pipelines_.size();
                }
                AC04_LOG_INFO("renderer", "GS pipelines prepared at startup: %zu in %.0f ms", built, ms);
            }
        });
    }
}

void GsBackend::StopPipelineWarmUp() {
    warm_stop_ = true;
    for (std::thread& t : warm_threads_) t.join();
    warm_threads_.clear();
}

void GsBackend::Record(VkCommandBuffer cmd, int frame, const DrawList& list, const std::vector<TextureOp>& ops) {
    const std::vector<DrawCommand>& draws = list.draws;
    // Images replaced while this frame slot was last recorded: its commands have completed.
    for (Image& img : retired_[frame]) DestroyImage(img);
    for (VkFramebuffer fb : retired_fb_[frame]) vkDestroyFramebuffer(ctx_->device, fb, nullptr);
    retired_[frame].clear();
    retired_fb_[frame].clear();
    ++record_count_;
    pass_stats_ = PassStats{};
    feedback_copy_ = FeedbackCopy{};
    if (!scratch_) scratch_ = std::make_unique<RecordScratch>();
    // 1. Textures: the GPU copy of local memory brought up to date and the textures decoded from
    // it, in stream order (also without draws: the copy must follow every op), and the textures
    // decoded on the CPU uploaded.
    RecordTextureWork(cmd, frame, draws, ops);
    Mark(cmd, GpuSection::kTextureWork);
    if (draws.empty()) return;

    // 2. Vertices (sprites, lines and points become two triangles).
    auto& verts = scratch_->verts;
    auto& ranges = scratch_->ranges;  // first, count per draw
    auto& boxes = scratch_->boxes;    // x0, y0, x1, y1 of each draw's vertices
    verts.clear();
    ranges.clear();
    boxes.clear();
    // Room for the usual case (triangles keep their vertices; a sprite or line pair becomes 6).
    verts.reserve(list.vertices.size() * 3);
    for (const DrawCommand& d : draws) {
        const std::size_t first = verts.size();
        const VertexSpan dv = list.Vertices(d);
        // Sprites, lines and points take the Z of the vertex that gives their colour (`g`).
        auto vtx = [&](const GsVertex& g, float x, float y, float s, float t, float q) {
            verts.push_back(Vertex{x, y, DepthValue(g.z, d.zbuf), g.r, g.g, g.b, g.a, s, t, q, static_cast<float>(g.fog)});
        };
        auto tex = [&](const GsVertex& g, float& s, float& t, float& q) {
            if (d.fst) s = g.u, t = g.v, q = 1.0f;
            else s = g.s, t = g.t, q = g.q;
        };
        const std::size_t n = dv.size();
        if (d.kind == PrimKind::kTriangle) {
            for (const GsVertex& g : dv) {
                float s, t, q;
                tex(g, s, t, q);
                vtx(g, g.x, g.y, s, t, q);
            }
        } else if (d.kind == PrimKind::kSprite) {
            // At N > 1 the edges move to the borders of the native pixels the GS covers (pixel x
            // when x0 <= x < x1), texture coordinates along: sprites that meet between two native
            // sample points (the glow pass's 32-pixel strips end at 31.5, the next starts at 32)
            // leave no uncovered host pixels (renderer.md §1 fix 9). 1x is unchanged.
            const auto found = targets_.find(TargetKey(d.Fbp(), d.Fbw(), d.Fpsm()));
            const int ds = found != targets_.end() ? found->second.scale
                                                   : TargetScale(std::max<int>(64, static_cast<int>(d.Fbw()) * 64));
            for (std::size_t i = 0; i + 1 < n; i += 2) {
                const GsVertex& a = dv[i];
                const GsVertex& b = dv[i + 1];
                float s0, t0, q0, s1, t1, q1;
                tex(a, s0, t0, q0);
                tex(b, s1, t1, q1);
                float ax = a.x, bx = b.x, ay = a.y, by = b.y;
                if (ds > 1) {
                    auto snap = [](float& p0, float& p1, float& c0, float& c1) {
                        if (p0 == p1) return;
                        const float lo = std::min(p0, p1), hi = std::max(p0, p1);
                        const float nlo = std::ceil(lo) - 0.5f, nhi = std::ceil(hi) - 0.5f;
                        const float k = (c1 - c0) / (p1 - p0);
                        const float n0 = p0 < p1 ? nlo : nhi, n1 = p0 < p1 ? nhi : nlo;
                        const float e0 = c0 + (n0 - p0) * k, e1 = c0 + (n1 - p0) * k;
                        p0 = n0, p1 = n1, c0 = e0, c1 = e1;
                    };
                    snap(ax, bx, s0, s1);
                    snap(ay, by, t0, t1);
                }
                vtx(b, ax, ay, s0, t0, q1);
                vtx(b, bx, ay, s1, t0, q1);
                vtx(b, ax, by, s0, t1, q1);
                vtx(b, bx, ay, s1, t0, q1);
                vtx(b, bx, by, s1, t1, q1);
                vtx(b, ax, by, s0, t1, q1);
            }
        } else if (d.kind == PrimKind::kLine) {
            for (std::size_t i = 0; i + 1 < n; i += 2) {
                const GsVertex& a = dv[i];
                const GsVertex& b = dv[i + 1];
                float dx = b.x - a.x, dy = b.y - a.y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0.0f) continue;
                dx /= len;
                dy /= len;
                const float nx = -dy * 0.5f, ny = dx * 0.5f;
                float s0, t0, q0, s1, t1, q1;
                tex(a, s0, t0, q0);
                tex(b, s1, t1, q1);
                vtx(a, a.x + nx, a.y + ny, s0, t0, q0);
                vtx(b, b.x + nx, b.y + ny, s1, t1, q1);
                vtx(a, a.x - nx, a.y - ny, s0, t0, q0);
                vtx(b, b.x + nx, b.y + ny, s1, t1, q1);
                vtx(b, b.x - nx, b.y - ny, s1, t1, q1);
                vtx(a, a.x - nx, a.y - ny, s0, t0, q0);
            }
        } else {
            for (const GsVertex& g : dv) {
                float s, t, q;
                tex(g, s, t, q);
                vtx(g, g.x, g.y, s, t, q);
                vtx(g, g.x + 1, g.y, s, t, q);
                vtx(g, g.x, g.y + 1, s, t, q);
                vtx(g, g.x + 1, g.y, s, t, q);
                vtx(g, g.x + 1, g.y + 1, s, t, q);
                vtx(g, g.x, g.y + 1, s, t, q);
            }
        }
        ranges.emplace_back(first, verts.size() - first);
        std::array<float, 4> box{1e9f, 1e9f, -1e9f, -1e9f};
        for (std::size_t v = first; v < verts.size(); ++v) {
            box[0] = std::min(box[0], verts[v].x);
            box[1] = std::min(box[1], verts[v].y);
            box[2] = std::max(box[2], verts[v].x);
            box[3] = std::max(box[3], verts[v].y);
        }
        boxes.push_back(box);
    }
    if (verts.empty()) return;
    if (!EnsureBuffer(vertices_[frame], verts.size() * sizeof(Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)) return;
    std::memcpy(vertices_[frame].mapped, verts.data(), verts.size() * sizeof(Vertex));
    vmaFlushAllocation(ctx_->allocator, vertices_[frame].alloc, 0, VK_WHOLE_SIZE);

    // 3. Draws, grouped by render target (targets are created first: not inside a render pass).
    // Untextured sprites into a known Z buffer are depth fills (no colour target of their own).
    auto depth_fill = [&](const DrawCommand& d) {
        return d.kind == PrimKind::kSprite && !d.textured && zbuf_owner_.count(d.Fbp()) != 0;
    };
    // A sprite that reads a Z buffer as a texture and draws into another Z buffer (the half-size
    // copy of the scene's depth for the cloud pass of the no-save intro and mission 12): a depth copy
    // between the owners' depth images (gs_depth_copy.frag), not a colour draw.
    auto z_texture = [](const DrawCommand& d) { return d.textured && Bits(d.tex0, 20, 6) >= kPsmZ32; };
    auto depth_copy = [&](const DrawCommand& d) {
        const std::uint32_t tbp = static_cast<std::uint32_t>(Bits(d.tex0, 0, 14));
        return d.kind == PrimKind::kSprite && z_texture(d) && (tbp & 31) == 0 && zbuf_owner_.count(tbp >> 5) != 0 &&
               zbuf_owner_.count(d.Fbp()) != 0;
    };
    // Texels of draw i's texture that its pixels can read, [u0, u1) x [v0, v1): the box of its
    // vertices' texture coordinates with the bilinear margin, through the wrap mode (repeat: the whole
    // texture; region repeat: MAX up to MIN | MAX; clamp and region clamp: the box within the
    // texture or the region). Used to copy or convert only that part of a frame buffer read as a
    // texture (validation.md §2al).
    auto read_box = [&](std::size_t i, const DrawCommand& d) {
        const int size[2] = {1 << std::min<int>(static_cast<int>(Bits(d.tex0, 26, 4)), 10),
                             1 << std::min<int>(static_cast<int>(Bits(d.tex0, 30, 4)), 10)};
        float lo[2] = {1e9f, 1e9f}, hi[2] = {-1e9f, -1e9f};
        bool known = ranges[i].second > 0;
        for (std::size_t k = ranges[i].first; known && k < ranges[i].first + ranges[i].second; ++k) {
            const Vertex& v = verts[k];
            float c[2] = {v.s, v.t};
            if (!d.fst) {
                if (!(v.q > 0.0f)) {
                    known = false;  // the perspective divide can leave the vertices' range
                    break;
                }
                c[0] = v.s / v.q * static_cast<float>(size[0]);
                c[1] = v.t / v.q * static_cast<float>(size[1]);
            }
            for (int a = 0; a < 2; ++a) {
                lo[a] = std::min(lo[a], std::clamp(c[a], -1e6f, 1e6f));
                hi[a] = std::max(hi[a], std::clamp(c[a], -1e6f, 1e6f));
            }
        }
        std::array<int, 4> box{};
        for (int a = 0; a < 2; ++a) {
            const int mode = static_cast<int>(Bits(d.clamp, a == 0 ? 0 : 2, 2));
            const int mn = static_cast<int>(Bits(d.clamp, a == 0 ? 4 : 24, 10));
            const int mx = static_cast<int>(Bits(d.clamp, a == 0 ? 14 : 34, 10));
            int b0 = 0, b1 = size[a];
            if (mode == 3) {
                // (c & MIN) | MAX for any c: from MAX up to MIN | MAX.
                b0 = mx;
                b1 = (mn | mx) + 1;
            } else if (mode != 0) {
                const int c0 = mode == 2 ? mn : 0;
                const int c1 = mode == 2 ? std::max(mx + 1, mn + 1) : size[a];
                b0 = c0;
                b1 = c1;
                if (known) {
                    b0 = std::clamp(static_cast<int>(std::floor(lo[a])) - 1, c0, c1 - 1);
                    b1 = std::clamp(static_cast<int>(std::ceil(hi[a])) + 2, b0 + 1, c1);
                }
            }
            box[a] = b0;
            box[a + 2] = b1;
        }
        return box;
    };
    {
        // Native rows each target needs this frame (its scissors), then the scratch images for the
        // largest target and this frame's descriptor set.
        std::map<std::uint64_t, int> rows;
        // Z buffers tested in this frame are owned before any draw runs, so a depth copy into one
        // finds its owner in the first frame it is used (the cloud pass that tests it comes after).
        for (const DrawCommand& d : draws) {
            if (!d.aa_point && !depth_fill(d) && Bits(d.test, 16, 1) && Bits(d.test, 18, 1) &&
                zbuf_owner_.count(static_cast<std::uint32_t>(Bits(d.zbuf, 0, 9))) == 0)
                zbuf_owner_[static_cast<std::uint32_t>(Bits(d.zbuf, 0, 9))] = {TargetKey(d.Fbp(), d.Fbw(), d.Fpsm()), d.zbuf};
        }
        for (std::size_t i = 0; i < draws.size(); ++i) {
            const DrawCommand& d = draws[i];
            if (d.aa_point || depth_fill(d) || depth_copy(d) || ranges[i].second == 0) continue;
            const int bottom = std::min(static_cast<int>(Bits(d.scissor, 48, 11)) + 1,
                                        static_cast<int>(std::ceil(std::max(boxes[i][3], 0.0f))) + 2);
            int& r = rows[TargetKey(d.Fbp(), d.Fbw(), d.Fpsm())];
            r = std::max(r, std::min(bottom, kTargetHeight));
        }
        for (const DrawCommand& d : draws) {
            if (!d.aa_point && !depth_fill(d) && !depth_copy(d)) GetTarget(cmd, frame, d.Fbp(), d.Fbw(), d.Fpsm(), rows[TargetKey(d.Fbp(), d.Fbw(), d.Fpsm())]);
        }
        int sw = 0, sh = 0;
        for (const auto& [key, t] : targets_) {
            if (t.scale == 1) continue;  // 1x targets keep the 1x rules (copies clipped to the scratch size)
            sw = std::max(sw, t.img.width);
            sh = std::max(sh, t.img.height);
        }
        EnsureScratch(frame, cmd, sw, sh);
        const VkImageView movie_view = movie_.view ? movie_.view : dummy_texture_.img.view;
        if (clut_img_.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            VkClearColorValue zero{};
            VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            Transition(cmd, clut_img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            vkCmdClearColorImage(cmd, clut_img_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
            Transition(cmd, clut_img_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        VkDescriptorImageInfo infos[4] = {{sampler_, feedback_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                          {sampler_, date_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                          {linear_sampler_, movie_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                          {sampler_, clut_img_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
        VkWriteDescriptorSet w[4]{};
        for (int i = 0; i < 4; ++i) {
            w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w[i].dstSet = sets_[frame];
            w[i].dstBinding = static_cast<uint32_t>(i + 1);
            w[i].descriptorCount = 1;
            w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w[i].pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(ctx_->device, 4, w, 0, nullptr);
    }
    Target* open = nullptr;
    // Dynamic state already set in this render pass: draws that repeat it skip the call (the hangar
    // records ~4,000 draws per frame, and every call is costly under the validation layer).
    VkPipeline bound_pipeline = VK_NULL_HANDLE;
    VkRect2D bound_scissor{{-1, -1}, {0, 0}};
    Params bound_params{};
    bool params_valid = false;
    // Dynamic depth state and blend constant (SplitKey), set again after every pass break (other
    // pipelines may have been bound in between).
    bool dynamic_valid = false;
    VkCompareOp bound_depth_op = VK_COMPARE_OP_ALWAYS;
    bool bound_depth_write = false;
    float bound_alpha_constant = 1.0f;
    VkDescriptorSet bound_texture = VK_NULL_HANDLE;  // set 1
    // The pipeline of the last key: consecutive draws mostly share one, and GetPipeline locks and
    // looks it up in the map.
    std::uint64_t last_key = 0;
    VkPipeline last_pipeline = VK_NULL_HANDLE;
    auto pipeline = [&](std::uint64_t key) {
        if (last_pipeline == VK_NULL_HANDLE || key != last_key) {
            last_pipeline = GetPipeline(key);
            last_key = key;
        }
        return last_pipeline;
    };
    std::uint32_t pass_draws = 0;  // draw calls of the open pass (GPU profile)
    auto end_pass = [&](GpuSection section = GpuSection::kDraws) {
        if (open) {
            vkCmdEndRenderPass(cmd);
            Mark(cmd, section, open->fbp | open->fbw << 9 | open->psm << 15, pass_draws);
        }
        open = nullptr;
    };
    auto begin_pass = [&](Target* target) {
        if (open == target) return;
        end_pass();
        Transition(cmd, target->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rbi.renderPass = pass_;
        rbi.framebuffer = target->fb;
        rbi.renderArea.extent = {static_cast<uint32_t>(target->img.width), static_cast<uint32_t>(target->img.height)};
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        ++pass_stats_.render_passes;
        open = target;
        pass_draws = 0;
        bound_pipeline = VK_NULL_HANDLE;
        bound_scissor = VkRect2D{{-1, -1}, {0, 0}};
        params_valid = false;
        dynamic_valid = false;
        VkViewport vpt{0.0f, 0.0f, static_cast<float>(target->img.width), static_cast<float>(target->img.height), 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &vpt);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertices_[frame].buffer, &offset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame], 0, nullptr);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &dummy_texture_.set, 0, nullptr);
        bound_texture = dummy_texture_.set;
    };
    present_valid_ = false;
    conv_count_ = 0;
    vkResetDescriptorPool(ctx_->device, conv_pool_[frame], 0);
    vkResetDescriptorPool(ctx_->device, depth_copy_pool_[frame], 0);
    depth_copy_count_ = 0;
    for (std::size_t i = 0; i < draws.size(); ++i) {
        const DrawCommand& d = draws[i];
        if (d.aa_point) {
            // A flight view's 3D is complete: anti-alias its rectangle before the HUD is drawn.
            if (aa_mode_ != AntiAlias::kOff && aa_ready_) {
                auto it = targets_.find(TargetKey(d.Fbp(), d.Fbw(), d.Fpsm()));
                if (it != targets_.end()) {
                    end_pass();
                    ApplyAntiAlias(cmd, it->second, d);
                    Mark(cmd, GpuSection::kAntiAlias);
                }
            }
            continue;
        }
        if (ranges[i].second == 0) continue;
        if (depth_fill(d)) {
            // The sprites' colour (RGB = Z bits 0-23, A = bits 24-31) is the Z written into the
            // owner's depth image over their rectangles (clipped to the scissor).
            const auto& [owner_key, owner_zbuf] = zbuf_owner_[d.Fbp()];
            auto it = targets_.find(owner_key);
            if (it == targets_.end()) continue;
            Target* owner = &it->second;
            begin_pass(owner);
            const int x0 = static_cast<int>(Bits(d.scissor, 0, 11)), x1 = static_cast<int>(Bits(d.scissor, 16, 11)) + 1;
            const int y0 = static_cast<int>(Bits(d.scissor, 32, 11)), y1 = static_cast<int>(Bits(d.scissor, 48, 11)) + 1;
            const VertexSpan dv = list.Vertices(d);
            for (std::size_t v = 0; v + 1 < dv.size(); v += 2) {
                const GsVertex& a = dv[v];
                const GsVertex& b = dv[v + 1];
                const int rx0 = std::clamp(static_cast<int>(std::ceil(std::min(a.x, b.x))), std::max(x0, 0), std::min(x1, owner->native_w));
                const int rx1 = std::clamp(static_cast<int>(std::ceil(std::max(a.x, b.x))), std::max(x0, 0), std::min(x1, owner->native_w));
                const int ry0 = std::clamp(static_cast<int>(std::ceil(std::min(a.y, b.y))), std::max(y0, 0), std::min(y1, owner->native_h));
                const int ry1 = std::clamp(static_cast<int>(std::ceil(std::max(a.y, b.y))), std::max(y0, 0), std::min(y1, owner->native_h));
                if (rx1 <= rx0 || ry1 <= ry0) continue;
                const int sc = owner->scale;
                const std::uint32_t z = b.r | b.g << 8 | b.b << 16 | static_cast<std::uint32_t>(b.a) << 24;
                VkClearAttachment ca{VK_IMAGE_ASPECT_DEPTH_BIT, 0, {}};
                ca.clearValue.depthStencil.depth = DepthValue(z, owner_zbuf);
                VkClearRect cr{{{rx0 * sc, ry0 * sc}, {static_cast<uint32_t>((rx1 - rx0) * sc), static_cast<uint32_t>((ry1 - ry0) * sc)}}, 0, 1};
                vkCmdClearAttachments(cmd, 1, &ca, 1, &cr);
            }
            continue;
        }
        if (depth_copy(d)) {
            const std::uint32_t src_zbp = static_cast<std::uint32_t>(Bits(d.tex0, 0, 14)) >> 5;
            const auto& [src_key, src_zbuf] = zbuf_owner_[src_zbp];
            const auto& [dst_key, dst_zbuf] = zbuf_owner_[d.Fbp()];
            auto src_it = targets_.find(src_key);
            auto dst_it = targets_.find(dst_key);
            if (src_it == targets_.end() || dst_it == targets_.end() || src_it == dst_it ||
                depth_copy_count_ >= kMaxDepthCopies) {
                continue;
            }
            Target* src = &src_it->second;
            Target* dst = &dst_it->second;
            VkDescriptorSet set = VK_NULL_HANDLE;
            VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            dai.descriptorPool = depth_copy_pool_[frame];
            dai.descriptorSetCount = 1;
            dai.pSetLayouts = &depth_copy_set_layout_;
            if (vkAllocateDescriptorSets(ctx_->device, &dai, &set) != VK_SUCCESS) continue;
            ++depth_copy_count_;
            VkDescriptorImageInfo info{sampler_, src->depth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            w.pImageInfo = &info;
            vkUpdateDescriptorSets(ctx_->device, 1, &w, 0, nullptr);
            end_pass();
            Transition(cmd, src->depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
            begin_pass(dst);
            const int ts = dst->scale;
            const int x0 = static_cast<int>(Bits(d.scissor, 0, 11)), x1 = static_cast<int>(Bits(d.scissor, 16, 11)) + 1;
            const int y0 = static_cast<int>(Bits(d.scissor, 32, 11)), y1 = static_cast<int>(Bits(d.scissor, 48, 11)) + 1;
            VkRect2D sc{{std::max(x0, 0) * ts, std::max(y0, 0) * ts},
                        {static_cast<uint32_t>(std::max(0, std::min(x1, dst->native_w) - std::max(x0, 0)) * ts),
                         static_cast<uint32_t>(std::max(0, std::min(y1, dst->native_h) - std::max(y0, 0)) * ts)}};
            vkCmdSetScissor(cmd, 0, 1, &sc);
            // Z bits of each format (ZBUF.PSM 0: Z32, 1: Z24, else 16): the same Z value moves to
            // the other format's depth range.
            auto zbits = [](std::uint64_t zbuf) {
                const std::uint32_t psm = static_cast<std::uint32_t>((zbuf >> 24) & 0xF);
                return psm == 0 ? 32 : psm == 1 ? 24 : 16;
            };
            struct {
                float target_size[2];
                std::int32_t src_scale;
                float ratio;
                std::int32_t src_size[2];
            } pc{{static_cast<float>(dst->native_w), static_cast<float>(dst->native_h)},
                 src->scale,
                 std::ldexp(1.0f, zbits(src_zbuf) - zbits(dst_zbuf)),
                 {src->depth.width, src->depth.height}};
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_copy_pipeline_);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, depth_copy_layout_, 0, 1, &set, 0, nullptr);
            vkCmdPushConstants(cmd, depth_copy_layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(pc), &pc);
            vkCmdDraw(cmd, static_cast<uint32_t>(ranges[i].second), 1, static_cast<uint32_t>(ranges[i].first), 0);
            ++pass_draws;
            ++pass_stats_.draws;
            // The main pipeline layout is bound again by the next pass.
            end_pass(GpuSection::kDepthCopy);
            Transition(cmd, src->depth, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
            continue;
        }
        if (z_texture(d)) {
            LogOnce("z-texture", "Z buffer read as a texture outside a depth copy (not drawn)");
            continue;
        }
        Target* target = GetTarget(cmd, frame, d.Fbp(), d.Fbw(), d.Fpsm());
        if (!target) continue;
        // Bring the target up to date with a newer view of the same memory (another format or
        // width) before anything reads feedback_ for this draw.
        if (Target* newer = NewerView(*target)) {
            end_pass();
            Sync(cmd, frame, *target, *newer);
        }
        int tex_scale = 1;  // host texels per GS texel of what feedback_ holds for this draw
        if (d.texture_from_frame >= 0) {
            // Frame buffer read as a texture (field copy, heat haze, glow): the part the draw reads is
            // copied into feedback_ first, unless feedback_ still holds it from an earlier draw (same
            // image, not drawn into since). The first copy of the draw buffer in a frame is also the
            // image to present (ADR-006 §4), copied whole.
            // Several targets can share a block pointer (the menus' 640-wide and the flight's 512-wide
            // buffers both sit at FBP 0; the sky's work pages are drawn as CT32 and as CT16S): the one
            // drawn last holds the memory. Read in its own layout and width, it is copied as it is;
            // otherwise the texture is rebuilt from it through the GS memory layout (Convert).
            Target* src = nullptr;
            const std::uint32_t tbw = static_cast<std::uint32_t>(Bits(d.tex0, 14, 6));
            const std::uint32_t tpsm = static_cast<std::uint32_t>(Bits(d.tex0, 20, 6));
            for (auto& [key, t] : targets_) {
                if (t.fbp * 32 != static_cast<std::uint32_t>(d.texture_from_frame)) continue;
                if (src == nullptr || t.last_write > src->last_write ||
                    (t.last_write == src->last_write && t.fbw == tbw)) {
                    src = &t;
                }
            }
            const bool direct = src && src->psm == 0 && (tpsm == kPsmCt32 || tpsm == kPsmCt24) && src->fbw == tbw;
            if (src && !direct) {
                end_pass();
                Convert(cmd, frame, *src, d, read_box(i, d));
            } else if (src) {
                tex_scale = src->scale;
                // The copy keeps the source's scale: the draw samples it at tex_scale host texels per
                // GS texel (gs.frag flag 16), at the same host pixel position as in the source.
                const int s = src->scale;
                const int w = std::min(src->img.width, feedback_.width), h = std::min(src->img.height, feedback_.height);
                const std::array<int, 4> box = read_box(i, d);
                const int cx0 = std::clamp(box[0] * s, 0, w), cx1 = std::clamp(box[2] * s, cx0, w);
                const int cy0 = std::clamp(box[1] * s, 0, h), cy1 = std::clamp(box[3] * s, cy0, h);
                const FeedbackCopy& fc = feedback_copy_;
                const bool held = fc.image == src->img.image && fc.write == src->last_write && fc.x0 <= cx0 &&
                                  fc.y0 <= cy0 && cx1 <= fc.x1 && cy1 <= fc.y1;
                const bool copy = !held && cx1 > cx0 && cy1 > cy0;
                const bool field_copy = dispfb_ == 0 || static_cast<std::uint32_t>(dispfb_ & 0x1FF) == d.Fbp();
                const bool present = !present_valid_ && field_copy;
                if (copy || present) {
                    end_pass();
                    Transitions(cmd, {{&src->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                                      {&feedback_, copy ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : feedback_.layout},
                                      {&present_, present ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : present_.layout}});
                    if (copy) {
                        VkImageCopy c{};
                        c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        c.dstSubresource = c.srcSubresource;
                        c.srcOffset = {cx0, cy0, 0};
                        c.dstOffset = c.srcOffset;
                        c.extent = {static_cast<uint32_t>(cx1 - cx0), static_cast<uint32_t>(cy1 - cy0), 1};
                        vkCmdCopyImage(cmd, src->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, feedback_.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
                        CountCopy(kCopyFrameTexture, c.extent.width, c.extent.height);
                        feedback_copy_ = FeedbackCopy{src->img.image, src->last_write, cx0, cy0, cx1, cy1};
                    }
                    if (present) {
                        VkImageCopy c{};
                        c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                        c.dstSubresource = c.srcSubresource;
                        c.extent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
                        vkCmdCopyImage(cmd, src->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, present_.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
                        CountCopy(kCopyField, c.extent.width, c.extent.height);
                        present_valid_ = true;
                        present_fbp_ = src->fbp;
                        present_w_ = src->native_w;
                        present_h_ = std::max(src->used_height, 1);
                        present_src_scale_ = src->scale;
                        // Field copy (DrawDisplayTransfer): MODULATE by the vertex colour LerpRgba(0, 0x808080,
                        // brightness), so the brightness option scales the displayed image.
                        const VertexSpan dv = list.Vertices(d);
                        const bool modulate = d.textured && Bits(d.tex0, 35, 2) == 0 && !dv.empty();
                        present_scale_[0] = modulate ? dv[0].r / 128.0f : 1.0f;
                        present_scale_[1] = modulate ? dv[0].g / 128.0f : 1.0f;
                        present_scale_[2] = modulate ? dv[0].b / 128.0f : 1.0f;
                    }
                    // The source stays a transfer source until it is drawn into again (begin_pass); the
                    // draw's own target becomes an attachment in the same barrier.
                    Transitions(cmd, {{&feedback_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                      {&present_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                                      {&target->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}});
                    Mark(cmd, copy ? GpuSection::kFrameTexture : GpuSection::kField);
                }
            }
        }
        // Palette drawn on the GPU (gs_state.h DrawCommand::clut_from_frame): its 16x16 CT32 image
        // (blocks CBP..CBP+3, a 2x2 group of 8x8 blocks of the page) is copied from the target at
        // that page, brought up to date with its newest view, into clut_img_ (native pixels).
        bool frame_clut_ready = false;
        if (d.textured && d.clut_from_frame >= 0) {
            const std::uint32_t cbp = static_cast<std::uint32_t>(d.clut_from_frame);
            Target* src = nullptr;
            for (auto& [key, t] : targets_) {
                if (t.fbp == cbp >> 5 && t.psm == 0 && (!src || t.last_write > src->last_write)) src = &t;
            }
            int bx = -1, by = -1;
            for (std::uint32_t y = 0; y < 32 && bx < 0; y += 8) {
                for (std::uint32_t x = 0; x < 64; x += 8) {
                    if (GsMemory::PixelAddress(kPsmCt32, 0, 1, x, y) / 256 == (cbp & 31)) {
                        bx = static_cast<int>(x);
                        by = static_cast<int>(y);
                        break;
                    }
                }
            }
            if (src && bx >= 0 && bx + 16 <= src->native_w && by + 16 <= src->native_h) {
                end_pass();
                if (Target* newer = NewerView(*src)) Sync(cmd, frame, *src, *newer);
                const int s = src->scale;
                VkImageBlit b{};
                b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                b.dstSubresource = b.srcSubresource;
                b.srcOffsets[0] = {bx * s, by * s, 0};
                b.srcOffsets[1] = {(bx + 16) * s, (by + 16) * s, 1};
                b.dstOffsets[1] = {16, 16, 1};
                Transitions(cmd, {{&src->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                                  {&clut_img_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
                vkCmdBlitImage(cmd, src->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, clut_img_.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
                CountCopy(kCopyPalette, 16, 16);
                Transitions(cmd, {{&clut_img_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                  {&target->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}});
                Mark(cmd, GpuSection::kPalette);
                frame_clut_ready = true;
            } else {
                LogOnce("clut-frame", "palette drawn on the GPU without a 32-bit frame buffer at its CBP (drawn with indices)");
            }
        }
        begin_pass(target);
        target->last_write = ++write_clock_;
        // Only Z buffers that are really tested (GEQUAL/GREATER) are owned: menus keep ZTE with
        // ZTST ALWAYS and their ZBP can alias other buffers.
        if (Bits(d.test, 16, 1) && Bits(d.test, 18, 1))
            zbuf_owner_[static_cast<std::uint32_t>(Bits(d.zbuf, 0, 9))] = {TargetKey(d.Fbp(), d.Fbw(), d.Fpsm()), d.zbuf};
        // Scissor (inclusive) and the height of the target that has been drawn.
        const int x0 = static_cast<int>(Bits(d.scissor, 0, 11)), x1 = static_cast<int>(Bits(d.scissor, 16, 11));
        const int y0 = static_cast<int>(Bits(d.scissor, 32, 11)), y1 = static_cast<int>(Bits(d.scissor, 48, 11));
        const int sx0 = std::clamp(x0, 0, target->native_w), sx1 = std::clamp(x1 + 1, 0, target->native_w);
        const int sy0 = std::clamp(y0, 0, target->native_h), sy1 = std::clamp(y1 + 1, 0, target->native_h);
        if (sx1 <= sx0 || sy1 <= sy0) continue;
        target->used_height = std::max(target->used_height, sy1);
        const int ts = target->scale;
        // Destination alpha test (TEST.DATE): the pixels whose stored alpha bit 7 differs from DATM
        // are not drawn. The shader reads the target's alpha from a copy of the region this draw
        // covers, taken before the draw (the GS tests each pixel before the draw writes it; within
        // one draw, later primitives see the copy, not earlier primitives of the same draw).
        // Blend equations fixed-function blending cannot express (C = Ad other than Cs * Ad + Cd,
        // and D = the A operand: MapBlend) read Cd and Ad from the same copy and blend in the shader
        // (flag 28), with the same limit for primitives that overlap within one draw.
        const bool date = Bits(d.test, 14, 1) != 0;
        const bool blending = d.blend || d.aa1;
        const bool shader_blend = blending && !MapBlend(d.alpha).supported;
        if (date || shader_blend) {
            const int bx0 = std::clamp(static_cast<int>(std::floor(boxes[i][0])) - 1, sx0, sx1);
            const int by0 = std::clamp(static_cast<int>(std::floor(boxes[i][1])) - 1, sy0, sy1);
            const int bx1 = std::clamp(static_cast<int>(std::ceil(boxes[i][2])) + 2, sx0, std::min(sx1, date_.width / ts));
            const int by1 = std::clamp(static_cast<int>(std::ceil(boxes[i][3])) + 2, sy0, sy1);
            if (bx1 <= bx0 || by1 <= by0) continue;
            end_pass();
            VkImageCopy c{};
            c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.dstSubresource = c.srcSubresource;
            c.srcOffset = {bx0 * ts, by0 * ts, 0};
            c.dstOffset = c.srcOffset;
            c.extent = {static_cast<uint32_t>((bx1 - bx0) * ts), static_cast<uint32_t>((by1 - by0) * ts), 1};
            Transitions(cmd, {{&target->img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL},
                              {&date_, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL}});
            vkCmdCopyImage(cmd, target->img.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, date_.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
            CountCopy(kCopyDstRegion, c.extent.width, c.extent.height);
            Transitions(cmd, {{&date_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                              {&target->img, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}});
            Mark(cmd, GpuSection::kDstRegion);
            begin_pass(target);
        }
        VkRect2D sc{{sx0 * ts, sy0 * ts}, {static_cast<uint32_t>((sx1 - sx0) * ts), static_cast<uint32_t>((sy1 - sy0) * ts)}};
        if (std::memcmp(&sc, &bound_scissor, sizeof(sc)) != 0) {
            vkCmdSetScissor(cmd, 0, 1, &sc);
            bound_scissor = sc;
        }

        // Blend and write mask.
        BlendState blend;
        // PRIM.AA1 blends with the ALPHA register too, with the pixel's coverage as As: the inside
        // of a primitive has full coverage (0x80). The partial coverage of edge pixels is not
        // modelled (HUD lines: Cs + Cd).
        if (blending) {
            blend = MapBlend(d.alpha);
            if (!blend.supported) blend = BlendState{};  // the shader blends (flag 28): written as is
            if (d.pabe) LogOnce("pabe", "PABE not implemented");
            if (!d.colclamp) LogOnce("colclamp", "COLCLAMP 0 (wrap) not implemented");
        }
        const std::uint32_t m = d.Fbmsk();
        std::uint32_t write = 0;
        for (int c = 0; c < 4; ++c) {
            const std::uint32_t bits = (m >> (c * 8)) & 0xFF;
            if (bits != 0xFF) write |= 1u << c;
            if (bits != 0 && bits != 0xFF) LogOnce("fbmsk", "partial FBMSK channel mask not implemented");
        }
        Params p{};
        VkDescriptorSet texture_set = VK_NULL_HANDLE;  // the draw's texture image (set 1), when it has one
        // Native size: gs.vert maps GS pixels onto the whole (scaled) image.
        p.target_size[0] = static_cast<float>(target->native_w);
        p.target_size[1] = static_cast<float>(target->native_h);
        p.scale = ts;
        p.tex_scale = tex_scale;
        std::int32_t flags = 0;
        if (d.textured) {
            flags |= 1;
            if (d.fst) flags |= 2;
            flags |= static_cast<std::int32_t>(Bits(d.tex0, 35, 2)) << 2;
            if (Bits(d.tex0, 34, 1)) flags |= 1 << 4;
            if (Bits(d.tex1, 5, 1)) flags |= 1 << 5;  // MMAG linear
            flags |= static_cast<std::int32_t>(Bits(d.clamp, 0, 2)) << 12;
            flags |= static_cast<std::int32_t>(Bits(d.clamp, 2, 2)) << 14;
            p.tex_size[0] = 1 << std::min<int>(static_cast<int>(Bits(d.tex0, 26, 4)), 10);
            p.tex_size[1] = 1 << std::min<int>(static_cast<int>(Bits(d.tex0, 30, 4)), 10);
            p.region[0] = static_cast<std::int32_t>(Bits(d.clamp, 4, 10));
            p.region[1] = static_cast<std::int32_t>(Bits(d.clamp, 14, 10));
            p.region[2] = static_cast<std::int32_t>(Bits(d.clamp, 24, 10));
            p.region[3] = static_cast<std::int32_t>(Bits(d.clamp, 34, 10));
            const std::uint32_t levels = d.texture ? d.texture->levels : 0;
            p.tex1 = static_cast<std::int32_t>(Bits(d.tex1, 6, 3) | std::min<std::uint32_t>(levels, 7) << 3 |
                                               Bits(d.tex1, 0, 1) << 6 | Bits(d.tex1, 19, 2) << 7 |
                                               Bits(d.tex1, 32, 12) << 9 | Bits(d.tex1, 5, 1) << 21);
            if (frame_clut_ready) flags |= 1 << 29;  // palette indices, looked up in clut_img_
            if (d.texture_from_frame >= 0) {
                flags |= 1 << 16;
            } else if (d.texture) {
                auto it = textures_.find(d.texture->serial);
                if (it == textures_.end()) continue;  // no image (RecordTextureWork logged why)
                texture_set = it->second.set;
                p.rect[2] = static_cast<std::int32_t>(d.texture->base_width);
                p.rect[3] = static_cast<std::int32_t>(d.texture->base_height);
                p.origin[0] = static_cast<std::int32_t>(d.texture->origin_u);
                p.origin[1] = static_cast<std::int32_t>(d.texture->origin_v);
                int mx = 0, my = 0;
                if (ts > 1 && movie_.view && d.texture_guest_source != 0 &&
                    MovieTexelOrigin(d.texture_guest_source, d.texture_guest_width, movie_.guest_addr,
                                     movie_.guest_bytes, movie_.height, mx, my)) {
                    // Movie column (ADR-006 Â§5): sampled from the upscaled picture at the picture
                    // pixel its texel maps to (flag 27; rect = column origin and picture size).
                    flags |= 1 << 27;
                    p.rect[0] = mx;
                    p.rect[1] = my;
                    p.rect[2] = movie_.width;
                    p.rect[3] = movie_.height;
                } else if (ts > 1 && d.kind == PrimKind::kSprite) {
                    // 2D sprites sample at the UV of the native pixel each host pixel lies in, so
                    // texel-exact HUD, text and atlas art keep their native look without seams
                    // between atlas cells (renderer.md Â§1.4 fix 1).
                    flags |= 1 << 26;
                }
            } else {
                continue;
            }
        }
        // Alpha test with its fail mode (TEST.AFAIL): 0 KEEP and 2 ZB_ONLY write no colour (the depth
        // write of ZB_ONLY is not done), 1 FB_ONLY writes the colour, 3 RGB_ONLY writes RGB but not
        // A: a second draw of the failing pixels with alpha masked.
        std::uint32_t afail = 0;
        if (Bits(d.test, 0, 1)) {
            flags |= 1 << 6;
            flags |= static_cast<std::int32_t>(Bits(d.test, 1, 3)) << 7;
            p.aref = static_cast<std::int32_t>(Bits(d.test, 4, 8));
            afail = static_cast<std::uint32_t>(Bits(d.test, 12, 2));
            if (afail == 1) flags &= ~(1 << 6);
        }
        if (Bits(d.alpha, 4, 2) == 2) flags |= 1 << 17;
        if (blending && blend.src_over_128) flags |= 1 << 18;
        if (d.aa1 && !d.blend) flags |= 1 << 25;  // AA1 alone: As = coverage = 1.0
        if (target->psm != 0) flags |= 1 << 24;  // 16-bit frame buffer: 5-bit colour, 1-bit alpha
        if (date) {
            flags |= 1 << 22;
            if (Bits(d.test, 15, 1)) flags |= 1 << 23;
        }
        if (shader_blend) {
            flags |= 1 << 28;
            p.alpha = static_cast<std::int32_t>(Bits(d.alpha, 0, 8));
        }
        if (d.fog) {
            flags |= 1 << 20;
            p.fogcol = static_cast<std::int32_t>(d.fogcol & 0xFFFFFF);
        }
        p.fix = static_cast<float>(Bits(d.alpha, 32, 8));
        p.flags = flags;
        const std::uint64_t depth = DepthKey(d.test, d.zbuf);
        auto bind = [&](std::uint64_t full_key) {
            const DrawState st = SplitKey(full_key);
            const VkPipeline pl = pipeline(st.pipeline_key);
            if (!dynamic_valid || st.depth_op != bound_depth_op) {
                vkCmdSetDepthCompareOpEXT(cmd, st.depth_op);
                bound_depth_op = st.depth_op;
                ++pass_stats_.depth_state_sets;
            }
            if (!dynamic_valid || st.depth_write != bound_depth_write) {
                vkCmdSetDepthWriteEnableEXT(cmd, st.depth_write ? VK_TRUE : VK_FALSE);
                bound_depth_write = st.depth_write;
                ++pass_stats_.depth_state_sets;
            }
            if (!dynamic_valid || st.alpha_constant != bound_alpha_constant) {
                const float c[4] = {0.0f, 0.0f, 0.0f, st.alpha_constant};
                vkCmdSetBlendConstants(cmd, c);
                bound_alpha_constant = st.alpha_constant;
            }
            dynamic_valid = true;
            if (pl != bound_pipeline) {
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl);
                bound_pipeline = pl;
                ++pass_stats_.pipeline_binds;
            }
            if (!params_valid || std::memcmp(&p, &bound_params, sizeof(p)) != 0) {
                vkCmdPushConstants(cmd, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(p), &p);
                ++pass_stats_.push_constants;
                bound_params = p;
                params_valid = true;
            }
        };
        if (texture_set && texture_set != bound_texture) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &texture_set, 0, nullptr);
            bound_texture = texture_set;
            ++pass_stats_.texture_binds;
        }
        bind(PipelineKey(blend, write) | depth);
        vkCmdDraw(cmd, static_cast<uint32_t>(ranges[i].second), 1, static_cast<uint32_t>(ranges[i].first), 0);
        ++pass_draws;
        ++pass_stats_.draws;
        if ((flags & (1 << 6)) != 0 && afail == 3 && (write & 7) != 0) {
            p.flags = flags | 1 << 19;  // keep only the pixels that fail the test
            bind(PipelineKey(blend, write & 7) | depth);
            vkCmdDraw(cmd, static_cast<uint32_t>(ranges[i].second), 1, static_cast<uint32_t>(ranges[i].first), 0);
            ++pass_draws;
            ++pass_stats_.draws;
        }
    }
    end_pass();
}

const GsBackend::Target* GsBackend::DisplayTarget() const {
    // No field copy this frame: the frame buffer at DISPFB's FBP with DISPFB's width (menus FBW 10
    // and flight FBW 8 both draw at block 0); before any DISPFB, or when none matches, the first
    // frame buffer at block 0.
    const std::uint32_t fbp = static_cast<std::uint32_t>(dispfb_ & 0x1FF);
    const std::uint32_t fbw = static_cast<std::uint32_t>((dispfb_ >> 9) & 0x3F);
    const Target* first = nullptr;
    for (const auto& [key, t] : targets_) {
        if (dispfb_ != 0 && t.fbp == fbp && t.fbw == fbw && t.psm == 0) return &t;
        if (!first && t.fbp == 0 && t.psm == 0) first = &t;
    }
    return first;
}

void GsBackend::PresentSize(int& w, int& h) const {
    w = h = 0;
    if (present_valid_) {
        w = present_w_;
        h = present_h_;
    } else if (const Target* t = DisplayTarget()) {
        w = t->native_w;
        h = std::max(t->used_height, 1);
    }
    // The NTSC display shows at most 448 lines (ADR-006 Â§4: the 448-line draw buffer); the flight
    // scissor also covers line 448, which is never displayed. (DISPLAY2.DH cannot bound it: the menus
    // set a 224-line field display and still draw and present 448 lines.)
    h = std::min(h, 448);
}

void GsBackend::CopyPresent(VkCommandBuffer cmd, VkImage dst, int dst_w, int dst_h) {
    int w = 0, h = 0;
    PresentSize(w, h);
    if (w == 0 || h == 0) return;
    Image* src = present_valid_ ? &present_ : nullptr;
    int src_scale = present_src_scale_;
    if (!src) {
        if (const Target* t = DisplayTarget()) {
            src = const_cast<Image*>(&t->img);
            src_scale = t->scale;
        }
    }
    if (!src) return;
    const VkImageLayout restore = src->layout;
    Transition(cmd, *src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = dst;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    Barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, nullptr, 1, &b);
    if (src_scale == scale_) {
        VkImageCopy c{};
        c.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.dstSubresource = c.srcSubresource;
        c.extent = {static_cast<uint32_t>(std::min(w * src_scale, dst_w)), static_cast<uint32_t>(std::min(h * src_scale, dst_h)), 1};
        vkCmdCopyImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    } else {
        // A buffer kept at another scale (device size limit): resampled to the native size x N.
        VkImageBlit bl{};
        bl.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        bl.dstSubresource = bl.srcSubresource;
        bl.srcOffsets[1] = {w * src_scale, h * src_scale, 1};
        bl.dstOffsets[1] = {std::min(w * scale_, dst_w), std::min(h * scale_, dst_h), 1};
        vkCmdBlitImage(cmd, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bl,
                       VK_FILTER_LINEAR);
    }
    b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    Barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, nullptr, 1, &b);
    Transition(cmd, *src, restore);
}

}  // namespace ac04::render::gs
