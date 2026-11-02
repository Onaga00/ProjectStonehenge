// Command-line parameters of the executable. Every parameter is documented in
// docs/port/command-line.md; keep both in sync.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ac04::platform {

struct CommandLine {
    bool show_help = false;
    bool show_version = false;
    std::string iso_path;              // --iso <path>        overrides settings.json paths.iso
    std::string user_data_dir;         // --user-data <dir>   overrides %LOCALAPPDATA%\ProjectStonehenge
    std::optional<unsigned> internal_scale;  // --internal-scale <1-8> overrides presentation.internal_scale
    std::string antialiasing;                // --antialiasing off|fxaa|smaa overrides presentation.antialiasing
    std::optional<bool> debug_overlay; // --debug-overlay / --no-debug-overlay
    std::optional<bool> fps_overlay;   // --fps-overlay / --no-fps-overlay
    bool gpu_profile = false;          // --gpu-profile: GPU time per section of the frame in the log
    bool verify_iso = false;           // --verify-iso          ignore the verification cache
    bool allow_unverified_iso = false; // --allow-unverified-iso (development builds only)
    bool dump_ram_after_load = false;  // --dump-ram-after-load  (development builds only)
    bool dump_ram_at_first_scenestep = false;  // --dump-ram-at-first-scenestep (development builds only)
    // --dump-ram-at <addr>[,<hit>] (development builds only): RAM checkpoint at a guest address.
    std::optional<unsigned> dump_ram_at;
    unsigned dump_ram_at_hit = 1;
    std::string memcard_dir;           // --memcard <dir>  (development builds only): card folder for slot 1
    bool no_memcard = false;           // --no-memcard     (development builds only): slot 1 empty
    std::vector<unsigned> screenshot_at;  // --screenshot-at <vblank>[,...] (development builds only)
    std::string pad_script;               // --pad-script <vblank>:<button>[,...] (development builds only)
    bool record_input = false;            // --record-input (development builds only): per-tick input log
    std::string replay_input;             // --replay-input <file> (development builds only): input from a log
    std::string replay_p2m2;              // --replay-p2m2 <file> (development builds only): PCSX2 input recording
    int p2m2_offset = 0;                  // --p2m2-offset <n> (development builds only): recording frame = tick + n
    bool hash_log = false;                // --hash-log (development builds only): RAM hash per vblank
    bool stress_host = false;             // --stress-host (development builds only): random main-thread delays
    bool vk_validation = false;           // --vk-validation (development builds only): Vulkan validation layer on
    bool cpu_texture_decode = false;      // --cpu-texture-decode (development builds only): GS textures decoded on the CPU
    bool verify_texture_decode = false;   // --verify-texture-decode (development builds only): GPU decodes checked against the CPU
    // --frame-interpolation on|off (development builds only): forces frame interpolation for this
    // session, also in validation runs (performance and picture checks of a replayed run).
    std::optional<bool> frame_interpolation;
    bool dump_audio = false;              // --dump-audio (development builds only): mixer output to a WAV file
    std::optional<unsigned> dump_dma_at;  // --dump-dma-at <vblank> (development builds only): VIF1/GIF capture of one step
    bool freeze_sim = false;              // --freeze-sim (development builds only): missions draw the world, no step
    // --mission <1-18> / --mission-id <id> / --mission-mode campaign|free (development builds only):
    // start that mission with the default aircraft instead of the intro or the title.
    int mission = 0;
    int mission_id = -1;
    bool mission_free = false;
    int heap_fill = -1;                   // --heap-fill <byte> (development builds only): fill the freed work heap
    std::string play_gsdump;              // --play-gsdump <file> (development builds only): draw a PCSX2 GS dump, then exit
    bool dump_vram = false;               // --dump-vram (development builds only): screenshots also write GS memory
    bool log_allocs = false;              // --log-allocs (development builds only): work-heap allocation log
    std::optional<unsigned> fast_until;   // --fast-until <vblank> (development builds only): run uncapped up to that vblank
    std::optional<unsigned> exit_at;      // --exit-at <vblank> (development builds only): normal exit after that vblank
    std::optional<unsigned> movie_upscale;  // --movie-upscale <2-4> (development builds only): run the movie upscaling stage alone
};

struct CommandLineResult {
    CommandLine options;
    std::vector<std::string> errors;   // unknown parameters, missing values
};

// argv[0] is the program name and is skipped. Arguments are UTF-8.
CommandLineResult ParseCommandLine(int argc, const char* const* argv);

// Text shown for --help.
std::string CommandLineHelp();

}  // namespace ac04::platform
