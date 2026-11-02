#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <string>
#include <vector>

#include "platform/controls_file.h"

using namespace ac04::platform;

namespace {

bool Has(const std::vector<InputSource>& list, const char* text) {
    InputSource s;
    REQUIRE(ParseInputSource(text, s));
    return std::find(list.begin(), list.end(), s) != list.end();
}

}  // namespace

TEST_CASE("controls: the default file reads without a problem") {
    std::vector<std::string> warnings;
    bool compatible = false;
    const ControlBindings b = ParseControls(DefaultControlsText(), warnings, &compatible);
    for (const std::string& w : warnings) INFO(w);
    CHECK(warnings.empty());
    CHECK(compatible);
    CHECK(b == DefaultControls());
    // The input names are explained before the first section.
    const std::string& text = DefaultControlsText();
    CHECK(text.find("INPUT NAMES") < text.find("[Keyboard]"));
}

TEST_CASE("controls: a file of another version is incompatible") {
    for (const char* text : {"[Keyboard]\nMissile = F\n", "Version = 2\n[Keyboard]\n", "Version = x\n"}) {
        std::vector<std::string> warnings;
        bool compatible = true;
        ParseControls(text, warnings, &compatible);
        CHECK_FALSE(compatible);
    }
    std::vector<std::string> warnings;
    bool compatible = false;
    ParseControls("version = 1\n", warnings, &compatible);
    CHECK(compatible);
}

TEST_CASE("controls: AC7-style keyboard defaults") {
    const ControlBindings b = DefaultControls();
    CHECK(Has(b.keyboard[kR1], "key:W"));        // throttle up
    CHECK(Has(b.keyboard[kL1], "key:S"));        // throttle down
    CHECK(Has(b.keyboard[kLxMinus], "key:A"));   // roll left
    CHECK(Has(b.keyboard[kLxPlus], "key:D"));
    CHECK(Has(b.keyboard[kLyPlus], "key:3"));    // pitch up
    CHECK(Has(b.keyboard[kLyMinus], "key:1"));
    CHECK(Has(b.keyboard[kL2], "key:Q"));        // yaw left
    CHECK(Has(b.keyboard[kR2], "key:E"));
    CHECK(Has(b.keyboard[kCross], "mouse:left"));   // machine gun
    CHECK(Has(b.keyboard[kCross], "key:Return"));   // Accept = Enter: two actions on one PS2 button
    CHECK(Has(b.keyboard[kCircle], "key:Space"));   // missile
    CHECK(Has(b.keyboard[kCircle], "mouse:right"));
    CHECK(Has(b.keyboard[kSelect], "wheel:up"));    // change weapon
    CHECK(Has(b.keyboard[kRyMinus], "key:Keypad 8"));
    CHECK(Has(b.keyboard[kStart], "key:Escape"));
    CHECK(b.keyboard[kL3].empty());  // "Left stick button =": no input
    CHECK(b.mouse.horizontal == MouseTarget::Roll);
    CHECK(b.mouse.vertical == MouseTarget::Pitch);
    CHECK(b.mouse.return_to_centre);
}

TEST_CASE("controls: gamepad defaults are the layout of earlier builds") {
    const ControlBindings b = DefaultControls();
    CHECK(Has(b.gamepad[kCross], "pad:south"));
    CHECK(Has(b.gamepad[kSquare], "pad:west"));
    CHECK(Has(b.gamepad[kL1], "pad:leftshoulder"));
    CHECK(Has(b.gamepad[kL2], "axis:lefttrigger+"));
    CHECK(Has(b.gamepad[kSelect], "pad:back"));
    CHECK(Has(b.gamepad[kUp], "pad:dpup"));
    CHECK(Has(b.gamepad[kLyMinus], "axis:lefty-"));
    CHECK(Has(b.gamepad[kRxPlus], "axis:rightx+"));
    for (int c = 0; c < kControlCount; ++c) {
        INFO(ControlName(c));
        CHECK(b.gamepad[c].size() == 1);
    }
}

TEST_CASE("controls: player spellings") {
    InputSource s;
    CHECK(ParseFriendlyInput("w", s));
    CHECK(s.kind == InputSource::Key);
    CHECK(s.code == SDL_SCANCODE_W);
    CHECK(ParseFriendlyInput("Left Ctrl", s));
    CHECK(s.code == SDL_SCANCODE_LCTRL);
    CHECK(ParseFriendlyInput("Enter", s));
    CHECK(s.code == SDL_SCANCODE_RETURN);
    CHECK(ParseFriendlyInput("Keypad -", s));
    CHECK(s.code == SDL_SCANCODE_KP_MINUS);
    CHECK(ParseFriendlyInput("Minus", s));
    CHECK(s.code == SDL_SCANCODE_MINUS);
    CHECK(ParseFriendlyInput("d-pad up", s));
    CHECK(s.kind == InputSource::PadButton);
    CHECK(s.code == SDL_GAMEPAD_BUTTON_DPAD_UP);
    CHECK(ParseFriendlyInput("RIGHT TRIGGER", s));
    CHECK(s.kind == InputSource::Axis);
    CHECK(s.code == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    CHECK(ParseFriendlyInput("Left Stick Up", s));
    CHECK(s.code == SDL_GAMEPAD_AXIS_LEFTY);
    CHECK(s.direction == -1);
    CHECK(ParseFriendlyInput("Mouse Wheel Down", s));
    CHECK(s.kind == InputSource::MouseWheel);
    CHECK(ParseFriendlyInput("Joy Axis 7", s));
    CHECK(s.kind == InputSource::JoyAxis);
    CHECK(s.direction == 0);
    CHECK(ParseFriendlyInput("Joy Axis 12-", s));
    CHECK(s.code == 12);
    CHECK(s.direction == -1);
    CHECK(ParseFriendlyInput("Joy Button 4", s));
    CHECK(s.kind == InputSource::JoyButton);
    CHECK(ParseFriendlyInput("key:Z", s));  // the technical syntax still works
    CHECK(s.code == SDL_SCANCODE_Z);
    CHECK_FALSE(ParseFriendlyInput("Mouse Lft", s));
    CHECK_FALSE(ParseFriendlyInput("", s));
}

TEST_CASE("controls: a player's file") {
    std::vector<std::string> warnings;
    const ControlBindings b = ParseControls(
        "[keyboard]\n"
        "throttle UP = Up Arrow, Mouse Lft   ; a typo: the action keeps its default\n"
        "Missile = F, Mouse Right\n"
        "Map =\n"
        "Afterburner = B\n"
        "[MOUSE]\n"
        "left/right = camera\n"
        "Sensitivity = 1,5\n"
        "Return to center = off\n"
        "[Gamepad]\n"
        "Cross = East\n"
        "Circle = South\n"
        "[Wheel]\n"
        "Steer = X\n",
        warnings);
    CHECK(warnings.size() == 3);  // the typo (and the unknown key name), the unknown action, the section
    CHECK(Has(b.keyboard[kR1], "key:W"));  // default kept
    CHECK(Has(b.keyboard[kCircle], "key:F"));
    CHECK_FALSE(Has(b.keyboard[kCircle], "key:Space"));
    CHECK(b.keyboard[kSquare].empty());
    CHECK(b.mouse.horizontal == MouseTarget::Camera);
    CHECK(b.mouse.vertical == MouseTarget::Pitch);
    CHECK(b.mouse.sensitivity == doctest::Approx(1.5f));
    CHECK_FALSE(b.mouse.return_to_centre);
    CHECK(Has(b.gamepad[kCross], "pad:east"));
    CHECK(Has(b.gamepad[kCircle], "pad:south"));
    CHECK(Has(b.gamepad[kSquare], "pad:west"));  // untouched
}

TEST_CASE("controls: lines the port cannot read") {
    std::vector<std::string> warnings;
    ParseControls("Pitch up = W\n[Keyboard]\nno equals sign\n[Mouse]\nSensitivity = 50\nUp/down = Throttle\n",
                  warnings);
    CHECK(warnings.size() == 4);
    for (const std::string& w : warnings) CHECK(w.rfind("controls.ini line ", 0) == 0);
}
