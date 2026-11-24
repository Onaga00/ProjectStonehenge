#include "platform/settings.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <system_error>

namespace ac04::platform {
namespace {

using json = nlohmann::ordered_json;

template <typename T>
struct EnumName {
    T value;
    const char* name;
};

constexpr EnumName<ScaleFilter> kFilters[] = {{ScaleFilter::Sharp, "sharp"},
                                              {ScaleFilter::Smooth, "smooth"}};
// "borderless" is the name earlier builds wrote for the fullscreen window.
constexpr EnumName<WindowMode> kWindowModes[] = {{WindowMode::Windowed, "windowed"},
                                                 {WindowMode::Fullscreen, "fullscreen"},
                                                 {WindowMode::Fullscreen, "borderless"}};
constexpr EnumName<FocusLossAudio> kFocusLoss[] = {{FocusLossAudio::KeepPlaying, "keep_playing"},
                                                   {FocusLossAudio::Mute, "mute"}};
constexpr EnumName<ScreenRatioDefault> kScreenRatios[] = {{ScreenRatioDefault::Monitor, "monitor"},
                                                          {ScreenRatioDefault::Game, "game"}};

constexpr EnumName<AntiAliasing> kAntiAliasing[] = {{AntiAliasing::Off, "off"},
                                                   {AntiAliasing::Fxaa, "fxaa"},
                                                   {AntiAliasing::Smaa, "smaa"}};
constexpr EnumName<TextFilter> kTextFilters[] = {{TextFilter::Original, "original"},
                                                 {TextFilter::Sharp, "sharp"}};

DisplayInfo g_DefaultsDisplay;
bool g_ValidationDefaults = false;
bool g_DefaultsIntegratedGpu = false;

template <typename T, size_t N>
const char* NameOf(const EnumName<T> (&table)[N], T value) {
    for (const auto& e : table)
        if (e.value == value) return e.name;
    return "?";
}

// Reads obj[key] into `out` when present and valid; otherwise keeps the default and warns.
class Reader {
public:
    Reader(const json& root, std::vector<std::string>& warnings) : root_(root), warnings_(warnings) {}

    // Section `name` with the keys it may hold; any other key makes the file incompatible.
    const json* Section(const char* name, std::initializer_list<const char*> keys) {
        auto it = root_.find(name);
        if (it == root_.end()) return nullptr;
        if (!it->is_object()) {
            Warn(std::string(name) + ": not an object");
            return nullptr;
        }
        for (const auto& item : it->items()) {
            bool known = false;
            for (const char* k : keys) known = known || item.key() == k;
            if (!known) Warn(std::string(name) + "." + item.key() + ": unknown key");
        }
        return &*it;
    }

    void Bool(const json* sec, const char* sec_name, const char* key, bool& out) {
        if (const json* v = Find(sec, key)) {
            if (v->is_boolean()) out = v->get<bool>();
            else Invalid(sec_name, key);
        }
    }

    void Int(const json* sec, const char* sec_name, const char* key, int& out, int lo, int hi) {
        if (const json* v = Find(sec, key)) {
            if (v->is_number_integer() && v->get<long long>() >= lo && v->get<long long>() <= hi)
                out = v->get<int>();
            else Invalid(sec_name, key);
        }
    }

    void String(const json* sec, const char* sec_name, const char* key, std::string& out) {
        if (const json* v = Find(sec, key)) {
            if (v->is_string()) out = v->get<std::string>();
            else Invalid(sec_name, key);
        }
    }

    template <typename T, size_t N>
    void Enum(const json* sec, const char* sec_name, const char* key, const EnumName<T> (&table)[N],
              T& out) {
        if (const json* v = Find(sec, key)) {
            if (v->is_string()) {
                const std::string s = v->get<std::string>();
                for (const auto& e : table) {
                    if (s == e.name) {
                        out = e.value;
                        return;
                    }
                }
            }
            Invalid(sec_name, key);
        }
    }

    void Invalid(const char* sec_name, const char* key) {
        Warn(std::string(sec_name) + "." + key + ": invalid value");
    }
    void Warn(std::string text) { warnings_.push_back(std::move(text)); }

private:
    static const json* Find(const json* sec, const char* key) {
        if (!sec) return nullptr;
        auto it = sec->find(key);
        return it == sec->end() ? nullptr : &*it;
    }

    const json& root_;
    std::vector<std::string>& warnings_;
};

bool operator==(const InputSettings& a, const InputSettings& b) {
    return a.modern_controls == b.modern_controls;
}

}  // namespace

const char* ToString(ScaleFilter v) { return NameOf(kFilters, v); }
const char* ToString(WindowMode v) { return NameOf(kWindowModes, v); }
const char* ToString(FocusLossAudio v) { return NameOf(kFocusLoss, v); }
const char* ToString(ScreenRatioDefault v) { return NameOf(kScreenRatios, v); }
const char* ToString(AntiAliasing v) { return NameOf(kAntiAliasing, v); }
const char* ToString(TextFilter v) { return NameOf(kTextFilters, v); }

Settings DefaultSettings(const DisplayInfo& display) {
    Settings s;
    s.input = DefaultInputSettings();
    // V-sync on (User request 2026-10-22; before: only at 60 and 120 Hz). At another known rate the
    // frame cap, which applies with v-sync off, is the highest one at or below it.
    s.presentation.vsync = true;
    const int hz = display.refresh_hz;
    if (hz != 0 && hz != 60 && hz != 120) {
        int cap = kFrameCaps[0];
        for (int c : kFrameCaps)
            if (c <= hz) cap = c;
        s.presentation.frame_cap = cap;
    }
    if (!g_ValidationDefaults && !g_DefaultsIntegratedGpu) {
        s.presentation.antialiasing = AntiAliasing::Fxaa;
        s.presentation.internal_scale = DefaultInternalScale(display.height);
    }
    if (g_ValidationDefaults) s.presentation.text_filter = TextFilter::Original;
    return s;
}

int DefaultInternalScale(int height) {
    constexpr int kPictureLines = 448;  // the PS2 draw buffer (ADR-006 §4)
    if (height <= 0) return 1;
    return std::clamp((height + kPictureLines / 2) / kPictureLines, 1, kMaxInternalScale);
}

void SetDefaultsDisplay(const DisplayInfo& display) { g_DefaultsDisplay = display; }
const DisplayInfo& DefaultsDisplay() { return g_DefaultsDisplay; }
void SetValidationDefaults(bool on) { g_ValidationDefaults = on; }
void SetDefaultsIntegratedGpu(bool on) { g_DefaultsIntegratedGpu = on; }
bool DefaultsIntegratedGpu() { return g_DefaultsIntegratedGpu; }

bool IsValidFrameCap(int fps) {
    for (int c : kFrameCaps)
        if (c == fps) return true;
    return false;
}

InputSettings DefaultInputSettings() {
    // The bindings' defaults are in controls.ini (platform/controls_file.cpp).
    return InputSettings{};
}

SettingsStore::SettingsStore() : settings_(DefaultSettings(g_DefaultsDisplay)) {}

SettingsStore::LoadResult SettingsStore::Load(const std::filesystem::path& file, std::vector<std::string>& warnings) {
    file_ = file;
    settings_ = DefaultSettings(g_DefaultsDisplay);
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) return LoadResult::Missing;
    std::ifstream in(file, std::ios::binary);
    std::stringstream ss;
    if (in) ss << in.rdbuf();
    if (!in) {
        warnings.push_back("settings.json cannot be read");
        return LoadResult::Rejected;
    }
    return LoadFromText(ss.str(), warnings) ? LoadResult::Loaded : LoadResult::Rejected;
}

bool SettingsStore::LoadFromText(const std::string& text, std::vector<std::string>& warnings) {
    Settings s = DefaultSettings(g_DefaultsDisplay);
    settings_ = s;
    json root = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
    if (root.is_discarded() || !root.is_object()) {
        warnings.push_back("settings.json is not valid JSON");
        return false;
    }
    const size_t first_warning = warnings.size();
    Reader r(root, warnings);
    for (const auto& item : root.items()) {
        const std::string& key = item.key();
        if (key != "version" && key != "presentation" && key != "paths" && key != "audio" && key != "input" &&
            key != "debug")
            r.Warn(key + ": unknown key");
    }
    if (auto v = root.find("version"); v != root.end() && !(v->is_number_integer() && *v == kSettingsVersion))
        r.Warn("version: not " + std::to_string(kSettingsVersion));

    if (const json* p = r.Section("presentation", {"filter", "vsync", "frame_cap", "window_mode", "window_width",
                                                   "window_height", "internal_scale", "default_screen_ratio",
                                                   "ultrawide", "ultrawide_4x3_fov", "antialiasing",
                                                   "frame_interpolation", "text_filter"})) {
        r.Enum(p, "presentation", "filter", kFilters, s.presentation.filter);
        r.Bool(p, "presentation", "vsync", s.presentation.vsync);
        int cap = s.presentation.frame_cap;
        r.Int(p, "presentation", "frame_cap", cap, 1, 100000);
        if (IsValidFrameCap(cap)) s.presentation.frame_cap = cap;
        else r.Invalid("presentation", "frame_cap");
        r.Enum(p, "presentation", "window_mode", kWindowModes, s.presentation.window_mode);
        r.Int(p, "presentation", "window_width", s.presentation.window_width, 320, 16384);
        r.Int(p, "presentation", "window_height", s.presentation.window_height, 240, 16384);
        r.Int(p, "presentation", "internal_scale", s.presentation.internal_scale, 1, kMaxInternalScale);
        r.Enum(p, "presentation", "default_screen_ratio", kScreenRatios, s.presentation.default_screen_ratio);
        r.Bool(p, "presentation", "ultrawide", s.presentation.ultrawide);
        r.Bool(p, "presentation", "ultrawide_4x3_fov", s.presentation.ultrawide_4x3_fov);
        r.Enum(p, "presentation", "antialiasing", kAntiAliasing, s.presentation.antialiasing);
        r.Bool(p, "presentation", "frame_interpolation", s.presentation.frame_interpolation);
        r.Enum(p, "presentation", "text_filter", kTextFilters, s.presentation.text_filter);
    }
    if (const json* p = r.Section("paths", {"iso"})) {
        r.String(p, "paths", "iso", s.paths.iso);
    }
    if (const json* p = r.Section("audio", {"on_focus_loss"})) {
        r.Enum(p, "audio", "on_focus_loss", kFocusLoss, s.audio.on_focus_loss);
    }
    if (const json* p = r.Section("input", {"modern_controls"})) {
        auto mc = p->find("modern_controls");
        if (mc != p->end()) {
            if (mc->is_array() && mc->size() == 2 && (*mc)[0].is_boolean() && (*mc)[1].is_boolean()) {
                s.input.modern_controls = {(*mc)[0].get<bool>(), (*mc)[1].get<bool>()};
            } else {
                r.Invalid("input", "modern_controls");
            }
        }
    }
    if (const json* p = r.Section("debug", {"overlay_enabled", "overlay_toggle", "fps_overlay"})) {
        r.Bool(p, "debug", "overlay_enabled", s.debug.overlay_enabled);
        r.Bool(p, "debug", "fps_overlay", s.debug.fps_overlay);
        r.String(p, "debug", "overlay_toggle", s.debug.overlay_toggle);
        if (s.debug.overlay_toggle.empty()) r.Invalid("debug", "overlay_toggle");
    }
    if (warnings.size() != first_warning) return false;
    settings_ = s;
    return true;
}

bool SettingsStore::ReplaceWithDefaults(std::string* error) {
    settings_ = DefaultSettings(g_DefaultsDisplay);
    std::error_code ec;
    std::filesystem::remove(file_, ec);
    return Save(error);
}

std::string SettingsStore::ToText() const {
    const Settings& s = settings_;
    json root = json::object();
    root["version"] = kSettingsVersion;
    auto& p = root["presentation"];
    p["filter"] = ToString(s.presentation.filter);
    p["vsync"] = s.presentation.vsync;
    p["frame_cap"] = s.presentation.frame_cap;
    p["window_mode"] = ToString(s.presentation.window_mode);
    p["window_width"] = s.presentation.window_width;
    p["window_height"] = s.presentation.window_height;
    p["internal_scale"] = s.presentation.internal_scale;
    p["default_screen_ratio"] = ToString(s.presentation.default_screen_ratio);
    p["ultrawide"] = s.presentation.ultrawide;
    p["ultrawide_4x3_fov"] = s.presentation.ultrawide_4x3_fov;
    p["antialiasing"] = ToString(s.presentation.antialiasing);
    p["frame_interpolation"] = s.presentation.frame_interpolation;
    p["text_filter"] = ToString(s.presentation.text_filter);
    root["paths"]["iso"] = s.paths.iso;
    root["audio"]["on_focus_loss"] = ToString(s.audio.on_focus_loss);
    auto& in = root["input"];
    in["modern_controls"] = json::array({s.input.modern_controls[0], s.input.modern_controls[1]});
    auto& d = root["debug"];
    d["overlay_enabled"] = s.debug.overlay_enabled;
    d["overlay_toggle"] = s.debug.overlay_toggle;
    d["fps_overlay"] = s.debug.fps_overlay;
    return root.dump(2) + "\n";
}

bool SettingsStore::Save(std::string* error) const {
    if (file_.empty()) {
        if (error) *error = "no settings file path";
        return false;
    }
    // Write to a temporary file, then replace, so a crash never leaves a half-written file.
    std::filesystem::path tmp = file_;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            if (error) *error = "cannot write " + tmp.u8string();
            return false;
        }
        out << ToText();
        if (!out) {
            if (error) *error = "write failed: " + tmp.u8string();
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file_, ec);
    if (ec) {
        if (error) *error = "cannot replace " + file_.u8string() + ": " + ec.message();
        return false;
    }
    return true;
}

void SettingsStore::Update(SettingsSection section, const std::function<void(Settings&)>& change) {
    change(settings_);
    Notify(section);
}

void SettingsStore::Replace(const Settings& next) {
    const Settings prev = settings_;
    settings_ = next;
    const auto& a = prev.presentation;
    const auto& b = next.presentation;
    if (a.filter != b.filter || a.vsync != b.vsync || a.frame_cap != b.frame_cap ||
        a.window_mode != b.window_mode || a.window_width != b.window_width ||
        a.window_height != b.window_height || a.internal_scale != b.internal_scale ||
        a.default_screen_ratio != b.default_screen_ratio || a.ultrawide != b.ultrawide ||
        a.ultrawide_4x3_fov != b.ultrawide_4x3_fov ||
        a.antialiasing != b.antialiasing || a.frame_interpolation != b.frame_interpolation ||
        a.text_filter != b.text_filter)
        Notify(SettingsSection::Presentation);
    if (prev.paths.iso != next.paths.iso) Notify(SettingsSection::Paths);
    if (prev.audio.on_focus_loss != next.audio.on_focus_loss) Notify(SettingsSection::Audio);
    if (!(prev.input == next.input)) Notify(SettingsSection::Input);
    if (prev.debug.overlay_enabled != next.debug.overlay_enabled ||
        prev.debug.overlay_toggle != next.debug.overlay_toggle || prev.debug.fps_overlay != next.debug.fps_overlay)
        Notify(SettingsSection::Debug);
}

int SettingsStore::Subscribe(SettingsSection section, Listener listener) {
    listeners_.push_back({next_id_, section, std::move(listener)});
    return next_id_++;
}

void SettingsStore::Unsubscribe(int id) {
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
        if (it->id == id) {
            listeners_.erase(it);
            return;
        }
    }
}

void SettingsStore::Notify(SettingsSection section) {
    for (const auto& e : listeners_)
        if (e.section == section) e.listener(settings_);
}

}  // namespace ac04::platform
