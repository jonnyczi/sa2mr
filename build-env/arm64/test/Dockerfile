# aarch64 test bench for the Steam Frame release, run under QEMU user emulation on the x86_64
# host: Debian 13 arm64 (a recent glibc, as SteamOS has) with Mesa's lavapipe (a CPU Vulkan
# driver), Xvfb, and the X11/Wayland/ALSA client libraries SDL3 loads at run time.
FROM --platform=linux/arm64 debian:trixie
ARG UID=1000
ARG GID=1000
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      mesa-vulkan-drivers libvulkan1 vulkan-tools xvfb xauth x11-utils zlib1g libasound2t64 \
      libx11-6 libxext6 libxrandr2 libxcursor1 libxi6 libxfixes3 libxss1 libxkbcommon0 libxkbcommon-x11-0 \
      libwayland-client0 libwayland-cursor0 libwayland-egl1 libdecor-0-0 libegl1 libgl1 libudev1 \
      python3 procps file binutils \
    && rm -rf /var/lib/apt/lists/*
RUN groupadd -g ${GID} dev && useradd -m -u ${UID} -g ${GID} -s /bin/bash dev
USER dev
WORKDIR /home/dev
