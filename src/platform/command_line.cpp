#include "platform/command_line.h"

#include <cstring>

namespace ac04::platform {

CommandLineResult ParseCommandLine(int argc, const char* const* argv) {
    CommandLineResult result;
    CommandLine& o = result.options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) {
                result.errors.push_back("missing value after " + arg);
                return;
            }
            out = argv[++i];
        };
#if !AC04_DEV_BUILD
        // A development-only parameter in a release build: one error, and its value (when it takes
        // one) is skipped so that it is not reported as an unknown parameter as well.
        auto dev_only = [&](bool takes_value) {
            result.errors.push_back(arg + " is only available in development builds");
            if (takes_value && i + 1 < argc) ++i;
        };
#endif
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            o.show_help = true;
        } else if (arg == "--version") {
            o.show_version = true;
        } else if (arg == "--iso") {
            value(o.iso_path);
        } else if (arg == "--user-data") {
            value(o.user_data_dir);
        } else if (arg == "--internal-scale") {
            std::string spec;
            value(spec);
            char* end = nullptr;
            const unsigned long v = std::strtoul(spec.c_str(), &end, 10);
            if (end == spec.c_str() || *end != 0 || v < 1 || v > 8) {
                result.errors.push_back("--internal-scale expects a factor from 1 to 8: " + spec);
            } else {
                o.internal_scale = static_cast<unsigned>(v);
            }
        } else if (arg == "--antialiasing") {
            value(o.antialiasing);
            if (o.antialiasing != "off" && o.antialiasing != "fxaa" && o.antialiasing != "smaa")
                result.errors.push_back("--antialiasing expects off, fxaa or smaa: " + o.antialiasing);
        } else if (arg == "--debug-overlay") {
            o.debug_overlay = true;
        } else if (arg == "--no-debug-overlay") {
            o.debug_overlay = false;
        } else if (arg == "--fps-overlay") {
            o.fps_overlay = true;
        } else if (arg == "--no-fps-overlay") {
            o.fps_overlay = false;
        } else if (arg == "--gpu-profile") {
            o.gpu_profile = true;
        } else if (arg == "--verify-iso") {
            o.verify_iso = true;
        } else if (arg == "--dump-ram-after-load") {
#if AC04_DEV_BUILD
            o.dump_ram_after_load = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--dump-ram-at-first-scenestep") {
#if AC04_DEV_BUILD
            o.dump_ram_at_first_scenestep = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--pad-script") {
#if AC04_DEV_BUILD
            value(o.pad_script);
#else
            dev_only(true);
#endif
        } else if (arg == "--record-input" || arg == "--hash-log" || arg == "--stress-host") {
#if AC04_DEV_BUILD
            if (arg == "--record-input") o.record_input = true;
            if (arg == "--hash-log") o.hash_log = true;
            if (arg == "--stress-host") o.stress_host = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--replay-p2m2" || arg == "--p2m2-offset") {
#if AC04_DEV_BUILD
            if (arg == "--replay-p2m2") {
                value(o.replay_p2m2);
            } else {
                std::string v;
                value(v);
                char* end = nullptr;
                const long n = std::strtol(v.c_str(), &end, 10);
                if (v.empty() || *end != 0) result.errors.push_back("--p2m2-offset expects an integer: " + v);
                o.p2m2_offset = static_cast<int>(n);
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--replay-input") {
#if AC04_DEV_BUILD
            value(o.replay_input);
#else
            dev_only(true);
#endif
        } else if (arg == "--vk-validation") {
#if AC04_DEV_BUILD
            o.vk_validation = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--cpu-texture-decode") {
#if AC04_DEV_BUILD
            o.cpu_texture_decode = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--verify-texture-decode") {
#if AC04_DEV_BUILD
            o.verify_texture_decode = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--frame-interpolation") {
#if AC04_DEV_BUILD
            std::string v;
            value(v);
            if (v == "on") o.frame_interpolation = true;
            else if (v == "off") o.frame_interpolation = false;
            else result.errors.push_back("--frame-interpolation expects on or off: " + v);
#else
            dev_only(true);
#endif
        } else if (arg == "--freeze-sim") {
#if AC04_DEV_BUILD
            o.freeze_sim = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--mission" || arg == "--mission-id" || arg == "--mission-mode") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            if (arg == "--mission-mode") {
                if (spec == "campaign" || spec == "free") {
                    o.mission_free = spec == "free";
                } else {
                    result.errors.push_back("--mission-mode expects campaign or free: " + spec);
                }
            } else {
                char* end = nullptr;
                const unsigned long v = std::strtoul(spec.c_str(), &end, 0);
                const bool number = arg == "--mission";
                if (end == spec.c_str() || *end != 0 || (number ? (v < 1 || v > 18) : v > 0xFF)) {
                    result.errors.push_back(number ? "--mission expects a campaign mission number 1-18: " + spec
                                                   : "--mission-id expects a mission id 0-255 (or 0x00-0xFF): " + spec);
                } else if (number) {
                    o.mission = static_cast<int>(v);
                } else {
                    o.mission_id = static_cast<int>(v);
                }
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--heap-fill") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            char* end = nullptr;
            const unsigned long v = std::strtoul(spec.c_str(), &end, 0);
            if (end == spec.c_str() || *end != 0 || v > 0xFF) {
                result.errors.push_back("--heap-fill expects a byte value (0-255 or 0x00-0xFF): " + spec);
            } else {
                o.heap_fill = static_cast<int>(v);
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--play-gsdump") {
#if AC04_DEV_BUILD
            value(o.play_gsdump);
#else
            dev_only(true);
#endif
        } else if (arg == "--dump-vram") {
#if AC04_DEV_BUILD
            o.dump_vram = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--log-allocs") {
#if AC04_DEV_BUILD
            o.log_allocs = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--dump-audio") {
#if AC04_DEV_BUILD
            o.dump_audio = true;
#else
            dev_only(false);
#endif
        } else if (arg == "--dump-dma-at") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            char* end = nullptr;
            const unsigned long v = std::strtoul(spec.c_str(), &end, 10);
            if (end == spec.c_str() || *end != 0 || v == 0) {
                result.errors.push_back("--dump-dma-at expects a vblank number above 0: " + spec);
            } else {
                o.dump_dma_at = static_cast<unsigned>(v);
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--fast-until") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            char* end = nullptr;
            const unsigned long v = std::strtoul(spec.c_str(), &end, 10);
            if (end == spec.c_str() || *end != 0) {
                result.errors.push_back("--fast-until expects a vblank number: " + spec);
            } else {
                o.fast_until = static_cast<unsigned>(v);
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--exit-at") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            char* end = nullptr;
            const unsigned long v = std::strtoul(spec.c_str(), &end, 10);
            if (end == spec.c_str() || *end != 0) {
                result.errors.push_back("--exit-at expects a vblank number: " + spec);
            } else {
                o.exit_at = static_cast<unsigned>(v);
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--movie-upscale") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            char* end = nullptr;
            const unsigned long v = std::strtoul(spec.c_str(), &end, 10);
            if (end == spec.c_str() || *end != 0 || v < 2 || v > 4) {
                result.errors.push_back("--movie-upscale expects a factor from 2 to 4: " + spec);
            } else {
                o.movie_upscale = static_cast<unsigned>(v);
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--screenshot-at") {
#if AC04_DEV_BUILD
            std::string spec;
            value(spec);
            const char* s = spec.c_str();
            while (*s != 0) {
                char* end = nullptr;
                o.screenshot_at.push_back(static_cast<unsigned>(std::strtoul(s, &end, 10)));
                if (end == s) {
                    result.errors.push_back("--screenshot-at expects vblank numbers: " + spec);
                    break;
                }
                s = *end == ',' ? end + 1 : end;
            }
#else
            dev_only(true);
#endif
        } else if (arg == "--dump-ram-at" || arg == "--memcard" || arg == "--no-memcard") {
#if AC04_DEV_BUILD
            if (arg == "--no-memcard") {
                o.no_memcard = true;
            } else if (arg == "--memcard") {
                value(o.memcard_dir);
            } else {
                std::string spec;
                value(spec);
                if (!spec.empty()) {
                    char* end = nullptr;
                    const unsigned long addr = std::strtoul(spec.c_str(), &end, 16);
                    unsigned long hit = 1;
                    if (end != nullptr && *end == ',') hit = std::strtoul(end + 1, &end, 10);
                    if (end == nullptr || *end != 0 || hit == 0) {
                        result.errors.push_back("--dump-ram-at expects <hex address>[,<hit>]: " + spec);
                    } else {
                        o.dump_ram_at = static_cast<unsigned>(addr);
                        o.dump_ram_at_hit = static_cast<unsigned>(hit);
                    }
                }
            }
#else
            dev_only(arg != "--no-memcard");
#endif
        } else if (arg == "--allow-unverified-iso") {
#if AC04_DEV_BUILD
            o.allow_unverified_iso = true;
#else
            dev_only(false);
#endif
        } else {
            result.errors.push_back("unknown parameter: " + arg);
        }
    }
    return result;
}

std::string CommandLineHelp() {
    return "Usage: ProjectStonehenge [options]\n"
           "\n"
           "  --iso <path>          Disc image to use (overrides paths.iso in settings.json)\n"
           "  --user-data <dir>     Folder for settings, saves and logs\n"
           "                        (default: %LOCALAPPDATA%\\ProjectStonehenge)\n"
           "  --internal-scale <n>  Internal rendering resolution factor 1-8 for this session\n"
           "                        (overrides presentation.internal_scale)\n"
           "  --antialiasing <m>    Anti-aliasing for this session: off, fxaa or smaa\n"
           "                        (overrides presentation.antialiasing)\n"
           "  --debug-overlay      Enable the debug overlay for this session\n"
           "  --no-debug-overlay    Disable the debug overlay for this session\n"
           "  --fps-overlay         Show the frame rate and frame time (top right) this session\n"
           "  --no-fps-overlay      Hide the frame rate overlay this session\n"
           "  --gpu-profile         Write the GPU time of each part of the frame to the log at exit\n"
           "  --verify-iso          Check the disc image checksum again (ignore the cache)\n"
#if AC04_DEV_BUILD
           "  --allow-unverified-iso  Development builds: continue with an image whose\n"
           "                        checksum differs from the supported version\n"
           "  --dump-ram-after-load  Development builds: write guest RAM after the executable\n"
           "                        is loaded to <user data>\\dumps\\ (research only)\n"
           "  --dump-ram-at-first-scenestep  Development builds: write guest RAM at the first\n"
           "                        SceneStep call to <user data>\\dumps\\ (research only)\n"
           "  --dump-ram-at <addr>[,<hit>]  Development builds: write guest RAM when the game\n"
           "                        reaches a RAM checkpoint address (hex; hit number, default 1)\n"
           "  --memcard <dir>       Development builds: folder memory card for slot 1\n"
           "  --no-memcard          Development builds: no memory card in slot 1\n"
           "  --screenshot-at <n>[,...]  Development builds: native screenshot after vblank n\n"
           "                        (also F8) to <user data>\\screenshots\\\n"
           "  --pad-script <v>:<button>[,...]  Development builds: press a pad-1 button for 6\n"
           "                        vblanks from vblank v (cross circle triangle square start select\n"
           "                        up down left right l1 r1 l2 r2)\n"
           "  --record-input        Development builds: write the pad state of every game tick to\n"
           "                        <user data>\\inputs\\input.ac4i\n"
           "  --replay-input <file> Development builds: take the pads from an input log (live\n"
           "                        devices ignored)\n"
           "  --replay-p2m2 <file>  Development builds: take the pads from a PCSX2 input recording\n"
           "                        (.p2m2, power-on recordings); live devices ignored\n"
           "  --p2m2-offset <n>     Development builds: tick t reads recording frame t + n\n"
           "  --hash-log            Development builds: write a hash of guest RAM per vblank to\n"
           "                        <user data>\\dumps\\hashes.ac4h (tools/research/hashdiff.py)\n"
           "  --stress-host         Development builds: random delays on the main thread (with\n"
           "                        --hash-log: determinism check against a calm run)\n"
           "  --vk-validation       Development builds: enable the Vulkan validation layer (when installed;\n"
           "                        it makes draw-heavy screens such as the hangar slow)\n"
           "  --cpu-texture-decode  Development builds: decode GS textures on the CPU (the reference decoder)\n"
           "                        instead of the GPU\n"
           "  --verify-texture-decode  Development builds: decode GS textures on both the GPU and the CPU and\n"
           "                        log every difference (slow)\n"
           "  --frame-interpolation <on|off>  Development builds: force frame interpolation for this\n"
           "                        session, also in validation runs (still only above 60 frames per second)\n"
           "  --dump-audio          Development builds: write the mixed sound to\n"
           "                        <user data>\\dumps\\audio.wav at exit (research only)\n"
           "  --exit-at <n>         Development builds: exit normally after vblank n\n"
           "  --fast-until <n>      Development builds: run the game as fast as possible up to vblank\n"
           "                        n (one picture about every 100 ms), then at normal speed\n"
           "  --dump-dma-at <n>     Development builds: write the VIF1 words and GIF data of the step\n"
           "                        starting at vblank n to <user data>\\dumps (research only)\n"
           "  --freeze-sim          Development builds: in a mission, draw the world from the start\n"
           "                        camera without running the simulation step (S5 block A)\n"
           "  --mission <1-18>      Development builds: after the startup screens, start this campaign\n"
           "                        mission with the default aircraft (F-4E, weapon 0, NORMAL)\n"
           "  --mission-id <id>     Development builds: as --mission with a raw mission id (0x23 intro,\n"
           "                        0x24 after mission 18, 0x25 opening, free-mission ids)\n"
           "  --mission-mode <m>    Development builds: campaign (default) or free (free mission)\n"
           "  --heap-fill <byte>    Development builds: fill the work heap freed at each scene change\n"
           "                        with this byte (two runs show the bytes the game never wrote)\n"
           "  --play-gsdump <file>  Development builds: draw an uncompressed PCSX2 GS dump with the port's\n"
           "                        renderer, write gsdump_native.png and gsdump_pcsx2.png, then exit\n"
           "  --dump-vram           Development builds: screenshots also write the GS local memory to\n"
           "                        <user data>\\dumps\\vram_<vblank>.bin (tools/research/gsvram.py)\n"
           "  --log-allocs          Development builds: log every work-heap allocation with its\n"
           "                        caller to <user data>\\dumps\\allocs.txt (tools/research/heapdiff.py)\n"
           "  --movie-upscale <n>   Development builds: run the movie upscaling stage (FSR 1) with\n"
           "                        factor n (2-4) while the game is still drawn at 1x; screenshots\n"
           "                        during a movie also write its output (movie_x<n>_<vblank>.png)\n"
#endif
           "  --version             Show the version and exit\n"
           "  --help                Show this text and exit\n"
           "\n"
           "Full list and details: docs/port/command-line.md\n";
}

}  // namespace ac04::platform
