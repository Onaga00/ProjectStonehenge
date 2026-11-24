// ProjectStonehenge entry point. Startup order: command line -> paths -> log -> settings -> window ->
// renderer -> disc image check -> executable into guest memory -> audio -> input -> main loop.
// Milestone B: no game code runs yet; the window shows the test pattern through the output pass.
#include <algorithm>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "build_info.h"
#include "common/guest_memory.h"
#include "common/debug_hooks.h"
#include "common/port_hooks.h"
#include "common/guest_stack.h"
#include "game/game.h"
#include "platform/audio.h"
#include "platform/command_line.h"
#include "platform/controls_file.h"
#include "platform/crash.h"
#include "platform/debug/overlay.h"
#include "platform/elf_loader.h"
#include "platform/frame_pacer.h"
#include "platform/iop/iop_services.h"
#include "platform/hash_log.h"
#include "platform/input.h"
#include "platform/input_log.h"
#include "platform/p2m2_reader.h"
#include "platform/iso_image.h"
#include "platform/log.h"
#include "platform/paths.h"
#include "platform/png_writer.h"
#include "platform/audio/spu_mixer.h"
#include "platform/render/gs_dump.h"
#include "platform/render/frame_interpolator.h"
#include "platform/render/render_thread.h"
#include "platform/render/renderer.h"
#include "platform/settings.h"
#include "platform/system_info.h"
#include "platform/window.h"
#include "sce/hw_registers.h"
#include "sce/runtime.h"
#include "sce/sce_host.h"

namespace {

using namespace ac04;
using namespace ac04::platform;

// --pad-script (development builds): a pad-1 button pressed for 6 vblanks from `vblank`.
struct ScriptedPress {
    u64 vblank;
    std::uint16_t bit;
};

// The pads each game tick reads (docs/port/slices.md §B P1). The main thread latches both pads before
// it releases a tick, so the game sees exactly one state per tick whatever the host frame rate:
// live devices in normal play; with a pad script or an input log only those (validation runs, no
// live device can leak in). Every latched tick can be written to an input log and replayed.
struct TickInputFeed;
TickInputFeed* g_feed = nullptr;

struct TickInputFeed {
    platform::InputBackend* input = nullptr;
    std::vector<ScriptedPress> script;
    platform::InputLogReader replay;
    platform::InputLogWriter record;
    platform::P2m2Reader p2m2;
    int p2m2_offset = 0;
    bool use_replay = false;
    bool use_p2m2 = false;
    bool p2m2_end_logged = false;
    // Modern control types per player (settings input.modern_controls): live devices only; scripts,
    // input logs and PCSX2 recordings already hold what the game reads.
    std::array<bool, 2> modern{};
    platform::PadState latched[2];

    bool Live() const { return !use_replay && !use_p2m2 && script.empty(); }

    // Pads for the tick that will have vblank count `vblank` (0 = boot, before the first tick).
    void Latch(u64 vblank) {
        platform::PadState pads[2];
        if (use_p2m2) {
            const s64 frame = static_cast<s64>(vblank) + p2m2_offset;
            if (vblank == 0 || frame < 0 || !p2m2.Frame(static_cast<u32>(frame), pads)) {
                platform::NeutralPads(pads);
                if (vblank != 0 && frame >= 0 && !p2m2_end_logged) {
                    AC04_LOG_INFO("input", "PCSX2 recording ended at tick %llu (pads neutral from here)",
                                  static_cast<unsigned long long>(vblank));
                    p2m2_end_logged = true;
                }
            }
        } else if (use_replay) {
            if (!replay.Get(vblank, pads)) platform::NeutralPads(pads);
        } else if (!script.empty()) {
            if (vblank == 0) {
                pads[0] = platform::PadState{};  // as the live backend before its first update
                pads[1] = platform::PadState{};
            } else {
                platform::NeutralPads(pads);
            }
        } else {
            pads[0] = input->Port(0);
            pads[1] = input->Port(1);
            for (int p = 0; p < 2; ++p)
                if (modern[p]) platform::SwapShoulderButtons(pads[p]);
        }
        for (const ScriptedPress& sp : script) {
            if (vblank >= sp.vblank && vblank < sp.vblank + 6) {
                pads[0].connected = true;
                pads[0].buttons = static_cast<std::uint16_t>(pads[0].buttons & ~sp.bit);
            }
        }
        latched[0] = pads[0];
        latched[1] = pads[1];
        // The digital map cycle travels with the pads (input logs), so replays see it too.
        for (int p = 0; p < 2; ++p) port::Input().digital_map_cycle[p] = pads[p].map_cycle;
        if (record.IsOpen() && vblank != 0) record.Write(platform::TickInput{vblank, {pads[0], pads[1]}});
    }
};

constexpr const char* kAppTitle = "ProjectStonehenge";

std::string VersionText() {
    return std::string(kAppTitle) + " " + AC04_VERSION_STRING + " (" + AC04_GIT_COMMIT + ", " +
           (AC04_DEV_BUILD ? "development" : "release") + " build)";
}

// Disc image check and executable load (milestone B). Returns false when the port must exit (the
// reason has been shown and logged). `unverified` is set when a development build continues
// with --allow-unverified-iso.
// Research aid (development builds): guest RAM + scratchpad for tools/research/ramdiff.py.
// Averages each n x n block of an RGBA8 image (rounded); w and h become the reduced size.
void BoxDownscale(std::vector<uint8_t>& rgba, int& w, int& h, int n) {
    const int ow = w / n, oh = h / n;
    std::vector<uint8_t> out(static_cast<size_t>(ow) * oh * 4);
    for (int y = 0; y < oh; ++y) {
        for (int x = 0; x < ow; ++x) {
            for (int c = 0; c < 4; ++c) {
                unsigned sum = 0;
                for (int j = 0; j < n; ++j) {
                    const uint8_t* row = &rgba[(static_cast<size_t>(y * n + j) * w + static_cast<size_t>(x) * n) * 4 + c];
                    for (int i = 0; i < n; ++i) sum += row[i * 4];
                }
                out[(static_cast<size_t>(y) * ow + x) * 4 + c] = static_cast<uint8_t>((sum + n * n / 2) / (n * n));
            }
        }
    }
    rgba.swap(out);
    w = ow;
    h = oh;
}

// Writes the native image as `<stem>.png`. At internal scale N > 1 the image as rendered goes to
// `<prefix>internal_x<N><suffix>.png` and `<stem>.png` is its N x N box average at the native size,
// for the comparisons with PCSX2's native screenshots (tools/research/imgdiff.py).
bool WriteNativeScreenshot(render::Renderer& renderer, const std::filesystem::path& dir, const std::string& stem,
                           const std::string& scaled_stem) {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
    if (!renderer.CaptureNative(rgba, w, h)) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    bool ok = true;
    const int scale = renderer.NativeScale();
    if (scale > 1) {
        ok = WritePng(dir / (scaled_stem + ".png"), rgba.data(), w, h);
        BoxDownscale(rgba, w, h, scale);
    }
    ok = WritePng(dir / (stem + ".png"), rgba.data(), w, h) && ok;
    if (!ok) AC04_LOG_ERROR("renderer", "cannot write the screenshot %s", stem.c_str());
    return true;
}

// Written to the user data folder, never into the repo: it contains game data.
void DumpGuestRam(const Paths& paths, const char* name) {
    const auto dir = paths.user_data / "dumps";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / name;
    if (std::FILE* f = platform::OpenFile(file, "wb")) {
        std::fwrite(guest::Mem().Ram(), 1, guest::kRamSize, f);
        std::fwrite(guest::Mem().Scratchpad(), 1, guest::kScratchpadSize, f);
        std::fclose(f);
        AC04_LOG_INFO("loader", "guest RAM dumped to %s (vblank %llu)", PathToUtf8(file).c_str(),
                      static_cast<unsigned long long>(sce::GameRuntime::Get().VBlankCount()));
    } else {
        AC04_LOG_ERROR("loader", "cannot write %s", PathToUtf8(file).c_str());
    }
}

#if AC04_DEV_BUILD
// --play-gsdump (development builds): draws a PCSX2 single-frame GS dump with the port's renderer and
// writes the native picture and PCSX2's own screenshot side by side, then the port exits. Checks the
// renderer on exactly the input PCSX2 drew, without the game (docs/port/command-line.md).
int PlayGsDump(const CommandLine& cl, const Paths& paths, render::Renderer& renderer) {
    render::GsDump dump;
    std::string error;
    if (!render::LoadGsDump(PathFromUtf8(cl.play_gsdump), dump, error)) {
        AC04_LOG_ERROR("renderer", "--play-gsdump: %s", error.c_str());
        return 1;
    }
    u64 dispfb = 0, display = 0;
    std::memcpy(&dispfb, dump.privileged.data() + 0x90, 8);
    std::memcpy(&display, dump.privileged.data() + 0xA0, 8);
    AC04_LOG_INFO("renderer", "GS dump: %zu frame(s), DISPFB2 %016llX", dump.frames.size(),
                  static_cast<unsigned long long>(dispfb));
    // Twice: the native image takes the presented size one frame late.
    for (int pass = 0; pass < 2; ++pass) {
        renderer.LoadGsLocalMemory(dump.memory.data());
        for (const auto& frame : dump.frames) {
            std::vector<std::vector<u8>> steps{frame};
            std::vector<std::vector<render::gs::SourceMark>> marks;
            std::vector<std::vector<u8>> pool;
            renderer.SubmitGsSteps(steps, marks, pool);
            renderer.SetDisplay(dispfb, display);
            if (!renderer.DrawFrame()) return 1;
        }
    }
    const auto dir = paths.user_data / "screenshots";
    WriteNativeScreenshot(renderer, dir, "gsdump_native",
                          "gsdump_internal_x" + std::to_string(renderer.NativeScale()));
    if (!dump.shot_rgba.empty())
        WritePng(dir / "gsdump_pcsx2.png", dump.shot_rgba.data(), dump.shot_width, dump.shot_height);
    AC04_LOG_INFO("renderer", "GS dump drawn: screenshots/gsdump_native.png, gsdump_pcsx2.png");
    return 0;
}
#endif

// `chosen` receives the path the user picked with "Select ISO..." in the problem message (the
// caller saves it and calls again); empty when the user quit.
bool PrepareDisc(const CommandLine& cl, const Paths& paths, const Settings& settings, Window& window,
                 render::Renderer& renderer, IsoImage& image, LoadedExecutable& exe, bool& unverified,
                 std::string& chosen) {
    unverified = false;
    chosen.clear();
    const std::filesystem::path iso_path = PathFromUtf8(settings.paths.iso);
    auto fail = [&](const IsoCheck& check) {
        const MessageText text = DescribeIsoProblem(check, iso_path, paths.settings_file);
        AC04_LOG_ERROR("iso", "%s", FormatMessageText(text).c_str());
        const int button = ShowChoiceBox(std::string(kAppTitle) + " - Disc image", text,
                                         {"Select ISO...", "Quit"}, true, &window);
        if (button == 0) chosen = ChooseFile(&window, "PlayStation 2 disc image (*.iso)", "iso");
        return false;
    };

    IsoCheck quick = image.OpenAndQuickCheck(iso_path);
    if (quick.result == IsoCheckResult::WrongImage && cl.allow_unverified_iso) {
        AC04_LOG_WARN("iso", "not the supported image (%s); continuing (--allow-unverified-iso)", quick.detail.c_str());
        unverified = true;
    } else if (quick.result != IsoCheckResult::Ok) {
        return fail(quick);
    }
    AC04_LOG_INFO("iso", "image: %s (volume '%s')", PathToUtf8(iso_path).c_str(),
                  image.Iso().Volume().volume_id.c_str());

    if (!unverified) {
        // Full SHA-1 on a worker thread; the window keeps responding and shows the progress.
        std::atomic<bool> cancel{false}, done{false};
        std::atomic<int> percent{0};
        IsoCheck full;
        std::thread worker([&] {
            full = image.FullCheck(paths.user_data / "iso_cache.json", cl.verify_iso,
                                   [&](double p) { percent = static_cast<int>(p * 100.0); }, cancel);
            done = true;
        });
        int shown = -1;
        while (!done) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) cancel = true;
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) renderer.OnWindowResized();
            }
            if (percent != shown) {
                shown = percent;
                window.SetTitle(VersionText() + " - verifying disc image " + std::to_string(shown) + "%");
            }
            renderer.DrawFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
        worker.join();
        window.SetTitle(VersionText());
        if (full.result == IsoCheckResult::Cancelled) {
            AC04_LOG_INFO("iso", "verification cancelled by the user");
            return false;
        }
        if (full.result == IsoCheckResult::HashMismatch && cl.allow_unverified_iso) {
            AC04_LOG_WARN("iso", "checksum mismatch (%s); continuing (--allow-unverified-iso)", full.detail.c_str());
            unverified = true;
        } else if (full.result != IsoCheckResult::Ok) {
            return fail(full);
        } else {
            AC04_LOG_INFO("iso", "verified: SHA-1 %s (%s)", full.found_sha1.c_str(),
                          full.from_cache ? "cached result" : "computed now");
        }
    }

    std::string error;
    guest::Mem().Reset();
    guest::ResetStack();
    if (!LoadGameExecutable(image.Iso(), guest::Mem(), unverified, exe, error)) {
        AC04_LOG_ERROR("loader", "%s", error.c_str());
        MessageText m;
        m.headline = "The game could not be started";
        m.explanation = "The game program (SLUS_201.52) could not be loaded from the disc image.";
        m.steps = {"Make a new, unmodified dump of your disc and choose it in settings.json (\"paths\": \"iso\").",
                   "If the problem stays, keep the session log for a bug report."};
        m.details = {{"Reason", error}, {"Disc image", PathToUtf8(iso_path)}, {"Session log", PathToUtf8(log::FilePath())}};
        ShowErrorBox(std::string(kAppTitle) + " - Error", m, &window);
        return false;
    }
    for (const auto& seg : exe.segments)
        AC04_LOG_INFO("loader", "segment 0x%08X: %u bytes from the file, %u in memory", seg.vaddr,
                      seg.file_size, seg.mem_size);
    for (const auto& z : exe.zeroed) AC04_LOG_INFO("loader", "zeroed 0x%08X (+0x%X)", z.first, z.second);
    AC04_LOG_INFO("loader", "%s loaded: entry 0x%08X, gp 0x%08X, SHA-1 %s, guest sp 0x%08X",
                  SupportedImage::kExecutableName, exe.entry, exe.gp, exe.sha1.c_str(), guest::StackPointer());
    if (unverified) window.SetTitle(VersionText() + " [UNVERIFIED DISC IMAGE]");
    if (cl.dump_ram_after_load) DumpGuestRam(paths, "ram_after_load.bin");
    return true;
}

// The desktop mode of a display (the monitor the port starts on): size and refresh rate.
DisplayInfo QueryDisplay(SDL_DisplayID id) {
    DisplayInfo d;
    if (const SDL_DisplayMode* m = SDL_GetDesktopDisplayMode(id)) {
        d.width = m->w;
        d.height = m->h;
        d.refresh_hz = static_cast<int>(std::lround(m->refresh_rate));
    }
    return d;
}

// Refresh period of the display the window is on, in seconds (0 when unknown).
double DisplayRefreshSeconds(SDL_Window* window) {
    const SDL_DisplayMode* m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    if (m == nullptr) return 0.0;
    if (m->refresh_rate_numerator > 0 && m->refresh_rate_denominator > 0)
        return static_cast<double>(m->refresh_rate_denominator) / m->refresh_rate_numerator;
    return m->refresh_rate > 0.0f ? 1.0 / m->refresh_rate : 0.0;
}

// The frame cap in force: none with v-sync on (the presentation engine paces the frames).
int EffectiveFrameCap(const PresentationSettings& p) { return p.vsync ? 0 : p.frame_cap; }

// A rejected settings.json: what was wrong, and that the file is being replaced.
void ReportRejected(const std::vector<std::string>& problems) {
    for (const auto& w : problems) AC04_LOG_WARN("settings", "%s", w.c_str());
    AC04_LOG_WARN("settings", "settings.json rejected; deleted and replaced with the defaults");
}

// The GRAPHICS page's view of the presentation settings (port_hooks.h).
port::GraphicsOptions ToGraphicsOptions(const PresentationSettings& p) {
    port::GraphicsOptions g;
    g.fullscreen = p.window_mode == WindowMode::Fullscreen;
    g.vsync = p.vsync;
    g.frame_cap = p.frame_cap;
    g.internal_scale = p.internal_scale;
    g.smooth_filter = p.filter == ScaleFilter::Smooth;
    g.antialiasing = static_cast<s32>(p.antialiasing);
    g.frame_interpolation = p.frame_interpolation;
    g.sharp_text = p.text_filter == TextFilter::Sharp;
    g.ultrawide = p.ultrawide;
    g.ultrawide_4x3_fov = p.ultrawide_4x3_fov;
    return g;
}

void ApplyGraphicsOptions(const port::GraphicsOptions& g, PresentationSettings& p) {
    p.window_mode = g.fullscreen ? WindowMode::Fullscreen : WindowMode::Windowed;
    p.vsync = g.vsync;
    if (IsValidFrameCap(g.frame_cap)) p.frame_cap = g.frame_cap;
    p.internal_scale = std::clamp(static_cast<int>(g.internal_scale), 1, platform::kMaxInternalScale);
    p.filter = g.smooth_filter ? ScaleFilter::Smooth : ScaleFilter::Sharp;
    if (g.antialiasing >= 0 && g.antialiasing <= 2) p.antialiasing = static_cast<AntiAliasing>(g.antialiasing);
    p.frame_interpolation = g.frame_interpolation;
    p.text_filter = g.sharp_text ? TextFilter::Sharp : TextFilter::Original;
    p.ultrawide = g.ultrawide;
    p.ultrawide_4x3_fov = g.ultrawide_4x3_fov;
}

// Reloads settings.json when it changes on disk, so values can be edited while the port runs.
// Checked once per second.
class SettingsWatcher {
public:
    // `overrides` re-applies the session-only command-line values to the reloaded settings.
    SettingsWatcher(SettingsStore& store, std::function<void(Settings&)> overrides)
        : store_(store), overrides_(std::move(overrides)) {
        Remember();
    }

    void Poll() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_check_ < std::chrono::seconds(1)) return;
        last_check_ = now;
        std::error_code ec;
        const auto time = std::filesystem::last_write_time(store_.File(), ec);
        if (ec || time == last_time_) return;
        last_time_ = time;
        SettingsStore fresh;
        std::vector<std::string> warnings;
        if (fresh.Load(store_.File(), warnings) == SettingsStore::LoadResult::Rejected) {
            ReportRejected(warnings);
            std::string error;
            if (!fresh.ReplaceWithDefaults(&error))
                AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
            Remember();
        }
        AC04_LOG_INFO("settings", "settings.json changed on disk, reloaded");
        Settings next = fresh.Get();
        overrides_(next);
        store_.Replace(next);
    }
    void Remember() {
        std::error_code ec;
        last_time_ = std::filesystem::last_write_time(store_.File(), ec);
    }

private:
    SettingsStore& store_;
    std::function<void(Settings&)> overrides_;
    std::filesystem::file_time_type last_time_{};
    std::chrono::steady_clock::time_point last_check_{};
};

int Run(int argc, char** argv) {
    const CommandLineResult cl = ParseCommandLine(argc, argv);
    if (cl.options.show_help || !cl.errors.empty()) {
        std::string text;
        for (const auto& e : cl.errors) text += "Error: " + e + "\n";
        if (!text.empty()) text += "\n";
        text += CommandLineHelp();
        std::fputs(text.c_str(), stdout);
        if (!AC04_DEV_BUILD) ShowInfoBox(kAppTitle, text);
        return cl.errors.empty() ? 0 : 2;
    }
    if (cl.options.show_version) {
        std::puts(VersionText().c_str());
        if (!AC04_DEV_BUILD) ShowInfoBox(kAppTitle, VersionText());
        return 0;
    }

    InstallCrashHandler();
    InitTimerResolution();

    Paths paths;
    std::string error;
    if (!ResolvePaths(PathFromUtf8(cl.options.user_data_dir), paths, error)) {
        MessageText m;
        m.headline = "The port cannot create its data folder";
        m.explanation = "Settings, saves and the session log are kept in this folder.";
        m.steps = {"Check that the drive has free space and that the folder can be written.",
                   "Or start the port with  --user-data \"<another folder>\""};
        m.details = {{"Reason", error}};
        ShowErrorBox(std::string(kAppTitle) + " - Error", m);
        return 1;
    }
    log::Open(paths.logs);
    AC04_LOG_INFO("startup", "%s", VersionText().c_str());
    AC04_LOG_INFO("startup", "OS: %s", OperatingSystemDescription().c_str());
    AC04_LOG_INFO("startup", "user data: %s", PathToUtf8(paths.user_data).c_str());

    PrepareInputHints();  // pressure-sensitive buttons of DualShock 3 pads (input.h)
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        AC04_LOG_ERROR("startup", "SDL_Init failed: %s", SDL_GetError());
        MessageText m;
        m.headline = "The port could not start";
        m.explanation = "The window, sound and controller system (SDL) could not be initialised.";
        m.steps = {"Update the graphics and audio drivers, then restart the computer."};
        m.details = {{"Reason", SDL_GetError()}};
        ShowErrorBox(std::string(kAppTitle) + " - Error", m);
        return 1;
    }
    // Defaults follow the monitor the port starts on (the primary display, where the window opens):
    // fullscreen, v-sync, FXAA, the internal scale closest to its height, the SCREEN RATIO default
    // (settings.h DefaultSettings). Validation runs (development builds: a pad script, an input log,
    // a PCSX2 recording, --memcard or --no-memcard) keep 1x and no anti-aliasing, so their
    // screenshots compare with the baselines.
    const DisplayInfo start_display = QueryDisplay(SDL_GetPrimaryDisplay());
    SetDefaultsDisplay(start_display);
    const bool validation_run = !cl.options.pad_script.empty() || !cl.options.replay_input.empty() ||
                                !cl.options.replay_p2m2.empty() || !cl.options.memcard_dir.empty() ||
                                cl.options.no_memcard;
    SetValidationDefaults(validation_run);
    AC04_LOG_INFO("startup", "display: %dx%d, %d Hz (%s); default internal scale %dx%s", start_display.width,
                  start_display.height, start_display.refresh_hz, start_display.Wide() ? "wider than 4:3" : "4:3",
                  DefaultSettings(start_display).presentation.internal_scale,
                  validation_run ? " (validation run)" : "");

    SettingsStore settings;
    bool settings_from_defaults = false;  // settings.json written with the defaults this session
    {
        std::vector<std::string> warnings;
        const auto result = settings.Load(paths.settings_file, warnings);
        settings_from_defaults = result != SettingsStore::LoadResult::Loaded;
        if (result == SettingsStore::LoadResult::Rejected) {
            // Unreadable, invalid or incompatible: deleted and replaced with the defaults.
            ReportRejected(warnings);
            if (!settings.ReplaceWithDefaults(&error))
                AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
        } else if (result == SettingsStore::LoadResult::Missing) {
            if (!settings.Save(&error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
        }
    }
    // Session-only overrides from the command line (not saved).
    const auto apply_overrides = [&cl](Settings& s) {
        if (cl.options.debug_overlay.has_value()) s.debug.overlay_enabled = *cl.options.debug_overlay;
        if (!cl.options.iso_path.empty()) s.paths.iso = cl.options.iso_path;
        if (cl.options.internal_scale) s.presentation.internal_scale = static_cast<int>(*cl.options.internal_scale);
        if (cl.options.fps_overlay.has_value()) s.debug.fps_overlay = *cl.options.fps_overlay;
        if (!cl.options.antialiasing.empty())
            s.presentation.antialiasing = cl.options.antialiasing == "smaa"   ? AntiAliasing::Smaa
                                          : cl.options.antialiasing == "fxaa" ? AntiAliasing::Fxaa
                                                                              : AntiAliasing::Off;
    };
    {
        Settings s = settings.Get();
        apply_overrides(s);
        settings.Replace(s);
    }
    AC04_LOG_INFO("startup", "settings in effect:\n%s", settings.ToText().c_str());

    Window window;
    if (!window.Create(VersionText(), settings.Get().presentation, error)) {
        AC04_LOG_ERROR("startup", "%s", error.c_str());
        MessageText m;
        m.headline = "The game window could not be opened";
        m.steps = {"Update the graphics driver.", "If it happens again, keep the session log for a bug report."};
        m.details = {{"Reason", error}, {"Session log", PathToUtf8(log::FilePath())}};
        ShowErrorBox(std::string(kAppTitle) + " - Error", m);
        return 1;
    }

    render::Renderer renderer;
    renderer.SetGpuProfile(cl.options.gpu_profile);
    if (!renderer.Init(window.Handle(), settings.Get().presentation, cl.options.vk_validation,
                       paths.pipeline_cache_file, error)) {
        AC04_LOG_ERROR("renderer", "%s", error.c_str());
        MessageText m;
        m.headline = "The graphics renderer could not start";
        m.explanation = "The port needs a graphics card (GPU) with Vulkan 1.2 support and a driver from 2021 or later.";
        m.steps = {"Update the graphics driver.",
                   "On a computer with two GPUs, make sure the port runs on the dedicated one (Windows graphics settings)."};
        m.details = {{"Reason", error}, {"Session log", PathToUtf8(log::FilePath())}};
        ShowErrorBox(std::string(kAppTitle) + " - Error", m, &window);
        return 1;
    }
    // Integrated GPU (User decision 2026-11-03): the defaults keep internal scale 1 and anti-aliasing
    // off. The GPU is known only now, so a settings file written with the defaults in this session
    // (first start, or a rejected file) gets those two values, in the file and in effect, unless
    // the command line set them. A file that already existed keeps the player's values.
    if (renderer.Context().gpu.integrated) {
        SetDefaultsIntegratedGpu(true);
        const PresentationSettings d = DefaultSettings(start_display).presentation;
        AC04_LOG_INFO("startup", "integrated GPU: default internal scale %dx, anti-aliasing %s", d.internal_scale,
                      ToString(d.antialiasing));
        if (settings_from_defaults) {
            SettingsStore saved;
            std::vector<std::string> ignored;
            saved.Load(paths.settings_file, ignored);
            Settings file = saved.Get();
            file.presentation.internal_scale = d.internal_scale;
            file.presentation.antialiasing = d.antialiasing;
            saved.Replace(file);
            if (!saved.Save(&error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
            Settings now = settings.Get();
            if (!cl.options.internal_scale) now.presentation.internal_scale = d.internal_scale;
            if (cl.options.antialiasing.empty()) now.presentation.antialiasing = d.antialiasing;
            if (now.presentation.internal_scale != settings.Get().presentation.internal_scale ||
                now.presentation.antialiasing != settings.Get().presentation.antialiasing) {
                settings.Replace(now);  // no listeners yet: the renderer takes it directly
                renderer.ApplySettings(now.presentation);
            }
        }
    }
    // GS textures are decoded on the GPU; the CPU reference decoder in development builds only.
    if (cl.options.verify_texture_decode) renderer.SetTextureDecode(render::gs::TextureDecode::kBoth);
    else if (cl.options.cpu_texture_decode) renderer.SetTextureDecode(render::gs::TextureDecode::kCpu);

#if AC04_DEV_BUILD
    if (!cl.options.play_gsdump.empty()) {
        const int rc = PlayGsDump(cl.options, paths, renderer);
        renderer.Shutdown();
        window.Destroy();
        SDL_Quit();
        log::Close();
        return rc;
    }
#endif
    // Disc image check; a problem message can let the user pick another image, which is saved to
    // settings.json (file values) and checked again.
    std::unique_ptr<IsoImage> disc;
    LoadedExecutable executable;
    bool unverified_disc = false;
    for (;;) {
        disc = std::make_unique<IsoImage>();
        std::string chosen;
        if (PrepareDisc(cl.options, paths, settings.Get(), window, renderer, *disc, executable, unverified_disc, chosen))
            break;
        if (chosen.empty()) {
            renderer.Shutdown();
            window.Destroy();
            SDL_Quit();
            log::Close();
            return 1;
        }
        AC04_LOG_INFO("iso", "disc image chosen: %s", chosen.c_str());
        Settings next = settings.Get();
        next.paths.iso = chosen;
        settings.Replace(next);
        SettingsStore saved;
        std::vector<std::string> ignored;
        saved.Load(paths.settings_file, ignored);  // file values (a rejected file gives the defaults)
        Settings file = saved.Get();
        file.paths.iso = chosen;
        saved.Replace(file);
        if (!saved.Save(&error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
    }

    AudioOutput audio;
    if (audio.Open(error)) {
        AC04_LOG_INFO("audio", "output: %s, 48000 Hz stereo", audio.DeviceName().c_str());
    } else {
        // The port keeps running without sound.
        AC04_LOG_ERROR("audio", "%s", error.c_str());
    }
    audio.ApplySettings(settings.Get().audio);

    InputBackend input;
    input.ApplySettings(settings.Get().input);
    // Key, mouse and gamepad bindings (controls.ini, written with the defaults when missing).
    ControlsFile controls_file(paths.controls_file);
    input.ApplyControls(controls_file.Load());
    input.Start();
    // The mouse steers only in flight, with the window focused and the debug overlay closed; the
    // window then keeps the pointer (relative mouse mode).
    bool mouse_steers = false;

    FramePacer pacer;
    pacer.SetCap(EffectiveFrameCap(settings.Get().presentation));

    // Fixed-step clock of the simulation (ADR-002).
    sce::TickClock clock;
    // Render thread (render_thread.h): started before the main loop. Until then the renderer is
    // used directly; afterwards only the render thread calls it, and changes for it travel with the
    // next frame (presentation settings, window size) or wait until it is idle (debug overlay).
    render::RenderThread render_thread;
    std::optional<PresentationSettings> pending_presentation;
    // Presented frames since the last presentation change (its log line reports their rate).
    u64 period_frames = 0;
    u64 period_dropped = 0;
    auto period_start = std::chrono::steady_clock::now();
    // Change notifications: the platform modules follow settings.json at run time.
    settings.Subscribe(SettingsSection::Presentation, [&](const Settings& s) {
        const auto now = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(now - period_start).count();
        AC04_LOG_INFO("settings", "previous presentation: %llu frames in %.1f s (%.1f fps), %llu ticks dropped",
                      static_cast<unsigned long long>(period_frames), secs,
                      secs > 0.0 ? static_cast<double>(period_frames) / secs : 0.0,
                      static_cast<unsigned long long>(clock.DroppedTicks() - period_dropped));
        period_frames = 0;
        period_dropped = clock.DroppedTicks();
        period_start = now;
        if (render_thread.Running()) pending_presentation = s.presentation;
        else renderer.ApplySettings(s.presentation);
        window.ApplyMode(s.presentation);
        pacer.SetCap(EffectiveFrameCap(s.presentation));
        AC04_LOG_INFO("settings", "presentation: filter %s, vsync %s, cap %d%s, %s, internal scale %d, anti-aliasing %s, frame interpolation %s, text %s",
                      ToString(s.presentation.filter), s.presentation.vsync ? "on" : "off", s.presentation.frame_cap,
                      s.presentation.vsync ? " (not used)" : "", ToString(s.presentation.window_mode),
                      s.presentation.internal_scale, ToString(s.presentation.antialiasing),
                      s.presentation.frame_interpolation ? "on" : "off", ToString(s.presentation.text_filter));
    });
    settings.Subscribe(SettingsSection::Audio, [&](const Settings& s) { audio.ApplySettings(s.audio); });
    settings.Subscribe(SettingsSection::Input, [&](const Settings& s) { input.ApplySettings(s.input); });
    // Debug overlay (optional debug layer, milestone D): created only when enabled in settings.
    DebugOverlay overlay;
    if (!overlay.Apply(settings.Get().debug, window.Handle(), renderer, error)) {
        AC04_LOG_ERROR("overlay", "debug overlay unavailable: %s", error.c_str());
    }
    settings.Subscribe(SettingsSection::Debug, [&](const Settings& s) {
        std::string overlay_error;
        render_thread.WaitIdle();  // the overlay sets the renderer's hook and waits for the device
        if (!overlay.Apply(s.debug, window.Handle(), renderer, overlay_error))
            AC04_LOG_ERROR("overlay", "debug overlay unavailable: %s", overlay_error.c_str());
    });
    SettingsWatcher watcher(settings, apply_overrides);
    // What the CONTROLLER SETUP and GRAPHICS pages changed in a tick (port_hooks.h): applied at
    // once; saved to settings.json on ACCEPT (file values, so session-only overrides are not saved).
    // The GRAPHICS page hands over every change while it is open (save_graphics only on ACCEPT).
    // Writes a change into settings.json's own values (session-only command-line values are not saved).
    const auto save_settings_file = [&](const std::function<void(Settings&)>& change) {
        SettingsStore saved;
        std::vector<std::string> ignored;
        saved.Load(paths.settings_file, ignored);  // a rejected file gives the defaults
        Settings file = saved.Get();
        change(file);
        saved.Replace(file);
        std::string save_error;
        if (!saved.Save(&save_error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", save_error.c_str());
        watcher.Remember();
    };
    const auto apply_menu_requests = [&]() {
        port::MenuOptions& menu = port::Menu();
        if (!menu.set_modern_controls && !menu.set_graphics) return;
        const auto modern = menu.set_modern_controls;
        const auto graphics = menu.set_graphics;
        const bool save = modern.has_value() || menu.save_graphics;
        menu.set_modern_controls.reset();
        menu.set_graphics.reset();
        menu.save_graphics = false;
        const auto change = [&](Settings& s) {
            if (modern) s.input.modern_controls = *modern;
            if (graphics) ApplyGraphicsOptions(*graphics, s.presentation);
        };
        Settings next = settings.Get();
        change(next);
        settings.Replace(next);
        if (save) save_settings_file(change);
    };
    // The debug overlay's Graphics panel (presentation only): applied at once, saved on request.
    const auto apply_overlay_requests = [&]() {
        const auto request = overlay.TakePresentationRequest();
        if (!request) return;
        const auto change = [&](Settings& s) { s.presentation = request->values; };
        Settings next = settings.Get();
        change(next);
        settings.Replace(next);
        if (request->save) {
            save_settings_file(change);
            AC04_LOG_INFO("settings", "presentation saved to settings.json from the debug overlay");
        }
    };

    // Game thread (ADR-002): SDK layer wired to the disc, the pads and the IOP stand-ins, then the
    // crt0 equivalent runs until its first vsync wait.
    // SPU2 subset mixer (ADR-004): advanced by the game thread per step, drained by the audio device.
    // Static: outlives the audio device thread and the game thread at shutdown.
    static platform::audio::SpuMixer mixer;
    audio.SetSource(&mixer);
    mixer.SetCapture(cl.options.dump_audio);
    {
        // Reverb presets: libsd's table in the disc's IRX/LIBSD.IRX (read like any other game data).
        IsoEntry entry;
        std::vector<std::uint8_t> libsd;
        if (!disc->Iso().Find("IRX/LIBSD.IRX", entry) || !disc->Iso().ReadFile(entry, libsd) || !mixer.LoadReverbPresets(libsd))
            AC04_LOG_WARN("audio", "reverb presets not found in IRX/LIBSD.IRX: reverb is off");
    }
    debug::SetFreezeSim(cl.options.freeze_sim);
    if (cl.options.mission != 0 || cl.options.mission_id >= 0) {
        debug::BootMission m;
        m.active = true;
        m.number = cl.options.mission;
        m.id = cl.options.mission_id;
        m.free = cl.options.mission_free;
        debug::SetBootMission(m);
        log::Write(log::Level::Info, "debug", "--mission: campaign mission %d, mission id %d, %s mode", m.number,
                   m.id, m.free ? "free" : "campaign");
    }
    debug::SetHeapFill(cl.options.heap_fill);
    sce::EeRegisters::Get().Install(guest::Mem());
    {
        sce::SceHost& host = sce::Host();
        host.iso = &disc->Iso();
        // Movie upscaling side path (ADR-006 §5): only when the stage runs (factor above 1).
        if (cl.options.movie_upscale) renderer.SetMovieUpscaleFactor(static_cast<int>(*cl.options.movie_upscale));
        host.movie_pictures = renderer.MovieUpscaleFactor() > 1;
        // --pad-script (development builds): scripted pad-1 presses, held for 6 vblanks each.
        static TickInputFeed feed;
        feed.input = &input;
        std::vector<ScriptedPress>& pad_script = feed.script;
        {
            static const std::pair<const char*, int> kNames[] = {
                {"select", 0}, {"l3", 1}, {"r3", 2}, {"start", 3}, {"up", 4}, {"right", 5}, {"down", 6}, {"left", 7},
                {"l2", 8}, {"r2", 9}, {"l1", 10}, {"r1", 11}, {"triangle", 12}, {"circle", 13}, {"cross", 14}, {"square", 15}};
            std::string spec = cl.options.pad_script;
            size_t pos = 0;
            while (pos < spec.size()) {
                const size_t end = std::min(spec.find(',', pos), spec.size());
                const std::string item = spec.substr(pos, end - pos);
                const size_t colon = item.find(':');
                bool ok = false;
                if (colon != std::string::npos) {
                    const std::string name = item.substr(colon + 1);
                    for (const auto& [n, bit] : kNames) {
                        if (name == n) {
                            pad_script.push_back({std::strtoull(item.c_str(), nullptr, 10), static_cast<std::uint16_t>(1u << bit)});
                            ok = true;
                        }
                    }
                }
                if (!ok) AC04_LOG_WARN("input", "--pad-script: cannot read '%s'", item.c_str());
                pos = end + 1;
            }
        }
        if (!cl.options.replay_input.empty()) {
            feed.use_replay = feed.replay.Open(platform::PathFromUtf8(cl.options.replay_input));
            if (feed.use_replay) {
                AC04_LOG_INFO("input", "input log %s: %zu ticks (live devices ignored)", cl.options.replay_input.c_str(),
                              feed.replay.Size());
            } else {
                AC04_LOG_ERROR("input", "cannot read the input log %s", cl.options.replay_input.c_str());
            }
        }
        if (!cl.options.replay_p2m2.empty()) {
            std::string err;
            feed.use_p2m2 = feed.p2m2.Open(platform::PathFromUtf8(cl.options.replay_p2m2), err);
            feed.p2m2_offset = cl.options.p2m2_offset;
            if (!feed.use_p2m2) {
                AC04_LOG_ERROR("input", "cannot read the PCSX2 recording %s: %s", cl.options.replay_p2m2.c_str(), err.c_str());
            } else {
                AC04_LOG_INFO("input", "PCSX2 recording %s: %u frames, game '%s', offset %d%s", cl.options.replay_p2m2.c_str(),
                              feed.p2m2.FrameCount(), feed.p2m2.GameName().c_str(), feed.p2m2_offset,
                              feed.p2m2.FromSavestate() ? " (starts from a save state: not supported, input only)" : "");
            }
        }
        if (cl.options.record_input) {
            const auto dir = paths.user_data / "inputs";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            if (!feed.record.Open(dir / "input.ac4i")) AC04_LOG_ERROR("input", "cannot write the input log");
        }
        if (!feed.Live())
            AC04_LOG_INFO("input", "pads from the %s only",
                          feed.use_p2m2 ? "PCSX2 recording" : (feed.use_replay ? "input log" : "pad script"));
        feed.Latch(0);
        g_feed = &feed;
        host.read_pad = [](int port) { return g_feed->latched[port & 1]; };
        host.iop_services.clear();
        const iop::IopRead iop_read = [](std::uint32_t addr, std::uint32_t size) -> const std::uint8_t* {
            auto& ram = sce::IopRam();
            if (static_cast<std::uint64_t>(addr) + size > ram.size()) return nullptr;
            return ram.data() + addr;
        };
        host.iop_services.push_back(iop::MakeSoundService(&mixer, iop_read));
        host.on_vblank = [] { mixer.AdvanceStep(); };
        if (cl.options.dump_dma_at) {
            // Development capture: one step's VIF1 stream and GIF output for tools/research/dmacap.py.
            host.dma_capture_at = *cl.options.dump_dma_at;
            host.on_dma_capture = [&paths](u64 vblank, const std::vector<u32>& vif, const std::vector<u8>& gif) {
                const auto dir = paths.user_data / "dumps";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                char name[64];
                std::snprintf(name, sizeof(name), "vif1_%06llu.bin", static_cast<unsigned long long>(vblank));
                if (std::FILE* f = platform::OpenFile(dir / name, "wb")) {
                    std::fwrite(vif.data(), 4, vif.size(), f);
                    std::fclose(f);
                }
                std::snprintf(name, sizeof(name), "gif_%06llu.bin", static_cast<unsigned long long>(vblank));
                if (std::FILE* f = platform::OpenFile(dir / name, "wb")) {
                    std::fwrite(gif.data(), 1, gif.size(), f);
                    std::fclose(f);
                }
                AC04_LOG_INFO("dma", "step capture at vblank %llu: %zu VIF1 words, %zu GIF bytes",
                              static_cast<unsigned long long>(vblank), vif.size() / 2, gif.size());
            };
        }
        host.iop_services.push_back(iop::MakeVagService(&mixer, iop_read));
        host.iop_services.push_back(iop::MakeUsbJoyService());
        // Slot 1: the port's saves folder (a formatted empty card until the first save), or a card
        // folder / no card given on the command line (development builds, validation runs).
        if (cl.options.no_memcard) {
            host.memcard.reset();
            AC04_LOG_INFO("saves", "memory card: none (--no-memcard)");
        } else if (!cl.options.memcard_dir.empty()) {
            host.memcard = std::make_unique<memcard::FolderCard>(PathFromUtf8(cl.options.memcard_dir), false);
            AC04_LOG_INFO("saves", "memory card: %s (%s)", cl.options.memcard_dir.c_str(),
                          !host.memcard->Present() ? "no card" : host.memcard->Formatted() ? "formatted" : "unformatted");
        } else {
            // The port's own card: when the saves folder holds no card yet (first start), a new,
            // formatted PCSX2 folder card is created at once (`_pcsx2_superblock`), so the folder
            // is a valid card for PCSX2 too before the game's first save (PCSX2 treats a folder
            // without the superblock as unformatted). An existing card is used as it is.
            host.memcard = std::make_unique<memcard::FolderCard>(paths.saves, true);
            std::error_code ec;
            if (!std::filesystem::exists(paths.saves / "_pcsx2_superblock", ec)) {
                if (host.memcard->Format() == 0)
                    AC04_LOG_INFO("saves", "no memory card in %s: a new one was created (PCSX2 folder card, 8 MB, formatted)",
                                  PathToUtf8(paths.saves).c_str());
                else
                    AC04_LOG_ERROR("saves", "cannot create the memory card in %s", PathToUtf8(paths.saves).c_str());
            }
        }
    }
    sce::GameRuntime& game = sce::GameRuntime::Get();
    debug::SetVBlankSource([] { return sce::GameRuntime::Get().VBlankCount(); });
    if (cl.options.dump_ram_at_first_scenestep) {
        // Runs on the game thread at the entry of the first SceneStep (docs/port/validation.md §2d).
        debug::SetFirstSceneStepHook([&paths] { DumpGuestRam(paths, "ram_first_scenestep.bin"); });
    }
    if (cl.options.dump_ram_at) {
        // RAM checkpoint (docs/port/validation-strategy.md §2.1): runs on the game thread where the
        // translated code reports the PCSX2 breakpoint address.
        const unsigned addr = *cl.options.dump_ram_at;
        const unsigned hit = cl.options.dump_ram_at_hit;
        debug::SetCheckpointHook(addr, hit, [&paths, addr, hit] {
            char name[64];
            std::snprintf(name, sizeof(name), "ram_%08X_%u.bin", addr, hit);
            DumpGuestRam(paths, name);
        });
    }
    static std::FILE* alloc_log = nullptr;
    if (cl.options.log_allocs) {
        // Work-heap allocation log (development builds): one line per WorkHeapAlloc call, for
        // tools/research/heapdiff.py. Written on the game thread; closed at exit.
        const auto dir = paths.user_data / "dumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        alloc_log = platform::OpenFile(dir / "allocs.txt", "w");
        if (alloc_log == nullptr) {
            AC04_LOG_ERROR("loader", "cannot write the allocation log");
        } else {
            std::fprintf(alloc_log, "# vblank address size align caller_rva\n");
            debug::SetAllocHook([](u32 address, s32 size, u32 align, u64 caller_rva) {
                std::fprintf(alloc_log, "%llu %08X %X %X %llX\n",
                             static_cast<unsigned long long>(sce::GameRuntime::Get().VBlankCount()), address,
                             static_cast<unsigned>(size), align, static_cast<unsigned long long>(caller_rva));
            });
        }
    }
    {
        // SCREEN RATIO default (port baseline, User request 2026-10-20): 16:9 when the window's
        // monitor is wider than 4:3, unless settings say "game". Validation runs (development
        // builds: a pad script, an input log, a PCSX2 recording, --memcard or --no-memcard) keep the
        // PS2 default, so their guest RAM stays comparable with PCSX2.
        const DisplayInfo window_display = QueryDisplay(SDL_GetDisplayForWindow(window.Handle()));
        const bool by_monitor = settings.Get().presentation.default_screen_ratio == ScreenRatioDefault::Monitor;
        const bool validation = !g_feed->Live() || !cl.options.memcard_dir.empty() || cl.options.no_memcard;
        port::Boot().wide_screen_ratio = by_monitor && window_display.Wide() && !validation;
        // QUIT GAME in the main menu (port baseline, User request 2026-10-22): the same rule.
        port::Boot().quit_item = !validation;
        // Save texts for a computer (port baseline, User request 2026-10-22): the same rule.
        port::Boot().pc_texts = !validation;
        AC04_LOG_INFO("startup", "QUIT GAME in the main menu and save texts for a computer: %s",
                      validation ? "off (validation run)" : "on");
        AC04_LOG_INFO("startup", "SCREEN RATIO default: %s (%s)", port::Boot().wide_screen_ratio ? "16:9" : "4:3",
                      !by_monitor ? "settings: game" : validation ? "validation run: PS2 default"
                                                     : window_display.Wide() ? "monitor wider than 4:3" : "4:3 monitor");
    }
    game.Start([] { game::Entry(); });
    AC04_LOG_INFO("game", "boot reached the first vsync wait (%zu translated functions registered)",
                  guest::RegisteredGuestFunctionCount());

    AC04_LOG_INFO("startup", "ready");
    log::Flush();

    platform::RamHashLog hash_log;
    if (cl.options.hash_log) {
        const auto dir = paths.user_data / "dumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        if (!hash_log.Open(dir / "hashes.ac4h", guest::kRamSize))
            AC04_LOG_ERROR("loader", "cannot write the RAM hash log");
    }
    std::mt19937 stress_rng(std::random_device{}());

    auto last_time = std::chrono::steady_clock::now();
    sce::RefreshSnap refresh_snap;
    double refresh_seconds = 0.0;
    // Frame interpolation (ADR-002, platform/render/frame_interpolator.h): the blended copy of the
    // newest step is drawn instead of the step itself when more frames than steps are presented.
    // The interpolator and its buffers belong to the render thread; the main thread decides when
    // interpolation is on.
    render::FrameInterpolator interpolator;
    bool interpolating = false;
    bool interpolation_reset = false;
    std::vector<u8> blended;
    std::vector<render::gs::SourceMark> blended_marks;
    // Session statistics (log at exit): frames presented at normal speed, and the blended copies
    // (render thread; read after it has stopped).
    u64 normal_frames = 0, blended_frames = 0;
    double normal_seconds = 0.0, build_ms = 0.0;
    // Host time of the game ticks at normal speed (game thread: game code, with the VIF1 sends and
    // the VU1 HLE counted apart) and of the main thread's waits for the render thread (log at exit
    // and Performance window; two clock reads per tick and per wait).
    u64 timed_ticks = 0;
    double tick_ms_total = 0.0, tick_vif1_ms_total = 0.0, tick_ms_worst = 0.0, render_wait_ms_total = 0.0;
    double frame_render_wait_ms = 0.0, last_render_wait_ms = 0.0;
    const auto ms_since = [](std::chrono::steady_clock::time_point t) {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
    };
    u64 frame_number = 0;
    bool game_end_logged = false;
    bool running = true;
    std::vector<unsigned> screenshot_due = cl.options.screenshot_at;
    std::sort(screenshot_due.begin(), screenshot_due.end());
    bool take_screenshot = false;
    const u64 fast_until = cl.options.fast_until ? *cl.options.fast_until : 0;
    const auto fast_start = std::chrono::steady_clock::now();
    bool window_resized = false;
    u64 last_movie_serial = 0;

    // The render thread's part of a frame, in the order the main loop did it before the thread:
    // the frame's GS steps (fast-forward segments without draws first), the blended copy, display
    // state and the movie picture, a screenshot of the frame presented last, then the new frame.
    render_thread.Start([&](render::FrameJob& job) {
        if (job.presentation) renderer.ApplySettings(*job.presentation);
        if (job.window_resized) renderer.OnWindowResized();
        renderer.SetWindowPixelSize(job.window_w, job.window_h);
        if (job.interpolation_reset) interpolator.Reset();
        std::vector<std::vector<u8>>& pool = job.returned_steps;
        for (size_t i = 0; i + 1 < job.segments.size(); ++i) {
            render::FrameJob::Segment& seg = job.segments[i];
            renderer.SetGsDrawsEnabled(seg.draws);
            renderer.SubmitGsSteps(seg.steps, seg.marks, pool);
        }
        renderer.SetGsDrawsEnabled(true);
        if (job.segments.empty()) job.segments.emplace_back();
        std::vector<std::vector<u8>>& steps = job.segments.back().steps;
        std::vector<std::vector<render::gs::SourceMark>>& marks = job.segments.back().marks;
        bool blend = false;
        if (job.interpolating) {
            // A frame without steps shows the newest step again, further blended.
            bool current = steps.empty();
            if (!current && job.runs.size() == steps.size() && marks.size() == steps.size()) {
                interpolator.PushStep(steps.back(), marks.back(), job.runs.back());
                current = true;
            }
            // A frame that had to catch up more than one step adds no blended copy (its cost
            // would make the next frame later still).
            if (!current) interpolator.Reset();
            else if (job.ticks_run <= 1) {
                const auto build_start = std::chrono::steady_clock::now();
                blend = interpolator.Build(job.alpha, blended, blended_marks);
                build_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - build_start).count();
                if (blend) ++blended_frames;
            }
        }
        // With a blended copy the newest step is replaced by it: the copy makes the same GS
        // register and local memory changes (only the blended runs' vertices differ). Earlier
        // steps of this frame are drawn as usual: what a step draws can stay in the frame
        // buffers for later steps (the hangar), so a step's draws are never left out.
        if (blend && !steps.empty()) {
            steps.back().clear();
            if (pool.size() < 4) pool.push_back(std::move(steps.back()));
            steps.pop_back();
            if (marks.size() > steps.size()) marks.resize(steps.size());
        }
        renderer.SubmitGsSteps(steps, marks, pool);
        if (blend) renderer.SubmitBlendedStep(blended, blended_marks, interpolator.StepSerial());
        // The run logs go back to the game side's pool, cleared (PushStep hands back the log of
        // the step it dropped).
        for (render::vu1::RunLog& log : job.runs) log.Clear();
        renderer.SetDisplay(job.dispfb, job.display);
        renderer.SetDisplayAspect(job.aspect_w, job.aspect_h);
        renderer.SetHudXScale(job.hud_x_scale);
        if (job.movie) {
            render::FrameJob::Movie& mp = *job.movie;
            renderer.SubmitMoviePicture(mp.serial, mp.guest_addr, mp.guest_bytes, mp.width, mp.height, mp.rgba);
        }
        if (job.screenshot) {
            const auto dir = paths.user_data / "screenshots";
            char name[64];
            std::snprintf(name, sizeof(name), "%06llu", static_cast<unsigned long long>(job.vblank));
            if (WriteNativeScreenshot(renderer, dir, std::string("native_") + name,
                                      "internal_x" + std::to_string(renderer.NativeScale()) + "_" + name)) {
                std::error_code ec;
                std::vector<uint8_t> rgba;
                int w = 0, h = 0;
                // --dump-vram: the GS local memory as well (research only, contains game data).
                if (cl.options.dump_vram) {
                    std::filesystem::create_directories(paths.user_data / "dumps", ec);
                    std::snprintf(name, sizeof(name), "vram_%06llu.bin", static_cast<unsigned long long>(job.vblank));
                    if (std::FILE* f = platform::OpenFile(paths.user_data / "dumps" / name, "wb")) {
                        std::fwrite(renderer.GsLocalMemory(), 1, render::gs::GsMemory::kSize, f);
                        std::fclose(f);
                    }
                }
                // --movie-upscale: the upscaling stage output of the current movie picture as well.
                if (renderer.MovieUpscaleFactor() > 1 && renderer.CaptureMovieUpscaled(rgba, w, h)) {
                    std::snprintf(name, sizeof(name), "movie_x%d_%06llu.png", renderer.MovieUpscaleFactor(),
                                  static_cast<unsigned long long>(job.vblank));
                    if (!WritePng(dir / name, rgba.data(), w, h))
                        AC04_LOG_ERROR("renderer", "cannot write the screenshot %s", name);
                }
            }
        }
        if (!renderer.DrawFrame()) job.device_lost = true;
    });

    while (running) {
        // The debug overlay's ImGui frame (events, build, draw) is not shared between threads:
        // while it is shown, frames run one after the other.
        frame_render_wait_ms = 0.0;
        if (overlay.Visible() || overlay.FpsVisible()) {
            const auto wait_start = std::chrono::steady_clock::now();
            render_thread.WaitIdle();
            frame_render_wait_ms += ms_since(wait_start);
        }
        render::FrameJob job;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                    running = false;
                    break;
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_RESTORED:
                    window_resized = true;
                    break;
#if AC04_DEV_BUILD
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_F8 && !event.key.repeat) take_screenshot = true;
                    break;
#endif
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    audio.OnFocusChanged(true);
                    break;
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    audio.OnFocusChanged(false);
                    break;
                default:
                    break;
            }
            overlay.HandleEvent(event);
            input.HandleEvent(event);
        }
        watcher.Poll();
        if (controls_file.Changed()) {
            AC04_LOG_INFO("input", "controls.ini changed on disk, reloaded");
            input.ApplyControls(controls_file.Load());
        }
        {
            const bool focused = (SDL_GetWindowFlags(window.Handle()) & SDL_WINDOW_INPUT_FOCUS) != 0;
            const bool want = input.MouseSteers() && g_feed->Live() && port::Input().in_flight[0] && focused &&
                              !overlay.Visible();
            if (want != mouse_steers) {
                SDL_SetWindowRelativeMouseMode(window.Handle(), want);
                mouse_steers = want;
            }
        }
        input.Update(/*keyboard_captured=*/overlay.WantsKeyboard());
        // Fixed 59.94 Hz simulation: release the ticks due since the last frame (catch-up <= 4);
        // the game thread runs them and parks again before the frame is presented.
        const auto now = std::chrono::steady_clock::now();
        const double measured = std::chrono::duration<double>(now - last_time).count();
        last_time = now;
        // V-sync on: each presented frame is one display refresh (RefreshSnap), so the ticks per
        // frame do not follow the jitter of where the CPU blocks. The refresh rate is read again
        // every second (the window can move to another display).
        if (frame_number++ % 60 == 0) refresh_seconds = DisplayRefreshSeconds(window.Handle());
        const double elapsed = refresh_snap.Step(measured, settings.Get().presentation.vsync ? refresh_seconds : 0.0);
        // --fast-until (development builds): ticks run back to back for about 100 ms per frame
        // until that vblank, and only the last tick's draws are shown; the steps before it update GS
        // registers and local memory only. The game sees the same ticks and pads as at normal speed
        // (nothing in the game or the IOP stand-ins reads host time). The last kFastSettle ticks
        // before a screenshot and before the end run one per frame with all draws, as at normal
        // speed, because what a frame shows depends on how the ticks were grouped into frames.
        const bool fast = fast_until != 0 && game.VBlankCount() < fast_until;
        {
            // Interpolation only helps above 60 presented frames per second (v-sync: the display's
            // refresh rate; otherwise the frame cap). Never in validation runs (their screenshots
            // compare with PCSX2) or while fast-forwarding. The VIF records the program runs of
            // this frame's steps only while it is on.
            const PresentationSettings& pr = settings.Get().presentation;
            const double rate = pr.vsync ? (refresh_seconds > 0.0 ? 1.0 / refresh_seconds : 60.0) : pr.frame_cap;
            const bool allowed = cl.options.frame_interpolation ? *cl.options.frame_interpolation
                                                                : pr.frame_interpolation && !validation_run;
            const bool want = allowed && !fast && rate > 62.0;
            if (want != interpolating) {
                interpolating = want;
                interpolation_reset = true;
                AC04_LOG_INFO("renderer", "frame interpolation: %s (%.0f frames per second)", want ? "on" : "off", rate);
            }
            sce::Dma().record_runs = want;
        }
        const u32 ticks = fast ? 0xFFFFFFFFu : clock.Advance(elapsed);
        // One tick at a time: the pads are sampled and latched before each tick (slices.md §B P1).
        u32 ticks_run = 0;
        double frame_tick_ms = 0.0, frame_vif1_ms = 0.0;
        for (u32 t = 0; t < ticks; ++t) {
            if (fast) {
                constexpr u64 kFastSettle = 60;
                const u64 v = game.VBlankCount();
                if (v >= fast_until) break;
                const bool settle = v + kFastSettle >= fast_until ||
                                    (!screenshot_due.empty() && v + kFastSettle >= screenshot_due.front());
                if (ticks_run > 0 && (settle || (cl.options.exit_at && v >= *cl.options.exit_at) ||
                                      std::chrono::steady_clock::now() - now > std::chrono::milliseconds(100)))
                    break;
                if (!settle) {
                    // Keep GS memory current step by step without building draws.
                    sce::DmaStats& dma = sce::Dma();
                    render::FrameJob::Segment seg;
                    seg.draws = false;
                    seg.steps = std::move(dma.gs_steps);
                    seg.marks = std::move(dma.gs_steps_marks);
                    dma.gs_steps.clear();
                    dma.gs_steps_marks.clear();
                    job.segments.push_back(std::move(seg));
                    dma.gs_steps_runs.clear();
                }
            }
            ++ticks_run;
            if (t > 0) SDL_PumpEvents();
            input.StepTick(mouse_steers);  // the mouse stick and wheel presses advance once per tick
            input.Update(/*keyboard_captured=*/overlay.WantsKeyboard());
            {
                // Port options the menu pages show (port_hooks.h), current before the tick.
                port::MenuOptions& menu = port::Menu();
                menu.modern_controls = settings.Get().input.modern_controls;
                menu.graphics = ToGraphicsOptions(settings.Get().presentation);
                menu.graphics_defaults = ToGraphicsOptions(DefaultSettings(DefaultsDisplay()).presentation);
                g_feed->modern = menu.modern_controls;
            }
            {
                // Flight only: frontend menus and movies keep the original presentation. The actual
                // window aspect drives both the camera expansion and the output rectangle.
                int window_w = 0, window_h = 0;
                window.PixelSize(window_w, window_h);
                const bool wide = (game::g_MainContext().flags & 0x10000u) != 0;
                const bool flight = game::g_MainContext().scene == 2;
                const auto& pr = settings.Get().presentation;
                port::SetDisplayOptions(wide, flight && pr.ultrawide && !validation_run, window_w, window_h,
                                        pr.ultrawide_4x3_fov);
            }
            g_feed->Latch(game.VBlankCount() + 1);
            port::Input().in_flight = {};  // set again by the game while it reads the flight controls
            const auto tick_start = std::chrono::steady_clock::now();
            const u64 vif1_ns_before = sce::Dma().vif1_ns;
            const bool tick_ok = game.RunTicks(1);
            const double tick_ms = ms_since(tick_start);
            const double vif1_ms = static_cast<double>(sce::Dma().vif1_ns - vif1_ns_before) / 1e6;
            frame_tick_ms += tick_ms;
            frame_vif1_ms += vif1_ms;
            if (!fast) {
                ++timed_ticks;
                tick_ms_total += tick_ms;
                tick_vif1_ms_total += vif1_ms;
                tick_ms_worst = std::max(tick_ms_worst, tick_ms);
            }
            if (!tick_ok) {
                if (!game_end_logged) AC04_LOG_WARN("game", "the game thread has ended");
                game_end_logged = true;
                break;
            }
            apply_menu_requests();
            if (hash_log.IsOpen())
                hash_log.Write(game.VBlankCount(), guest::Mem().Ram(), guest::kRamSize,
                               guest::Mem().Scratchpad(), guest::kScratchpadSize);
        }
        if (fast) {
            if (game.VBlankCount() >= fast_until) {
                // Back to normal speed: start the clock afresh and drop the sound backlog.
                last_time = std::chrono::steady_clock::now();
                mixer.DropQueued();
                AC04_LOG_INFO("game", "fast-forward reached vblank %llu after %.1f s",
                              static_cast<unsigned long long>(game.VBlankCount()),
                              std::chrono::duration<double>(last_time - fast_start).count());
            }
        }
        // The game is parked: its GS data and the latest movie picture (side path) can be taken.
        {
            sce::DmaStats& dma = sce::Dma();
            render::FrameJob::Segment seg;
            seg.steps = std::move(dma.gs_steps);
            seg.marks = std::move(dma.gs_steps_marks);
            dma.gs_steps.clear();
            dma.gs_steps_marks.clear();
            job.segments.push_back(std::move(seg));
            job.runs = std::move(dma.gs_steps_runs);
            dma.gs_steps_runs.clear();
            job.interpolating = interpolating;
            job.interpolation_reset = interpolation_reset;
            interpolation_reset = false;
            job.ticks_run = ticks_run;
            job.alpha = clock.Alpha();
            job.dispfb = dma.dispenv[2];
            job.display = dma.dispenv[3];
        }
        // Vibration as the game last set it (scePadSetActDirect), on the live gamepads only: runs
        // fed from a script or a recording leave the pads still.
        for (int port = 0; port < 2; ++port) {
            const auto& motors = sce::Host().pad_motors[static_cast<size_t>(port)];
            const bool live = g_feed->Live();
            input.SetVibration(port, live && motors[0] != 0, live ? motors[1] : 0);
        }
        // Display aspect and HUD compensation selected before the tick (port_hooks.h): original
        // 4:3 / 16:9, or Hor+ ultrawide during flight on a wider window.
        job.aspect_w = port::Display().aspect_w;
        job.aspect_h = port::Display().aspect_h;
        job.hud_x_scale = port::Display().hud_x_scale;
        {
            // A new picture is copied for the render thread (the decoder keeps its buffer).
            const sce::MoviePicture& mp = sce::Dma().movie_picture;
            if (mp.serial != 0 && mp.serial != last_movie_serial && !mp.rgba.empty()) {
                job.movie = render::FrameJob::Movie{mp.serial, mp.guest_addr, mp.guest_bytes, mp.width, mp.height, mp.rgba};
                last_movie_serial = mp.serial;
            }
        }
        // Movie side path (ADR-006 §5) only while the upscaling stage runs (the internal scale can
        // change at run time through settings.json).
        sce::Host().movie_pictures = renderer.MovieUpscaleFactor() > 1;
        game.Interpolate(clock.Alpha());
        if (overlay.Active()) {
            const sce::DmaStats& dma = sce::Dma();
            OverlayFrameData od;
            od.frame_seconds = measured;
            od.ticks_run = ticks_run;
            od.vblank_count = game.VBlankCount();
            od.dropped_ticks = clock.DroppedTicks();
            od.alpha = clock.Alpha();
            od.game_running = game.Running();
            od.ticks_ms = frame_tick_ms;
            od.ticks_vif1_ms = frame_vif1_ms;
            od.render_wait_ms = last_render_wait_ms;
            od.gif_qwords = dma.frame_qwords_gif;
            od.vif1_qwords = dma.frame_qwords_vif1;
            od.gif_sends = dma.sends[2];
            od.vif1_sends = dma.sends[1];
            od.bad_chains = dma.bad_chains;
            od.dispenv = dma.dispenv;
            od.presentation = settings.Get().presentation;
            for (int port = 0; port < 2; ++port) {
                od.pads[static_cast<size_t>(port)] = input.Port(port);
                od.pad_devices[static_cast<size_t>(port)] = input.DeviceInfo(port);
            }
            if (overlay.Visible() || overlay.FpsVisible()) {  // ImGui: see the loop start
                const auto wait_start = std::chrono::steady_clock::now();
                render_thread.WaitIdle();
                frame_render_wait_ms += ms_since(wait_start);
                // The render thread is idle: its native image size can be read (Graphics panel).
                od.native_width = renderer.NativeWidth();
                od.native_height = renderer.NativeHeight();
                od.native_scale = renderer.NativeScale();
            }
            overlay.BuildFrame(od);
            apply_overlay_requests();
        }
        // Development builds: --exit-at ends the session normally (dumps written at exit).
        if (cl.options.exit_at && game.VBlankCount() >= *cl.options.exit_at) running = false;
        // QUIT GAME in the main menu (port baseline): ends the session as closing the window does.
        if (port::Menu().quit) running = false;
        // Native screenshot (development builds): F8, or --screenshot-at after the given vblanks.
        while (!screenshot_due.empty() && game.VBlankCount() >= screenshot_due.front()) {
            screenshot_due.erase(screenshot_due.begin());
            take_screenshot = true;
        }
        if (take_screenshot) {
            take_screenshot = false;
            job.screenshot = true;
            job.vblank = game.VBlankCount();
        }
        job.presentation = std::move(pending_presentation);
        pending_presentation.reset();
        job.window_resized = window_resized;
        window_resized = false;
        SDL_GetWindowSizeInPixels(window.Handle(), &job.window_w, &job.window_h);
        {
            // The previous frame is presented when Submit returns; its buffers go back to the game
            // side's pools while the game is parked.
            const auto wait_start = std::chrono::steady_clock::now();
            render::FrameJob done = render_thread.Submit(std::move(job));
            frame_render_wait_ms += ms_since(wait_start);
            last_render_wait_ms = frame_render_wait_ms;
            sce::DmaStats& dma = sce::Dma();
            for (std::vector<u8>& s : done.returned_steps) {
                if (dma.gs_step_pool.size() >= 4) break;
                s.clear();
                dma.gs_step_pool.push_back(std::move(s));
            }
            for (render::vu1::RunLog& log : done.runs) {
                if (dma.gs_runs_pool.size() >= 3) break;
                dma.gs_runs_pool.push_back(std::move(log));
            }
            if (done.device_lost) {
                MessageText m;
                m.headline = "The graphics device stopped working";
                m.explanation = "The GPU stopped responding while the game was running.";
                m.steps = {"Restart the port.", "If it happens again, update the graphics driver and keep the session log."};
                m.details = {{"Session log", PathToUtf8(log::FilePath())}};
                ShowErrorBox(std::string(kAppTitle) + " - Error", m, &window);
                break;
            }
        }
        ++period_frames;
        if (!fast) {
            ++normal_frames;
            normal_seconds += measured;
            render_wait_ms_total += frame_render_wait_ms;
            pacer.WaitNextFrame();
        }
        // --stress-host: random main-thread delays, so catch-up batches vary (determinism check).
        if (cl.options.stress_host)
            std::this_thread::sleep_for(std::chrono::milliseconds(std::uniform_int_distribution<int>(0, 40)(stress_rng)));
    }

    render_thread.Stop();

    // Remember the windowed size for the next start.
    if (settings.Get().presentation.window_mode == WindowMode::Windowed) {
        int w = 0, h = 0;
        window.WindowedSize(w, h);
        const auto& p = settings.Get().presentation;
        if (w > 0 && h > 0 && (w != p.window_width || h != p.window_height)) {
            SettingsStore saved;
            std::vector<std::string> ignored;
            saved.Load(paths.settings_file, ignored);  // file values, without session overrides
                                                       // (a rejected file gives the defaults)
            Settings s = saved.Get();
            s.presentation.window_width = w;
            s.presentation.window_height = h;
            saved.Replace(s);
            if (!saved.Save(&error)) AC04_LOG_ERROR("settings", "cannot save settings.json: %s", error.c_str());
        }
    }

    game.Stop();
    if (alloc_log != nullptr) {
        debug::SetAllocHook(nullptr);
        std::fclose(alloc_log);
    }
    if (cl.options.dump_audio) {
        // 48 kHz stereo s16 WAV of everything the mixer produced (research only, contains game audio).
        const auto& pcm = mixer.Capture();
        const auto dir = paths.user_data / "dumps";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::ofstream f(dir / "audio.wav", std::ios::binary);
        const std::uint32_t bytes = static_cast<std::uint32_t>(pcm.size() * 2);
        auto u32 = [&f](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&f](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
        f.write("RIFF", 4);
        u32(36 + bytes);
        f.write("WAVEfmt ", 8);
        u32(16);
        u16(1);
        u16(2);
        u32(48000);
        u32(48000 * 4);
        u16(4);
        u16(16);
        f.write("data", 4);
        u32(bytes);
        f.write(reinterpret_cast<const char*>(pcm.data()), bytes);
    }
    overlay.Shutdown();
    AC04_LOG_INFO("game", "stopped after %llu vblanks (%llu ticks dropped)",
                  static_cast<unsigned long long>(game.VBlankCount()),
                  static_cast<unsigned long long>(clock.DroppedTicks()));
    if (normal_frames > 0 && normal_seconds > 0.0)
        AC04_LOG_INFO("renderer", "%llu frames at normal speed in %.1f s (%.1f fps); %llu with a blended copy (%.2f ms each to build)",
                      static_cast<unsigned long long>(normal_frames), normal_seconds,
                      static_cast<double>(normal_frames) / normal_seconds, static_cast<unsigned long long>(blended_frames),
                      blended_frames > 0 ? build_ms / static_cast<double>(blended_frames) : 0.0);
    if (timed_ticks > 0 && normal_frames > 0)
        AC04_LOG_INFO("game", "game thread per tick at normal speed: %.2f ms on average over %llu ticks (VIF1 and VU1 HLE %.2f ms of it), slowest %.2f ms; main thread waiting for the render thread (presentation and v-sync included) %.2f ms per frame",
                      tick_ms_total / static_cast<double>(timed_ticks), static_cast<unsigned long long>(timed_ticks),
                      tick_vif1_ms_total / static_cast<double>(timed_ticks), tick_ms_worst,
                      render_wait_ms_total / static_cast<double>(normal_frames));
    renderer.Shutdown();
    audio.Close();
    window.Destroy();
    SDL_Quit();
    AC04_LOG_INFO("startup", "clean exit");
    log::Close();
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    return Run(argc, argv);
}
