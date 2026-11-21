# x86_64 cross-build environment for the aarch64 (Steam Frame) host binaries: the opensa2-build
# image plus clang/lld 19 and GNU binutils for aarch64. The target sysroot is Valve's sniper
# arm64 SDK, extracted to build-env/arm64/sysroot and mounted at /sysroot.
FROM opensa2-build
USER root
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      clang-19 lld-19 llvm-19 binutils-aarch64-linux-gnu libexpat1-dev g++-14-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*
USER dev
