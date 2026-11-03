// Port settings (ADR-005 §4, ADR-006 "Settings architecture").
// - One struct with defaults; settings.json load/save; change notification per section, so the
//   GRAPHICS page or the debug overlay can change values at run time.
// - Missing keys take their default. A file that cannot be read, is not valid JSON, has another
//   version, an unknown key or an invalid value is rejected as a whole: the defaults are used and
//   the caller replaces the file (User decision 2026-10-20, docs/port/command-line.md).
// - Only port settings live here; the game's own options (brightness, control type, ...) stay in
//   the game's save data on the memory card.
// - Game code never reads settings (the controller and GRAPHICS pages go through port_hooks.h).
#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ac04::platform {

enum class ScaleFilter { Sharp, Smooth };
enum class WindowMode { Windowed, Fullscreen };  // fullscreen = borderless desktop-size window
enum class FocusLossAudio { KeepPlaying, Mute };
// The game's SCREEN RATIO option when no save data has been loaded: from the monitor's shape
// (16:9 on a screen wider than 4:3), or the game's own default (4:3).
enum class ScreenRatioDefault { Monitor, Game };
// Anti-aliasing of the flight views' 3D before their HUD is drawn (presentation only).
enum class AntiAliasing { Off, Fxaa, Smaa };

// The display the port starts on (0 = unknown): the defaults depend on it.
struct DisplayInfo {
    int width = 0, height = 0;  // desktop mode in pixels
    int refresh_hz = 0;         // rounded
    bool Wide() const { return width > 0 && height > 0 && width * 3 > height * 4 + height / 10; }
};

struct PresentationSettings {
    ScaleFilter filter = ScaleFilter::Sharp;
    bool vsync = true;
    int frame_cap = 360;            // presented frames per second with v-sync off: 60, 120, 144, 240 or 360
    WindowMode window_mode = WindowMode::Fullscreen;
    int window_width = 1280;        // windowed client size
    int window_height = 960;
    int internal_scale = 1;         // renderer scale factor 1-kMaxInternalScale (docs/port/renderer.md §1)
    ScreenRatioDefault default_screen_ratio = ScreenRatioDefault::Monitor;
    AntiAliasing antialiasing = AntiAliasing::Off;
    // Frame interpolation (ADR-002): above 60 presented frames per second the 3D is shown between
    // the last two game steps (platform/render/frame_interpolator.h). Presentation only.
    bool frame_interpolation = true;
};

struct PathSettings {
    std::string iso;                // UTF-8 path of the disc image
};

struct AudioSettings {
    FocusLossAudio on_focus_loss = FocusLossAudio::KeepPlaying;
};

struct InputSettings {
    // The key, mouse and gamepad bindings are in controls.ini (platform/controls_file.h; version 1
    // of settings.json held them as input.bindings).
    // Per player: the Modern variant of the game's CONTROL TYPE (L1/L2 and R1/R2 swapped before
    // the game reads the pad; the save data keeps the Classic type). Default on for both players.
    std::array<bool, 2> modern_controls{true, true};
};

struct DebugSettings {
    bool overlay_enabled = AC04_DEV_BUILD != 0;  // on by default in development builds
    std::string overlay_toggle = "\\";           // typed character (or "key:<name>")
    bool fps_overlay = false;                    // frame rate and frame time in the top right corner
};

struct Settings {
    PresentationSettings presentation;
    PathSettings paths;
    AudioSettings audio;
    InputSettings input;
    DebugSettings debug;
};

enum class SettingsSection { Presentation, Paths, Audio, Input, Debug };

// Values accepted by the frame cap setting.
bool IsValidFrameCap(int fps);
constexpr int kFrameCaps[] = {60, 120, 144, 240, 360};
// 2 (2026-10-31): input.bindings moved to controls.ini. A file of another version is rejected
// (deleted and replaced with the defaults), as every incompatible file.
constexpr int kSettingsVersion = 2;
constexpr int kMaxInternalScale = 8;
InputSettings DefaultInputSettings();
// The defaults on `display` (User requests 2026-10-20 and 2026-10-22): fullscreen, v-sync on, FXAA,
// the internal scale whose picture (448 lines x N) is closest to the display's height; with a
// refresh rate other than 60 or 120 Hz the frame cap (used with v-sync off) is the highest one at
// or below it. Validation runs (SetValidationDefaults) keep 1x and no anti-aliasing; so does an
// integrated GPU (SetDefaultsIntegratedGpu, User decision 2026-11-03).
Settings DefaultSettings(const DisplayInfo& display);
// Internal scale for a display `height` pixels high: 448 x N closest to it, 1-8 (1 when unknown).
int DefaultInternalScale(int height);
// The display every SettingsStore takes its defaults from (set once at startup).
void SetDefaultsDisplay(const DisplayInfo& display);
const DisplayInfo& DefaultsDisplay();
// Development validation runs (pad script, input log, PCSX2 recording, --memcard, --no-memcard):
// the defaults keep internal scale 1 and anti-aliasing off, so screenshots compare with the
// baselines. Set once at startup.
void SetValidationDefaults(bool on);
// The GPU the renderer chose is an integrated one (VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU): the
// defaults keep internal scale 1 and anti-aliasing off. Set once at startup, after the renderer
// started (main.cpp applies it to a settings file written with the defaults in the same session).
void SetDefaultsIntegratedGpu(bool on);
bool DefaultsIntegratedGpu();

class SettingsStore {
public:
    using Listener = std::function<void(const Settings&)>;

    SettingsStore();

    enum class LoadResult { Loaded, Missing, Rejected };
    // Loads `file`. Missing: the defaults (the caller writes the file). Rejected: the problems are
    // in `warnings` and the store holds the defaults; the caller replaces the file
    // (ReplaceWithDefaults). The store always ends up with a usable Settings value.
    LoadResult Load(const std::filesystem::path& file, std::vector<std::string>& warnings);
    // Parses JSON text (used by Load and by tests). False (with warnings) when rejected.
    bool LoadFromText(const std::string& text, std::vector<std::string>& warnings);
    // Deletes the file and writes the defaults in its place.
    bool ReplaceWithDefaults(std::string* error = nullptr);

    bool Save(std::string* error = nullptr) const;
    std::string ToText() const;

    const Settings& Get() const { return settings_; }

    // Changes one section and notifies its listeners.
    void Update(SettingsSection section, const std::function<void(Settings&)>& change);
    // Replaces everything (e.g. after the file changed on disk) and notifies every section whose
    // values changed.
    void Replace(const Settings& next);

    int Subscribe(SettingsSection section, Listener listener);
    void Unsubscribe(int id);

    const std::filesystem::path& File() const { return file_; }

private:
    struct Entry {
        int id;
        SettingsSection section;
        Listener listener;
    };
    void Notify(SettingsSection section);

    Settings settings_;
    std::filesystem::path file_;
    std::vector<Entry> listeners_;
    int next_id_ = 1;
};

const char* ToString(ScaleFilter v);
const char* ToString(WindowMode v);
const char* ToString(FocusLossAudio v);
const char* ToString(ScreenRatioDefault v);
const char* ToString(AntiAliasing v);

}  // namespace ac04::platform
