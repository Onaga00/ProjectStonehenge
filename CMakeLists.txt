cmake_minimum_required(VERSION 3.24)

project(ProjectStonehenge
    VERSION 0.9.7
    DESCRIPTION "Native PC port of Ace Combat 04 (SLUS_201.52)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "The skeleton targets Windows first (ADR-005). Linux support is added after the port is complete.")
endif()

option(AC04_BUILD_TESTS "Build the doctest unit tests" ON)

# Build version shown in the session log: project version + git commit, if available.
find_package(Git QUIET)
set(AC04_GIT_COMMIT "unknown")
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AC04_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=no
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE AC04_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(AC04_GIT_DIRTY)
        string(APPEND AC04_GIT_COMMIT "-dirty")
    endif()
endif()
configure_file(src/common/build_info.h.in "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)
# Windows file properties of the executable ("Details" tab): version, description, company.
set(AC04_FILE_DESCRIPTION "Unofficial native PC port of Ace Combat 04: Shattered Skies")
configure_file(src/platform/win32/version.rc.in "${CMAKE_BINARY_DIR}/generated/version.rc" @ONLY)

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
# which is not given; User decision 2026-10-13, docs/port/risks.md; -ffp-contract=off for
# GCC/Clang when Linux is added).
target_compile_options(ac04_options INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /MP /fp:precise)
target_compile_definitions(ac04_options INTERFACE
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS
    # Development build = every configuration except Release (CLAUDE.md, debug overlay default).
    AC04_DEV_BUILD=$<IF:$<CONFIG:Release>,0,1>)
target_include_directories(ac04_options INTERFACE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/generated")

add_subdirectory(src)

# Release archive: cmake --build --preset release --target package_release
# -> out/release/ProjectStonehenge-<version>-win64.zip (cmake/PackageRelease.cmake).
add_custom_target(package_release
    COMMAND "${CMAKE_COMMAND}" -DCONFIG=$<CONFIG> -DEXE=$<TARGET_FILE:ProjectStonehenge> -DVERSION=${PROJECT_VERSION}
            "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}" -P "${CMAKE_SOURCE_DIR}/cmake/PackageRelease.cmake"
    DEPENDS ProjectStonehenge
    COMMENT "Packaging the release archive"
    VERBATIM)

if(AC04_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
