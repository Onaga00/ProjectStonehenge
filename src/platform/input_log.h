// Per-tick input stream (docs/port/slices.md §B P1): the pad states each game tick read. It is the
// whole external input of a run, so feeding it back reproduces the run (determinism checks, and
// later lockstep LAN play, ADR-008). File format (little-endian): "AC4I", u32 version 1, then one
// record per tick: u64 vblank, two pads of {u8 connected (bit 0; bit 1 the digital map cycle,
// 2026-10-31), u16 buttons, u8 rx ry lx ly, u8 pressure[12]} (19 bytes each).
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>

#include "platform/input.h"

namespace ac04::platform {

struct TickInput {
    std::uint64_t vblank = 0;
    PadState pad[2];
};

class InputLogWriter {
public:
    bool Open(const std::filesystem::path& path);
    void Write(const TickInput& tick);
    bool IsOpen() const { return out_.is_open(); }

private:
    std::ofstream out_;
};

class InputLogReader {
public:
    bool Open(const std::filesystem::path& path);
    // The pads of tick `vblank`; after the last record the last state is held. False when empty.
    bool Get(std::uint64_t vblank, PadState out[2]) const;
    std::size_t Size() const { return ticks_.size(); }

private:
    std::map<std::uint64_t, TickInput> ticks_;
};

// The pads with live devices ignored: port 0 connected and neutral, port 1 not connected.
void NeutralPads(PadState out[2]);

// Modern control types (port baseline, User approval 2026-10-20): L1 <-> L2 and R1 <-> R2,
// digital bits and pressures, so the game reads the Classic layout it was set to.
void SwapShoulderButtons(PadState& pad);

}  // namespace ac04::platform
