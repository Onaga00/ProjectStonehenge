# Linux release AppImage (target package_appimage; proposed, docs/port/linux.md):
# out/release/ProjectStonehenge-<version>-x86_64.AppImage with the release executable, the project's
# license and notice files, the README and keybinds.md, nothing else. Run as a script:
#   cmake -DCONFIG=<config> -DEXE=<exe> -DVERSION=<version> -DSOURCE_DIR=<repo> -DWORK_DIR=<dir> -P PackageAppImage.cmake
#
# Tools (docs/decisions/dependencies.md): appimagetool 1.9.1 and the AppImage type 2 runtime
# 20251108, both MIT, downloaded here at packaging time and pinned by SHA-256. The runtime becomes
# the AppImage's first bytes (listed in THIRD_PARTY_NOTICES.md); appimagetool is not shipped.
# The executable links SDL3 statically and loads libvulkan.so.1 at runtime (volk), so no shared
# library is bundled: the Vulkan loader and driver must come from the host (as on the Steam Deck).

if(NOT CONFIG STREQUAL "Release")
    message(FATAL_ERROR "package_appimage packages the Release configuration only (this build: ${CONFIG}).")
endif()
foreach(var EXE VERSION SOURCE_DIR WORK_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "PackageAppImage.cmake: ${var} not set")
    endif()
endforeach()

set(tools "${WORK_DIR}/appimage-tools")
set(appimagetool "${tools}/appimagetool-1.9.1-x86_64.AppImage")
set(runtime "${tools}/runtime-20251108-x86_64")
if(NOT EXISTS "${appimagetool}")
    file(DOWNLOAD "https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage"
         "${appimagetool}"
         EXPECTED_HASH SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
         TLS_VERIFY ON)
    file(CHMOD "${appimagetool}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endif()
if(NOT EXISTS "${runtime}")
    file(DOWNLOAD "https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64"
         "${runtime}"
         EXPECTED_HASH SHA256=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
         TLS_VERIFY ON)
endif()

set(name "ProjectStonehenge-${VERSION}-x86_64")
set(appdir "${WORK_DIR}/${name}.AppDir")
set(out_dir "${WORK_DIR}/release")
set(image "${out_dir}/${name}.AppImage")
file(REMOVE_RECURSE "${appdir}")
file(REMOVE "${image}")
file(MAKE_DIRECTORY "${appdir}/usr/bin" "${appdir}/usr/share/doc/projectstonehenge" "${out_dir}")

# The files of a release, and only these (same list as PackageRelease.cmake).
set(docs "${SOURCE_DIR}/LICENSE" "${SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${SOURCE_DIR}/README.md"
         "${SOURCE_DIR}/docs/release/keybinds.md")
foreach(f IN LISTS docs EXE)
    if(NOT EXISTS "${f}")
        message(FATAL_ERROR "PackageAppImage.cmake: missing ${f}")
    endif()
endforeach()
file(COPY "${EXE}" DESTINATION "${appdir}/usr/bin")
file(COPY ${docs} DESTINATION "${appdir}/usr/share/doc/projectstonehenge")
file(COPY "${SOURCE_DIR}/cmake/linux/projectstonehenge.desktop" "${SOURCE_DIR}/cmake/linux/projectstonehenge.svg"
     DESTINATION "${appdir}")
file(WRITE "${appdir}/AppRun"
"#!/bin/sh
# Starts the port from inside the AppImage. User data goes to $XDG_DATA_HOME/projectstonehenge.
HERE=\"$(dirname \"$(readlink -f \"$0\")\")\"
exec \"$HERE/usr/bin/ProjectStonehenge\" \"$@\"
")
file(CHMOD "${appdir}/AppRun" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

# No game data: no file with a game-data name, no known game file signature anywhere in a file.
file(GLOB_RECURSE staged "${appdir}/*")
foreach(f IN LISTS staged)
    get_filename_component(fname "${f}" NAME)
    string(TOLOWER "${fname}" lname)
    if(lname MATCHES "\\.(iso|cdp|tbl|smv|vpk|irx|img|p2s)$" OR lname MATCHES "^slus_")
        message(FATAL_ERROR "PackageAppImage.cmake: game-data file name in the release: ${fname}")
    endif()
    file(READ "${f}" hex HEX)
    foreach(sig 56414770 54494d32 524f4d444952)  # VAGp, TIM2, IOPRP ROMDIR
        string(FIND "${hex}" "${sig}" at)
        if(NOT at EQUAL -1)
            math(EXPR odd "${at} % 2")
            if(odd EQUAL 0)
                message(FATAL_ERROR "PackageAppImage.cmake: game file signature ${sig} in ${fname}")
            endif()
        endif()
    endforeach()
    # PS2 MIPS ELF (e_machine 8, little endian), not the x86-64 executable (e_machine 0x3E).
    string(REGEX MATCH "7f454c46(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)(..)0800" elf "${hex}")
    if(elf)
        message(FATAL_ERROR "PackageAppImage.cmake: embedded PS2 executable (MIPS ELF) in ${fname}")
    endif()
endforeach()

# APPIMAGE_EXTRACT_AND_RUN: appimagetool is itself an AppImage; this runs it without FUSE
# (containers). ARCH is required when the AppDir has no ELF at its root.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64
            "${appimagetool}" --no-appstream --runtime-file "${runtime}" "${appdir}" "${image}"
    RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "PackageAppImage.cmake: appimagetool failed (${result})")
endif()
message(STATUS "AppImage: ${image}")
