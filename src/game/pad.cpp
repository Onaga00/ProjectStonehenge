// Pad input (docs/port/skeleton-readiness.md §7): libpad connection state machine per port (Sony
// sample style), conversion of the raw scePadRead data into the game's pad state, button edges and
// mapped buttons, vibration.
//
// Port records: 2 x 100 bytes at 0x004121D8: [0] port, [1] slot, [2] state, [3] pad id,
// [4] last read type, [5] previous type, +0x18 raw read buffer (32 bytes), +0x38 copy used by
// ConvertPadInput, +0x58 actuator align (6 bytes), +0x5E actuator values (6 bytes).
// Game pad state: ctx->pad_state[port] (0x50 bytes): [0] type, [1] buttons, [2] pressed,
// [3] repeat, [4] hold count, +0x14 32 converted values, +0x34 4 x {0, 0x80, 0, 0xFF} axis
// calibration, +0x44 vibration flags, +0x45/+0x46 motor values, +0x48 2 timers.
#include "game/game.h"

#include "common/ee_math.h"
#include "common/guest_stack.h"
#include "common/port_hooks.h"
#include "sce/sce.h"

namespace ac04::game {

using guest::GRef;

constexpr u32 kPadPorts = 0x004121D8;

// EE: 0x001011B0
// Returns the type byte of the read data (0 when the read failed or status byte != 0).
u8 ReadPadData(s32 port, s32 slot, u32 buf) {
    if (scePadRead(port, slot, buf) == 0) return 0;
    if (GRef<u8>(buf) != 0) return 0;
    return GRef<u8>(buf + 1);
}

// EE: 0x001011F0
void InitPadPort(s32 port, s32 slot, u32 rec) {
    GRef<s32>(rec) = port;
    GRef<s32>(rec + 4) = slot;
    GRef<u32>(rec + 8) = 0;
    GRef<u32>(rec + 0xC) = 0;
    GRef<u32>(rec + 0x10) = 0;
    GRef<u32>(rec + 0x14) = 0;
    for (u32 i = 0; i < 0x20; ++i) {
        const u8 v = (i - 4u) < 4u ? 0x80 : 0xFF;  // analog bytes 4..7 centred, the rest released
        GRef<u8>(rec + 0x38 + i) = v;
        GRef<u8>(rec + 0x18 + i) = v;
    }
    for (u32 i = 0; i < 6; ++i) {
        GRef<u8>(rec + 0x58 + i) = 0xFF;
        GRef<u8>(rec + 0x5E + i) = 0;
    }
}

// EE: 0x00101298
void UpdatePadConnection(u32 rec) {
    const s32 port = GRef<s32>(rec);
    const s32 slot = GRef<s32>(rec + 4);
    const u32 status = static_cast<u32>(scePadGetState(port, slot));
    if (status > 7) {
        const u64 args[1] = {status};
        ee::printf(0x00403840 /* "Error: Pad Status %d\n" */, args, 1);
    }
    u32& state = GRef<u32>(rec + 8);
    if (status == 0) state = 0;
    const bool stable = status == 6 || status == 2;
    switch (state) {
        case 0: {
            if (!stable) return;
            s32 id = scePadInfoMode(port, slot, 1, 0);
            GRef<s32>(rec + 0xC) = id;
            if (id == 0) return;
            const s32 ext = scePadInfoMode(port, slot, 2, 0);
            if (ext > 0) GRef<s32>(rec + 0xC) = ext;
            id = GRef<s32>(rec + 0xC);
            GRef<u32>(rec + 0x14) = 0;
            if (id != 5) {
                if (id < 6) {
                    if (id != 3 && id > 3) {  // 4: digital pad -> try analog mode
                        state = 0x28;
                        return;
                    }
                } else if (id == 7) {  // DualShock 2
                    state = 0x46;
                    return;
                }
            }
            state = 100;
            break;
        }
        default:
            state = 0;
            break;
        case 0x28:
            if (scePadInfoMode(port, slot, 2, -1) != 0) {
                state = 0x29;
                return;
            }
            state = 100;
            break;
        case 0x29:
            if (scePadSetMainMode(port, slot, 1, 3) == 1) state = 0x2A;
            break;
        case 0x2A: {
            const s32 r = scePadGetReqState(port, slot);
            if (r == 1) state = 0x29;
            if (r == 0) state = 0;
            break;
        }
        case 0x46:
            if (scePadInfoAct(port, slot, -1, 0) == 0) state = 100;
            GRef<u8>(rec + 0x59) = 1;
            GRef<u8>(rec + 0x58) = 0;
            for (u32 i = 2; i < 6; ++i) GRef<u8>(rec + 0x58 + i) = 0xFF;
            if (scePadSetActAlign(port, slot, rec + 0x58) != 0) state = 0x47;
            break;
        case 0x47: {
            const s32 r = scePadGetReqState(port, slot);
            if (r == 1) state = 0x46;
            if (r == 0) state = 0x48;
            break;
        }
        case 0x48:
            if (scePadInfoPressMode(port, slot) != 0) {
                state = 0x4C;
                return;
            }
            state = 0x50;
            break;
        case 0x4C:
            if (scePadEnterPressMode(port, slot) == 0) state = 0x48;
            state = 0x4D;
            break;
        case 0x4D: {
            const s32 r = scePadGetReqState(port, slot);
            if (r == 1) state = 0x4C;
            if (r != 0) return;
            state = 0x50;
            break;
        }
        case 0x50:
            if (stable) {
                ReadPadData(port, slot, rec + 0x18);
                for (u32 i = 0; i < 0x20; ++i) GRef<u8>(rec + 0x38 + i) = GRef<u8>(rec + 0x18 + i);
                state = 99;
            }
            break;
        case 99:
            if (stable) {
                const u32 type = ReadPadData(port, slot, rec + 0x18);
                GRef<u32>(rec + 0x10) = type;
                if (GRef<u32>(rec + 0x14) != 0 && type != GRef<u32>(rec + 0x14)) {
                    // The pad type changed (e.g. analog button): reconnect.
                    GRef<u32>(rec + 0x14) = 0;
                    state = 0;
                }
                GRef<u32>(rec + 0x14) = GRef<u32>(rec + 0x10);
            }
            for (u32 i = 0; i < 0x20; ++i) GRef<u8>(rec + 0x38 + i) = GRef<u8>(rec + 0x18 + i);
            break;
    }
}

// EE: 0x00101688
// Scales one raw axis/pressure byte with a dead zone (*dz) and the calibration {?, centre, min, max}
// at `cal`. kind 0: pressure 0..255; 1: negative half; 2: positive half; 3: full axis around 0x80.
u32 ScalePadAxis(u8 kind, u32 dz, u32 cal, u32 value) {
    value &= 0xFF;
    const u32 lo = GRef<u8>(cal + 2);
    const u32 hi = GRef<u8>(cal + 3);
    if (value < lo) value = lo;
    if (hi < value) value = hi;
    s32 num, den;
    u32 d;
    if (kind == 1) {
        d = GRef<u8>(dz);
        const u32 centre = GRef<u8>(cal + 1);
        if (static_cast<s32>(centre - d) <= static_cast<s32>(value)) return 0;
        num = static_cast<s32>(((centre - value) & 0xFF) - d);
        den = static_cast<s32>(centre - lo);
    } else if (kind == 0) {
        d = GRef<u8>(dz);
        if (d == 0) return value;
        if (d < value) {
            const s32 range = static_cast<s32>(hi - d);
            return static_cast<u32>(static_cast<s32>((value - d) * 0xFF) / range) & 0xFF;
        }
        return 0;
    } else if (kind == 3) {
        const u32 dzv = GRef<u8>(dz);
        const u32 centre = GRef<u8>(cal + 1);
        const u32 upper = centre + dzv;
        if (value <= upper) {
            if (static_cast<s32>(0x80 - dzv) <= static_cast<s32>(value)) return 0x80;
            const s32 range = static_cast<s32>((centre - lo) - dzv);
            const s32 q = static_cast<s32>(((centre - dzv) - value) * 0x80) / range;
            return (static_cast<u32>(-q) - 0x80u) & 0xFF;
        }
        const s32 range = static_cast<s32>((hi - centre) - dzv);
        return (static_cast<u32>(static_cast<s32>((value - upper) * 0x7F) / range) - 0x80u) & 0xFF;
    } else if (kind == 2) {
        d = GRef<u8>(dz);
        const u32 centre = GRef<u8>(cal + 1);
        if (value <= centre + d) return 0;
        num = static_cast<s32>(((value - centre) & 0xFF) - d);
        den = static_cast<s32>(hi - centre);
    } else {
        return value;
    }
    return static_cast<u32>((num * 0xFF) / static_cast<s32>(den - static_cast<s32>(d))) & 0xFF;
}

// EE: 0x00101858
void ConvertPadInput(u32 rec, u32 pad) {
    if (GRef<u32>(rec + 8) != 99) {
        // Not connected: neutral state.
        GRef<u32>(pad + 4) = 0;
        GRef<u32>(pad) = 0x80;
        GRef<u32>(pad + 8) = 0;
        GRef<u32>(pad + 0xC) = 0;
        GRef<u32>(pad + 0x10) = 0;
        for (u32 i = 0; i < 0x20; ++i) {
            GRef<u8>(pad + 0x14 + i) = (i == 0x10 || i == 0x13 || i == 0x16 || i == 0x19) ? 0x80 : 0;
        }
        for (u32 i = 0; i < 4; ++i) {
            GRef<u8>(pad + 0x34 + 4 * i) = 0;
            GRef<u8>(pad + 0x35 + 4 * i) = 0x80;
            GRef<u8>(pad + 0x36 + 4 * i) = 0;
            GRef<u8>(pad + 0x37 + 4 * i) = 0xFF;
        }
        StopPadVibration(pad);
        return;
    }
    // Mapping table by pad type: 32 entries of {kind<<4 | type, source}, then 4 dead-zone bytes at +0x40.
    u32 table;
    const u8 mode = GRef<u8>(rec + 0x39);
    if (mode == 0x79) {
        GRef<u32>(pad) = 5;  // DualShock 2 (pressure)
        table = 0x0029FE50;
    } else if (mode == 0x73) {
        GRef<u32>(pad) = 4;  // analog
        table = 0x0029FE08;
    } else if (mode == 0xFF) {
        GRef<u32>(pad) = 6;  // USB joystick data
        table = 0x0029FE98;
    } else {
        GRef<u32>(pad) = 0xFF;  // digital
        table = 0x0029FDC0;
    }
    if (GRef<u32>(pad) - 4u < 3) {
        // Circularise both sticks: radius = (int)sqrt(x*x + y*y) clamped to 0x7F.
        for (u32 stick = 0; stick < 2; ++stick) {
            const u32 pa = rec + 0x38 + (stick == 0 ? 4 : 6);
            const u32 pb = rec + 0x38 + (stick == 0 ? 5 : 7);
            const s32 x = static_cast<s32>(GRef<u8>(pa)) - 0x80;
            const s32 y = static_cast<s32>(GRef<u8>(pb)) - 0x80;
            u32 r = static_cast<u32>(fpu::FloatToInt(fpu::Sqrt(fpu::IntToFloat(x * x + y * y))));
            if (static_cast<s32>(r) > 0x7F) r = 0x7F;
            if (x != 0 && y != 0) {
                const s32 ax = x < 0 ? -x : x;
                const s32 ay = y < 0 ? -y : y;
                u8 other;
                if (ay < ax) {
                    if (x < 0) r = ~r;
                    GRef<u8>(pa) = static_cast<u8>(static_cast<u8>(r) + 0x80);
                    other = static_cast<u8>(static_cast<s32>(static_cast<u32>(y) * r) / x);
                } else {
                    if (y < 0) r = ~r;
                    other = static_cast<u8>(r);
                    GRef<u8>(pa) = static_cast<u8>(static_cast<u8>(static_cast<s32>(static_cast<u32>(x) * r) / y) + 0x80);
                }
                GRef<u8>(pb) = static_cast<u8>(other + 0x80);
            }
        }
    }
    // Pressed mask from the raw button bytes (active low): byte 3 -> bits 0-7, byte 2 -> 8-15.
    u32 pressed = ~(static_cast<u32>(GRef<u8>(rec + 0x3B)) | 0xFFFF0000u | static_cast<u32>(GRef<u8>(rec + 0x3A)) << 8);
    guest::GuestFrame locals(0x10);
    const u32 no_dz = locals.Address(0);   // 1 byte: 0
    const u32 no_cal = locals.Address(4);  // {0, 0, 0, 0xFF}
    for (u32 i = 0; i < 0x20; ++i) {
        GRef<u8>(no_dz) = 0;
        GRef<u32>(no_cal) = 0;
        GRef<u8>(no_cal + 3) = 0xFF;
        const u8 entry = GRef<u8>(table + 2 * i);
        const u8 source = GRef<u8>(table + 2 * i + 1);
        const u8 kind = entry >> 4;
        const u8 type = entry & 0xF;
        u32 dz, cal;
        if (source < 4) {
            dz = table + source + 0x40;
            cal = pad + 0x34 + 4u * source;
        } else {
            dz = no_dz;
            cal = no_cal;
        }
        const u32 out = pad + 0x14 + i;
        if (type == 0) {
            GRef<u8>(out) = 0;
        } else if (type == 1) {
            GRef<u8>(out) = ((static_cast<s32>(pressed) >> (i & 0x1F)) & 1) != 0 ? 0xFF : 0;
        } else if (type == 2) {
            const u32 v = ScalePadAxis(kind, dz, cal, GRef<u8>(rec + 0x3C + source));
            const bool inside = kind == 3 ? ((v - 0x40u) & 0xFF) < 0x81 : v < 0x81;
            GRef<u8>(out) = static_cast<u8>(v);
            if (!inside) pressed |= 1u << (i & 0x1F);
        } else if (type == 3) {
            GRef<u8>(out) = static_cast<u8>(ScalePadAxis(kind, dz, cal, GRef<u8>(rec + 0x3C + source)));
        }
    }
    // Edges and auto-repeat (first repeat after 31 frames, then every 4).
    const u32 prev = GRef<u32>(pad + 4);
    GRef<u32>(pad + 8) = ~prev & pressed;
    if (prev == pressed) {
        const u32 held = GRef<u32>(pad + 0x10);
        if (held < 0x1F) GRef<u32>(pad + 0xC) = 0;
        else if ((held & 3) == 0) GRef<u32>(pad + 0xC) = pressed;
        else GRef<u32>(pad + 0xC) = 0;
        GRef<u32>(pad + 0x10) = held + 1;
    } else {
        GRef<u32>(pad + 0xC) = ~prev & pressed;
        GRef<u32>(pad + 0x10) = 0;
    }
    GRef<u32>(pad + 4) = pressed;
}

// EE: 0x00101CF0
// Game button word from the pad mask (15 masks at 0x0029FEE0), with edges and auto-repeat.
void UpdatePadButtonEdges(u32 pad, u32 out) {
    u32 buttons = 0;
    for (u32 i = 0; i < 0xF; ++i) {
        if ((GRef<u32>(pad + 4) & GRef<u32>(0x0029FEE0 + 4 * i)) != 0) buttons |= 1u << i;
    }
    const u32 prev = GRef<u32>(out);
    const u32 pressed = ~prev & buttons;
    GRef<u32>(out + 4) = pressed;
    if (prev == buttons) {
        const u32 held = GRef<u32>(out + 0xC);
        if (held < 0x1F) GRef<u32>(out + 8) = 0;
        else if ((held & 3) == 0) GRef<u32>(out + 8) = buttons;
        else GRef<u32>(out + 8) = 0;
        GRef<u32>(out + 0xC) = held + 1;
    } else {
        GRef<u32>(out + 8) = pressed;
        GRef<u32>(out + 0xC) = 0;
    }
    GRef<u32>(out) = buttons;
}

// EE: 0x00101D80
// Builds 16 masks (+0x00) and 16 value indices (+0x40) from a 32-byte map; index 6 also at +0x4C.
void BuildPadButtonMap(u32 map, u32 out) {
    for (u32 i = 0; i < 0x10; ++i) {
        GRef<u32>(out + 4 * i) = 0;
        GRef<u8>(out + 0x40 + i) = 0xFF;
    }
    for (u32 i = 0; i < 0x20; ++i) {
        const u32 v = GRef<u8>(map + i);
        if (v == 0xFF) continue;
        if ((v & 0x80) == 0) {
            GRef<u32>(out + 4 * v) |= 1u << i;
        } else {
            GRef<u8>(out + (v & 0x7F) + 0x40) = static_cast<u8>(i);
        }
        if (v == 6) GRef<u8>(out + 0x4C) = static_cast<u8>(i);
    }
}

// EE: 0x00101E28
void UpdateMappedPadButtons(u32 pad, u32 ext, u32 map_out) {
    const u32 type = GRef<u32>(pad);
    u32 map;
    if (type == 4) map = 0x0029FF40;
    else if (type < 5) map = 0x0029FF20;
    else if (type == 5 || type == 6) map = 0x0029FF60;
    else map = 0x0029FF20;
    BuildPadButtonMap(map, map_out);
    u32 buttons = 0;
    for (u32 i = 0; i < 0x10; ++i) {
        if ((GRef<u32>(pad + 4) & GRef<u32>(map_out + 4 * i)) != 0) buttons |= 1u << i;
        const u8 index = GRef<u8>(map_out + 0x40 + i);
        if (index != 0xFF) GRef<u8>(ext + 0x10 + i) = GRef<u8>(pad + 0x14 + index);
    }
    // port: digital map cycle (port_hooks.h InputHooks): with a device that gives square no
    // pressure, square also sets the MAP bit 9 the analog pad's map 0x0029FF40 gives it (entry 7 =
    // 9; 0x0029FF60 has only its value, 0x8D). No entry of 0x0029FF60 sets bit 9.
    if (type == 5) {
        const u32 port = (pad - (g_pMainContext().addr + 0x210)) / 0x50;
        if (port < 2 && port::Input().digital_map_cycle[port] && (GRef<u32>(pad + 4) & 0x80u) != 0) buttons |= 0x200u;
    }
    const u32 prev = GRef<u32>(ext);
    const u32 pressed = ~prev & buttons;
    GRef<u32>(ext + 4) = pressed;
    if (prev == buttons) {
        const u8 held = GRef<u8>(ext + 0xC);
        if (held < 0x21) GRef<u32>(ext + 8) = 0;
        else if ((held & 3) == 0) GRef<u32>(ext + 8) = buttons;
        else GRef<u32>(ext + 8) = 0;
        GRef<u8>(ext + 0xC) = static_cast<u8>(held + 1);
        if (static_cast<s32>(static_cast<u32>(held + 1) * 0x1000000u) < 0) GRef<u8>(ext + 0xC) = 0x20;
    } else {
        GRef<u32>(ext + 8) = pressed;
        GRef<u8>(ext + 0xC) = 0;
    }
    GRef<u32>(ext) = buttons;
    // Bit 6 (H: start/select-type button): short press vs. hold over 15 frames.
    if ((buttons & 0x40) == 0) {
        if (static_cast<u32>(GRef<u8>(ext + 0xD)) - 1u < 0xE) {
            GRef<u32>(ext + 4) |= 0x40;
            GRef<u8>(ext + 0xD) = 0;
        } else {
            GRef<u8>(ext + 0xD) = 0;
        }
    } else {
        u8 count = GRef<u8>(ext + 0xD);
        GRef<u32>(ext + 4) &= 0xFFFFFFBFu;
        if (count < 0x10) {
            count = static_cast<u8>(count + 1);
            GRef<u8>(ext + 0xD) = count;
        }
        if (count == 0xF) GRef<u32>(ext + 4) |= 8;
        if (count > 0xE) GRef<u32>(ext) |= 8;
    }
}

// EE: 0x00102010
s32 SendPadVibration(u32 pad, u32 rec) {
    GRef<u8>(rec + 0x5E) = GRef<u8>(pad + 0x45);
    GRef<u8>(rec + 0x5F) = GRef<u8>(pad + 0x46);
    scePadSetActDirect(GRef<s32>(rec), GRef<s32>(rec + 4), rec + 0x5E);
    return 0;
}

// EE: 0x00102050
s32 PadSystemInit() {
    BindUsbJoyRpc();
    scePadInit(0);
    for (s32 port = 0; port < 2; ++port) {
        InitPadPort(port, 0, kPadPorts + 100u * static_cast<u32>(port));
        const s32 ok = port == 0 ? scePadPortOpen(0, 0, 0x002A0080) : scePadPortOpen(port, 0, 0x002A0180);
        if (ok == 0) {
            const u64 args[2] = {static_cast<u64>(port), 0};
            ee::printf(0x004039F8 /* "ERROR: scePadPortOpen %d:%d\n" */, args, 2);
            return 0;
        }
        const u32 pad = g_pMainContext().addr + 0x210 + 0x50u * static_cast<u32>(port);
        GRef<u8>(pad) = 0x80;
        for (u32 i = 1; i < 0x14; ++i) GRef<u8>(pad + i) = 0;
        for (s32 i = 0x1F; i >= 0; --i) GRef<u8>(pad + 0x14 + static_cast<u32>(i)) = 0;
        for (u32 i = 0; i < 4; ++i) {
            GRef<u8>(pad + 0x34 + 4 * i) = 0;
            GRef<u8>(pad + 0x35 + 4 * i) = 0x80;
            GRef<u8>(pad + 0x36 + 4 * i) = 0;
            GRef<u8>(pad + 0x37 + 4 * i) = 0xFF;
        }
        const u32 ext = g_pMainContext().addr + 0x2D0 + 0x20u * static_cast<u32>(port);
        GRef<u8>(ext + 0xE) = 0;
        GRef<u8>(ext + 0xF) = 0;
    }
    ApplyVibrationOptions(0x002A15D0);
    return 0;
}

// EE: 0x001021E8
// Exit path only (unreachable by default).
void ShutdownPads() {
    StopPadVibration(g_pMainContext().addr + 0x210);
    StopPadVibration(g_pMainContext().addr + 0x260);
    scePadEnd();
}

// EE: 0x00102220
// Per frame: USB joystick read, then per port the connection state machine, USB data merged into
// the port record when a USB device answers, conversion, buttons, vibration.
s32 UpdatePads() {
    ReadUsbJoy(0x004121A8);
    for (u32 port = 0; port < 2; ++port) {
        UpdatePadConnection(kPadPorts + port * 100);
        // Port 1 takes the second USB device record, port 0 the first (as the original).
        const u32 usb = port == 1 ? 0x004121A9 : 0x004121BD;
        if (GRef<u8>(usb) == 0) {
            const u32 rec = kPadPorts + port * 100;
            GRef<u32>(rec) = port;
            GRef<u32>(rec + 4) = 0;
            GRef<u32>(rec + 8) = 99;
            GRef<u32>(rec + 0xC) = 7;
            GRef<u32>(rec + 0x10) = GRef<u8>(usb + 1);
            GRef<u32>(rec + 0x14) = GRef<u8>(usb + 1);
            GRef<u8>(rec + 0x38) = 0;
            GRef<u8>(rec + 0x39) = GRef<u8>(usb + 1);
            s32 a = static_cast<s32>(GRef<u8>(usb + 0x13)) - 0x20;
            if (a < 0) a = 0;
            s32 b = static_cast<s32>(GRef<u8>(usb + 0x12)) - 0x20;
            if (b < 0) b = 0;
            GRef<u8>(usb + 0x13) = static_cast<u8>((a * 0xFF) / 0xDF);
            GRef<u8>(usb + 0x12) = static_cast<u8>((b * 0xFF) / 0xDF);
            for (u32 i = 0; i < 0x12; ++i) GRef<u8>(rec + 0x3A + i) = GRef<u8>(usb + 2 + i);
        }
        const u32 pad = g_pMainContext().addr + 0x210 + port * 0x50;
        ConvertPadInput(kPadPorts + port * 100, pad);
        UpdatePadButtonEdges(g_pMainContext().addr + 0x210 + port * 0x50, g_pMainContext().addr + 0x2B0 + port * 0x10);
        UpdateMappedPadButtons(g_pMainContext().addr + 0x210 + port * 0x50, g_pMainContext().addr + 0x2D0 + port * 0x20,
                               0x0029FFC0 + port * 0x50);
        TickPadVibration(g_pMainContext().addr + 0x210 + port * 0x50);
        SendPadVibration(g_pMainContext().addr + 0x210 + port * 0x50, kPadPorts + port * 100);
    }
    return 0;
}

// EE: 0x00102460
// Dead (no reference): sets pad byte +0x44 and stops the vibration.
s32 func_00102460(u32 pad) {
    GRef<u8>(pad + 0x44) = 1;
    StopPadVibration(pad);
    return 0;
}

// EE: 0x00102488
s32 StopPadVibration(u32 pad) {
    for (u32 i = 0; i < 2; ++i) {
        GRef<u8>(pad + 0x45 + i) = 0;
        GRef<u32>(pad + 0x48 + 4 * i) = 0;
    }
    return 0;
}

// EE: 0x001024C0
s32 SetPadVibrationEnabled(u32 pad, s64 enable, s64 test) {
    StopPadVibration(pad);
    u8 flags = GRef<u8>(pad + 0x44);
    flags = enable == 0 ? (flags | 1) : (flags & 0xFE);
    GRef<u8>(pad + 0x44) = flags;
    if (test == 1) StartPadVibration(GRef<f32>(0x0040DA04), pad);
    return 0;
}

// EE: 0x00102530
s32 StartPadVibration(f32 seconds, u32 pad) {
    if (GRef<s32>(pad) == 6) return 0;  // USB joystick: no motors
    GRef<u8>(pad + 0x46) = 0xFF;
    GRef<u8>(pad + 0x45) = 1;
    const s32 frames = fpu::FloatToInt(fpu::Mul(seconds, 60.0f));
    GRef<s32>(pad + 0x4C) = frames;
    GRef<s32>(pad + 0x48) = frames;
    return 0;
}

// EE: 0x001025C8
// Vibration request: motor 0 on/off and motor 1 strength for 16 frames, unless vibration is
// disabled (+0x44 bit 0) or the pad is a USB joystick (type 6).
s32 func_001025C8(u32 pad, u8 on, u8 strength) {
    if ((GRef<u8>(pad + 0x44) & 1) != 0 || GRef<s32>(pad) == 6) return 0;
    GRef<u8>(pad + 0x45) = on != 0 ? 1 : 0;
    GRef<u8>(pad + 0x46) = strength;
    GRef<s32>(pad + 0x4C) = 0x10;
    GRef<s32>(pad + 0x48) = 0x10;
    return 0;
}

// EE: 0x00102620
s32 TickPadVibration(u32 pad) {
    for (u32 i = 0; i < 2; ++i) {
        s32& timer = GRef<s32>(pad + 0x48 + 4 * i);
        if (timer == 0) {
            GRef<u8>(pad + 0x45 + i) = 0;
        } else {
            timer -= 1;
        }
    }
    return 0;
}

// EE: 0x00102578
// Dead (no reference): raises motor `motor`'s strength (+0x45 + motor) to `strength` with timer
// `frames` when the new strength is higher; nothing when vibration is disabled or on a USB joystick.
s32 func_00102578(u32 pad, s32 motor, u8 strength, u32 frames) {
    if ((GRef<u8>(pad + 0x44) & 1) != 0) return 0;
    if (GRef<s32>(pad) == 6) return 0;
    u8& cur = GRef<u8>(pad + 0x45 + static_cast<u32>(motor));
    if (cur < strength) {
        cur = strength;
        GRef<u32>(pad + 0x48 + 4u * static_cast<u32>(motor)) = frames;
    }
    return 0;
}

}  // namespace ac04::game
