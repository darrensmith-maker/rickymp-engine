#!/usr/bin/env bash
set -euo pipefail

jobs="${BUILD_JOBS:-$(nproc)}"
cmake --build build --parallel "$jobs"
cmake --install build
ccache --show-stats || true

mkdir -p artifacts
ref="${GITHUB_REF_NAME:-manual}"
ref="${ref//\//-}"
archive="artifacts/ArenaMP-Linux-x86_64-SteamDeck-${ref}.tar.gz"
bash CI/linux/package-portable.sh stage package "$archive"
