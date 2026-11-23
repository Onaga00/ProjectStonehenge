# Linux build and AppImage (Steam Deck)

Status: **proposed** (issue #13, Steam Deck support). Moves the Linux part of ADR-005 §1 ahead of
"after the port is complete"; the amendment is in ADR-005 §1a and needs the owner's decision.

## 1. What the Linux build is

- The same code as the Windows build. Game code (`src/game/`), the SDK layer (`src/sce/`) and the
  renderer, audio and input backends are unchanged: they already used only C++17, SDL3 and Vulkan.
- OS-specific code: five files in `src/platform/posix/` mirror `src/platform/win32/`:

| Windows | Linux | Interface |
|---|---|---|
| `crash_win32.cpp` (unhandled-exception filter) | `crash_posix.cpp`: `sigaction` for SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT on an alternate stack, `backtrace` + `dladdr` for `module+0xoffset` frames, then the default action | `platform/crash.h` |
| `file_reader_win32.cpp` (`ReadFile` with `OVERLAPPED`) | `file_reader_posix.cpp`: `pread` (positioned, thread-safe), `posix_fadvise(RANDOM)` | `platform/file_reader.h` |
| `paths_win32.cpp` (`%LOCALAPPDATA%\ProjectStonehenge`) | `paths_posix.cpp`: `$XDG_DATA_HOME/projectstonehenge`, fallback `~/.local/share/projectstonehenge` (as ADR-005 §2 already specified); executable folder from `/proc/self/exe` | `platform/paths.h` |
| `system_info_win32.cpp` (`RtlGetVersion`) | `system_info_posix.cpp`: `PRETTY_NAME` from `/etc/os-release` + `uname` (e.g. `SteamOS (Linux 6.x)`) | `platform/system_info.h` |
| `timer_win32.cpp` (high-resolution waitable timer) | `timer_posix.cpp`: `sleep_for` until ~1 ms before the deadline, then yield-spin (same split) | `platform/frame_pacer.h` |

- One shared helper: `platform::OpenFile(path, mode)` (`platform/paths.h`) replaces the six
  `_wfopen` calls in `main.cpp` and `log.cpp` (Windows keeps `_wfopen` inside it).
- The user-data layout under the data folder is the same as on Windows (`saves/`, `settings.json`,
  `controls.ini`, `logs/`, `cache/`), so a `saves/` folder can be copied between Windows, Linux
  and PCSX2.

## 2. Compiler flags (same behaviour as MSVC)

The port must compute exactly what the PS2 computes, so the GCC/Clang build is configured to give
the same floating-point and integer guarantees as the MSVC build (`CMakeLists.txt`):

| Flag | Why | MSVC counterpart |
|---|---|---|
| `-ffp-contract=off` | no fused multiply-add contraction of EE float operations | `/fp:precise` (no `/fp:contract`) |
| `-frounding-math` (only `vu0_hle.cpp`, `vu1_programs.cpp`) | these files switch the SSE rounding mode to chop (`vu0::micro::ChopScope`); the compiler must not fold or move float operations across the switch | `#pragma fenv_access(on)` in the same files |
| `-msse2 -mfpmath=sse` | single-precision math in SSE registers, never x87 extended precision | x64 default |
| `-fno-strict-aliasing` | the guest memory model reinterprets bytes; MSVC never optimises on type-based aliasing | default |
| `-fwrapv` | signed overflow wraps, as MIPS `addu`/`subu` do | MSVC does not exploit signed overflow in practice |
| `-static-libstdc++ -static-libgcc` (executable) | the binary needs only glibc ≥ 2.35 from the host | `/MT` (static runtime) |

CMake stops on a host that is not x86-64 (the VU rounding code uses the SSE control register).

## 3. Building

Supported build environment: **Ubuntu 22.04** (glibc 2.35, so the binary also runs on SteamOS 3
and current distributions), in the container described by `cmake/linux/Dockerfile`:

```sh
docker build -t stonehenge-linux cmake/linux
docker run --rm -e AC04_GIT_COMMIT=$(git rev-parse --short=12 HEAD) -v "$PWD:/src:ro" -v "$PWD/out/release:/out" -v stonehenge-build:/build \
    stonehenge-linux sh /src/cmake/linux/build.sh release appimage
```

`build.sh [debug|dev|release] [appimage]` configures, builds, runs the unit tests and, with
`appimage`, packages the AppImage and copies it to `/out`. On a Linux machine with the same
packages installed (see the Dockerfile) the presets work directly:

```sh
cmake --preset linux
cmake --build --preset linux-release
ctest --preset linux-release
cmake --build --preset linux-release --target package_appimage   # -> build/linux/release/*.AppImage
```

No Vulkan SDK and no system SDL are needed: SDL3 is linked statically, Vulkan is loaded at run time
(volk, `libvulkan.so.1` from the host).

## 4. AppImage

`cmake/PackageAppImage.cmake` (target `package_appimage`, Release only) builds
`ProjectStonehenge-<version>-x86_64.AppImage`:

- contents: the release executable, `LICENSE`, `THIRD_PARTY_NOTICES.md`, `README.md`,
  `keybinds.md` (the same files as the Windows zip), a `.desktop` entry and an icon
  (`cmake/linux/`); the same game-data name and signature checks as `PackageRelease.cmake`;
- no shared libraries are bundled: the executable needs only glibc, and the Vulkan loader and
  driver must be the host's (bundling them would break the GPU driver match);
- tools, downloaded at packaging time and pinned by SHA-256 (`docs/decisions/dependencies.md`):
  appimagetool 1.9.1 (not shipped) and the AppImage type 2 runtime 20251108, which becomes the
  first part of the AppImage file (listed in `THIRD_PARTY_NOTICES.md`).

## 5. Steam Deck

1. Copy the AppImage and your ISO to the Deck (Desktop Mode). Make the AppImage executable
   (Dolphin: right-click → Properties → Permissions → *Is executable*, or `chmod +x`).
2. Start it once in Desktop Mode: the **Select ISO...** button opens the file picker; the choice
   is saved in `~/.local/share/projectstonehenge/settings.json`.
3. Steam → *Add a Non-Steam Game* → pick the AppImage. Start it from Game Mode. Do not force a
   Proton version: it is a native Linux program.
4. Controls: Steam Input presents the Deck's controls as an Xbox-style gamepad, which SDL maps like
   any other pad (`controls.ini`). The default display settings for the 1280×800 screen are
   fullscreen and internal scale 2× (448 × 2 = 896 lines is the closest to 800).

Saves are in `~/.local/share/projectstonehenge/saves/`, a PCSX2 folder memory card like on Windows.

## 6. Validation (2026-11-22)

| Check | Result |
|---|---|
| Unit tests (`ac04_tests`, doctest) in the container, RelWithDebInfo and Release | 118 test cases, 2755 assertions, all pass (the PCSX2 test vectors and the determinism tests included) |
| Guest RAM against the Windows build: `--mission 1 --pad-script 800:cross --hash-log --fast-until 2400`, compared with `tools/research/hashdiff.py` | the 46 vblanks the Linux run reached are identical to the Windows run (same game code; the Windows build was RelWithDebInfo from the same base commit) |
| The same run, headless (Xvfb, Mesa lavapipe, **no audio device**) | **open:** after vblank 46 the game thread stops advancing (one core busy, no further vblank). The session log shows that the audio device could not be opened (the container has no sound card); whether that is the cause is not confirmed. Not seen on the Steam Deck |
| Steam Deck (SteamOS, Game Mode, Release AppImage) | boots, menus and missions play; the GRAPHICS page shows the 1280×800 defaults (fullscreen, v-sync, internal scale 2×) |
| Windows build with the shared changes (`paths`, `log`, `main.cpp`, CMake) | the changed sources compile with MSVC without warnings; a full Windows link and test run was not repeated for this change |

Open: run the headless check with `SDL_AUDIO_DRIVER=dummy`; if it then passes, the port must keep
running when no audio device opens (all platforms).
