cmake_minimum_required(VERSION 3.24)

project(ProjectStonehenge
    VERSION 0.9.7
    DESCRIPTION "Native PC port of Ace Combat 04 (SLUS_201.52)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "Supported hosts: Windows and Linux (x86-64) (ADR-005).")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(AMD64|x86_64|amd64)$")
    message(FATAL_ERROR "The port needs an x86-64 host (SSE rounding control for the VU arithmetic).")
endif()

option(AC04_BUILD_TESTS "Build the doctest unit tests" ON)

# Build version shown in the session log: project version + git commit, if available.
# AC04_GIT_COMMIT_OVERRIDE: for builds without a git checkout (the Linux container mounts only the
# source tree): cmake -DAC04_GIT_COMMIT_OVERRIDE=<commit> ...
find_package(Git QUIET)
set(AC04_GIT_COMMIT "")
if(AC04_GIT_COMMIT_OVERRIDE)
    set(AC04_GIT_COMMIT "${AC04_GIT_COMMIT_OVERRIDE}")
elseif(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AC04_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AC04_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(AC04_GIT_COMMIT AND AC04_GIT_DIRTY)
        string(APPEND AC04_GIT_COMMIT "-dirty")
    endif()
endif()
if(NOT AC04_GIT_COMMIT)
    set(AC04_GIT_COMMIT "unknown")
endif()
configure_file(src/common/build_info.h.in "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)
# Windows file properties of the executable ("Details" tab): version, description, company.
set(AC04_FILE_DESCRIPTION "Unofficial native PC port of Ace Combat 04: Shattered Skies")
if(WIN32)
    configure_file(src/platform/win32/version.rc.in "${CMAKE_BINARY_DIR}/generated/version.rc" @ONLY)
endif()

# C/C++ runtime: Release builds link it statically (/MT), so the released executable needs no Visual
# C++ Redistributable (User decision 2026-10-22); development builds keep the DLL runtime (/MD, /MDd).
# Set before the dependencies are fetched: every library must use the same runtime, also those
# whose own cmake_minimum_required predates policy CMP0091.
set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<NOT:$<CONFIG:Release>>:DLL>")

include(cmake/Dependencies.cmake)
include(cmake/Shaders.cmake)

# Common compile settings for our own targets (never applied to dependencies).
add_library(ac04_options INTERFACE)
# /fp:precise: no FMA contraction of EE float operations (MSVC contracts only with /fp:contract,
# which is not given; User decision 2026-10-13, docs/port/risks.md).
# GCC/Clang (Linux): the same guarantees as the MSVC build.
#   -ffp-contract=off       no FMA contraction (matches /fp:precise).
#   -fno-strict-aliasing    MSVC does not optimise on type-based aliasing; the guest memory model
#                           reinterprets bytes freely.
#   -fwrapv                 signed overflow wraps, as MIPS addu/subu do.
#   -msse2 -mfpmath=sse     float math in SSE registers, never x87 (x86-64 default; stated anyway).
# The VU translation units also get -frounding-math (src/CMakeLists.txt), the GCC counterpart of
# their #pragma fenv_access(on).
if(MSVC)
    target_compile_options(ac04_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP /fp:precise)
    target_compile_definitions(ac04_options INTERFACE
        UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(ac04_options INTERFACE -Wall -Wextra -Wno-unused-parameter
        -ffp-contract=off -fno-strict-aliasing -fwrapv -msse2 -mfpmath=sse)
endif()
target_compile_definitions(ac04_options INTERFACE
    # Development build = every configuration except Release (CLAUDE.md, debug overlay default).
    AC04_DEV_BUILD=$<IF:$<CONFIG:Release>,0,1>)
target_include_directories(ac04_options INTERFACE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")

add_subdirectory(src)

# Release archive (Windows): cmake --build --preset release --target package_release
# -> out/release/ProjectStonehenge-<version>-win64.zip (cmake/PackageRelease.cmake).
if(WIN32)
    add_custom_target(package_release
        COMMAND "${CMAKE_COMMAND}" -DCONFIG=$<CONFIG> -DEXE=$<TARGET_FILE:ProjectStonehenge> -DVERSION=${PROJECT_VERSION}
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}" -P "${CMAKE_SOURCE_DIR}/cmake/PackageRelease.cmake"
        DEPENDS ProjectStonehenge
        COMMENT "Packaging the release archive"
        VERBATIM)
else()
    # Linux: cmake --build --preset linux-release --target package_appimage
    # -> <build>/release/ProjectStonehenge-<version>-x86_64.AppImage (cmake/PackageAppImage.cmake).
    add_custom_target(package_appimage
        COMMAND "${CMAKE_COMMAND}" -DCONFIG=$<CONFIG> -DEXE=$<TARGET_FILE:ProjectStonehenge> -DVERSION=${PROJECT_VERSION}
                "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}" "-DWORK_DIR=${CMAKE_BINARY_DIR}"
                -P "${CMAKE_SOURCE_DIR}/cmake/PackageAppImage.cmake"
        DEPENDS ProjectStonehenge
        COMMENT "Packaging the AppImage"
        VERBATIM)
endif()

if(AC04_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
