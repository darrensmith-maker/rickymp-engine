#!/usr/bin/env bash
set -euo pipefail

build_dir="${1:-build}"
install_script="$build_dir/cmake_install.cmake"

if [[ ! -f "$install_script" ]]; then
    echo "ArenaMP macOS: generated install script not found: $install_script" >&2
    echo "Run CI/macos-arm64/configure.sh before this check." >&2
    exit 1
fi

scratch_root="$(mktemp -d "${TMPDIR:-/tmp}/arenamp-install-syntax.XXXXXX")"
cleanup() {
    rm -rf "$scratch_root"
}
trap cleanup EXIT

# A deliberately nonexistent component executes no installation actions, but
# CMake must still parse the complete generated cmake_install.cmake tree. This
# catches malformed install(CODE) blocks before the expensive application build.
cmake --install "$build_dir" \
    --component ArenaMPInstallSyntaxCheck \
    --prefix "$scratch_root/prefix"

echo "ArenaMP macOS: generated CMake install scripts parsed successfully."
