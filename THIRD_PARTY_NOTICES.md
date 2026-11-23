# Third-party notices

Every third-party dependency of this project, whether fetched at build time or copied into `third_party/`, is listed here. The list is updated in the same change that adds, updates or removes a dependency (rules: `.claude/rules/third-party.md`; check: `python tools/check_notices.py`).

This project is licensed under GPL-3.0 (`LICENSE`). Each dependency's license must be GPL-3.0 compatible; see `docs/decisions/dependencies.md`.

## Format

One row per dependency:

| Name | Version / commit | License | URL | Modified | Included as |
|---|---|---|---|---|---|
| *example: SDL3* | *3.2.x tag or commit* | *zlib* | *https://…* | *no* | *FetchContent (URL_HASH SHA256)* |

- **Name**: the `FetchContent_Declare` name or the `third_party/<name>` folder name (the check compares these case-insensitively).
- **Modified**: `yes` only for copies in `third_party/` that carry local changes (listed in their `UPSTREAM.md`).
- **Included as**: `FetchContent`, `ExternalProject` or `third_party/<name>`.

## Dependencies

| Name | Version / commit | License | URL | Modified | Included as |
|---|---|---|---|---|---|

| SDL3 | 3.4.16 (release archive) | zlib; the archive also contains HIDAPI (`src/hidapi`, Alan Ott / Signal 11 Software and contributors, compiled in: SDL's HIDAPI gamepad drivers, e.g. the PS3 drivers that report DualShock 3 pressures), offered under GPL-3.0, BSD-style or the original HIDAPI license; this project uses it under **GPL-3.0** | https://github.com/libsdl-org/SDL | no | FetchContent (URL_HASH SHA256) |
| Vulkan-Headers | vulkan-sdk-1.4.363.0 | Apache-2.0 (some files MIT) | https://github.com/KhronosGroup/Vulkan-Headers | no | FetchContent (URL_HASH SHA256) |
| volk | vulkan-sdk-1.4.363.0 | MIT | https://github.com/zeux/volk | no | FetchContent (URL_HASH SHA256) |
| VulkanMemoryAllocator | v3.4.0 | MIT | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | no | FetchContent (URL_HASH SHA256) |
| glslang | vulkan-sdk-1.4.363.0 | BSD-3-Clause, BSD-2-Clause, MIT, Apache-2.0, NVIDIA AML, GPL-3.0-or-later with Bison exception (build tool only, not linked) | https://github.com/KhronosGroup/glslang | no | FetchContent (URL_HASH SHA256) |
| nlohmann_json | v3.12.0 (json.tar.xz release asset) | MIT | https://github.com/nlohmann/json | no | FetchContent (URL_HASH SHA256) |
| doctest | v2.5.3 | MIT | https://github.com/doctest/doctest | no | FetchContent (URL_HASH SHA256) |
| imgui | v1.92.9b | MIT | https://github.com/ocornut/imgui | no | FetchContent (URL_HASH SHA256); core + `imgui_impl_sdl3` + `imgui_impl_vulkan` compiled by us, debug overlay only |
| FidelityFX-FSR | v1.0.2 (FSR 1, `ffx_fsr1.h` v1.20210629) | MIT | https://github.com/GPUOpen-Effects/FidelityFX-FSR | no | FetchContent (URL_HASH SHA256); only the shader headers `ffx-fsr/ffx_a.h` and `ffx-fsr/ffx_fsr1.h` are used, compiled into our SPIR-V (movie upscaling stage, ADR-006 §5); the sample is not built |
| smaa | commit 71c806a838bdd7d517df19192a20f0c61b3ca29d (2013-11-06; the repository has no releases) | MIT | https://github.com/iryoku/smaa | no | FetchContent (URL_HASH SHA256); `SMAA.hlsl` is included by our GLSL shaders (`SMAA_GLSL_4`) and compiled into our SPIR-V; `Textures/AreaTex.h` and `Textures/SearchTex.h` (lookup textures) are compiled into the renderer; the demos are not built |
| fxaa | `FXAA3_11.h` from NVIDIA GraphicsSamples v3.00 (2014-2015), via the fork lyntel/GraphicsSamples at commit 76fd049ed388628dfbf525397632c901fb925e30 (NVIDIA's own repository is no longer online) | BSD-3-Clause (NVIDIA Corporation) | https://github.com/lyntel/GraphicsSamples/blob/76fd049ed388628dfbf525397632c901fb925e30/samples/es3-kepler/FXAA/FXAA3_11.h | no | FetchContent (single file, URL_HASH SHA256); included by our GLSL shader (`FXAA_GLSL_130`) and compiled into our SPIR-V |
| libmpeg2 | 0.5.1 (release archive, VideoLAN contrib mirror) | GPL-2.0-or-later | https://libmpeg2.sourceforge.io/ | no | FetchContent (URL_HASH SHA256); the decoder core (`libmpeg2/*.c` without the CPU-specific files) compiled by us as plain C with our own `config.h`; `libvo`, `convert` and the tools are not built |
| AppImage type 2 runtime | 20251108 (release asset `runtime-x86_64`, SHA-256 2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d) | MIT; the binary statically contains libfuse (LGPL-2.1-or-later), squashfuse (BSD-2-Clause) and zstd (BSD-3-Clause) | https://github.com/AppImage/type2-runtime | no | Linux AppImage only: downloaded by `cmake/PackageAppImage.cmake` at packaging time and placed unmodified at the front of the AppImage file; not in the repository, not in the Windows release |
