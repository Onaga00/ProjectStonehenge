// GS local memory model (ADR-003, docs/platform/gs-subset.md): 4 MiB addressed in 256-byte blocks
// (block pointers TBP/CBP/DBP) and 8 KiB pages, with the page and block layout of each pixel format
// the game uses. Host -> local IMAGE transfers write here; textures and CLUTs are decoded from here.
//
// Layout per format (GS User's Manual, "Local Memory"):
//   PSMCT32 page 64x32, block 8x8;  PSMCT16/16S page 64x64, block 16x8;
//   PSMT8   page 128x64, block 16x16; PSMT4 page 128x128, block 32x16.
// Blocks are placed inside a page by the per-format block tables and the pixels inside a block in
// the GS column order, so the memory image is byte for byte the GS's (and PCSX2's VRAM image,
// checked with tools/research/gsvram.py) and data written in one format and read in another
// (PSMT8H over CT32, CLUTs, frame buffers as textures) lands where the GS would put it.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ac04::render::gs {

enum Psm : std::uint32_t {
    kPsmCt32 = 0x00,
    kPsmCt24 = 0x01,
    kPsmCt16 = 0x02,
    kPsmCt16S = 0x0A,
    kPsmT8 = 0x13,
    kPsmT4 = 0x14,
    kPsmT8H = 0x1B,
    kPsmT4HL = 0x24,
    kPsmT4HH = 0x2C,
    kPsmZ32 = 0x30,
    kPsmZ24 = 0x31,
    kPsmZ16 = 0x32,
    kPsmZ16S = 0x3A,
};

// Bits per pixel of a format (0 = unsupported).
int BitsPerPixel(std::uint32_t psm);

// Size in pixels of one 256-byte block of a format: 8 x 8 for the 32-bit layouts (T8H, T4HL and
// T4HH live in the top byte of 32-bit pixels), 16 x 8 for 16-bit, 16 x 16 for T8, 32 x 16 for T4.
constexpr std::uint32_t BlockWidth(std::uint32_t psm) {
    return psm == kPsmT4 ? 32 : (psm == kPsmT8 || psm == kPsmCt16 || psm == kPsmCt16S || psm == kPsmZ16 || psm == kPsmZ16S) ? 16 : 8;
}
constexpr std::uint32_t BlockHeight(std::uint32_t psm) { return psm == kPsmT8 || psm == kPsmT4 ? 16 : 8; }

class GsMemory {
public:
    static constexpr std::uint32_t kSize = 4u * 1024u * 1024u;

    GsMemory() : mem_(kSize, 0) {}

    // Byte address of pixel (x, y) of a buffer at block pointer `bp` (256-byte units) with width
    // `bw` (64-pixel units). For 4-bit formats `nibble` receives 0 (low) or 1 (high).
    static std::uint32_t PixelAddress(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x,
                                      std::uint32_t y, std::uint32_t* nibble = nullptr);

    std::uint32_t ReadPixel(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x,
                            std::uint32_t y) const;
    // The raw values of the w x h pixels at (x0, y0), row by row (each as ReadPixel gives it).
    void ReadRect(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x0, std::uint32_t y0,
                  std::uint32_t w, std::uint32_t h, std::uint32_t* out) const;
    void WritePixel(std::uint32_t psm, std::uint32_t bp, std::uint32_t bw, std::uint32_t x, std::uint32_t y,
                    std::uint32_t value);

    // Writes `bytes` of transfer data into the rectangle (dsax, dsay, rrw, rrh) of the buffer
    // (dbp, dbw, dpsm), continuing at pixel `pos` of the rectangle (transfers may be split over
    // several IMAGE packets). Returns the new position.
    std::uint32_t WriteImage(std::uint32_t dbp, std::uint32_t dbw, std::uint32_t dpsm, std::uint32_t dsax,
                             std::uint32_t dsay, std::uint32_t rrw, std::uint32_t rrh, std::uint32_t pos,
                             const std::uint8_t* data, std::uint32_t bytes);

    // Change counter per 256-byte block, the texel block of every format (8 x 8 32-bit, 16 x 8
    // 16-bit, 16 x 16 8-bit, 32 x 16 4-bit pixels), incremented when a write changes its bytes: a
    // decoded texture stays valid while the blocks it read keep their generations
    // (GsState::DecodeTexture), and the CPU decoder re-decodes only the blocks that changed.
    std::uint32_t BlockGeneration(std::uint32_t addr) const { return block_gen_[(addr >> 8) & (kBlocks - 1)]; }
    // Number of block changes so far: a texture validated at this count is still valid.
    std::uint64_t WriteCount() const { return write_count_; }

    // Blocks changed since the last TakeDirtyBlocks (block numbers, addr >> 8): what the GPU copy of
    // local memory has not received yet (docs/port/renderer.md §2.3).
    bool IsDirty(std::uint32_t addr) const { return dirty_[(addr >> 8) & (kBlocks - 1)] != 0; }
    bool AnyDirty() const { return !dirty_list_.empty(); }
    // The changed blocks in ascending order, and forgets them.
    std::vector<std::uint32_t> TakeDirtyBlocks();

    const std::uint8_t* Data() const { return mem_.data(); }
    // Replaces the whole memory with a 4 MiB GS memory image (--play-gsdump: a PCSX2 VRAM image has
    // the same layout); every block counts as changed.
    void LoadImage(const std::uint8_t* image) {
        std::copy(image, image + kSize, mem_.begin());
        for (std::uint32_t b = 0; b < kBlocks; ++b) Touch(b << 8);
    }

private:
    static constexpr std::uint32_t kBlocks = 16384;  // 4 MiB / 256
    void Touch(std::uint32_t addr) {
        const std::uint32_t block = (addr >> 8) & (kBlocks - 1);
        ++block_gen_[block];
        ++write_count_;
        if (!dirty_[block]) {
            dirty_[block] = 1;
            dirty_list_.push_back(block);
        }
    }

    std::vector<std::uint8_t> mem_;
    std::vector<std::uint32_t> block_gen_ = std::vector<std::uint32_t>(kBlocks, 0);
    std::uint64_t write_count_ = 0;
    std::vector<std::uint8_t> dirty_ = std::vector<std::uint8_t>(kBlocks, 0);
    std::vector<std::uint32_t> dirty_list_;
    // WriteImage: per packet place (BITBLTBUF base/width/format, TRXPOS, TRXREG, start pixel, size)
    // the last data written there and the generation each block it wrote had afterwards.
    struct UploadKey {
        std::uint32_t dbp, dbw, dpsm, dsax, dsay, rrw, rrh, pos, bytes;
        bool operator==(const UploadKey& o) const {
            return dbp == o.dbp && dbw == o.dbw && dpsm == o.dpsm && dsax == o.dsax && dsay == o.dsay && rrw == o.rrw &&
                   rrh == o.rrh && pos == o.pos && bytes == o.bytes;
        }
    };
    struct UploadKeyHash {
        std::size_t operator()(const UploadKey& k) const {
            std::uint64_t h = 0xCBF29CE484222325ull;
            for (std::uint32_t v : {k.dbp, k.dbw, k.dpsm, k.dsax, k.dsay, k.rrw, k.rrh, k.pos, k.bytes})
                h = (h ^ v) * 0x100000001B3ull;
            return static_cast<std::size_t>(h);
        }
    };
    struct LastUpload {
        std::vector<std::uint8_t> data;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> blocks;  // block number, generation
        // Written by the whole-block path: the 256 bytes it stored in each block (same order as
        // `blocks`), so the same bytes sent again after other uploads overwrote those blocks are
        // stored without assembling the blocks again. Empty for the other paths.
        std::vector<std::uint8_t> block_bytes;
    };
    std::unordered_map<UploadKey, LastUpload, UploadKeyHash> uploads_;
};

}  // namespace ac04::render::gs
