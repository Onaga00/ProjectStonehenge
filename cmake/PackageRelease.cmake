# Release archive (target package_release): out/release/ProjectStonehenge-<version>-win64.zip with
# the release executable, the project's license and notice files, the README and keybinds.md
# (docs/release/), nothing else. Run as a script:
#   cmake -DCONFIG=<config> -DEXE=<exe> -DVERSION=<version> -DSOURCE_DIR=<repo> -P PackageRelease.cmake
# Only the Release configuration is packaged (development builds carry the debug layer and the
# development parameters). The staged files are checked for the signatures of game file formats
# (as tools/hooks/pre-commit does): the archive must never contain game data.

if(NOT CONFIG STREQUAL "Release")
    message(FATAL_ERROR "package_release packages the Release configuration only (this build: ${CONFIG}). "
                        "Use: cmake --build --preset release --target package_release")
endif()
foreach(var EXE VERSION SOURCE_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "PackageRelease.cmake: ${var} not set")
    endif()
endforeach()

set(name "ProjectStonehenge-${VERSION}-win64")
set(stage "${SOURCE_DIR}/out/release/${name}")
set(archive "${SOURCE_DIR}/out/release/${name}.zip")
file(REMOVE_RECURSE "${stage}")
file(REMOVE "${archive}")
file(MAKE_DIRECTORY "${stage}")

# The files of a release, and only these.
set(files "${EXE}" "${SOURCE_DIR}/LICENSE" "${SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${SOURCE_DIR}/README.md"
          "${SOURCE_DIR}/docs/release/keybinds.md")  # the players' controls guide (User request 2026-11-10)
foreach(f IN LISTS files)
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "PackageRelease.cmake: missing ${f}")
    endif()
    file(COPY "${f}" DESTINATION "${stage}")
endforeach()

# No game data: no file with a game-data name, no known game file signature anywhere in a file.
file(GLOB staged "${stage}/*")
foreach(f IN LISTS staged)
    get_filename_component(fname "${f}" NAME)
    string(TOLOWER "${fname}" lname)
    if(lname MATCHES "\\.(iso|cdp|tbl|smv|vpk|irx|img|p2s)$" OR lname MATCHES "^slus_")
        message(FATAL_ERROR "PackageRelease.cmake: game-data file name in the release: ${fname}")
    endif()
    file(READ "${f}" hex HEX)
    # VAGp, TIM2, PS2 MIPS ELF header (7F 'ELF' ... e_machine 8), IOPRP ROMDIR.
    foreach(sig 56414770 54494d32 524f4d444952)
        string(FIND "${hex}" "${sig}" at)
        if(NOT at EQUAL -1)
            math(EXPR odd "${at} % 2")
            if(odd EQUAL 0)
                message(FATAL_ERROR "PackageRelease.cmake: game file signature ${sig} in ${fname}")
            endif()
        endif()
    endforeach()
    string(REGEX MATCH "7f454c46(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)0800" elf "${hex}")
    if(elf)
        message(FATAL_ERROR "PackageRelease.cmake: embedded PS2 executable (MIPS ELF) in ${fname}")
    endif()
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${archive}" --format=zip "${name}"
                WORKING_DIRECTORY "${SOURCE_DIR}/out/release" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "PackageRelease.cmake: cannot write ${archive}")
endif()
message(STATUS "Release archive: ${archive}")
