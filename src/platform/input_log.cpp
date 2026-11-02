#include "platform/input_log.h"

#include <cstring>
#include <utility>

namespace ac04::platform {

namespace {

constexpr char kMagic[4] = {'A', 'C', '4', 'I'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kPadBytes = 19;

void PutPad(std::uint8_t* p, const PadState& s) {
    // Bit 1: the digital map cycle (port_hooks.h InputHooks), written only for a connected pad;
    // logs of earlier builds hold 0 or 1 (cycle off), and their readers take any non-zero byte as
    // connected.
    p[0] = static_cast<std::uint8_t>(s.connected ? (s.map_cycle ? 3 : 1) : 0);
    p[1] = static_cast<std::uint8_t>(s.buttons);
    p[2] = static_cast<std::uint8_t>(s.buttons >> 8);
    p[3] = s.rx;
    p[4] = s.ry;
    p[5] = s.lx;
    p[6] = s.ly;
    std::memcpy(p + 7, s.pressure.data(), 12);
}

PadState GetPad(const std::uint8_t* p) {
    PadState s;
    s.connected = (p[0] & 1) != 0;
    s.map_cycle = s.connected && (p[0] & 2) != 0;
    s.buttons = static_cast<std::uint16_t>(p[1] | p[2] << 8);
    s.rx = p[3];
    s.ry = p[4];
    s.lx = p[5];
    s.ly = p[6];
    std::memcpy(s.pressure.data(), p + 7, 12);
    return s;
}

}  // namespace

bool InputLogWriter::Open(const std::filesystem::path& path) {
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) return false;
    out_.write(kMagic, 4);
    out_.write(reinterpret_cast<const char*>(&kVersion), 4);
    return static_cast<bool>(out_);
}

void InputLogWriter::Write(const TickInput& tick) {
    if (!out_) return;
    std::uint8_t rec[8 + 2 * kPadBytes];
    std::memcpy(rec, &tick.vblank, 8);
    PutPad(rec + 8, tick.pad[0]);
    PutPad(rec + 8 + kPadBytes, tick.pad[1]);
    out_.write(reinterpret_cast<const char*>(rec), sizeof(rec));
}

bool InputLogReader::Open(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[4];
    std::uint32_t version = 0;
    in.read(magic, 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    if (!in || std::memcmp(magic, kMagic, 4) != 0 || version != kVersion) return false;
    std::uint8_t rec[8 + 2 * kPadBytes];
    while (in.read(reinterpret_cast<char*>(rec), sizeof(rec))) {
        TickInput t;
        std::memcpy(&t.vblank, rec, 8);
        t.pad[0] = GetPad(rec + 8);
        t.pad[1] = GetPad(rec + 8 + kPadBytes);
        ticks_[t.vblank] = t;
    }
    return true;
}

bool InputLogReader::Get(std::uint64_t vblank, PadState out[2]) const {
    if (ticks_.empty()) return false;
    auto it = ticks_.upper_bound(vblank);
    if (it == ticks_.begin()) {
        NeutralPads(out);
        return true;
    }
    --it;
    out[0] = it->second.pad[0];
    out[1] = it->second.pad[1];
    return true;
}

void NeutralPads(PadState out[2]) {
    out[0] = PadState{};
    out[0].connected = true;
    out[1] = PadState{};
}

void SwapShoulderButtons(PadState& pad) {
    // Byte 3 of the buttons: bit 0 L2, bit 1 R2, bit 2 L1, bit 3 R1. Pressures: 8 L1, 9 R1, 10 L2,
    // 11 R2.
    const std::uint16_t b = pad.buttons;
    const std::uint16_t low = static_cast<std::uint16_t>((b >> 8) & 0x3), high = static_cast<std::uint16_t>((b >> 10) & 0x3);
    pad.buttons = static_cast<std::uint16_t>((b & ~0x0F00u) | (low << 10) | (high << 8));
    std::swap(pad.pressure[8], pad.pressure[10]);
    std::swap(pad.pressure[9], pad.pressure[11]);
}

}  // namespace ac04::platform
