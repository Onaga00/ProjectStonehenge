// libmpeg (SDK 2.3) on libmpeg2 0.5.1: the MPEG-2 video decoding the movie player uses
// (docs/formats/SMV.md, slice S3). On the PS2 libmpeg drives the IPU; here libmpeg2 decodes the
// elementary stream and IpuCsc() does the IPU's colour conversion.
//
// Game use (SMV reader 0x00212630-0x00212960, draw 0x00213048): one sceMpegCreate per movie; per
// SMV chunk sceMpegAddBs with the chunk's video (each chunk is a complete sequence ending with a
// sequence end code); one sceMpegGetPicture per movie iteration into a 0x460-macroblock RGBA32
// buffer; when sceMpegIsEnd reports the sequence end, sceMpegReset and the next chunk.
//
// Guest-visible state: the sceMpeg struct (+0 width, +4 height, +8 frameCount, +0x10..+0x3F pts/
// dts/flags pairs, +0x40 sys = the aligned work area) and the first word of sys (end flag, read by
// sceMpegIsEnd 0x00270CA8). The rest of libmpeg's work area and its globals (0x003FCF5C..) are not
// reproduced (expected RAM differences, category sdk).
#include <algorithm>
#include <cstring>
#include <inttypes.h>
#include <map>
#include <vector>

extern "C" {
#include "mpeg2.h"
}

#include "common/guest_memory.h"
#include "common/guest_ref.h"
#include "platform/log.h"
#include "sce/sce.h"
#include "sce/sce_host.h"

using ac04::guest::GRef;

namespace {

struct Decoder {
    mpeg2dec_t* dec = nullptr;
    std::vector<u8> bs;  // copy of the bitstream added by sceMpegAddBs (the chunk buffer is reused)
    bool fed = false;
    bool end = false;
};

std::map<u32, Decoder>& Decoders() {
    static std::map<u32, Decoder> d;
    return d;
}

u32 Sys(u32 mp) { return GRef<u32>(mp + 0x40); }

void SetEnd(u32 mp, Decoder& d, bool end) {
    d.end = end;
    const u32 sys = Sys(mp);
    if (sys != 0) GRef<u32>(sys) = end ? 1u : 0u;
}

}  // namespace

namespace ac04::sce {

// IPU colour space conversion, the integer method of the hardware as PCSX2 2.8.2 implements it
// (IPU yuv2rgb reference: coefficients in 1/128, BT.601 ranges). Alpha: 0x80 on every pixel
// (libmpeg leaves the IPU alpha thresholds TH0/TH1 at 0; confirmed in the image buffer of the S3
// PCSX2 state s3_ss01_mid, port/validation.md S3-3).
void IpuCsc(u8 y, u8 cb, u8 cr, u8* rgba) {
    const s32 lum = (0x95 * std::max(0, static_cast<s32>(y) - 16)) >> 6;
    const s32 c_r = static_cast<s32>(cr) - 128;
    const s32 c_b = static_cast<s32>(cb) - 128;
    const s32 rcr = (0xCC * c_r) >> 6;
    const s32 gcr = (-0x68 * c_r) >> 6;
    const s32 gcb = (-0x32 * c_b) >> 6;
    const s32 bcb = (0x102 * c_b) >> 6;
    rgba[0] = static_cast<u8>(std::clamp((lum + rcr + 1) >> 1, 0, 255));
    rgba[1] = static_cast<u8>(std::clamp((lum + gcr + gcb + 1) >> 1, 0, 255));
    rgba[2] = static_cast<u8>(std::clamp((lum + bcb + 1) >> 1, 0, 255));
    rgba[3] = 0x80;
}

}  // namespace ac04::sce

// PS2 (0x00270870): resets the IPU and its DMA channels. Nothing to do on the PC.
void sceMpegInit() {}

// PS2 (sceMpegCreate): carves its state out of the work area (aligned to 4), clears the sceMpeg
// struct and resets the decoder. Returns `mp`.
u32 sceMpegCreate(u32 mp, u32 work, s32 size) {
    const u32 sys = (work + 3) & ~3u;
    if (static_cast<u32>(size) - (sys - work) < 0x118) {
        ac04::log::WriteOnce("mpeg:work", ac04::log::Level::Error, "mpeg", "sceMpegCreate: work area too small");
        return 0;
    }
    GRef<u32>(mp + 0x40) = sys;
    GRef<s32>(mp + 0) = 0;
    GRef<s32>(mp + 4) = 0;
    GRef<s32>(mp + 8) = 0;
    GRef<u64>(mp + 0x10) = ~0ull;
    GRef<u64>(mp + 0x18) = ~0ull;
    GRef<u64>(mp + 0x20) = 0;
    GRef<u64>(mp + 0x28) = ~0ull;
    GRef<u64>(mp + 0x30) = ~0ull;
    GRef<u64>(mp + 0x38) = 0;
    // The game never calls sceMpegDelete, and `mp` lies in a work-heap block whose address depends
    // on the heap state at movie start. One decoder runs at a time (one SMV reader, one IPU), so
    // the decoders of earlier movies are released here (host memory only; guest RAM unchanged).
    for (auto& [addr, old] : Decoders()) {
        if (old.dec != nullptr) mpeg2_close(old.dec);
    }
    Decoders().clear();
    Decoder& d = Decoders()[mp];
    mpeg2_accel(0);  // portable C paths only
    d.dec = mpeg2_init();
    d.bs.clear();
    d.fed = false;
    SetEnd(mp, d, false);
    return mp;
}

// PS2 (0x00270CC8): restarts the decoder for a new bitstream: end flag and frameCount cleared.
// The reference pictures are kept: the SMV chunks are one stream cut into sequences, and most
// chunks begin with an open GOP whose leading B-pictures predict from the previous chunk's last
// reference picture (SS01: 1-2 per chunk from chunk 4 on). A full libmpeg2 reset loses that
// reference and those pictures decode as garbage (flicker with broken colours at chunk changes).
// After a partial reset libmpeg2 does not offer the previous chunk's last picture again (checked:
// SS01 returns exactly the file's picture count per chunk).
void sceMpegReset(u32 mp) {
    Decoder& d = Decoders()[mp];
    if (d.dec != nullptr) mpeg2_reset(d.dec, 0);
    d.bs.clear();
    d.fed = false;
    GRef<s32>(mp + 8) = 0;
    SetEnd(mp, d, false);
}

// PS2 (0x00270B58): adds `size` bytes of elementary stream at `bs` (the SDK keeps the address and
// a 16-byte rounded size and DMAs it to the IPU while decoding). The port copies it, because the
// game reuses the chunk buffer for the read-ahead only after sceMpegIsEnd.
void sceMpegAddBs(u32 mp, u32 bs, s32 size) {
    Decoder& d = Decoders()[mp];
    const u8* p = size > 0 ? ac04::guest::Mem().Translate(bs, static_cast<u32>(size)) : nullptr;
    d.bs.assign(p, p == nullptr ? p : p + size);
    d.fed = false;
}

// PS2 (0x00270CA8): 1 once the sequence end code has been decoded.
s32 sceMpegIsEnd(u32 mp) { return Sys(mp) != 0 ? GRef<s32>(Sys(mp)) : 0; }

// PS2 (0x00270B90): decodes the next picture (display order) into `rgb32` as RGBA32 macroblocks of
// 16x16 pixels (0x400 bytes), column by column: macroblock (x, y) at ((x * rows) + y) * 0x400, so
// each 16-pixel column is one contiguous 16 x height image (the draw 0x00213048 uploads it so).
// At most `mbcount` macroblocks are written. Sets width/height and counts the picture in
// frameCount; the end flag is set by the call that returns the sequence's last picture.
// HYPOTHESIS: frameCount counting. PCSX2 (s3_ss01_mid) shows one less than the pictures returned
// since sceMpegReset (46 vs 47), and flags (+0x20) 0x19B where the port leaves 0; the game reads
// neither (expected sdk differences, port/ramdiff-expected-s3.csv).
s32 sceMpegGetPicture(u32 mp, u32 rgb32, s32 mbcount) {
    Decoder& d = Decoders()[mp];
    if (d.dec == nullptr) return -1;
    if (!d.fed) {
        if (!d.bs.empty()) mpeg2_buffer(d.dec, d.bs.data(), d.bs.data() + d.bs.size());
        d.fed = true;
    }
    const mpeg2_info_t* info = mpeg2_info(d.dec);
    for (;;) {
        const mpeg2_state_t state = mpeg2_parse(d.dec);
        if (state == STATE_BUFFER) {
            SetEnd(mp, d, true);  // bitstream used up without a sequence end code
            return -1;
        }
        if (state == STATE_INVALID || state == STATE_INVALID_END) {
            ac04::log::WriteOnce("mpeg:invalid", ac04::log::Level::Warning, "mpeg", "MPEG-2 stream error");
            continue;
        }
        const bool end = state == STATE_END;
        if ((state == STATE_SLICE || end) && info->display_fbuf != nullptr && info->sequence != nullptr) {
            const s32 w = static_cast<s32>(info->sequence->width);
            const s32 h = static_cast<s32>(info->sequence->height);
            const s32 cw = static_cast<s32>(info->sequence->chroma_width);
            const s32 cols = (w + 15) / 16, rows = (h + 15) / 16;
            const u8* const* buf = info->display_fbuf->buf;
            // Movie upscaling side path (ADR-006 §5): a row-major host copy for the renderer.
            ac04::sce::MoviePicture* host = nullptr;
            if (ac04::sce::Host().movie_pictures) {
                host = &ac04::sce::Dma().movie_picture;
                host->serial += 1;
                host->guest_addr = rgb32;
                host->guest_bytes = static_cast<u32>(std::min(mbcount, cols * rows)) * 0x400;
                host->width = w;
                host->height = h;
                host->rgba.assign(static_cast<size_t>(w) * h * 4, 0);
            }
            s32 mb = 0;
            for (s32 x = 0; x < cols; ++x) {
                for (s32 y = 0; y < rows; ++y, ++mb) {
                    if (mb >= mbcount) break;
                    u8* out = ac04::guest::Mem().Translate(rgb32 + static_cast<u32>(mb) * 0x400, 0x400);
                    if (out == nullptr) continue;
                    for (s32 py = 0; py < 16; ++py) {
                        const s32 sy = std::min(y * 16 + py, h - 1);
                        for (s32 px = 0; px < 16; ++px) {
                            const s32 sx = std::min(x * 16 + px, w - 1);
                            u8* px_out = out + (py * 16 + px) * 4;
                            ac04::sce::IpuCsc(buf[0][sy * w + sx], buf[1][(sy >> 1) * cw + (sx >> 1)],
                                              buf[2][(sy >> 1) * cw + (sx >> 1)], px_out);
                            if (host != nullptr && sy == y * 16 + py && sx == x * 16 + px)
                                std::memcpy(&host->rgba[(static_cast<size_t>(sy) * w + sx) * 4], px_out, 4);
                        }
                    }
                }
            }
            GRef<s32>(mp + 0) = w;
            GRef<s32>(mp + 4) = h;
            GRef<s32>(mp + 8) += 1;
            if (end) SetEnd(mp, d, true);
            return mb;
        }
        if (end) {
            SetEnd(mp, d, true);
            return 0;
        }
    }
}
