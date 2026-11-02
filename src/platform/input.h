// Input backend (approved baseline: rebinding). Host keyboard, mouse and SDL3 gamepads are mapped
// onto the exact DualShock 2 data that scePadRead returns; the game's own ConvertPadInput is
// unchanged. The bindings come from controls.ini (platform/controls_file.h, 2026-10-31).
//
// Pressure-sensitive buttons (2026-10-31, docs/port/port-features.md §1 "Pressure"): the game puts
// a DualShock 2 into pressure mode and reads the pressure of L1/R1 (throttle), L2/R2 (yaw) and
// square (map zoom). A pad whose SDL driver reports its buttons' pressures as extra joystick axes
// (SDL's HIDAPI PS3 drivers: DualShock 3, and PS3-compatible pads such as DualShock 2 adapters
// that present themselves as one) feeds those pressures automatically, following the gamepad
// bindings; any other device can feed a pressure with a `joyaxis:` binding. Keys and plain buttons
// give 0 or 255.
//
// Keyboard and mouse (2026-10-31, User request): their bindings name actions (throttle up, missile,
// …) resolved to the PS2 controls of the Classic NORMAL layout, and they keep their action under
// the Modern control types (the backend places them so that the Modern swap lands them back). The
// mouse is a stick (roll, pitch, yaw or camera) that steers only in flight.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "platform/settings.h"

union SDL_Event;
struct SDL_Gamepad;

namespace ac04::platform {

// One controller in PS2 terms.
struct PadState {
    bool connected = false;
    // Digital buttons, active-low as in the scePadRead buffer bytes 2–3:
    //   byte 2: select L3 R3 start up right down left (bits 0..7)
    //   byte 3: L2 R2 L1 R1 triangle circle cross square (bits 0..7)
    std::uint16_t buttons = 0xFFFF;
    std::uint8_t rx = 0x7F, ry = 0x7F, lx = 0x7F, ly = 0x7F;  // neutral as in PCSX2
    // Pressure (0 = released, 255 = full) in scePadRead order:
    // right left up down triangle circle cross square L1 R1 L2 R2.
    std::array<std::uint8_t, 12> pressure{};
    // Not pad data: the port's digital map cycle for this player (port_hooks.h InputHooks), on
    // when the devices give square no pressure. Recorded in input logs with the pad.
    bool map_cycle = false;
};

// The host device on a port, for the debug overlay's input panel.
struct PadDeviceInfo {
    std::string name;            // empty: no gamepad (port 1 then has the keyboard only)
    int pressure_buttons = 0;    // buttons whose pressure the device reports (0: digital only)
};

// SDL hints the input backend needs; call before SDL_Init. Environment variables of the same name
// still win (SDL's hint priority), so a player can switch them off.
void PrepareInputHints();

// PS2 controls in a fixed order: the first 16 are the digital buttons in scePadRead bit order
// (bytes 2 and 3), the last 8 are the analog stick half-axes.
enum Control {
    kSelect, kL3, kR3, kStart, kUp, kRight, kDown, kLeft,
    kL2, kR2, kL1, kR1, kTriangle, kCircle, kCross, kSquare,
    kLxMinus, kLxPlus, kLyMinus, kLyPlus, kRxMinus, kRxPlus, kRyMinus, kRyPlus,
    kControlCount
};
// "select", "l3", …, "ry+".
const char* ControlName(int control);

// One host input of a binding: "key:Z", "pad:south", "axis:lefttrigger+", "joyaxis:7",
// "joyaxis:2-", "joybutton:4", "mouse:left", "wheel:up". False when the text is not one.
struct InputSource {
    enum Kind { Key, PadButton, Axis, JoyAxis, JoyButton, MouseButton, MouseWheel } kind = Key;
    int code = 0;        // SDL_Scancode / SDL_GamepadButton / SDL_GamepadAxis / joystick axis or button index / SDL mouse button
    int direction = +1;  // Axis, JoyAxis: +1 or -1 (half axis); JoyAxis 0: the full axis, resting at its minimum; MouseWheel: +1 up, -1 down
    bool operator==(const InputSource& o) const {
        return kind == o.kind && code == o.code && direction == o.direction;
    }
};
bool ParseInputSource(const std::string& text, InputSource& source);

// What a mouse axis steers (controls.ini [Mouse]).
enum class MouseTarget { None, Roll, Pitch, Yaw, Camera };

struct MouseSettings {
    MouseTarget horizontal = MouseTarget::Roll;
    MouseTarget vertical = MouseTarget::Pitch;
    float sensitivity = 1.0f;       // 0.1-10
    bool invert_vertical = false;   // off: mouse forward = stick forward (nose down)
    bool return_to_centre = true;   // on: the stick follows the mouse's speed; off: it stays where it was moved
    bool operator==(const MouseSettings& o) const {
        return horizontal == o.horizontal && vertical == o.vertical && sensitivity == o.sensitivity &&
               invert_vertical == o.invert_vertical && return_to_centre == o.return_to_centre;
    }
};

// The bindings of controls.ini, by PS2 control.
struct ControlBindings {
    // [Gamepad]: host inputs of each PS2 control; the Modern control types swap them as a pad.
    std::array<std::vector<InputSource>, kControlCount> gamepad;
    // [Keyboard]: the actions' inputs, resolved to the PS2 control of the Classic NORMAL layout;
    // they keep their action under the Modern control types. Port 1 only.
    std::array<std::vector<InputSource>, kControlCount> keyboard;
    MouseSettings mouse;
    bool operator==(const ControlBindings& o) const {
        return gamepad == o.gamepad && keyboard == o.keyboard && mouse == o.mouse;
    }
};

// One control's value from its sources: `pressed` is the digital bit, `level` the pressure or stick
// deflection 0..1. A digital source gives 0 or 1; an analog source (axis, or a button with its
// pressure axis) gives its level and counts as pressed above half travel, except a button with a
// pressure axis, whose own digital bit counts (a DualShock reports a light press as pressed).
struct ControlLevel {
    bool pressed = false;
    float level = 0.0f;
    void Add(bool source_pressed, float source_level) {
        pressed = pressed || source_pressed;
        level = source_level > level ? source_level : level;
    }
    void Add(const ControlLevel& o) { Add(o.pressed, o.level); }
};

// The mouse as a stick, one game tick at a time (pixels of relative motion in, deflection -1..1
// out). With return to centre the deflection follows the mouse's speed and falls back when it
// stops; without, it stays where the mouse moved it.
class MouseStick {
public:
    void Step(float dx, float dy, const MouseSettings& settings);
    void Reset() { x_ = y_ = 0.0f; }
    float X() const { return x_; }
    float Y() const { return y_; }

private:
    float x_ = 0.0f, y_ = 0.0f;
};

// Wheel notches as button presses the game can see: each is pressed for 2 ticks, then released for
// 1 tick before the next queued notch.
class WheelPresses {
public:
    void Add(int notches);
    void Step();
    bool Pressed() const { return pressed_; }

private:
    int pending_ = 0;
    int phase_ = 0;
    bool pressed_ = false;
};

class InputBackend {
public:
    InputBackend();
    ~InputBackend();

    // Logs every joystick SDL sees (name, gamepad mapping yes/no) and opens the gamepads (call once after SDL_Init).
    void Start();
    void ApplySettings(const InputSettings& input);  // Modern control types
    void ApplyControls(const ControlBindings& controls);
    void HandleEvent(const SDL_Event& event);  // gamepad hot-plug, mouse wheel
    // Once per game tick, before Update: the mouse stick and wheel presses advance. `mouse_steers`:
    // the mouse is captured for flight (otherwise the stick stays centred).
    void StepTick(bool mouse_steers);
    // Samples keyboard, mouse and gamepads; keyboard and mouse buttons are ignored while
    // `keyboard_captured` (overlay focus).
    void Update(bool keyboard_captured);

    const PadState& Port(int port) const { return ports_[port]; }
    PadDeviceInfo DeviceInfo(int port) const;
    // True when a mouse axis steers something (controls.ini [Mouse]).
    bool MouseSteers() const;

    // Vibration of a DualShock 2 on the gamepad of `port`: small motor on/off (high-frequency
    // rumble) and large motor strength 0-255 (low-frequency rumble). Call once per presented
    // frame; the device is only told about changes (and refreshed before its rumble times out).
    void SetVibration(int port, bool small_on, std::uint8_t large);

private:
    using Sources = std::vector<InputSource>;
    // SDL gamepad buttons (SDL_GAMEPAD_BUTTON_SOUTH .. DPAD_RIGHT) -> joystick axis with its
    // pressure, -1 = none.
    using PressureAxes = std::array<int, 15>;
    void OpenGamepads();
    void FindPressureAxes(int port);
    ControlLevel Sample(const Sources& sources, int port, bool use_keyboard) const;
    bool HasPressure(const Sources& sources, int port) const;
    void AddMouseAxes(std::array<ControlLevel, kControlCount>& host) const;

    std::array<PadState, 2> ports_{};
    std::array<SDL_Gamepad*, 2> pads_{};
    std::array<PressureAxes, 2> pressure_axes_{};
    std::array<int, 2> pressure_count_{};
    struct Rumble {
        std::uint16_t low = 0, high = 0;
        std::uint64_t sent_ms = 0;  // SDL_GetTicks of the last SDL_RumbleGamepad, 0 = none
    };
    std::array<Rumble, 2> rumble_{};
    ControlBindings controls_{};
    std::array<bool, 2> modern_{};
    MouseStick mouse_;
    std::array<WheelPresses, 2> wheel_{};  // up, down
};

}  // namespace ac04::platform
