#include <doctest/doctest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "platform/settings.h"

using namespace ac04::platform;

TEST_CASE("settings: defaults") {
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK(store.LoadFromText("{}", warnings));
    CHECK(warnings.empty());
    const Settings& s = store.Get();
    CHECK(s.presentation.filter == ScaleFilter::Sharp);
    CHECK(s.presentation.vsync);
    CHECK(s.presentation.frame_cap == 360);
    CHECK(s.presentation.window_mode == WindowMode::Fullscreen);
    CHECK(s.presentation.internal_scale == 1);
    CHECK(s.presentation.default_screen_ratio == ScreenRatioDefault::Monitor);
    CHECK(s.presentation.ultrawide);
    CHECK(s.presentation.ultrawide_4x3_fov);
    CHECK_FALSE(s.debug.fps_overlay);
    CHECK(s.presentation.antialiasing == AntiAliasing::Fxaa);
    CHECK(s.presentation.frame_interpolation);
    CHECK(s.presentation.text_filter == TextFilter::Sharp);
    CHECK(s.audio.on_focus_loss == FocusLossAudio::KeepPlaying);
    CHECK(s.debug.overlay_toggle == "\\");
    CHECK(s.input.modern_controls[0]);
    CHECK(s.input.modern_controls[1]);
}

TEST_CASE("settings: defaults follow the display") {
    // V-sync on at every refresh rate (User request 2026-10-22); at a rate other than 60 or 120 Hz
    // the frame cap (used with v-sync off) is the highest one at or below it. FXAA; the internal
    // scale closest to the display height (2560 x 1440: 3x).
    struct Case {
        int hz;
        int cap;
    };
    for (const Case c : {Case{60, 360}, Case{120, 360}, Case{0, 360}, Case{75, 60}, Case{144, 144}, Case{165, 144},
                         Case{240, 240}, Case{500, 360}, Case{50, 60}}) {
        CAPTURE(c.hz);
        DisplayInfo d;
        d.width = 2560;
        d.height = 1440;
        d.refresh_hz = c.hz;
        const Settings s = DefaultSettings(d);
        CHECK(s.presentation.vsync);
        CHECK(s.presentation.frame_cap == c.cap);
        CHECK(s.presentation.window_mode == WindowMode::Fullscreen);
        CHECK(s.presentation.antialiasing == AntiAliasing::Fxaa);
        CHECK(s.presentation.internal_scale == 3);
    }
    // Missing keys of a file take the defaults of the display set for the stores.
    DisplayInfo d;
    d.refresh_hz = 144;
    d.height = 2160;
    SetDefaultsDisplay(d);
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK(store.LoadFromText(R"({"presentation":{"filter":"smooth"}})", warnings));
    CHECK(store.Get().presentation.vsync);
    CHECK(store.Get().presentation.frame_cap == 144);
    CHECK(store.Get().presentation.internal_scale == 5);
    SetDefaultsDisplay(DisplayInfo{});
    // Wider than 4:3 (with a 2.5 % margin): 16:9, 16:10, 21:9; not 4:3 or 5:4.
    auto wide = [](int w, int h) {
        DisplayInfo x;
        x.width = w;
        x.height = h;
        return x.Wide();
    };
    CHECK(wide(1920, 1080));
    CHECK(wide(1920, 1200));
    CHECK(wide(3440, 1440));
    CHECK_FALSE(wide(1024, 768));
    CHECK_FALSE(wide(1280, 1024));
    CHECK_FALSE(DisplayInfo{}.Wide());
}

TEST_CASE("settings: anti-aliasing") {
    for (const auto& [text, value] : {std::pair<const char*, AntiAliasing>{"off", AntiAliasing::Off},
                                      {"fxaa", AntiAliasing::Fxaa}, {"smaa", AntiAliasing::Smaa}}) {
        SettingsStore store;
        std::vector<std::string> warnings;
        CHECK(store.LoadFromText(std::string(R"({"presentation":{"antialiasing":")") + text + "\"}}", warnings));
        CHECK(store.Get().presentation.antialiasing == value);
        CHECK(nlohmann::json::parse(store.ToText())["presentation"]["antialiasing"] == text);
    }
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK_FALSE(store.LoadFromText(R"({"presentation":{"antialiasing":"msaa"}})", warnings));
    CHECK_FALSE(warnings.empty());
}

TEST_CASE("settings: ultrawide") {
    SettingsStore store;
    std::vector<std::string> warnings;
    int notified = 0;
    store.Subscribe(SettingsSection::Presentation, [&notified](const Settings&) { ++notified; });
    CHECK(store.LoadFromText(R"({"presentation":{"ultrawide":false}})", warnings));
    CHECK_FALSE(store.Get().presentation.ultrawide);
    CHECK(nlohmann::json::parse(store.ToText())["presentation"]["ultrawide"] == false);
    Settings next = store.Get();
    next.presentation.ultrawide = true;
    store.Replace(next);
    CHECK(notified == 1);
    CHECK_FALSE(store.LoadFromText(R"({"presentation":{"ultrawide":"auto"}})", warnings));
    CHECK(store.LoadFromText(R"({"presentation":{"ultrawide_4x3_fov":false}})", warnings));
    CHECK_FALSE(store.Get().presentation.ultrawide_4x3_fov);
    CHECK(nlohmann::json::parse(store.ToText())["presentation"]["ultrawide_4x3_fov"] == false);
}

TEST_CASE("settings: frame interpolation") {
    SettingsStore store;
    std::vector<std::string> warnings;
    int notified = 0;
    store.Subscribe(SettingsSection::Presentation, [&notified](const Settings&) { ++notified; });
    CHECK(store.LoadFromText(R"({"presentation":{"frame_interpolation":false}})", warnings));
    CHECK_FALSE(store.Get().presentation.frame_interpolation);
    CHECK(nlohmann::json::parse(store.ToText())["presentation"]["frame_interpolation"] == false);
    Settings next = store.Get();
    next.presentation.frame_interpolation = true;
    store.Replace(next);
    CHECK(notified == 1);  // a change notifies the presentation section
    CHECK_FALSE(store.LoadFromText(R"({"presentation":{"frame_interpolation":"yes"}})", warnings));
}

TEST_CASE("settings: text filter") {
    SettingsStore store;
    std::vector<std::string> warnings;
    int notified = 0;
    store.Subscribe(SettingsSection::Presentation, [&notified](const Settings&) { ++notified; });
    CHECK(store.LoadFromText(R"({"presentation":{"text_filter":"original"}})", warnings));
    CHECK(store.Get().presentation.text_filter == TextFilter::Original);
    CHECK(nlohmann::json::parse(store.ToText())["presentation"]["text_filter"] == "original");
    Settings next = store.Get();
    next.presentation.text_filter = TextFilter::Sharp;
    store.Replace(next);
    CHECK(notified == 1);  // a change notifies the presentation section
    CHECK(nlohmann::json::parse(store.ToText())["presentation"]["text_filter"] == "sharp");
    CHECK_FALSE(store.LoadFromText(R"({"presentation":{"text_filter":"blurry"}})", warnings));
}

TEST_CASE("settings: valid values are read") {
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK(store.LoadFromText(R"({"version":2,"presentation":{"filter":"smooth","vsync":false,"frame_cap":144,
        "window_mode":"fullscreen"},"paths":{"iso":"C:\\x.iso"},"audio":{"on_focus_loss":"mute"},
        "debug":{"overlay_enabled":true,"overlay_toggle":"key:F12","fps_overlay":true},
        "input":{"modern_controls":[false,true]}})",
                             warnings));
    CHECK(warnings.empty());
    const Settings& s = store.Get();
    CHECK(s.presentation.filter == ScaleFilter::Smooth);
    CHECK_FALSE(s.presentation.vsync);
    CHECK(s.presentation.frame_cap == 144);
    CHECK(s.presentation.window_mode == WindowMode::Fullscreen);
    CHECK(s.paths.iso == "C:\\x.iso");
    CHECK(s.audio.on_focus_loss == FocusLossAudio::Mute);
    CHECK(s.debug.overlay_enabled);
    CHECK(s.debug.overlay_toggle == "key:F12");
    CHECK(s.debug.fps_overlay);
    CHECK_FALSE(s.input.modern_controls[0]);
    CHECK(s.input.modern_controls[1]);
}

TEST_CASE("settings: a file of version 1 is incompatible") {
    // Version 2 (2026-10-31): the bindings moved to controls.ini. A version 1 file is rejected as a
    // whole (the caller deletes it and writes the defaults), its other values are not kept.
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK_FALSE(store.LoadFromText(R"({"version":1,"presentation":{"filter":"smooth"},
        "input":{"bindings":{"cross":["key:Space"]},"modern_controls":[false,true]}})",
                                   warnings));
    CHECK_FALSE(warnings.empty());
    CHECK(store.Get().presentation.filter == ScaleFilter::Sharp);
    CHECK(store.Get().input.modern_controls[0]);
    CHECK(nlohmann::json::parse(store.ToText())["version"] == 2);
    CHECK_FALSE(nlohmann::json::parse(store.ToText())["input"].contains("bindings"));
    // A version 2 file with bindings is incompatible, like any unknown key.
    CHECK_FALSE(store.LoadFromText(R"({"version":2,"input":{"bindings":{}}})", warnings));
}

TEST_CASE("settings: the window mode of earlier builds is still read") {
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK(store.LoadFromText(R"({"presentation":{"window_mode":"borderless"}})", warnings));
    CHECK(store.Get().presentation.window_mode == WindowMode::Fullscreen);
    CHECK(nlohmann::json::parse(store.ToText())["presentation"]["window_mode"] == "fullscreen");
}

TEST_CASE("settings: a file with an invalid value is rejected as a whole") {
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK_FALSE(store.LoadFromText(R"({"presentation":{"filter":"lanczos","frame_cap":75,"internal_scale":9,
        "vsync":"yes"},"input":{"turbo":["key:T"],"modern_controls":[true]},
        "paths":{"iso":"D:\\kept.iso"}})",
                                   warnings));
    CHECK(warnings.size() == 6);
    const Settings& s = store.Get();
    CHECK(s.presentation.filter == ScaleFilter::Sharp);
    CHECK(s.presentation.frame_cap == 360);
    CHECK(s.presentation.internal_scale == 1);
    CHECK(s.presentation.vsync);
    CHECK(s.paths.iso.empty());  // valid values of a rejected file are not kept either
    CHECK(s.input.modern_controls[0]);
}

TEST_CASE("settings: unknown keys and other versions are incompatible") {
    for (const char* text : {R"({"mods":{"x":1}})", R"({"presentation":{"future_option":true}})",
                             R"({"version":1})", R"({"version":3})", R"({"version":"2"})", R"({"audio":5})"}) {
        SettingsStore store;
        std::vector<std::string> warnings;
        CHECK_FALSE(store.LoadFromText(text, warnings));
        CHECK(warnings.size() == 1);
    }
}

TEST_CASE("settings: internal scale 1 to 8") {
    for (int n = 1; n <= 8; ++n) {
        SettingsStore store;
        std::vector<std::string> warnings;
        CHECK(store.LoadFromText(R"({"presentation":{"internal_scale":)" + std::to_string(n) + "}}", warnings));
        CHECK(warnings.empty());
        CHECK(store.Get().presentation.internal_scale == n);
    }
}

TEST_CASE("settings: default internal scale is the closest to the display height") {
    // 448 lines x N closest to the height, 1-8; unknown height: 1.
    struct Case {
        int height, scale;
    };
    for (const Case c : {Case{0, 1}, Case{480, 1}, Case{720, 2}, Case{768, 2}, Case{900, 2}, Case{1080, 2},
                         Case{1200, 3}, Case{1440, 3}, Case{1600, 4}, Case{2160, 5}, Case{2880, 6}, Case{4320, 8}}) {
        CAPTURE(c.height);
        CHECK(DefaultInternalScale(c.height) == c.scale);
    }
}

TEST_CASE("settings: validation runs keep 1x and no anti-aliasing") {
    DisplayInfo d;
    d.width = 3840;
    d.height = 2160;
    d.refresh_hz = 60;
    SetValidationDefaults(true);
    const Settings s = DefaultSettings(d);
    SetValidationDefaults(false);
    CHECK(s.presentation.vsync);
    CHECK(s.presentation.internal_scale == 1);
    CHECK(s.presentation.antialiasing == AntiAliasing::Off);
    CHECK(DefaultSettings(d).presentation.internal_scale == 5);
}

TEST_CASE("settings: an integrated GPU keeps 1x and no anti-aliasing") {
    DisplayInfo d;
    d.width = 1920;
    d.height = 1080;
    d.refresh_hz = 60;
    SetDefaultsIntegratedGpu(true);
    const Settings s = DefaultSettings(d);
    SetDefaultsIntegratedGpu(false);
    CHECK(s.presentation.vsync);
    CHECK(s.presentation.window_mode == DefaultSettings(d).presentation.window_mode);
    CHECK(s.presentation.internal_scale == 1);
    CHECK(s.presentation.antialiasing == AntiAliasing::Off);
    CHECK(DefaultSettings(d).presentation.internal_scale == 2);
    CHECK(DefaultSettings(d).presentation.antialiasing == AntiAliasing::Fxaa);
}

TEST_CASE("settings: broken JSON gives defaults") {
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK_FALSE(store.LoadFromText("{ not json", warnings));
    CHECK(warnings.size() == 1);
    CHECK(store.Get().presentation.frame_cap == 360);
    warnings.clear();
    CHECK_FALSE(store.LoadFromText("[1, 2]", warnings));
    CHECK(warnings.size() == 1);
}

TEST_CASE("settings: a saved file loads back unchanged") {
    SettingsStore store;
    Settings s = store.Get();
    s.presentation.internal_scale = 4;
    s.presentation.window_mode = WindowMode::Fullscreen;
    s.input.modern_controls = {false, false};
    store.Replace(s);
    SettingsStore again;
    std::vector<std::string> warnings;
    CHECK(again.LoadFromText(store.ToText(), warnings));
    CHECK(warnings.empty());
    CHECK(again.ToText() == store.ToText());
}

TEST_CASE("settings: a rejected file on disk is replaced with the defaults") {
    const auto file = std::filesystem::temp_directory_path() / "ac04_test_settings.json";
    {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out << R"({"presentation":{"internal_scale":4,"frame_cap":"fast"}})";
    }
    SettingsStore store;
    std::vector<std::string> warnings;
    CHECK(store.Load(file, warnings) == SettingsStore::LoadResult::Rejected);
    CHECK(store.ReplaceWithDefaults());
    SettingsStore again;
    warnings.clear();
    CHECK(again.Load(file, warnings) == SettingsStore::LoadResult::Loaded);
    CHECK(again.Get().presentation.internal_scale == 1);
    CHECK(again.ToText() == SettingsStore().ToText());
    std::filesystem::remove(file);
    CHECK(again.Load(file, warnings) == SettingsStore::LoadResult::Missing);
}

TEST_CASE("settings: change notification per section") {
    SettingsStore store;
    int presentation_calls = 0, audio_calls = 0;
    store.Subscribe(SettingsSection::Presentation, [&](const Settings&) { ++presentation_calls; });
    const int id = store.Subscribe(SettingsSection::Audio, [&](const Settings&) { ++audio_calls; });
    store.Update(SettingsSection::Presentation, [](Settings& s) { s.presentation.filter = ScaleFilter::Smooth; });
    CHECK(presentation_calls == 1);
    CHECK(audio_calls == 0);

    Settings next = store.Get();
    next.audio.on_focus_loss = FocusLossAudio::Mute;
    store.Replace(next);  // only the audio section changed
    CHECK(presentation_calls == 1);
    CHECK(audio_calls == 1);

    store.Unsubscribe(id);
    next.audio.on_focus_loss = FocusLossAudio::KeepPlaying;
    store.Replace(next);
    CHECK(audio_calls == 1);
}

TEST_CASE("settings: frame caps") {
    CHECK(IsValidFrameCap(60));
    CHECK(IsValidFrameCap(120));
    CHECK(IsValidFrameCap(144));
    CHECK(IsValidFrameCap(240));
    CHECK(IsValidFrameCap(360));
    CHECK_FALSE(IsValidFrameCap(0));
    CHECK_FALSE(IsValidFrameCap(75));
}
