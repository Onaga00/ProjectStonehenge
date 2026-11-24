# Third-party dependencies (approved by the User on 2026-10-11, docs/decisions/dependencies.md).
# Every one is fetched unmodified, pinned to a release archive with its SHA-256
# (.claude/rules/third-party.md) and listed in THIRD_PARTY_NOTICES.md.
# Header-only or hand-built dependencies use SOURCE_SUBDIR cmake-not-used so that their own
# CMake project is not added; we compile only the files we need.

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- SDL3 (zlib): window, input, audio -------------------------------------------------------
FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz
    URL_HASH SHA256=7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

# --- Vulkan-Headers (Apache-2.0 / MIT) --------------------------------------------------------
FetchContent_Declare(Vulkan-Headers
    URL      https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
    URL_HASH SHA256=4a078be12bef21cfebc09d878b77a63cff9d68f899254a0b00d0e37ef73e7f7e)

# --- volk (MIT): Vulkan function loader, compiled by us ---------------------------------------
FetchContent_Declare(volk
    URL      https://github.com/zeux/volk/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
    URL_HASH SHA256=1547d8d74395d4048fb3f4a6313da56db626e89b55db238d0d7e8944c8a645f3
    SOURCE_SUBDIR cmake-not-used)

# --- VulkanMemoryAllocator (MIT): header only ------------------------------------------------
FetchContent_Declare(VulkanMemoryAllocator
    URL      https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz
    URL_HASH SHA256=822aa850c6ce77346ae96a8a1d351d52e77e85929f35363849a0a4e638e0a2a1
    SOURCE_SUBDIR cmake-not-used)

# --- glslang (BSD-3 / BSD-2 / MIT / Apache-2.0 / AML / GPL-3.0+ with Bison exception) ----------
# Build tool only: compiles our GLSL shaders to SPIR-V. Not linked into the executable.
FetchContent_Declare(glslang
    URL      https://github.com/KhronosGroup/glslang/archive/refs/tags/vulkan-sdk-1.4.363.0.tar.gz
    URL_HASH SHA256=cda24ec765e6d2845d106b7caa593f34127195a88524cf87821ff0088fb6edec)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)

# --- nlohmann/json (MIT): settings.json --------------------------------------------------------
FetchContent_Declare(nlohmann_json
    URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    SOURCE_SUBDIR cmake-not-used)

# --- doctest (MIT): unit tests -------------------------------------------------------------------
FetchContent_Declare(doctest
    URL      https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
    SOURCE_SUBDIR cmake-not-used)

# --- Dear ImGui (MIT): debug overlay only (docs/port/port-features.md §8), compiled by us --------
FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
    URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
    SOURCE_SUBDIR cmake-not-used)

# --- AMD FidelityFX FSR 1 (MIT): movie upscaling stage (ADR-006 §5), shader headers only --------
# ffx_a.h + ffx_fsr1.h are included by our GLSL shaders (A_GLSL). The repo's sample is not built.
FetchContent_Declare(FidelityFX-FSR
    URL      https://github.com/GPUOpen-Effects/FidelityFX-FSR/archive/refs/tags/v1.0.2.tar.gz
    URL_HASH SHA256=92ee8f9630364ec3ad7fc16af5a990601ebf18142571527ed672b79b79d04acd
    SOURCE_SUBDIR cmake-not-used)

# --- libmpeg2 (GPL-2.0-or-later): MPEG-2 video decoder behind sceMpeg* (slice S3) -------------
# Release archive from the VideoLAN contrib mirror (the sourceforge download page is HTML only).
# Built by us as plain C (no MMX/AltiVec paths) with our own config.h; libvo and the converters are
# not used: the IPU colour conversion is done in src/sce/libmpeg.cpp.
FetchContent_Declare(libmpeg2
    URL      https://download.videolan.org/contrib/libmpeg2/libmpeg2-0.5.1.tar.gz
    URL_HASH SHA256=dee22e893cb5fc2b2b6ebd60b88478ab8556cb3b93f9a0d7ce8f3b61851871d4
    SOURCE_SUBDIR cmake-not-used)

# --- SMAA (MIT): optional anti-aliasing pass (presentation.antialiasing "smaa", User 2026-10-20) --
# SMAA.hlsl is included by our GLSL shaders (SMAA_GLSL_4); Textures/AreaTex.h and SearchTex.h are the
# precomputed lookup textures, compiled into the renderer. The demos are not built. The repository
# has no releases: pinned to the last commit (2013-11-06).
FetchContent_Declare(smaa
    URL      https://github.com/iryoku/smaa/archive/71c806a838bdd7d517df19192a20f0c61b3ca29d.tar.gz
    URL_HASH SHA256=046a435f5713f8bfe8148fe6305c2abf1409fc2122bee6070061c675e1e55f57
    SOURCE_SUBDIR cmake-not-used)

# --- NVIDIA FXAA 3.11 (BSD-3-Clause, NVIDIA GraphicsSamples 2014-2015 release): optional
# anti-aliasing pass (presentation.antialiasing "fxaa"). One shader header, included by our GLSL
# shader (FXAA_GLSL_130). NVIDIA's GraphicsSamples repository is no longer online; the file is taken
# from a public fork of it, pinned to a commit and checked by its SHA-256.
FetchContent_Declare(fxaa
    URL      https://raw.githubusercontent.com/lyntel/GraphicsSamples/76fd049ed388628dfbf525397632c901fb925e30/samples/es3-kepler/FXAA/FXAA3_11.h
    URL_HASH SHA256=ad82e2c292fa9ad91b8ad89dd5407f626766bcf3647b93556940353524b198c6
    DOWNLOAD_NO_EXTRACT TRUE
    SOURCE_SUBDIR cmake-not-used)

FetchContent_MakeAvailable(SDL3 Vulkan-Headers volk VulkanMemoryAllocator glslang nlohmann_json doctest imgui
    FidelityFX-FSR libmpeg2 smaa fxaa)

# Group third-party targets in the Visual Studio solution.
foreach(t IN ITEMS SDL3-static glslang glslang-standalone)
    if(TARGET ${t})
        set_property(TARGET ${t} PROPERTY FOLDER "third_party")
    endif()
endforeach()

add_library(ac04_volk STATIC "${volk_SOURCE_DIR}/volk.c")
target_include_directories(ac04_volk PUBLIC "${volk_SOURCE_DIR}")
target_compile_definitions(ac04_volk PUBLIC VK_NO_PROTOTYPES)
if(WIN32)
    target_compile_definitions(ac04_volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
else()
    # volk loads libvulkan.so.1 at runtime (dlopen); the window surface comes from SDL.
    target_link_libraries(ac04_volk PUBLIC ${CMAKE_DL_LIBS})
endif()
target_link_libraries(ac04_volk PUBLIC Vulkan::Headers)
set_property(TARGET ac04_volk PROPERTY FOLDER "third_party")

add_library(ac04_vma INTERFACE)
target_include_directories(ac04_vma SYSTEM INTERFACE "${vulkanmemoryallocator_SOURCE_DIR}/include")

add_library(ac04_json INTERFACE)
target_include_directories(ac04_json SYSTEM INTERFACE "${nlohmann_json_SOURCE_DIR}/single_include")

add_library(ac04_imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(ac04_imgui SYSTEM PUBLIC "${imgui_SOURCE_DIR}" "${imgui_SOURCE_DIR}/backends")
target_compile_definitions(ac04_imgui PUBLIC IMGUI_IMPL_VULKAN_USE_VOLK IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
target_link_libraries(ac04_imgui PUBLIC ac04_volk SDL3::SDL3-static)
set_property(TARGET ac04_imgui PROPERTY FOLDER "third_party")

add_library(ac04_doctest INTERFACE)
target_include_directories(ac04_doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

# FSR 1 shader headers for cmake/Shaders.cmake (GLSL include path); no C++ target uses them.
set(AC04_FSR1_INCLUDE_DIR "${fidelityfx-fsr_SOURCE_DIR}/ffx-fsr" CACHE INTERNAL "FSR 1 shader headers")
# SMAA and FXAA shader headers (GLSL include path), and the SMAA lookup textures (C++ include path).
set(AC04_SMAA_DIR "${smaa_SOURCE_DIR}" CACHE INTERNAL "SMAA sources")
set(AC04_FXAA_DIR "${fxaa_SOURCE_DIR}" CACHE INTERNAL "FXAA 3.11 header")
add_library(ac04_smaa_textures INTERFACE)
target_include_directories(ac04_smaa_textures SYSTEM INTERFACE "${smaa_SOURCE_DIR}/Textures")

# libmpeg2: the decoder core only (decode, header, slice, idct, motion_comp, alloc, cpu_*).
set(AC04_MPEG2_CONFIG_DIR "${CMAKE_BINARY_DIR}/libmpeg2-config")
file(WRITE "${AC04_MPEG2_CONFIG_DIR}/config.h"
"/* config.h for libmpeg2 0.5.1, written by cmake/Dependencies.cmake (ProjectStonehenge): portable C only. */
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define STDC_HEADERS 1
#define VERSION \"0.5.1\"
#if defined(_MSC_VER) && !defined(__cplusplus)
#define inline __inline
#endif
#if defined(_MSC_VER)
#define restrict __restrict
#else
#define restrict __restrict__
#endif
")
add_library(ac04_mpeg2 STATIC
    "${libmpeg2_SOURCE_DIR}/libmpeg2/alloc.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/cpu_accel.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/cpu_state.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/decode.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/header.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/idct.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/motion_comp.c"
    "${libmpeg2_SOURCE_DIR}/libmpeg2/slice.c")
target_include_directories(ac04_mpeg2 PRIVATE "${AC04_MPEG2_CONFIG_DIR}")
target_include_directories(ac04_mpeg2 SYSTEM PUBLIC "${libmpeg2_SOURCE_DIR}/include")
if(MSVC)
    target_compile_options(ac04_mpeg2 PRIVATE /W0)
else()
    target_compile_options(ac04_mpeg2 PRIVATE -w)
endif()
set_property(TARGET ac04_mpeg2 PROPERTY FOLDER "third_party")
