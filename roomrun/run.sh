#!/usr/bin/env bash
# Run a command inside the build container (after an incremental build), e.g. ./run.sh ./build/roomrun-gen --help
here="$(cd "$(dirname "$0")" && pwd)"
docker run --rm -u "$(id -u):$(id -g)" -v "$here":/w -w /w opensa2-build bash -c 'cmake --build build >/tmp/b.log 2>&1 || { grep -E "error" -A3 /tmp/b.log | head -40; exit 1; }; "$@"' _ "$@"
