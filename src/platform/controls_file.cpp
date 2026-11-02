#include "platform/controls_file.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "platform/log.h"

namespace ac04::platform {
namespace {

// The default file. Keyboard: the ACE COMBAT 7 PC layout as community lists give it (StrategyWiki,
// DefKey, Migman; User choice 2026-10-31); the keys those lists do not agree on (change target, map,
// views, level aircraft) are our choice until the User checks them in AC7's key settings.
// Gamepad: the layout of earlier builds.
constexpr const char* kDefaultText = R"(; ProjectStonehenge controls: keyboard, mouse and gamepad
;
; HOW TO CHANGE A CONTROL
;   Missile = Space                one input
;   Missile = Space, Mouse Right   several inputs, separated by commas
;   Map =                          no input
;   Save the file: the change applies at once, also while you play.
;   Lines starting with ; are notes. Delete this file to get the defaults back.
;
; INPUT NAMES (upper or lower case, both work)
;   Keys      A ... Z   0 ... 9   F1 ... F12   Space   Enter   Esc   Backspace   Tab
;             Up   Down   Left   Right   Insert   Delete   Home   End   PageUp   PageDown
;             Left Shift   Right Shift   Left Ctrl   Right Ctrl   Left Alt   Right Alt
;             Keypad 0 ... Keypad 9   Keypad Enter   Keypad +   Keypad -
;             Comma   Period   Semicolon   Minus
;   Mouse     Mouse Left   Mouse Right   Mouse Middle   Mouse Back   Mouse Forward
;             Mouse Wheel Up   Mouse Wheel Down
;   Gamepad   South   East   West   North   (face buttons by position: South = A on Xbox, Cross on PlayStation)
;             Left Shoulder   Right Shoulder   Left Trigger   Right Trigger
;             Left Stick Button   Right Stick Button   Start   Back   Guide
;             D-Pad Up   D-Pad Down   D-Pad Left   D-Pad Right
;             Left Stick Up / Down / Left / Right   Right Stick Up / Down / Left / Right
;   Adapters  Joy Axis 7 (whole axis)   Joy Axis 7+ / Joy Axis 7- (half)   Joy Button 4
;   [Mouse] steers   Roll   Pitch   Yaw   Camera   None
;
; Layout version of this file, do not change it: a file of another version is replaced with the defaults.
Version = 1

[Keyboard]
; Flight, as in ACE COMBAT 7 on the PC. Keys and mouse keep their action with every CONTROL TYPE
; (NORMAL or EASY, Modern or Classic); EASY has no yaw, so the yaw keys then do nothing.
Pitch up          = 3              ; nose up (with REVERSE off on CONTROLLER SETUP)
Pitch down        = 1
Roll left         = A
Roll right        = D
Yaw left          = Q
Yaw right         = E
Throttle up       = W
Throttle down     = S
Machine gun       = Mouse Left, Left Ctrl
Missile           = Space, Mouse Right
Change weapon     = Mouse Wheel Up, Mouse Wheel Down, 2
Change target     = R, Mouse Middle
Map               = M              ; each press: radar, map near, map half, whole map, radar
Change view       = V
Rear view         = C
Level aircraft    = X
Look up           = Keypad 8
Look down         = Keypad 2
Look left         = Keypad 4
Look right        = Keypad 6
Left stick button =
Pause             = Esc
; Menus. Menu up and Menu down are the PS2 d-pad, which also levels the aircraft and changes the
; view in flight.
Menu up           = Up
Menu down         = Down
Menu left         = Left
Menu right        = Right
Accept            = Enter, Keypad Enter
Back              = Backspace

[Mouse]
; What the mouse steers in flight: Roll, Pitch, Yaw, Camera or None. The game window keeps the
; mouse pointer only in flight; in menus, while paused and in other windows it is free.
Left/right        = Roll
Up/down           = Pitch
Sensitivity       = 1.0            ; 0.1 to 10
Invert up/down    = no             ; no: mouse forward = nose down, like pushing a stick
Return to centre  = yes            ; yes: stop the mouse and the stick centres; no: it stays where you left it

[Gamepad]
; PS2 button = button of your gamepad, by position: South is the lower face button (A on Xbox,
; Cross on PlayStation). What each PS2 button does is set by CONTROL TYPE on the game's
; CONTROLLER SETUP page.
Cross             = South
Circle            = East
Square            = West
Triangle          = North
L1                = Left Shoulder
R1                = Right Shoulder
L2                = Left Trigger
R2                = Right Trigger
L3                = Left Stick Button
R3                = Right Stick Button
Start             = Start
Select            = Back
Up                = D-Pad Up
Down              = D-Pad Down
Left              = D-Pad Left
Right             = D-Pad Right
Left stick left   = Left Stick Left
Left stick right  = Left Stick Right
Left stick up     = Left Stick Up
Left stick down   = Left Stick Down
Right stick left  = Right Stick Left
Right stick right = Right Stick Right
Right stick up    = Right Stick Up
Right stick down  = Right Stick Down
)";

// [Keyboard] actions and the PS2 control each one is in the Classic NORMAL layout (CONTROLLER
// SETUP's table 0x003AF5B8 / 0x003AF5F0: ○ MISSILE, × VULCAN, △ CHANGE TARGET, □ MAP, L1/R1
// THROTTLE, L2/R2 YAW, SELECT CHANGE WEAPON, START PAUSE, d-pad up LEVEL AIRCRAFT, d-pad down
// CHANGE VIEW, R3 REAR VIEW; the left stick flies, the right stick looks).
struct Action {
    const char* name;
    int control;
};
constexpr Action kActions[] = {
    {"Pitch up", kLyPlus},        {"Pitch down", kLyMinus},      {"Roll left", kLxMinus},
    {"Roll right", kLxPlus},      {"Yaw left", kL2},             {"Yaw right", kR2},
    {"Throttle up", kR1},         {"Throttle down", kL1},        {"Machine gun", kCross},
    {"Missile", kCircle},         {"Change weapon", kSelect},    {"Change target", kTriangle},
    {"Map", kSquare},             {"Change view", kDown},        {"Rear view", kR3},
    {"Level aircraft", kUp},      {"Look up", kRyMinus},         {"Look down", kRyPlus},
    {"Look left", kRxMinus},      {"Look right", kRxPlus},       {"Left stick button", kL3},
    {"Pause", kStart},            {"Menu up", kUp},              {"Menu down", kDown},
    {"Menu left", kLeft},         {"Menu right", kRight},        {"Accept", kCross},
    {"Back", kTriangle}};
constexpr int kActionCount = static_cast<int>(sizeof(kActions) / sizeof(kActions[0]));

// [Gamepad] entries: the PS2 controls.
constexpr const char* kPadControlNames[kControlCount] = {
    "Select", "L3", "R3", "Start", "Up", "Right", "Down", "Left",
    "L2", "R2", "L1", "R1", "Triangle", "Circle", "Cross", "Square",
    "Left stick left", "Left stick right", "Left stick up", "Left stick down",
    "Right stick left", "Right stick right", "Right stick up", "Right stick down"};

// Player spellings of host inputs, normalised (see Normalise), and their binding text.
struct Friendly {
    const char* name;
    const char* source;
};
constexpr Friendly kFriendly[] = {
    {"south", "pad:south"},           {"east", "pad:east"},
    {"west", "pad:west"},             {"north", "pad:north"},
    {"back", "pad:back"},             {"guide", "pad:guide"},
    {"start", "pad:start"},           {"leftshoulder", "pad:leftshoulder"},
    {"rightshoulder", "pad:rightshoulder"}, {"leftstickbutton", "pad:leftstick"},
    {"rightstickbutton", "pad:rightstick"}, {"dpadup", "pad:dpup"},
    {"dpaddown", "pad:dpdown"},       {"dpadleft", "pad:dpleft"},
    {"dpadright", "pad:dpright"},     {"lefttrigger", "axis:lefttrigger+"},
    {"righttrigger", "axis:righttrigger+"}, {"leftstickleft", "axis:leftx-"},
    {"leftstickright", "axis:leftx+"}, {"leftstickup", "axis:lefty-"},
    {"leftstickdown", "axis:lefty+"}, {"rightstickleft", "axis:rightx-"},
    {"rightstickright", "axis:rightx+"}, {"rightstickup", "axis:righty-"},
    {"rightstickdown", "axis:righty+"}, {"mouseleft", "mouse:left"},
    {"mouseright", "mouse:right"},    {"mousemiddle", "mouse:middle"},
    {"mouseback", "mouse:x1"},        {"mousex1", "mouse:x1"},
    {"mouseforward", "mouse:x2"},     {"mousex2", "mouse:x2"},
    {"mousewheelup", "wheel:up"},     {"mousewheeldown", "wheel:down"},
    {"enter", "key:Return"},          {"esc", "key:Escape"},
    {"comma", "key:,"},               {"period", "key:."},
    {"semicolon", "key:;"},           {"minus", "key:-"},
    {"ctrl", "key:Left Ctrl"},        {"shift", "key:Left Shift"},
    {"alt", "key:Left Alt"}};

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Lower case without spaces, '-', '_' and '/': "D-Pad Up" = "dpadup", "Left/right" = "leftright".
std::string Normalise(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '-' || c == '_' || c == '/') continue;
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

// A note starts with ';' or '#' at the start of the line or after a space.
std::string StripNote(const std::string& line) {
    for (std::size_t i = 0; i < line.size(); ++i) {
        if ((line[i] == ';' || line[i] == '#') && (i == 0 || std::isspace(static_cast<unsigned char>(line[i - 1]))))
            return line.substr(0, i);
    }
    return line;
}

bool ParseBool(const std::string& text, bool& out) {
    const std::string n = Normalise(text);
    if (n == "yes" || n == "on" || n == "true" || n == "1") {
        out = true;
        return true;
    }
    if (n == "no" || n == "off" || n == "false" || n == "0") {
        out = false;
        return true;
    }
    return false;
}

bool ParseTarget(const std::string& text, MouseTarget& out) {
    const std::string n = Normalise(text);
    if (n == "roll") out = MouseTarget::Roll;
    else if (n == "pitch") out = MouseTarget::Pitch;
    else if (n == "yaw") out = MouseTarget::Yaw;
    else if (n == "camera" || n == "look") out = MouseTarget::Camera;
    else if (n == "none" || n == "nothing" || n == "off") out = MouseTarget::None;
    else return false;
    return true;
}

// The whole file read into per-action / per-control lists on top of `base`.
struct Parsed {
    int version = 0;  // the file's "Version = N" line (0: none)
    std::vector<std::vector<InputSource>> actions = std::vector<std::vector<InputSource>>(kActionCount);
    std::array<std::vector<InputSource>, kControlCount> gamepad;
    MouseSettings mouse;
};

Parsed Parse(const std::string& text, const Parsed& base, std::vector<std::string>& warnings) {
    Parsed out = base;
    enum class Section { None, Keyboard, Mouse, Gamepad, Unknown } section = Section::None;
    std::istringstream in(text);
    std::string raw;
    int line_no = 0;
    const auto warn = [&](const std::string& what) {
        warnings.push_back("controls.ini line " + std::to_string(line_no) + ": " + what);
    };
    while (std::getline(in, raw)) {
        ++line_no;
        if (line_no == 1 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);  // Notepad's UTF-8 mark
        const std::string line = Trim(StripNote(raw));
        if (line.empty()) continue;
        if (line.front() == '[') {
            const std::string name = Normalise(line.substr(1, line.find(']') == std::string::npos ? std::string::npos
                                                                                               : line.find(']') - 1));
            if (name == "keyboard") section = Section::Keyboard;
            else if (name == "mouse") section = Section::Mouse;
            else if (name == "gamepad") section = Section::Gamepad;
            else {
                section = Section::Unknown;
                warn("unknown section " + line + " (its lines are ignored)");
            }
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            warn("'" + line + "' has no '='");
            continue;
        }
        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        const std::string nkey = Normalise(key);
        if (section == Section::Unknown) continue;
        if (section == Section::None) {
            if (nkey == "version") {
                char* end = nullptr;
                const long v = std::strtol(value.c_str(), &end, 10);
                if (value.empty() || *end != '\0' || v <= 0) warn("version '" + value + "' is not a number");
                else out.version = static_cast<int>(v);
            } else {
                warn("'" + key + "' is outside [Keyboard], [Mouse] or [Gamepad]");
            }
            continue;
        }
        if (section == Section::Mouse) {
            if (nkey == "leftright" || nkey == "horizontal") {
                if (!ParseTarget(value, out.mouse.horizontal)) warn("'" + value + "' is not Roll, Pitch, Yaw, Camera or None");
            } else if (nkey == "updown" || nkey == "vertical") {
                if (!ParseTarget(value, out.mouse.vertical)) warn("'" + value + "' is not Roll, Pitch, Yaw, Camera or None");
            } else if (nkey == "sensitivity") {
                std::string v = value;
                std::replace(v.begin(), v.end(), ',', '.');
                char* end = nullptr;
                const float f = std::strtof(v.c_str(), &end);
                if (v.empty() || *end != '\0' || !(f >= 0.1f && f <= 10.0f)) warn("sensitivity '" + value + "' is not 0.1 to 10");
                else out.mouse.sensitivity = f;
            } else if (nkey == "invertupdown" || nkey == "invert") {
                if (!ParseBool(value, out.mouse.invert_vertical)) warn("'" + value + "' is not yes or no");
            } else if (nkey == "returntocentre" || nkey == "returntocenter") {
                if (!ParseBool(value, out.mouse.return_to_centre)) warn("'" + value + "' is not yes or no");
            } else {
                warn("unknown mouse entry '" + key + "'");
            }
            continue;
        }
        // An input list: all of it or (on any unknown name) the default.
        std::vector<InputSource> list;
        bool ok = true;
        std::size_t start = 0;
        while (ok && start <= value.size() && !value.empty()) {
            const std::size_t comma = value.find(',', start);
            const std::string item = Trim(value.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (!item.empty()) {
                InputSource s;
                if (ParseFriendlyInput(item, s)) {
                    if (std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
                } else {
                    warn("'" + item + "' is not a known key, mouse or gamepad input (" + key + " keeps its default)");
                    ok = false;
                }
            }
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (!ok) continue;
        bool found = false;
        if (section == Section::Keyboard) {
            for (int a = 0; a < kActionCount; ++a) {
                if (Normalise(kActions[a].name) == nkey) {
                    out.actions[a] = list;
                    found = true;
                }
            }
        } else {
            for (int c = 0; c < kControlCount; ++c) {
                if (Normalise(kPadControlNames[c]) == nkey) {
                    out.gamepad[c] = list;
                    found = true;
                }
            }
        }
        if (!found) warn("unknown " + std::string(section == Section::Keyboard ? "action" : "PS2 button") + " '" + key + "'");
    }
    return out;
}

const Parsed& DefaultParsed() {
    static const Parsed parsed = [] {
        std::vector<std::string> warnings;
        return Parse(kDefaultText, Parsed{}, warnings);  // tests/test_controls_file.cpp: no warnings
    }();
    return parsed;
}

ControlBindings ToBindings(const Parsed& p) {
    ControlBindings b;
    b.gamepad = p.gamepad;
    b.mouse = p.mouse;
    for (int a = 0; a < kActionCount; ++a) {
        auto& list = b.keyboard[kActions[a].control];
        for (const InputSource& s : p.actions[a])
            if (std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
    }
    return b;
}

}  // namespace

const std::string& DefaultControlsText() {
    static const std::string text = kDefaultText;
    return text;
}

ControlBindings DefaultControls() { return ToBindings(DefaultParsed()); }

ControlBindings ParseControls(const std::string& text, std::vector<std::string>& warnings, bool* compatible) {
    Parsed base = DefaultParsed();
    base.version = 0;  // the file's own line counts
    const Parsed p = Parse(text, base, warnings);
    if (compatible) *compatible = p.version == kControlsVersion;
    return ToBindings(p);
}

bool ParseFriendlyInput(const std::string& text, InputSource& source) {
    const std::string t = Trim(text);
    if (t.empty()) return false;
    if (t.find(':') != std::string::npos && t.size() > 1) return ParseInputSource(t, source);
    // Raw joystick inputs: "Joy Axis 7", "Joy Axis 7+", "Joy Axis 7-", "Joy Button 4".
    std::string n = Normalise(t);
    if (n.rfind("joyaxis", 0) == 0 || n.rfind("joybutton", 0) == 0) {
        const bool axis = n.rfind("joyaxis", 0) == 0;
        std::string rest = n.substr(axis ? 7 : 9);
        const char last = t.back();
        if (last == '+' || last == '-') rest += last;  // Normalise drops '-'
        return ParseInputSource((axis ? "joyaxis:" : "joybutton:") + rest, source);
    }
    for (const Friendly& f : kFriendly)
        if (n == f.name) return ParseInputSource(f.source, source);
    return ParseInputSource("key:" + t, source);  // SDL's key names ("W", "Left Ctrl", "Keypad 8")
}

ControlBindings ControlsFile::Load() {
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << DefaultControlsText();
        if (out) AC04_LOG_INFO("input", "controls.ini written with the default controls");
        else AC04_LOG_ERROR("input", "cannot write controls.ini (default controls used)");
    }
    last_time_ = std::filesystem::last_write_time(path_, ec);
    std::ifstream in(path_, std::ios::binary);
    if (!in) {
        AC04_LOG_ERROR("input", "cannot read controls.ini (default controls used)");
        return DefaultControls();
    }
    std::stringstream ss;
    ss << in.rdbuf();
    in.close();
    std::vector<std::string> warnings;
    bool compatible = false;
    ControlBindings b = ParseControls(ss.str(), warnings, &compatible);
    if (!compatible) {
        // Another layout version (or none): deleted and replaced with the defaults, like an
        // incompatible settings.json (User rule 2026-10-31).
        std::filesystem::remove(path_, ec);
        std::ofstream out(path_, std::ios::binary | std::ios::trunc);
        out << DefaultControlsText();
        AC04_LOG_WARN("input", "controls.ini is not of version %d: deleted and replaced with the default controls%s",
                      kControlsVersion, out ? "" : " (cannot write the new file)");
        out.close();
        last_time_ = std::filesystem::last_write_time(path_, ec);
        return DefaultControls();
    }
    for (const std::string& w : warnings) AC04_LOG_WARN("input", "%s", w.c_str());
    return b;
}

bool ControlsFile::Changed() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_check_ < std::chrono::seconds(1)) return false;
    last_check_ = now;
    std::error_code ec;
    const auto time = std::filesystem::last_write_time(path_, ec);
    if (ec || time == last_time_) return false;
    last_time_ = time;
    return true;
}

}  // namespace ac04::platform
