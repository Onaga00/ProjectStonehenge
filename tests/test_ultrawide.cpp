#include <doctest/doctest.h>

#include "common/port_hooks.h"

using ac04::port::Display;
using ac04::port::SetDisplayOptions;
using ac04::port::UltrawideProjectionWidth;
using ac04::port::UltrawideUses4x3Distances;

TEST_CASE("ultrawide: original 4:3 and 16:9 presentation is unchanged") {
    SetDisplayOptions(false, true, 3440, 1440);
    CHECK(Display().aspect_w == 4);
    CHECK(Display().aspect_h == 3);
    CHECK(Display().projection_x_scale == 1.0f);
    CHECK(Display().hud_x_scale == 1.0f);
    SetDisplayOptions(true, true, 1920, 1080);
    CHECK(Display().aspect_w == 16);
    CHECK(Display().aspect_h == 9);
    CHECK(Display().projection_x_scale == 1.0f);
    CHECK(Display().hud_x_scale == 1.0f);
    SetDisplayOptions(true, true, 1920, 1200);  // 16:10 stays pillarboxed 16:9
    CHECK(Display().aspect_w == 16);
    CHECK(Display().aspect_h == 9);
    CHECK(Display().projection_x_scale == 1.0f);
}

TEST_CASE("ultrawide: 2560x1080 expands the world and inversely compensates the HUD") {
    SetDisplayOptions(true, true, 2560, 1080);
    CHECK(Display().aspect_w == 64);
    CHECK(Display().aspect_h == 27);
    CHECK(Display().projection_x_scale == doctest::Approx(4.0 / 3.0));
    CHECK(Display().hud_x_scale == doctest::Approx(0.75));
    CHECK(Display().projection_x_scale * Display().hud_x_scale == doctest::Approx(1.0));
}

TEST_CASE("ultrawide: 3440x1440 uses its exact 43:18 aspect") {
    SetDisplayOptions(true, true, 3440, 1440);
    CHECK(Display().aspect_w == 43);
    CHECK(Display().aspect_h == 18);
    CHECK(Display().projection_x_scale == doctest::Approx(43.0 / 32.0));
    CHECK(Display().hud_x_scale == doctest::Approx(32.0 / 43.0));
}

TEST_CASE("ultrawide: disabled or invalid windows retain 16:9") {
    SetDisplayOptions(true, false, 2560, 1080);
    CHECK(Display().aspect_w == 16);
    CHECK(Display().aspect_h == 9);
    CHECK(Display().projection_x_scale == 1.0f);
    CHECK(Display().hud_x_scale == 1.0f);
    SetDisplayOptions(true, true, 0, 0);
    CHECK(Display().aspect_w == 16);
    CHECK(Display().aspect_h == 9);
    CHECK(Display().projection_x_scale == 1.0f);
    CHECK(Display().hud_x_scale == 1.0f);
}

TEST_CASE("ultrawide: 4:3 field of view basis (default)") {
    // 2560x1080: 640 x (64/27) / (4/3) = 1137.78, the same as 853.33 x (64/27) / (16/9).
    SetDisplayOptions(true, true, 2560, 1080);
    CHECK(Display().ultrawide);
    CHECK(Display().keep_4x3_fov);
    CHECK(UltrawideProjectionWidth(640.0f, 640.0f * 4.0f / 3.0f) == doctest::Approx(1137.78f).epsilon(1e-4));
    // Whatever the 16:9 constant is (e.g. the replay's 0x0040DADC), the 4:3 basis is used.
    CHECK(UltrawideProjectionWidth(640.0f, 700.0f) == doctest::Approx(1137.78f).epsilon(1e-4));
    CHECK(UltrawideUses4x3Distances());
    SetDisplayOptions(true, true, 3440, 1440);
    CHECK(UltrawideProjectionWidth(640.0f, 853.33f) == doctest::Approx(1146.67f).epsilon(1e-4));
}

TEST_CASE("ultrawide: 16:9 field of view basis (Kimi's original behaviour)") {
    SetDisplayOptions(true, true, 2560, 1080, false);
    CHECK(Display().ultrawide);
    CHECK_FALSE(UltrawideUses4x3Distances());
    CHECK(UltrawideProjectionWidth(640.0f, 700.0f) == doctest::Approx(700.0f * 4.0f / 3.0f));
}

TEST_CASE("ultrawide: inactive keeps the original 16:9 width and distances") {
    SetDisplayOptions(true, true, 1920, 1080);
    CHECK_FALSE(Display().ultrawide);
    CHECK(UltrawideProjectionWidth(640.0f, 853.33f) == 853.33f);
    CHECK_FALSE(UltrawideUses4x3Distances());
    SetDisplayOptions(true, false, 2560, 1080);
    CHECK_FALSE(UltrawideUses4x3Distances());
}
