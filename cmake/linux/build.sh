#!/bin/sh
# Configure, build and test inside the cmake/linux/Dockerfile image (proposed; docs/port/linux.md).
#   build.sh [debug|dev|release] [appimage]
# Source at /src (read-only is fine), build tree at /build/linux. With "appimage" (release only) the
# AppImage is also copied to /out when that folder is mounted.
set -eu
preset=${1:-dev}
case "$preset" in
    debug) config=Debug ;;
    dev) config=RelWithDebInfo ;;
    release) config=Release ;;
    *) echo "usage: build.sh [debug|dev|release] [appimage]" >&2; exit 2 ;;
esac

# The container sees no git checkout: pass the commit for the session log with
#   docker run -e AC04_GIT_COMMIT=$(git rev-parse --short=12 HEAD) ...
cmake -S /src -B /build/linux -G "Ninja Multi-Config" "-DAC04_GIT_COMMIT_OVERRIDE=${AC04_GIT_COMMIT:-}"
cmake --build /build/linux --config "$config"
ctest --test-dir /build/linux -C "$config" --output-on-failure
if [ "${2:-}" = appimage ]; then
    cmake --build /build/linux --config "$config" --target package_appimage
    if [ -d /out ]; then
        cp /build/linux/release/*.AppImage /out/
    fi
fi
