#include "platform/input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "platform/log.h"

namespace ac04::platform {
namespace {

constexpr const char* kControlNames[kControlCount] = {
    "select", "l3", "r3", "start", "up", "right", "down", "left",
    "l2", "r2", "l1", "r1", "triangle", "circle", "cross", "square",
    "lx-", "lx+", "ly-", "ly+", "rx-", "rx+", "ry-", "ry+"};

// Pressure slot of each digital button (-1 = no pressure value): right left up down triangle
// circle cross square L1 R1 L2 R2.
constexpr int kPressureSlot[16] = {-1, -1, -1, -1, 2, 0, 3, 1, 10, 11, 8, 9, 4, 5, 6, 7};

// Host input names of the binding texts. Our own table (not SDL's mapping-string names), so the
// names stay stable and describe the button position, not a vendor label.
struct NamedButton {
    const char* name;
    SDL_GamepadButton button;
};
constexpr NamedButton kPadButtons[] = {
    {"south", SDL_GAMEPAD_BUTTON_SOUTH},          {"east", SDL_GAMEPAD_BUTTON_EAST},
    {"west", SDL_GAMEPAD_BUTTON_WEST},            {"north", SDL_GAMEPAD_BUTTON_NORTH},
    {"back", SDL_GAMEPAD_BUTTON_BACK},            {"guide", SDL_GAMEPAD_BUTTON_GUIDE},
    {"start", SDL_GAMEPAD_BUTTON_START},          {"leftstick", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"rightstick", SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {"leftshoulder", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"rightshoulder", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"dpup", SDL_GAMEPAD_BUTTON_DPAD_UP},
    {"dpdown", SDL_GAMEPAD_BUTTON_DPAD_DOWN},     {"dpleft", SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {"dpright", SDL_GAMEPAD_BUTTON_DPAD_RIGHT}};

struct NamedAxis {
    const char* name;
    SDL_GamepadAxis axis;
};
constexpr NamedAxis kPadAxes[] = {
    {"leftx", SDL_GAMEPAD_AXIS_LEFTX},   {"lefty", SDL_GAMEPAD_AXIS_LEFTY},
    {"rightx", SDL_GAMEPAD_AXIS_RIGHTX}, {"righty", SDL_GAMEPAD_AXIS_RIGHTY},
    {"lefttrigger", SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {"righttrigger", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}};

struct NamedMouseButton {
    const char* name;
    int button;
};
constexpr NamedMouseButton kMouseButtons[] = {{"left", SDL_BUTTON_LEFT},
                                              {"right", SDL_BUTTON_RIGHT},
                                              {"middle", SDL_BUTTON_MIDDLE},
                                              {"x1", SDL_BUTTON_X1},
                                              {"x2", SDL_BUTTON_X2}};

// Neutral 0x7F like PCSX2's DualShock 2 (run 06: a centred stick reads 0x7F); full deflection
// reaches 0x00 and 0xFF.
std::uint8_t StickByte(float negative, float positive) {
    const float d = positive - negative;
    const float v = 127.0f + (d >= 0.0f ? d * 128.0f : d * 127.0f);
    return static_cast<std::uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
}

// The control a keyboard or mouse input is placed on so that the Modern swap (L1 <-> L2, R1 <-> R2,
// applied to the latched pad) lands it on its action's control.
int ModernPartner(int control) {
    switch (control) {
        case kL1: return kL2;
        case kL2: return kL1;
        case kR1: return kR2;
        case kR2: return kR1;
        default: return control;
    }
}

}  // namespace

const char* ControlName(int control) {
    return control >= 0 && control < kControlCount ? kControlNames[control] : "?";
}

InputBackend::InputBackend() = default;

InputBackend::~InputBackend() {
    for (auto& pad : pads_) {
        if (pad) SDL_CloseGamepad(pad);
        pad = nullptr;
    }
}

bool ParseInputSource(const std::string& text, InputSource& source) {
    const auto colon = text.find(':');
    if (colon == std::string::npos) return false;
    const std::string kind = text.substr(0, colon);
    std::string name = text.substr(colon + 1);
    if (name.empty()) return false;
    if (kind == "key") {
        const SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
        if (sc == SDL_SCANCODE_UNKNOWN) return false;
        source = {InputSource::Key, sc, +1};
        return true;
    }
    if (kind == "pad") {
        for (const auto& e : kPadButtons) {
            if (name == e.name) {
                source = {InputSource::PadButton, e.button, +1};
                return true;
            }
        }
        return false;
    }
    if (kind == "mouse") {
        for (const auto& e : kMouseButtons) {
            if (name == e.name) {
                source = {InputSource::MouseButton, e.button, +1};
                return true;
            }
        }
        return false;
    }
    if (kind == "wheel") {
        if (name != "up" && name != "down") return false;
        source = {InputSource::MouseWheel, 0, name == "up" ? +1 : -1};
        return true;
    }
    int direction = 0;
    if (name.back() == '-' || name.back() == '+') {
        direction = name.back() == '-' ? -1 : +1;
        name.pop_back();
    }
    if (kind == "axis") {
        // Earlier builds read "axis:<name>" without a sign as the positive half: kept.
        if (direction == 0) direction = +1;
        for (const auto& e : kPadAxes) {
            if (name == e.name) {
                source = {InputSource::Axis, e.axis, direction};
                return true;
            }
        }
        return false;
    }
    // Raw joystick inputs of the port's gamepad, by SDL index (0-255).
    if (kind != "joyaxis" && kind != "joybutton") return false;
    if (name.empty() || name.size() > 3 ||
        !std::all_of(name.begin(), name.end(), [](char c) { return c >= '0' && c <= '9'; }))
        return false;
    const int index = std::stoi(name);
    if (index > 255) return false;
    if (kind == "joybutton") {
        if (direction != 0) return false;
        source = {InputSource::JoyButton, index, +1};
    } else {
        source = {InputSource::JoyAxis, index, direction};
    }
    return true;
}

void PrepareInputHints() {
    // DualShock 3 on Windows through Sony's sixaxis.sys or DsHidMini in its SXS mode: SDL's HIDAPI
    // driver then reports the pressures of the face buttons, d-pad and L1/R1 as joystick axes
    // (SDL keeps this driver off on Windows unless asked). PS3-compatible third-party pads (also
    // DualShock 2 adapters that present themselves as one) use SDL's third-party PS3 driver, which
    // is on by default.
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS3_SIXAXIS_DRIVER, "1");
}

void MouseStick::Step(float dx, float dy, const MouseSettings& settings) {
    // Return to centre: each tick keeps 80 % of the deflection and adds the motion, so a steady
    // 12 pixels per tick (720 per second) at sensitivity 1 holds full deflection and the stick is
    // back near the centre about 0.2 s after the mouse stops. Without: 400 pixels from the centre
    // to full deflection.
    constexpr float kKeep = 0.8f;
    constexpr float kSpeedGain = (1.0f - kKeep) / 12.0f;
    constexpr float kHoldGain = 1.0f / 400.0f;
    if (settings.invert_vertical) dy = -dy;
    if (settings.return_to_centre) {
        const float k = settings.sensitivity * kSpeedGain;
        x_ = x_ * kKeep + dx * k;
        y_ = y_ * kKeep + dy * k;
    } else {
        const float k = settings.sensitivity * kHoldGain;
        x_ += dx * k;
        y_ += dy * k;
    }
    x_ = std::clamp(x_, -1.0f, 1.0f);
    y_ = std::clamp(y_, -1.0f, 1.0f);
}

void WheelPresses::Add(int notches) {
    pending_ = std::min(pending_ + notches, 8);  // a fast spin does not queue presses for seconds
}

void WheelPresses::Step() {
    if (phase_ == 0 && pending_ > 0) {
        --pending_;
        phase_ = 3;
    }
    pressed_ = phase_ >= 2;
    if (phase_ > 0) --phase_;
}

void InputBackend::ApplySettings(const InputSettings& input) { modern_ = input.modern_controls; }

void InputBackend::ApplyControls(const ControlBindings& controls) { controls_ = controls; }

bool InputBackend::MouseSteers() const {
    return controls_.mouse.horizontal != MouseTarget::None || controls_.mouse.vertical != MouseTarget::None;
}

void InputBackend::Start() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetJoysticks(&count);
    if (count == 0) AC04_LOG_INFO("input", "no joystick or gamepad connected (keyboard on port 1)");
    for (int i = 0; i < count; ++i) {
        const char* name = SDL_GetJoystickNameForID(ids[i]);
        const bool gamepad = SDL_IsGamepad(ids[i]);
        AC04_LOG_INFO("input", "device %d: '%s' (%s)", i, name ? name : "?",
                      gamepad ? "gamepad" : "no SDL gamepad mapping: not used");
    }
    SDL_free(ids);
    OpenGamepads();
}

void InputBackend::OpenGamepads() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count && i < 2; ++i) {
        if (!pads_[i]) {
            pads_[i] = SDL_OpenGamepad(ids[i]);
            if (pads_[i]) {
                FindPressureAxes(i);
                if (pressure_count_[i] > 0) {
                    AC04_LOG_INFO("input", "port %d: gamepad '%s', pressure-sensitive buttons (%d)", i + 1,
                                  SDL_GetGamepadName(pads_[i]), pressure_count_[i]);
                } else {
                    AC04_LOG_INFO("input", "port %d: gamepad '%s'", i + 1, SDL_GetGamepadName(pads_[i]));
                }
            }
        }
    }
    SDL_free(ids);
}

// SDL's HIDAPI PS3 drivers (SDL_hidapi_ps3.c: DualShock 3 with sixaxis.sys or DsHidMini SXS, and
// PS3-compatible third-party pads) report the pressures as joystick axes 6-15 in the order of the
// SDL gamepad buttons, skipping back, guide, start and the stick buttons; -32768 = released,
// 32767 = full. L2/R2 come as the trigger axes. Only those drivers are trusted with this layout:
// their joystick GUID carries 'h' (SDL's HIDAPI marker) at byte 14.
void InputBackend::FindPressureAxes(int port) {
    pressure_axes_[port].fill(-1);
    pressure_count_[port] = 0;
    SDL_Gamepad* pad = pads_[port];
    SDL_Joystick* joy = pad ? SDL_GetGamepadJoystick(pad) : nullptr;
    if (joy == nullptr || SDL_GetGamepadType(pad) != SDL_GAMEPAD_TYPE_PS3) return;
    if (SDL_GetJoystickGUID(joy).data[14] != 'h' || SDL_GetNumJoystickAxes(joy) < 16) return;
    constexpr SDL_GamepadButton kOrder[10] = {
        SDL_GAMEPAD_BUTTON_SOUTH,   SDL_GAMEPAD_BUTTON_EAST,      SDL_GAMEPAD_BUTTON_WEST,
        SDL_GAMEPAD_BUTTON_NORTH,   SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT};
    for (int i = 0; i < 10; ++i) pressure_axes_[port][kOrder[i]] = 6 + i;
    pressure_count_[port] = 12;  // the ten above and the two triggers
}

PadDeviceInfo InputBackend::DeviceInfo(int port) const {
    PadDeviceInfo info;
    if (port < 0 || port > 1 || pads_[port] == nullptr) return info;
    const char* name = SDL_GetGamepadName(pads_[port]);
    info.name = name ? name : "?";
    info.pressure_buttons = pressure_count_[port];
    return info;
}

void InputBackend::HandleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        OpenGamepads();
    } else if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int i = 0; i < 2; ++i) {
            if (pads_[i] && SDL_GetGamepadID(pads_[i]) == event.gdevice.which) {
                AC04_LOG_INFO("input", "port %d: gamepad removed", i + 1);
                SDL_CloseGamepad(pads_[i]);
                pads_[i] = nullptr;
                rumble_[i] = {};
                pressure_axes_[i].fill(-1);
                pressure_count_[i] = 0;
            }
        }
    } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
        int notches = event.wheel.integer_y;
        if (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) notches = -notches;
        if (notches > 0) wheel_[0].Add(notches);
        if (notches < 0) wheel_[1].Add(-notches);
    }
}

void InputBackend::StepTick(bool mouse_steers) {
    float dx = 0.0f, dy = 0.0f;
    SDL_GetRelativeMouseState(&dx, &dy);  // the motion since the last tick (also read when not steering)
    if (mouse_steers) mouse_.Step(dx, dy, controls_.mouse);
    else mouse_.Reset();
    for (WheelPresses& w : wheel_) w.Step();
}

namespace {

// A raw joystick axis as 0..1: the full axis from its minimum (direction 0), or one half from the
// centre.
float JoyAxisLevel(Sint16 raw, int direction) {
    if (direction == 0) return (static_cast<float>(raw) + 32768.0f) / 65535.0f;
    return std::clamp(static_cast<float>(raw) / 32767.0f * static_cast<float>(direction), 0.0f, 1.0f);
}

}  // namespace

ControlLevel InputBackend::Sample(const Sources& sources, int port, bool use_keyboard) const {
    ControlLevel out;
    const bool* keys = use_keyboard ? SDL_GetKeyboardState(nullptr) : nullptr;
    const SDL_MouseButtonFlags mouse = use_keyboard ? SDL_GetMouseState(nullptr, nullptr) : 0;
    SDL_Gamepad* pad = pads_[port];
    SDL_Joystick* joy = pad ? SDL_GetGamepadJoystick(pad) : nullptr;
    for (const InputSource& s : sources) {
        switch (s.kind) {
            case InputSource::Key: {
                const bool on = keys && keys[s.code];
                out.Add(on, on ? 1.0f : 0.0f);
                break;
            }
            case InputSource::PadButton: {
                const bool on = pad && SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(s.code));
                const int axis = s.code >= 0 && s.code < 15 ? pressure_axes_[port][s.code] : -1;
                if (axis >= 0 && joy != nullptr) {
                    // The device's pressure; a press it reports keeps at least 1/255, so a pressed
                    // button never reads pressure 0.
                    float level = JoyAxisLevel(SDL_GetJoystickAxis(joy, axis), 0);
                    if (on) level = std::max(level, 1.0f / 255.0f);
                    out.Add(on, level);
                } else {
                    out.Add(on, on ? 1.0f : 0.0f);
                }
                break;
            }
            case InputSource::Axis:
                if (pad) {
                    const float a = SDL_GetGamepadAxis(pad, static_cast<SDL_GamepadAxis>(s.code)) / 32767.0f;
                    const float v = std::clamp(a * static_cast<float>(s.direction), 0.0f, 1.0f);
                    // A digital press needs more than half travel (triggers, analog bindings).
                    out.Add(v > 0.5f, v);
                }
                break;
            case InputSource::JoyAxis:
                if (joy != nullptr && s.code < SDL_GetNumJoystickAxes(joy)) {
                    const float v = JoyAxisLevel(SDL_GetJoystickAxis(joy, s.code), s.direction);
                    out.Add(v > 0.5f, v);
                }
                break;
            case InputSource::JoyButton: {
                const bool on = joy != nullptr && s.code < SDL_GetNumJoystickButtons(joy) &&
                                SDL_GetJoystickButton(joy, s.code);
                out.Add(on, on ? 1.0f : 0.0f);
                break;
            }
            case InputSource::MouseButton: {
                const bool on = (mouse & SDL_BUTTON_MASK(s.code)) != 0;
                out.Add(on, on ? 1.0f : 0.0f);
                break;
            }
            case InputSource::MouseWheel: {
                const bool on = use_keyboard && wheel_[s.direction > 0 ? 0 : 1].Pressed();
                out.Add(on, on ? 1.0f : 0.0f);
                break;
            }
        }
    }
    return out;
}

// True when one of the sources gives an in-between level (a pressure or an axis) on `port`.
bool InputBackend::HasPressure(const Sources& sources, int port) const {
    if (pads_[port] == nullptr) return false;
    for (const InputSource& s : sources) {
        if (s.kind == InputSource::Axis || s.kind == InputSource::JoyAxis) return true;
        if (s.kind == InputSource::PadButton && s.code >= 0 && s.code < 15 && pressure_axes_[port][s.code] >= 0)
            return true;
    }
    return false;
}

// The mouse stick on the controls its axes steer (port 1, keyboard side).
void InputBackend::AddMouseAxes(std::array<ControlLevel, kControlCount>& host) const {
    const auto add = [&host](MouseTarget target, bool vertical, float v) {
        int neg = -1, pos = -1;
        switch (target) {
            case MouseTarget::None: return;
            case MouseTarget::Roll: neg = kLxMinus; pos = kLxPlus; break;
            case MouseTarget::Pitch: neg = kLyMinus; pos = kLyPlus; break;
            case MouseTarget::Yaw: neg = kL2; pos = kR2; break;
            case MouseTarget::Camera:
                neg = vertical ? kRyMinus : kRxMinus;
                pos = vertical ? kRyPlus : kRxPlus;
                break;
        }
        const float n = std::max(0.0f, -v), p = std::max(0.0f, v);
        host[neg].Add(n > 0.5f, n);
        host[pos].Add(p > 0.5f, p);
    };
    add(controls_.mouse.horizontal, false, mouse_.X());
    add(controls_.mouse.vertical, true, mouse_.Y());
}

void InputBackend::SetVibration(int port, bool small_on, std::uint8_t large) {
    if (port < 0 || port > 1) return;
    SDL_Gamepad* pad = pads_[port];
    Rumble& r = rumble_[port];
    if (!pad) {
        r = {};
        return;
    }
    // The large motor is the low-frequency one; the small motor has no strength, only on/off.
    const std::uint16_t low = static_cast<std::uint16_t>(large * 257u);
    const std::uint16_t high = small_on ? 0xFFFF : 0;
    // SDL stops a rumble after its duration: running motors are sent again every 500 ms with a
    // 1 s duration, so a stalled frame loop never leaves the pad shaking for long.
    constexpr std::uint64_t kRefreshMs = 500;
    constexpr std::uint32_t kDurationMs = 1000;
    const std::uint64_t now = SDL_GetTicks();
    const bool changed = low != r.low || high != r.high;
    const bool running = low != 0 || high != 0;
    if (!changed && !(running && now - r.sent_ms >= kRefreshMs)) return;
    if (!changed && !running) return;
    if (!SDL_RumbleGamepad(pad, low, high, running ? kDurationMs : 0)) {
        log::WriteOnce("input:rumble", log::Level::Info, "input", "port %d: the gamepad has no rumble (%s)", port + 1,
                       SDL_GetError());
    } else if (running) {
        log::WriteOnce("input:rumble-on", log::Level::Info, "input", "port %d: vibration started", port + 1);
    }
    r.low = low;
    r.high = high;
    r.sent_ms = now;
}

void InputBackend::Update(bool keyboard_captured) {
    for (int port = 0; port < 2; ++port) {
        SDL_Gamepad* pad = pads_[port];
        const bool use_keyboard = port == 0 && !keyboard_captured;
        PadState& st = ports_[port];
        st.connected = port == 0 || pad != nullptr;
        st.buttons = 0xFFFF;
        st.pressure.fill(0);
        // Keyboard and mouse (port 1): by action, placed so that the Modern swap keeps the action.
        std::array<ControlLevel, kControlCount> host{};
        if (port == 0) {
            for (int c = 0; c < kControlCount; ++c) host[c] = Sample(controls_.keyboard[c], 0, use_keyboard);
            AddMouseAxes(host);
        }
        std::array<ControlLevel, kControlCount> level{};
        for (int c = 0; c < kControlCount; ++c) {
            level[c] = Sample(controls_.gamepad[c], port, use_keyboard);
            level[c].Add(host[modern_[port] ? ModernPartner(c) : c]);
        }
        for (int c = 0; c < 16; ++c) {
            if (level[c].pressed) st.buttons &= static_cast<std::uint16_t>(~(1u << c));
            if (kPressureSlot[c] >= 0)
                st.pressure[kPressureSlot[c]] = static_cast<std::uint8_t>(std::lround(level[c].level * 255.0f));
        }
        st.lx = StickByte(level[kLxMinus].level, level[kLxPlus].level);
        st.ly = StickByte(level[kLyMinus].level, level[kLyPlus].level);
        st.rx = StickByte(level[kRxMinus].level, level[kRxPlus].level);
        st.ry = StickByte(level[kRyMinus].level, level[kRyPlus].level);
        // The map button holds with a pressure (DualShock 3, pressure adapters); otherwise each
        // press steps the map as with the original's analog-mode pad.
        st.map_cycle = st.connected && !HasPressure(controls_.gamepad[kSquare], port);
    }
}

}  // namespace ac04::platform
