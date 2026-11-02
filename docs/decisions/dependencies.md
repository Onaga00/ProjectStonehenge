# Third-party dependencies

Status: **Approved by the User on 2026-10-11**: SDL3, Vulkan-Headers, volk, VulkanMemoryAllocator, glslang, doctest, nlohmann/json, Dear ImGui, libmpeg2 (not used before the movie phase). **Approved on 2026-10-13**: AMD FidelityFX FSR 1 (movie upscaling stage, ADR-006 §5). **Approved on 2026-10-20** (User: "if SMAA and FXAA are compatible with GPL-3 you can proceed"): SMAA and NVIDIA FXAA 3.11, after the licence check below. MoltenVK stays "later" (macOS). Any dependency not in this list still needs the User's approval.

When a dependency is added, its exact version and the license file shipped in that version are checked again and recorded in the table "Added" below and in `../../THIRD_PARTY_NOTICES.md`.

Repo license: **GPL-3.0** (`LICENSE`). A dependency is compatible if its license can be combined into a GPL-3.0 work: GPL-3.0(-or-later), GPL-2.0-**or-later**, LGPL-2.1+/3, Apache-2.0, MIT, BSD, zlib, BSL-1.0. Not compatible: GPL-2.0-**only**, proprietary, "non-commercial" licenses.

## Runtime

| Dependency | Purpose | License | GPL-3.0 compatible | Notes |
|---|---|---|---|---|
| **SDL3** | window, input (pads, joysticks), audio output, timing | zlib | yes | required by the project direction; use the stable 3.2.x line |
| **Vulkan-Headers** | Vulkan API headers | Apache-2.0 (+MIT) | yes (Apache-2.0 is GPL-3.0 compatible, not GPL-2.0-only) | header only |
| Vulkan loader | runtime | system-provided on Windows/Linux (Apache-2.0) | yes | not vendored |
| **volk** | function loader, avoids link-time dependency on the loader | MIT | yes | optional; SDL3 can also provide `vkGetInstanceProcAddr` |
| **VulkanMemoryAllocator** (VMA) | GPU memory allocation | MIT | yes | single header |
| **MoltenVK** | Vulkan on macOS (best effort target) | Apache-2.0 | yes | runtime library, not vendored |
| **MPEG-2 video decoder** (for SMV, see below) | decode MIS/SS/INTRO/STAFF movies | see options | — | must handle MPEG-2 MP@ML 640×448 elementary streams |
| **JSON library** for `settings.json` (ADR-005/006) | read/write settings | see options below | — | added 2026-10-11 |
| **Dear ImGui** (debug overlay only, `../port/port-features.md` §8) | immediate-mode UI for the optional debug layer; its repo includes SDL3 and Vulkan backends | MIT | yes | added in milestone D (v1.92.9b); fetched by FetchContent pinned to the release archive with checksum; compiled only into the debug layer, inactive unless the overlay is opened |
| **AMD FidelityFX FSR 1** (`FidelityFX-FSR`) | movie upscaling stage: EASU upscaling (+ RCAS sharpening if it improves the result) of decoded movie pictures to the internal-resolution factor (ADR-006 §5); passthrough at 1× | MIT | yes | approved 2026-10-13; shader headers only (`ffx_a.h`, `ffx_fsr1.h`), included by our GLSL with `A_GLSL`; added v1.0.2 |
| **SMAA** (`smaa`) | optional anti-aliasing of the flight views before the HUD (`presentation.antialiasing` `smaa`, `../port/port-features.md` §2): SMAA 1x, three passes | MIT | yes | approved 2026-10-20; shader source `SMAA.hlsl` included by our GLSL, lookup textures `AreaTex.h` / `SearchTex.h` compiled in |
| **NVIDIA FXAA 3.11** (`fxaa`) | optional anti-aliasing (`presentation.antialiasing` `fxaa`): one pass | BSD-3-Clause (2014-2015 release in NVIDIA GraphicsSamples) | yes | approved 2026-10-20; one shader header. The older 2010-2011 copies of `Fxaa3_11.h` carry only a warranty disclaimer and grant no licence: only the BSD-3 release is used |

### MPEG-2 decoder options

| Option | License | Compatible | Pros | Cons |
|---|---|---|---|---|
| **libmpeg2** (0.5.1) | GPL-2.0-or-later | yes | small C library; does exactly MPEG-2 video elementary streams, which is what the PS2 IPU decodes; low CPU | unmaintained since 2008 (stable, but no fixes); needs a local build |
| **FFmpeg** libavcodec, minimal build (`mpeg2video` decoder only) | LGPL-2.1-or-later (GPL if built with `--enable-gpl`) | yes | maintained, fast, well tested | large build system; easier as a system or prebuilt package; licence bookkeeping for the shipped binaries |
| pl_mpeg | MIT | yes | single header, trivial to integrate | **MPEG-1 only**: cannot decode the MPEG-2 streams in SMV files, so it is not usable |
| In-house decoder | GPL-3.0 (ours) | yes | no dependency | a correct MPEG-2 decoder is a lot of work |

Recommendation: **libmpeg2**, fetched by CMake FetchContent pinned with a checksum (copied into `third_party/` only if we have to modify it; `.claude/rules/third-party.md`), because it matches the IPU feature set and is small. FFmpeg is the fallback if libmpeg2 turns out to be insufficient (e.g. a stream feature it does not handle).

### JSON options (settings.json)

| Option | License | Compatible | Pros | Cons |
|---|---|---|---|---|
| **nlohmann/json** (3.12.x) | MIT | yes | single header, widely used, easy struct mapping (`to_json`/`from_json`), keeps unknown keys when round-tripping a `json` object | slower compile; heavier than needed for ~30 keys |
| In-house reader/writer | GPL-3.0 (ours) | yes | no dependency | a correct JSON parser (escapes, numbers, UTF-8) is ~600 lines plus tests |
| RapidJSON | MIT | yes | fast | verbose API |

Recommendation: **nlohmann/json**, pinned by FetchContent with a checksum. Settings are read once at startup and written on change, so speed does not matter.

### Debug overlay UI options

| Option | License | Notes |
|---|---|---|
| **Dear ImGui** | MIT | recommended: SDL3 + Vulkan backends maintained upstream, tables, child windows and plotting widgets in the main branch (docking only on its docking branch), widely used in emulator debuggers; square look by setting every rounding style value to 0 |
| Nuklear | MIT (or public domain) | single header, but no maintained Vulkan backend and fewer widgets (tables, memory editor) |
| RmlUi | MIT | HTML/CSS-like, heavier, aimed at game UIs rather than debug tools |

## Build and test only

| Dependency | Purpose | License | Compatible | Notes |
|---|---|---|---|---|
| **CMake ≥ 3.24** | build system | BSD-3 | n/a (tool) | chosen by the User |
| **glslang** (or **shaderc**) | GLSL → SPIR-V at build time | BSD-3 / Apache-2.0 / MIT mix (shaderc: Apache-2.0) | yes | build tool only; output SPIR-V is ours |
| **doctest** (or GoogleTest BSD-3, Catch2 BSL-1.0) | unit and behaviour tests | MIT | yes | doctest is a single header and fast to compile |

## Written in-house (no dependency proposed)

| Component | Why in-house |
|---|---|
| ISO 9660 reader | Only the primary volume descriptor, the directory records and 2048-byte sector reads are needed (the game looks files up by path, like `sceCdSearchFile`). About 300 lines. libcdio (GPL-3.0+) would be compatible but is far larger than needed |
| SHA-1 for the startup check | about 150 lines, standard algorithm (`../platform/executable.md`) |
| SPU ADPCM decoder, ADSR, voice mixer | small, documented, and must match the game's SOUND/VAG semantics (ADR-004) |

## Not allowed

- Any Sony SDK code or headers. `src/sce/` is written from observed behaviour only.
- PCSX2 source code is not proposed. Its GPL-3.0+ license would be compatible, but copying emulator internals is a separate decision for the User. Using PCSX2 as a behaviour reference is the existing practice.

## Added

| Dependency | Version | License file checked | GPL-3.0 compatible | Added in |
|---|---|---|---|---|
| SDL3 | 3.4.16 | `LICENSE.txt`: zlib; bundled HIDAPI (`src/hidapi/LICENSE.txt`: choice of GPL-3.0, BSD-style or original HIDAPI license, used under GPL-3.0; checked 2026-10-31 for DualShock 3 pressure support: part of the same pinned archive, not a separate dependency) | yes | milestone A |
| Vulkan-Headers | vulkan-sdk-1.4.363.0 | `LICENSE.md` + `LICENSES/`: Apache-2.0, MIT | yes | milestone A |
| volk | vulkan-sdk-1.4.363.0 | `LICENSE.md`: MIT | yes | milestone A |
| VulkanMemoryAllocator | v3.4.0 | `LICENSE.txt`: MIT | yes | milestone A |
| glslang | vulkan-sdk-1.4.363.0 | `LICENSE.txt` + `LICENSES/`: BSD-3, BSD-2, MIT, Apache-2.0, NVIDIA AML (permissive), GPL-3.0-or-later with Bison exception | yes; build tool only, its output (SPIR-V of our shaders) is ours | milestone A |
| nlohmann/json | v3.12.0 | `LICENSE.MIT`: MIT | yes | milestone A |
| doctest | v2.5.3 | `LICENSE.txt`: MIT | yes | milestone A |
| Dear ImGui | v1.92.9b | `LICENSE.txt`: MIT (Omar Cornut, 2014-2026) | yes | milestone D |
| FidelityFX-FSR (FSR 1) | v1.0.2 (release archive, SHA-256 `92ee8f96…04acd`) | `license.txt`: MIT (Advanced Micro Devices, 2021); same MIT text in the headers | yes | 2026-10-13, decisions for the slice plan (ADR-006 §5) |
| smaa | commit 71c806a8 (2013-11-06, archive SHA-256 `046a435f…55f57`) | `LICENSE.txt`: MIT (Jimenez, Echevarria, Masia, Navarro, Gutierrez, 2013); the same notice heads `SMAA.hlsl`, `AreaTex.h`, `SearchTex.h` | yes | 2026-10-20 (User request) |
| fxaa | `FXAA3_11.h` v3.00 of NVIDIA GraphicsSamples, fork lyntel/GraphicsSamples commit 76fd049e (file SHA-256 `ad82e2c2…98c6`) | header of the file: BSD-3-Clause, "Copyright (c) 2014-2015, NVIDIA CORPORATION" (redistribution in source and binary form with the notice; no endorsement). **Binary releases must ship this copyright notice, the conditions and the disclaimer** (e.g. a licences file next to the executable) | yes | 2026-10-20 (User request) |
| libmpeg2 | 0.5.1 (release archive from the VideoLAN contrib mirror, SHA-256 `dee22e89…71d4`) | `COPYING`: GPL-2.0, files say "version 2 or (at your option) any later version" | yes (GPL-2.0-or-later → GPL-3.0) | 2026-10-14, slice S3 (approved 2026-10-11, fetch confirmed by the User 2026-10-14) |
