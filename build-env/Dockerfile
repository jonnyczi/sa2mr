# Debian 13 (trixie) build environment matching OpenSA2's tools/pins.json
# (gcc 14.2.0, cmake 3.31.6, ninja 1.12.1, binutils 2.44, python 3.13.5,
#  meson 1.7.0, pkgconf 1.8.1, GNU patch 2.8, libasound2 1.2.14-1+deb13u1).
FROM debian:trixie

ARG UID=1000
ARG GID=1000

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential gcc g++ binutils cmake ninja-build meson pkgconf patch \
      python3 python3-venv python3-pip \
      git curl ca-certificates unzip xz-utils file bubblewrap \
      libasound2t64 libsdl2-2.0-0 \
      libvulkan-dev libvulkan1 mesa-vulkan-drivers \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev \
      libwayland-dev libxkbcommon-dev wayland-protocols \
      libgl-dev libegl-dev libpng-dev libzstd-dev \
    && rm -rf /var/lib/apt/lists/*

# OpenXR (the VR player, host/xr): the Khronos loader and headers.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      libopenxr-dev libopenxr-loader1 \
    && rm -rf /var/lib/apt/lists/*

RUN groupadd -g ${GID} dev && useradd -m -u ${UID} -g ${GID} -s /bin/bash dev
USER dev
WORKDIR /work
