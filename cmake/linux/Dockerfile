# Build environment for the Linux build and the AppImage (proposed; docs/port/linux.md).
# Ubuntu 22.04 (glibc 2.35): the binary then runs on SteamOS 3 and on current desktop distributions.
#
#   docker build -t stonehenge-linux cmake/linux
#   docker run --rm -v "<repo>:/src:ro" -v stonehenge-build:/build stonehenge-linux \
#       /src/cmake/linux/build.sh release
#
# Only build tools and the development headers SDL3 compiles against. SDL loads X11, Wayland,
# ALSA, PulseAudio/PipeWire, udev and D-Bus at runtime, so the executable links none of them.
FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git python3 python3-pip ca-certificates file \
        pkg-config \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
        libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev \
        libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev \
        libdrm-dev libgbm-dev \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 22.04 ships CMake 3.22; the project needs >= 3.24.
RUN python3 -m pip install --no-cache-dir "cmake==3.31.6" "ninja==1.11.1.4"

WORKDIR /build
