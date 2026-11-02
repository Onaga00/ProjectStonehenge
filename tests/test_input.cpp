#include <doctest/doctest.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <string>

#include "platform/input.h"
#include "platform/settings.h"

using namespace ac04::platform;

TEST_CASE("input: binding texts") {
    InputSource s;
    CHECK(ParseInputSource("key:Z", s));
    CHECK(s.kind == InputSource::Key);
    CHECK(s.code == SDL_SCANCODE_Z);

    CHECK(ParseInputSource("pad:rightshoulder", s));
    CHECK(s.kind == InputSource::PadButton);
    CHECK(s.code == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);

    CHECK(ParseInputSource("axis:lefttrigger+", s));
    CHECK(s.kind == InputSource::Axis);
    CHECK(s.code == SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    CHECK(s.direction == +1);
    CHECK(ParseInputSource("axis:lefty-", s));
    CHECK(s.direction == -1);
    CHECK(ParseInputSource("axis:righttrigger", s));  // earlier builds: no sign = positive half
    CHECK(s.direction == +1);

    // Raw joystick inputs: full axis (pressure resting at the minimum), halves, buttons.
    CHECK(ParseInputSource("joyaxis:7", s));
    CHECK(s.kind == InputSource::JoyAxis);
    CHECK(s.code == 7);
    CHECK(s.direction == 0);
    CHECK(ParseInputSource("joyaxis:12-", s));
    CHECK(s.code == 12);
    CHECK(s.direction == -1);
    CHECK(ParseInputSource("joybutton:4", s));
    CHECK(s.kind == InputSource::JoyButton);
    CHECK(s.code == 4);

    CHECK_FALSE(ParseInputSource("joyaxis:", s));
    CHECK_FALSE(ParseInputSource("joyaxis:x", s));
    CHECK_FALSE(ParseInputSource("joyaxis:256", s));
    CHECK_FALSE(ParseInputSource("joybutton:3+", s));
    CHECK_FALSE(ParseInputSource("pad:nosuchbutton", s));
    CHECK_FALSE(ParseInputSource("axis:nosuchaxis+", s));
    CHECK_FALSE(ParseInputSource("key:NoSuchKeyName", s));
    CHECK_FALSE(ParseInputSource("cross", s));
}

TEST_CASE("input: mouse buttons and wheel") {
    InputSource s;
    CHECK(ParseInputSource("mouse:left", s));
    CHECK(s.kind == InputSource::MouseButton);
    CHECK(s.code == SDL_BUTTON_LEFT);
    CHECK(ParseInputSource("mouse:x2", s));
    CHECK(s.code == SDL_BUTTON_X2);
    CHECK(ParseInputSource("wheel:down", s));
    CHECK(s.kind == InputSource::MouseWheel);
    CHECK(s.direction == -1);
    CHECK_FALSE(ParseInputSource("mouse:side", s));
    CHECK_FALSE(ParseInputSource("wheel:left", s));
}

TEST_CASE("input: mouse stick") {
    MouseSettings m;
    SUBCASE("return to centre: the deflection follows the speed and falls back") {
        MouseStick stick;
        for (int i = 0; i < 60; ++i) stick.Step(12.0f, 0.0f, m);  // 12 pixels per tick: full right
        CHECK(stick.X() == doctest::Approx(1.0f).epsilon(0.01));
        for (int i = 0; i < 60; ++i) stick.Step(6.0f, 0.0f, m);  // half the speed: half deflection
        CHECK(stick.X() == doctest::Approx(0.5f).epsilon(0.01));
        for (int i = 0; i < 30; ++i) stick.Step(0.0f, 0.0f, m);  // still: back to the centre in 0.5 s
        CHECK(std::abs(stick.X()) < 0.01f);
    }
    SUBCASE("without return to centre the stick stays and clamps") {
        m.return_to_centre = false;
        MouseStick stick;
        stick.Step(0.0f, 200.0f, m);
        CHECK(stick.Y() == doctest::Approx(0.5f));
        stick.Step(0.0f, 0.0f, m);
        CHECK(stick.Y() == doctest::Approx(0.5f));
        stick.Step(0.0f, 1000.0f, m);
        CHECK(stick.Y() == 1.0f);
    }
    SUBCASE("invert up/down and sensitivity") {
        m.return_to_centre = false;
        m.invert_vertical = true;
        m.sensitivity = 2.0f;
        MouseStick stick;
        stick.Step(100.0f, 100.0f, m);
        CHECK(stick.X() == doctest::Approx(0.5f));
        CHECK(stick.Y() == doctest::Approx(-0.5f));
    }
}

TEST_CASE("input: a wheel notch is one press the game sees") {
    WheelPresses w;
    w.Add(2);
    std::string seen;
    for (int i = 0; i < 8; ++i) {
        w.Step();
        seen += w.Pressed() ? 'X' : '.';
    }
    CHECK(seen == "XX.XX...");  // 2 ticks pressed, 1 released, then the next notch
}

TEST_CASE("input: pressure and digital bit of one control") {
    SUBCASE("digital sources give 0 or full pressure") {
        ControlLevel v;
        v.Add(false, 0.0f);
        CHECK_FALSE(v.pressed);
        CHECK(v.level == 0.0f);
        v.Add(true, 1.0f);
        CHECK(v.pressed);
        CHECK(v.level == 1.0f);
    }
    SUBCASE("a button with its pressure axis: the device's press and pressure") {
        ControlLevel v;
        v.Add(true, 0.25f);  // pressed lightly (DualShock 3 through SDL's HIDAPI PS3 driver)
        CHECK(v.pressed);
        CHECK(v.level == 0.25f);
    }
    SUBCASE("key and pressure button bound together: the highest level wins") {
        ControlLevel v;
        v.Add(false, 0.0f);  // key up
        v.Add(true, 0.4f);   // pad half pressed
        CHECK(v.pressed);
        CHECK(v.level == 0.4f);
        v.Add(true, 1.0f);   // key down
        CHECK(v.level == 1.0f);
    }
    SUBCASE("an axis below half travel gives pressure without the digital bit") {
        ControlLevel v;
        v.Add(0.3f > 0.5f, 0.3f);
        CHECK_FALSE(v.pressed);
        CHECK(v.level == 0.3f);
    }
}
