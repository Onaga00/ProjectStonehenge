// VIF1 + VU1 (ADR-003: VU1 programs as CPU HLE; docs/subsystems/vu-programs.md).
//
// Vif1 consumes the VIF1 code stream of the DMA chains the game sends on channel 1 (word by word,
// in order): DIRECT/DIRECTHL data goes to the GIF output as PATH2, UNPACK writes VU1 data memory,
// MPG records which .vutext block is resident (by the guest address of the microcode), and
// MSCAL/MSCALF/MSCNT run the resident program through its CPU reimplementation (vu1_programs.cpp),
// whose XGKICK output is appended to the same GIF output (PATH1) at that point of the stream.
//
// Registers modelled: CYCLE (CL/WL), MODE, MASK, ROW, COL, BASE, OFST, TOPS/TOP with the double
// buffer flag, ITOPS/ITOP, NUM. Not modelled: interrupts (the i bit), MARK, path masking.
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace ac04::render::vu1 {

constexpr std::uint32_t kVuMemQwords = 1024;  // 16 KiB
constexpr std::uint32_t kMicroPairs = 2048;   // 16 KiB of micro memory

// VU1 data memory and the registers the programs read.
struct Vu1State {
    std::array<std::uint32_t, kVuMemQwords * 4> mem{};
    std::uint32_t top = 0;   // xtop
    std::uint32_t itop = 0;  // xitop
    // Guest address of the microcode loaded at each micro-memory pair (0: not loaded).
    std::array<std::uint32_t, kMicroPairs> micro_source{};
    std::uint32_t next_pc = 0;  // where MSCNT continues (pair after the last E bit's delay slot)

    std::uint32_t* Qword(std::uint32_t addr) { return &mem[(addr & (kVuMemQwords - 1)) * 4]; }
    // The VU has no NaN or infinity: exponent 255 is an ordinary number (±1.x · 2^128). Read as
    // ±FLT_MAX so that it stays finite (0 × it = 0, as on the VU) instead of an IEEE NaN/Inf. Game
    // data can leave such words in slots a program multiplies by zero: the light colours that
    // InitLightSet 0x00136AA0 leaves uncleared (docs/port/validation.md §2aq HT-6).
    float F(std::uint32_t addr, int c) const {
        std::uint32_t b = mem[(addr & (kVuMemQwords - 1)) * 4 + c];
        if ((b & 0x7F800000u) == 0x7F800000u) b = (b & 0x80000000u) | 0x7F7FFFFFu;
        float f;
        std::memcpy(&f, &b, 4);
        return f;
    }
    std::int32_t I(std::uint32_t addr, int c) const { return static_cast<std::int32_t>(mem[(addr & (kVuMemQwords - 1)) * 4 + c]); }
};

// Frame interpolation (ADR-002; platform/render/frame_interpolator.h): what each VU1 program run of
// a step read and where its XGKICK output went, so that the run can be repeated at presentation time
// with blended matrices. Recorded only while interpolation is active (Vif1::SetRunLog).
struct RunRecord {
    std::uint32_t gif_offset = 0, gif_size = 0;  // the run's output in the step's GIF stream
    std::uint32_t source = 0;                    // program (guest address of its microcode)
    std::uint32_t pc = 0;                        // entry pair
    std::uint32_t top = 0, itop = 0;
    std::uint32_t vertex_source = 0;  // guest address of the UNPACK data that last wrote TOP + 32
    // VU memory at the start of the run (RunLog::mem from mem_offset): the absolute qwords
    // 0..kAbsQwords-1 (the backface history at 0-1, the glow program's constants at 32-49), then
    // rel_qwords qwords from TOP (the fixed block and the vertex records).
    static constexpr std::uint32_t kAbsQwords = 64;
    std::uint32_t mem_offset = 0, rel_qwords = 0;
};
struct RunLog {
    std::vector<RunRecord> runs;
    std::vector<std::uint32_t> mem;
    void Clear() {
        runs.clear();
        mem.clear();
    }
};

class Vif1 {
public:
    // `gif` receives PATH2 (DIRECT) and PATH1 (XGKICK) GIF data in stream order.
    explicit Vif1(std::vector<std::uint8_t>& gif) : gif_(&gif) {}
    void SetOutput(std::vector<std::uint8_t>& gif) { gif_ = &gif; }

    // A block of `n` 32-bit words of the stream; `guest_addr` is the guest address of the first word
    // (0 when unknown), used to identify microcode uploads.
    void Words(const std::uint8_t* p, std::uint32_t n, std::uint32_t guest_addr);
    void Word(std::uint32_t w, std::uint32_t guest_addr = 0) {
        Words(reinterpret_cast<const std::uint8_t*>(&w), 1, guest_addr);
    }

    Vu1State& Vu() { return vu_; }
    // Records the program runs into `log` (null: no recording).
    void SetRunLog(RunLog* log) { log_ = log; }
    // Number of program runs per program id since the last call (debug overlay / tests).
    std::uint64_t Runs() const { return runs_; }

private:
    // `data`: the command's data words (null for commands without data), in place or collected.
    void Command(std::uint32_t code, const std::uint8_t* data);
    void Unpack(const std::uint8_t* data);
    void RunProgram(std::uint32_t start, bool continue_);
    void SwapTops();

    std::vector<std::uint8_t>* gif_;
    Vu1State vu_;
    // VIF registers
    std::uint32_t cl_ = 1, wl_ = 1, mode_ = 0, mask_ = 0;
    std::array<std::uint32_t, 4> row_{}, col_{};
    std::uint32_t base_ = 0, ofst_ = 0, tops_ = 0, dbf_ = 0, itops_ = 0;
    // Command in progress: the code, and the data words still to collect.
    std::uint32_t code_ = 0;
    std::uint32_t need_ = 0;      // words still expected for the current command
    std::uint32_t direct_ = 0;    // DIRECT: words still to pass through
    std::vector<std::uint32_t> data_;
    std::uint32_t data_addr_ = 0;  // guest address of the first data word (MPG)
    std::uint64_t runs_ = 0;
    RunLog* log_ = nullptr;
    std::uint32_t vertex_source_ = 0;
};

}  // namespace ac04::render::vu1
