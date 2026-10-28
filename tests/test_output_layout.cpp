#include <doctest/doctest.h>

#include "platform/render/output_layout.h"
#include "platform/render/test_pattern.h"

using ac04::render::FitAspect;

TEST_CASE("output layout: 4:3 pillarbox on a 16:9 window") {
    auto r = FitAspect(1920, 1080, 4, 3);
    CHECK(r.width == 1440);
    CHECK(r.height == 1080);
    CHECK(r.x == 240);
    CHECK(r.y == 0);
}

TEST_CASE("output layout: letterbox on a tall window") {
    auto r = FitAspect(800, 1000, 4, 3);
    CHECK(r.width == 800);
    CHECK(r.height == 600);
    CHECK(r.x == 0);
    CHECK(r.y == 200);
}

TEST_CASE("output layout: exact 4:3 window and 16:9 aspect") {
    auto r = FitAspect(1280, 960, 4, 3);
    CHECK(r.x == 0);
    CHECK(r.y == 0);
    CHECK(r.width == 1280);
    CHECK(r.height == 960);
    auto w = FitAspect(1280, 960, 16, 9);
    CHECK(w.width == 1280);
    CHECK(w.height == 720);
    CHECK(w.y == 120);
}

TEST_CASE("output layout: ultrawide aspects fill their windows") {
    auto a = FitAspect(2560, 1080, 64, 27);
    CHECK(a.x == 0);
    CHECK(a.y == 0);
    CHECK(a.width == 2560);
    CHECK(a.height == 1080);
    auto b = FitAspect(3440, 1440, 43, 18);
    CHECK(b.x == 0);
    CHECK(b.y == 0);
    CHECK(b.width == 3440);
    CHECK(b.height == 1440);
    auto letterboxed = FitAspect(1920, 1080, 64, 27);
    CHECK(letterboxed.width == 1920);
    CHECK(letterboxed.height == 810);
    CHECK(letterboxed.y == 135);
}

TEST_CASE("output layout: degenerate sizes") {
    auto r = FitAspect(0, 600, 4, 3);
    CHECK(r.width == 0);
    CHECK(r.height == 0);
}

TEST_CASE("test pattern: native size") {
    auto px = ac04::render::MakeTestPattern(ac04::render::kNativeWidth, ac04::render::kNativeHeight);
    CHECK(px.size() == 512u * 448u * 4u);
    CHECK(px[3] == 255);  // opaque
}
