#!/usr/bin/env bash
set -euo pipefail

JOBS="$(sysctl -n hw.logicalcpu)"
QT_PREFIX="$(brew --prefix qt@5)"

rm -rf stage artifacts/dmg-root
mkdir -p stage artifacts

cmake --build build --parallel "$JOBS"
cmake --install build

APP_PATH=""
for candidate in stage/*.app; do
  if [[ -d "$candidate" ]]; then
    APP_PATH="$candidate"
    break
  fi
done
if [[ -z "$APP_PATH" ]]; then
  echo "ERROR: cmake --install did not produce an application bundle in stage/." >&2
  find stage -print >&2 || true
  exit 1
fi

# Homebrew's sdl2-compat 2.32.70 loads SDL3 by filename at runtime. On
# macOS the current loader first tries libSDL3.dylib, not the versioned
# Homebrew cellar name libSDL3.0.dylib. Keep one signed real dylib in
# Contents/Frameworks and expose it from Contents/MacOS as well, covering both
# @loader_path (when libSDL2 lives in Frameworks) and @executable_path lookups.
SDL3_PREFIX="$(brew --prefix sdl3)"
SDL3_SOURCE="$SDL3_PREFIX/lib/libSDL3.dylib"
SDL3_FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"
SDL3_MACOS_DIR="$APP_PATH/Contents/MacOS"
SDL3_BUNDLED="$SDL3_FRAMEWORKS_DIR/libSDL3.dylib"

if [[ ! -e "$SDL3_SOURCE" ]]; then
  echo "ERROR: SDL3 runtime was not found: $SDL3_SOURCE" >&2
  find "$SDL3_PREFIX/lib" -maxdepth 1 -print 2>/dev/null | sort >&2 || true
  exit 1
fi
mkdir -p "$SDL3_FRAMEWORKS_DIR" "$SDL3_MACOS_DIR"
rm -f \
  "$SDL3_FRAMEWORKS_DIR/libSDL3.dylib" \
  "$SDL3_FRAMEWORKS_DIR/libSDL3.0.dylib" \
  "$SDL3_FRAMEWORKS_DIR/libSDL3-3.0.dylib" \
  "$SDL3_FRAMEWORKS_DIR/libSDL3-3.0.0.dylib" \
  "$SDL3_MACOS_DIR/libSDL3.dylib" \
  "$SDL3_MACOS_DIR/libSDL3.0.dylib" \
  "$SDL3_MACOS_DIR/libSDL3-3.0.dylib" \
  "$SDL3_MACOS_DIR/libSDL3-3.0.0.dylib"
/bin/cp -L "$SDL3_SOURCE" "$SDL3_BUNDLED"
chmod 755 "$SDL3_BUNDLED"
if ! file -L "$SDL3_BUNDLED" | grep -q 'Mach-O'; then
  echo "ERROR: copied SDL3 runtime is not a Mach-O dylib: $SDL3_BUNDLED" >&2
  exit 1
fi
if ! lipo -archs "$SDL3_BUNDLED" | tr ' ' '\n' | grep -qx arm64; then
  echo "ERROR: copied SDL3 runtime does not contain arm64: $SDL3_BUNDLED" >&2
  lipo -info "$SDL3_BUNDLED" >&2 || true
  exit 1
fi
install_name_tool -id '@rpath/libSDL3.dylib' "$SDL3_BUNDLED"

# Current sdl2-compat uses libSDL3.dylib. Keep versioned aliases for older
# compatibility layers and third-party plugins without shipping duplicate code.
ln -s libSDL3.dylib "$SDL3_FRAMEWORKS_DIR/libSDL3.0.dylib"
ln -s libSDL3.dylib "$SDL3_FRAMEWORKS_DIR/libSDL3-3.0.dylib"
ln -s libSDL3.dylib "$SDL3_FRAMEWORKS_DIR/libSDL3-3.0.0.dylib"
ln -s ../Frameworks/libSDL3.dylib "$SDL3_MACOS_DIR/libSDL3.dylib"
ln -s ../Frameworks/libSDL3.dylib "$SDL3_MACOS_DIR/libSDL3.0.dylib"
ln -s ../Frameworks/libSDL3.dylib "$SDL3_MACOS_DIR/libSDL3-3.0.dylib"
ln -s ../Frameworks/libSDL3.dylib "$SDL3_MACOS_DIR/libSDL3-3.0.0.dylib"

echo "ArenaMP macOS: bundled SDL3 runtime at Contents/Frameworks/libSDL3.dylib"

# The framework under Contents/lib is only a staging copy used by
# BundleUtilities to resolve its original @executable_path/../lib install name.
# fixup_bundle must copy and rewrite the final framework into Contents/Frameworks,
# after which the staging copy is removed to avoid shipping Homebrew references.
if [[ -e "$APP_PATH/Contents/lib/MyGUIEngine.framework" ]]; then
  echo "ERROR: temporary MyGUI staging framework remains after cmake --install." >&2
  exit 1
fi
if ! find "$APP_PATH/Contents/Frameworks/MyGUIEngine.framework" -type f \
    -path '*/Versions/*/MyGUIEngine' -print -quit | grep -q .; then
  echo "ERROR: fixed MyGUI framework is missing from Contents/Frameworks." >&2
  exit 1
fi
if [[ ! -f "$APP_PATH/Contents/Frameworks/libSDL3.dylib" ]]; then
  echo "ERROR: SDL3 runtime required by sdl2-compat is missing from Contents/Frameworks." >&2
  exit 1
fi
if [[ ! -L "$APP_PATH/Contents/MacOS/libSDL3.dylib" ]]; then
  echo "ERROR: SDL3 executable-path alias is missing from Contents/MacOS." >&2
  exit 1
fi

# CMake BundleUtilities handles non-Qt dependencies. macdeployqt then performs
# the Qt-specific deployment pass before the final signature is created. The
# bundle contains more than one Qt executable, so explicitly include the
# launcher and browser in the deployment scan.
MACDEPLOYQT_ARGS=(
  "$APP_PATH"
  -always-overwrite
  -verbose=2
  "-executable=$APP_PATH/Contents/MacOS/tes3mp-browser"
  "-executable=$APP_PATH/Contents/MacOS/openmw-wizard"
)
"$QT_PREFIX/bin/macdeployqt" "${MACDEPLOYQT_ARGS[@]}"

# Homebrew libraries may retain absolute LC_RPATH entries even after their
# dependency install names have been rewritten into the bundle.
bash CI/macos-arm64/sanitize-bundle.sh "$APP_PATH"

# Exercise the exact SDL2 compatibility library packaged in the app. This
# catches filename/path mistakes that static Mach-O dependency scans cannot see
# because sdl2-compat opens SDL3 with dlopen() rather than LC_LOAD_DYLIB.
#
# Do not compile this probe against the Homebrew SDL headers. A direct clang
# invocation can otherwise miss the macOS SDK sysroot and fail in SDL_platform.h
# while including AvailabilityMacros.h before the runtime test even starts.
# Resolve the small SDL2 ABI surface with dlopen()/dlsym() instead and pass an
# explicit SDK root to clang.
SDL2_BUNDLED="$(find "$APP_PATH/Contents/Frameworks" -type f \
  \( -name 'libSDL2*.dylib' -o -path '*/SDL2.framework/Versions/*/SDL2' \) \
  -print -quit)"
if [[ -z "$SDL2_BUNDLED" ]]; then
  echo "ERROR: bundled SDL2 compatibility library was not found for runtime probe." >&2
  exit 1
fi
SDL_PROBE_DIR="$(mktemp -d /tmp/arenamp-sdl3-probe.XXXXXX)"
SDL_PROBE_SOURCE="$SDL_PROBE_DIR/probe.c"
SDL_PROBE_BINARY="$APP_PATH/Contents/MacOS/.arenamp-sdl3-probe"
cleanup_sdl_probe() {
  rm -rf "$SDL_PROBE_DIR"
  rm -f "$SDL_PROBE_BINARY"
}
trap cleanup_sdl_probe EXIT
cat > "$SDL_PROBE_SOURCE" <<'EOF'
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>

typedef int (*SDL_InitFn)(uint32_t flags);
typedef void (*SDL_QuitFn)(void);
typedef const char* (*SDL_GetErrorFn)(void);

static void* load_symbol(void* handle, const char* name)
{
    dlerror();
    void* symbol = dlsym(handle, name);
    const char* error = dlerror();
    if (error != NULL)
        fprintf(stderr, "ArenaMP SDL runtime probe: missing %s: %s\n", name, error);
    return symbol;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s /path/to/libSDL2.dylib\n", argv[0]);
        return 2;
    }

    void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (handle == NULL)
    {
        fprintf(stderr, "ArenaMP SDL runtime probe could not load sdl2-compat: %s\n", dlerror());
        return 1;
    }

    SDL_InitFn sdlInit = NULL;
    SDL_QuitFn sdlQuit = NULL;
    SDL_GetErrorFn sdlGetError = NULL;
    *(void**)(&sdlInit) = load_symbol(handle, "SDL_Init");
    *(void**)(&sdlQuit) = load_symbol(handle, "SDL_Quit");
    *(void**)(&sdlGetError) = load_symbol(handle, "SDL_GetError");
    if (sdlInit == NULL || sdlQuit == NULL || sdlGetError == NULL)
    {
        dlclose(handle);
        return 1;
    }

    const uint32_t SDL_INIT_TIMER_VALUE = 0x00000001u;
    if (sdlInit(SDL_INIT_TIMER_VALUE) != 0)
    {
        fprintf(stderr, "ArenaMP SDL runtime probe failed: %s\n", sdlGetError());
        dlclose(handle);
        return 1;
    }

    sdlQuit();
    dlclose(handle);
    return 0;
}
EOF
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
CLANG="$(xcrun --sdk macosx --find clang)"
[[ -d "$SDKROOT" ]] || {
  echo "ERROR: macOS SDK root was not found: $SDKROOT" >&2
  exit 1
}
"$CLANG" -arch arm64 \
  -isysroot "$SDKROOT" \
  -mmacosx-version-min="${MACOSX_DEPLOYMENT_TARGET:-15.0}" \
  "$SDL_PROBE_SOURCE" \
  -o "$SDL_PROBE_BINARY"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
  "$SDL_PROBE_BINARY" "$SDL2_BUNDLED"
cleanup_sdl_probe
trap - EXIT
echo "ArenaMP macOS: bundled sdl2-compat successfully loaded bundled SDL3"

# Start the packaged dedicated server from the same Resources working directory
# used by the launcher. The smoke test catches invalid macOS path conversion
# (for example ./server:scripts:serverCore.lua) before the app is signed or the
# DMG is published. A healthy server remains running until the test terminates it.
SERVER_SMOKE_LOG="$(pwd)/artifacts/macos-server-smoke.txt"
SERVER_SMOKE_BINARY="$(cd "$APP_PATH/Contents/MacOS" && pwd)/tes3mp-server"
SERVER_SMOKE_RESOURCES="$(cd "$APP_PATH/Contents/Resources" && pwd)"
: > "$SERVER_SMOKE_LOG"
(
  cd "$SERVER_SMOKE_RESOURCES"
  "$SERVER_SMOKE_BINARY" --no-logs > "$SERVER_SMOKE_LOG" 2>&1
) &
SERVER_SMOKE_PID=$!
sleep 3
if ! kill -0 "$SERVER_SMOKE_PID" 2>/dev/null; then
  wait "$SERVER_SMOKE_PID" || true
  cat "$SERVER_SMOKE_LOG" >&2 || true
  echo "ERROR: packaged tes3mp-server exited during startup smoke test." >&2
  exit 1
fi
kill -TERM "$SERVER_SMOKE_PID" 2>/dev/null || true
wait "$SERVER_SMOKE_PID" 2>/dev/null || true
if grep -Fq 'Script not found:' "$SERVER_SMOKE_LOG" \
    || grep -Fq './server:scripts:' "$SERVER_SMOKE_LOG"; then
  cat "$SERVER_SMOKE_LOG" >&2
  echo "ERROR: packaged tes3mp-server generated an invalid Lua script path." >&2
  exit 1
fi
grep -Fq 'TES3MP dedicated server' "$SERVER_SMOKE_LOG" || {
  cat "$SERVER_SMOKE_LOG" >&2 || true
  echo "ERROR: packaged tes3mp-server did not reach normal initialization." >&2
  exit 1
}
echo "ArenaMP macOS: packaged dedicated server loaded serverCore.lua successfully"

bash CI/macos-arm64/validate-bundle.sh "$APP_PATH" unsigned artifacts/macos-bundle-pre-sign.txt

# Sign after every fixup/deployment mutation and before creating the DMG.
# Ad-hoc signing is suitable for CI artifacts; notarization can be added later
# when an Apple Developer identity is available. Sign leaf code explicitly
# instead of using --deep because osgPlugins-* is a plain plugin directory, not
# a macOS bundle.
bash CI/macos-arm64/sign-bundle.sh "$APP_PATH"
bash CI/macos-arm64/validate-bundle.sh "$APP_PATH" signed artifacts/macos-bundle-validation.txt

DMG_ROOT="artifacts/dmg-root"
rm -rf "$DMG_ROOT"
mkdir -p "$DMG_ROOT"
ditto "$APP_PATH" "$DMG_ROOT/ArenaMP.app"
ln -s /Applications "$DMG_ROOT/Applications"

ref="${GITHUB_REF_NAME:-manual}"
ref="${ref//\//-}"
DMG_PATH="artifacts/ArenaMP-macOS-arm64-${ref}.dmg"
rm -f "$DMG_PATH"
hdiutil create -volname "ArenaMP" -srcfolder "$DMG_ROOT" -ov -format UDZO -fs HFS+ "$DMG_PATH"
hdiutil verify "$DMG_PATH"

# Mount the final image once and verify the exact application users receive.
MOUNT_DIR="$(mktemp -d /tmp/arenamp-dmg.XXXXXX)"
cleanup() {
  hdiutil detach "$MOUNT_DIR" -force >/dev/null 2>&1 || true
  rmdir "$MOUNT_DIR" >/dev/null 2>&1 || true
}
trap cleanup EXIT
hdiutil attach "$DMG_PATH" -nobrowse -readonly -mountpoint "$MOUNT_DIR" >/dev/null
bash CI/macos-arm64/validate-bundle.sh "$MOUNT_DIR/ArenaMP.app" signed artifacts/macos-dmg-validation.txt
hdiutil detach "$MOUNT_DIR" >/dev/null
rmdir "$MOUNT_DIR"
trap - EXIT

rm -rf "$DMG_ROOT"
shasum -a 256 "$DMG_PATH" > "$DMG_PATH.sha256"
ccache --show-stats | tee artifacts/ccache-stats.txt || true

echo "ArenaMP macOS package created: $DMG_PATH"
