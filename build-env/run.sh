#!/usr/bin/env bash
# Run a command inside the Debian 13 OpenSA2 build container.
#   build-env/run.sh '<command>'
# seccomp/apparmor unconfined: remill runs under `unshare -Urm` and the
# installer tests use bubblewrap, both need unprivileged user namespaces.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec docker run --rm -i \
  --security-opt seccomp=unconfined --security-opt apparmor=unconfined \
  -v "$ROOT/external/OpenSA2":/work \
  -v "$HOME/.local/share/Steam/steamapps/common/Sonic Adventure 2":/sa2:ro \
  -w /work opensa2-build bash -lc "$*"
