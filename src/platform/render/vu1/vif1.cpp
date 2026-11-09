#include "platform/render/vu1/vif1.h"

#include <algorithm>
#include <cstring>

#include "platform/log.h"
#include "platform/render/vu1/vu1_programs.h"

namespace ac04::render::vu1 {

namespace {

std::uint32_t UnpackBits(std::uint32_t cmd) {
    const std::uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    if (vl == 3) return 16;  // V4-5
    return (32u >> vl) * (vn + 1);
}

// Word i of a command's data (the stream's bytes, read in place or from the collected copy).
inline std::uint32_t Load32(const std::uint8_t* d, std::size_t i) {
    std::uint32_t v;
    std::memcpy(&v, d + i * 4, 4);
    return v;
}

}  // namespace

void Vif1::SwapTops() {
    dbf_ ^= 1;
    tops_ = base_ + (dbf_ != 0 ? ofst_ : 0);
}

void Vif1::Words(const std::uint8_t* p, std::uint32_t n, std::uint32_t guest_addr) {
    while (n > 0) {
        if (direct_ > 0) {
            const std::uint32_t take = direct_ < n ? direct_ : n;
            gif_->insert(gif_->end(), p, p + take * 4);
            direct_ -= take;
            p += take * 4;
            n -= take;
            if (guest_addr != 0) guest_addr += take * 4;
            continue;
        }
        if (need_ > 0) {
            if (data_.empty()) {
                data_addr_ = guest_addr;
                if (n >= need_) {
                    // The whole data of the command is in this block: the command reads it in place
                    // (the same words as from the collected copy below).
                    const std::uint8_t* const words = p;
                    const std::uint32_t take = need_;
                    need_ = 0;
                    p += take * 4;
                    n -= take;
                    if (guest_addr != 0) guest_addr += take * 4;
                    Command(code_, words);
                    continue;
                }
            }
            const std::uint32_t take = need_ < n ? need_ : n;
            const std::size_t old = data_.size();
            data_.resize(old + take);
            std::memcpy(&data_[old], p, take * 4);
            need_ -= take;
            p += take * 4;
            n -= take;
            if (guest_addr != 0) guest_addr += take * 4;
            if (need_ == 0) Command(code_, reinterpret_cast<const std::uint8_t*>(data_.data()));
            continue;
        }
        std::uint32_t w;
        std::memcpy(&w, p, 4);
        p += 4;
        --n;
        if (guest_addr != 0) guest_addr += 4;
        const std::uint32_t cmd = (w >> 24) & 0x7F;
        code_ = w;
        data_.clear();
        const std::uint32_t num = (w >> 16) & 0xFF, imm = w & 0xFFFF;
        if (cmd >= 0x60) {
            const std::uint32_t elems = num == 0 ? 256 : num;
            need_ = (elems * UnpackBits(cmd) + 31) / 32;
        } else if (cmd == 0x50 || cmd == 0x51) {
            direct_ = (imm == 0 ? 0x10000u : imm) * 4u;
        } else if (cmd == 0x4A) {
            need_ = (num == 0 ? 256u : num) * 2u;
        } else if (cmd == 0x20) {
            need_ = 1;
        } else if (cmd == 0x30 || cmd == 0x31) {
            need_ = 4;
        } else {
            Command(w, nullptr);
        }
    }
}

void Vif1::Command(std::uint32_t code, const std::uint8_t* data) {
    const std::uint32_t cmd = (code >> 24) & 0x7F;
    const std::uint32_t num = (code >> 16) & 0xFF, imm = code & 0xFFFF;
    switch (cmd) {
        case 0x00: case 0x06: case 0x07: case 0x10: case 0x11: case 0x13: break;  // NOP, MSKPATH3, MARK, FLUSH*
        case 0x01: cl_ = imm & 0xFF; wl_ = (imm >> 8) & 0xFF; break;
        case 0x02:  // OFFSET: the double buffer restarts at BASE
            ofst_ = imm & 0x3FF;
            dbf_ = 0;
            tops_ = base_;
            break;
        case 0x03: base_ = imm & 0x3FF; break;
        case 0x04: itops_ = imm & 0x3FF; break;
        case 0x05: mode_ = imm & 3; break;
        case 0x14: case 0x15: RunProgram(imm, false); break;  // MSCAL, MSCALF
        case 0x17: RunProgram(0, true); break;                 // MSCNT
        case 0x20: mask_ = Load32(data, 0); break;
        case 0x30: for (int i = 0; i < 4; ++i) row_[i] = Load32(data, static_cast<std::size_t>(i)); break;
        case 0x31: for (int i = 0; i < 4; ++i) col_[i] = Load32(data, static_cast<std::size_t>(i)); break;
        case 0x4A: {  // MPG: remember where each micro pair came from (program identity)
            const std::uint32_t pairs = num == 0 ? 256 : num;
            for (std::uint32_t i = 0; i < pairs; ++i)
                vu_.micro_source[(imm + i) & (kMicroPairs - 1)] = data_addr_ != 0 ? data_addr_ + i * 8 : 0;
            break;
        }
        default:
            if (cmd >= 0x60) {
                Unpack(data);
            } else {
                log::WriteOnce("vif1:unknown", log::Level::Warning, "renderer", "VIF1 code 0x%02x not handled", cmd);
            }
            break;
    }
    data_.clear();
}

void Vif1::Unpack(const std::uint8_t* data) {
    const std::uint32_t cmd = (code_ >> 24) & 0x7F;
    const std::uint32_t num = ((code_ >> 16) & 0xFF) == 0 ? 256 : ((code_ >> 16) & 0xFF);
    const std::uint32_t imm = code_ & 0xFFFF;
    const bool usn = (imm & 0x4000) != 0;
    const bool flg = (imm & 0x8000) != 0;
    const bool masked = (cmd & 0x10) != 0;
    const std::uint32_t vn = (cmd >> 2) & 3, vl = cmd & 3;
    const std::uint32_t bits = UnpackBits(cmd);
    const std::uint32_t addr = (imm & 0x3FF) + (flg ? tops_ : 0);
    if (wl_ > cl_ && wl_ != 0) {
        log::WriteOnce("vif1:fill", log::Level::Warning, "renderer", "VIF1 UNPACK fill mode (WL > CL) approximated");
    }
    const std::uint32_t cl = cl_ == 0 ? 1 : cl_, wl = wl_ == 0 ? 1 : wl_;
    // The run log keys a program run by where its vertex data (VU qword 32 on) came from.
    const std::uint32_t last = addr + ((num - 1) / wl) * cl + (num - 1) % wl;
    if (flg && addr <= tops_ + 32 && tops_ + 32 <= last) vertex_source_ = data_addr_;
    auto element = [&](std::uint32_t i, std::uint32_t c) -> std::uint32_t {
        // Component c of element i, sign/zero extended to 32 bits.
        const std::uint64_t bit = static_cast<std::uint64_t>(i) * bits;
        if (vl == 3) {  // V4-5: one 16-bit RGBA5551 pixel per element
            const std::uint32_t w = Load32(data, bit / 32) >> (bit % 32) & 0xFFFF;
            switch (c) {
                case 0: return (w & 0x1F) << 3;
                case 1: return ((w >> 5) & 0x1F) << 3;
                case 2: return ((w >> 10) & 0x1F) << 3;
                default: return ((w >> 15) & 1) << 7;
            }
        }
        const std::uint32_t width = 32u >> vl;
        std::uint32_t comp = c;
        if (vn == 0) comp = 0;                  // S: one value to all four
        else if (vn == 1 && c >= 2) comp = c - 2;  // V2: z, w repeat x, y
        else if (vn == 2 && c == 3) return 0xFFFFFFFFu;  // V3: w not written (marker)
        const std::uint64_t b = bit + static_cast<std::uint64_t>(comp) * width;
        std::uint32_t v;
        if (width == 32) {
            v = Load32(data, b / 32);
        } else {
            v = (Load32(data, b / 32) >> (b % 32)) & ((1u << width) - 1);
            if (!usn && (v >> (width - 1)) != 0) v |= ~((1u << width) - 1);
        }
        return v;
    };
    if (!masked && mode_ == 0 && vl != 3) {
        // Fast path (no write mask, no ROW addition): the same values as the loop below.
        const std::uint32_t width = 32u >> vl;
        const std::uint32_t field = width == 32 ? 0xFFFFFFFFu : (1u << width) - 1;
        const std::uint32_t outc = vn == 2 ? 3 : 4;  // V3 leaves w
        std::uint32_t comp_bits[4];
        for (std::uint32_t c = 0; c < 4; ++c)
            comp_bits[c] = (vn == 0 ? 0 : (vn == 1 ? (c & 1) : c)) * width;  // S: x to all, V2: z,w = x,y
        // Element i goes to qword addr + (i / wl) * cl + i % wl: the cycle counters step instead.
        std::uint32_t cycle_base = addr, in_cycle = 0;
        auto next = [&] {
            if (++in_cycle == wl) {
                in_cycle = 0;
                cycle_base += cl;
            }
        };
        if (vn == 3 && width == 32) {
            // V4-32: each element is the qword's four words as they are.
            for (std::uint32_t i = 0; i < num; ++i, next())
                std::memcpy(vu_.Qword(cycle_base + in_cycle), data + static_cast<std::size_t>(i) * 16, 16);
            return;
        }
        for (std::uint32_t i = 0; i < num; ++i, next()) {
            std::uint32_t* q = vu_.Qword(cycle_base + in_cycle);
            const std::uint64_t base = static_cast<std::uint64_t>(i) * bits;
            for (std::uint32_t c = 0; c < outc; ++c) {
                const std::uint64_t b = base + comp_bits[c];
                std::uint32_t v = Load32(data, b / 32);
                if (width != 32) {
                    v = (v >> (b % 32)) & field;
                    if (!usn && (v >> (width - 1)) != 0) v |= ~field;
                }
                q[c] = v;
            }
        }
        return;
    }
    for (std::uint32_t i = 0; i < num; ++i) {
        const std::uint32_t cycle = i / wl, pos = i % wl;
        std::uint32_t* q = vu_.Qword(addr + cycle * cl + pos);
        const std::uint32_t mask_row = pos < 3 ? pos : 3;
        for (std::uint32_t c = 0; c < 4; ++c) {
            std::uint32_t v = element(i, c);
            if (vn == 2 && c == 3 && vl != 3) continue;  // V3 leaves w
            const std::uint32_t m = masked ? (mask_ >> (mask_row * 8 + c * 2)) & 3 : 0;
            if (m == 3) continue;
            if (m == 1) {
                v = row_[c];
            } else if (m == 2) {
                v = col_[mask_row];
            } else if (mode_ == 1) {
                v += row_[c];
            } else if (mode_ == 2) {
                v += row_[c];
                row_[c] = v;
            }
            q[c] = v;
        }
    }
}

void Vif1::RunProgram(std::uint32_t start, bool continue_) {
    vu_.top = tops_;
    vu_.itop = itops_;
    SwapTops();
    const std::uint32_t pc = continue_ ? vu_.next_pc : start;
    std::uint32_t next = pc;
    RunRecord* rec = nullptr;
    if (log_ != nullptr && vu_.top + 32 < kVuMemQwords) {
        // VU memory the run reads (RunRecord): absolute 0..63, and from TOP the fixed block
        // (0-31) and the vertex records from 32 (count at TOP + 23: a byte for the terrain program).
        const std::uint32_t q23 = vu_.Qword(vu_.top + 23)[0];
        const std::uint32_t count = vu_.micro_source[0] == kProgramT ? (q23 & 0xFF) : (q23 & 0x3FF);
        const std::uint32_t rel = std::min<std::uint32_t>(kVuMemQwords - vu_.top, 32 + 4 * count + 8);
        RunRecord r;
        r.gif_offset = static_cast<std::uint32_t>(gif_->size());
        r.source = vu_.micro_source[0];
        r.pc = pc;
        r.top = vu_.top;
        r.itop = vu_.itop;
        r.vertex_source = vertex_source_;
        r.mem_offset = static_cast<std::uint32_t>(log_->mem.size());
        r.rel_qwords = rel;
        log_->mem.insert(log_->mem.end(), vu_.mem.begin(), vu_.mem.begin() + RunRecord::kAbsQwords * 4);
        log_->mem.insert(log_->mem.end(), vu_.mem.begin() + vu_.top * 4, vu_.mem.begin() + (vu_.top + rel) * 4);
        log_->runs.push_back(r);
        rec = &log_->runs.back();
    }
    if (!RunVu1Program(vu_, vu_.micro_source[0], pc, *gif_, next)) {
        log::WriteOnce("vu1:unknown", log::Level::Warning, "renderer",
                       "VU1 program with microcode from 0x%08X (start %u) not reimplemented; skipped",
                       vu_.micro_source[0], pc);
    }
    if (rec != nullptr) rec->gif_size = static_cast<std::uint32_t>(gif_->size()) - rec->gif_offset;
    vu_.next_pc = next;
    ++runs_;
}

}  // namespace ac04::render::vu1
