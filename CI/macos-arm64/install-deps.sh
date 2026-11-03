#!/usr/bin/env bash
set -euo pipefail

export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_INSTALL_CLEANUP=1
export HOMEBREW_CACHE="${HOMEBREW_CACHE:-$HOME/Library/Caches/Homebrew}"

if [[ "$(uname -m)" != "arm64" ]]; then
  echo "ERROR: ArenaMP macOS dependencies must be built on a native arm64 runner." >&2
  exit 1
fi

echo "ArenaMP macOS: using bundled Bullet with double precision (Homebrew Bullet is intentionally disabled)"
echo "ArenaMP macOS: pinning FFmpeg 4 because this OpenMW/TES3MP branch uses legacy FFmpeg APIs"

brew install cmake ninja ccache qt@5 boost sdl2-compat sdl3 openal-soft ffmpeg@4 \
  open-scene-graph webp lz4 unshield pkgconf luajit freetype

if [[ -n "${GITHUB_PATH:-}" ]]; then
  echo "$(brew --prefix qt@5)/bin" >> "$GITHUB_PATH"
  echo "$(brew --prefix ffmpeg@4)/bin" >> "$GITHUB_PATH"
fi

require_arm64_file() {
  local path="$1"
  local label="$2"
  if [[ ! -e "$path" ]]; then
    echo "ERROR: $label was not found: $path" >&2
    exit 1
  fi
  if file -L "$path" | grep -q 'Mach-O' && ! lipo -archs "$path" 2>/dev/null | tr ' ' '\n' | grep -qx arm64; then
    echo "ERROR: $label is not arm64: $path" >&2
    file -L "$path" >&2 || true
    lipo -info "$path" >&2 || true
    exit 1
  fi
}

QT_PREFIX="$(brew --prefix qt@5)"
FFMPEG_PREFIX="$(brew --prefix ffmpeg@4)"
OPENAL_PREFIX="$(brew --prefix openal-soft)"
OSG_PREFIX="$(brew --prefix open-scene-graph)"
SDL_PREFIX="$(brew --prefix sdl2-compat)"
SDL3_PREFIX="$(brew --prefix sdl3)"
WEBP_PREFIX="$(brew --prefix webp)"

require_arm64_file "$QT_PREFIX/lib/QtCore.framework/Versions/5/QtCore" "QtCore"
require_arm64_file "$FFMPEG_PREFIX/lib/libavcodec.dylib" "FFmpeg libavcodec"
require_arm64_file "$FFMPEG_PREFIX/lib/libavformat.dylib" "FFmpeg libavformat"
require_arm64_file "$OPENAL_PREFIX/lib/libopenal.dylib" "OpenAL Soft"
require_arm64_file "$SDL_PREFIX/lib/libSDL2.dylib" "SDL2 compatibility library"
SDL3_LIBRARY="$SDL3_PREFIX/lib/libSDL3.dylib"
if [[ ! -f "$SDL3_LIBRARY" ]]; then
  echo "ERROR: SDL3 runtime library required by sdl2-compat was not found: $SDL3_LIBRARY" >&2
  find "$SDL3_PREFIX/lib" -maxdepth 1 -print 2>/dev/null | sort >&2 || true
  exit 1
fi
require_arm64_file "$SDL3_LIBRARY" "SDL3 runtime library"
WEBP_LIBRARY="$(find "$WEBP_PREFIX/lib" -maxdepth 1 -type f -name 'libwebp*.dylib' -print -quit)"
SHARPYUV_LIBRARY="$(find "$WEBP_PREFIX/lib" -maxdepth 1 -type f -name 'libsharpyuv*.dylib' -print -quit)"
require_arm64_file "$WEBP_LIBRARY" "WebP runtime library"
require_arm64_file "$SHARPYUV_LIBRARY" "SharpYUV runtime library"

OSG_PLUGIN_DIR=""
if [[ -x "$OSG_PREFIX/bin/osgversion" ]]; then
  OSG_VERSION="$($OSG_PREFIX/bin/osgversion --version-number 2>/dev/null || true)"
  if [[ -n "$OSG_VERSION" && -d "$OSG_PREFIX/lib/osgPlugins-$OSG_VERSION" ]]; then
    OSG_PLUGIN_DIR="$OSG_PREFIX/lib/osgPlugins-$OSG_VERSION"
  fi
fi
if [[ -z "$OSG_PLUGIN_DIR" ]]; then
  for candidate in "$OSG_PREFIX"/lib/osgPlugins-*; do
    [[ -d "$candidate" ]] || continue
    OSG_PLUGIN_DIR="$candidate"
  done
fi
OSG_IMAGEIO_PLUGIN=""
for candidate in "$OSG_PLUGIN_DIR/osgdb_imageio.so" "$OSG_PLUGIN_DIR/osgdb_imageio.dylib"; do
  if [[ -f "$candidate" ]]; then
    OSG_IMAGEIO_PLUGIN="$candidate"
    break
  fi
done
if [[ -z "$OSG_IMAGEIO_PLUGIN" ]]; then
  echo "ERROR: Homebrew OpenSceneGraph does not contain the required ImageIO plugin." >&2
  find "$OSG_PREFIX/lib" -print 2>/dev/null | sort >&2 || true
  exit 1
fi
require_arm64_file "$OSG_IMAGEIO_PLUGIN" "OpenSceneGraph ImageIO plugin"

# TinyXML is built from the source already bundled in extern/oics.
MYGUI_ROOT="${RUNNER_TEMP:-$HOME}/MyGUI"
MYGUI_INSTALL="$MYGUI_ROOT/install"

mygui_install_ready() {
  [[ -f "$MYGUI_INSTALL/include/MYGUI/MyGUI.h" ]] && \
  { [[ -d "$MYGUI_INSTALL/lib/MyGUIEngine.framework" ]] || \
    [[ -f "$MYGUI_INSTALL/lib/libMyGUIEngine.dylib" ]] || \
    [[ -f "$MYGUI_INSTALL/lib/libMyGUIEngine.a" ]]; }
}

# MyGUI 3.4.2 installs headers, a library/framework and MYGUI.pc, but no
# MyGUIConfig.cmake. Validate the artifacts consumed by FindMyGUI.cmake.
if ! mygui_install_ready; then
  if [[ ! -d "$MYGUI_ROOT/.git" ]]; then
    rm -rf "$MYGUI_ROOT"
    git clone --depth 1 --branch MyGUI3.4.2 https://github.com/MyGUI/mygui.git "$MYGUI_ROOT"
  else
    git -C "$MYGUI_ROOT" fetch --depth 1 origin tag MyGUI3.4.2
    git -C "$MYGUI_ROOT" reset --hard MyGUI3.4.2
  fi

  rm -rf "$MYGUI_ROOT/build" "$MYGUI_INSTALL"
  cmake -S "$MYGUI_ROOT" -B "$MYGUI_ROOT/build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
    -DCMAKE_INSTALL_PREFIX="$MYGUI_INSTALL" \
    -DMYGUI_RENDERSYSTEM=4 \
    -DMYGUI_DISABLE_PLUGINS=ON \
    -DMYGUI_BUILD_DEMOS=OFF \
    -DMYGUI_BUILD_TOOLS=OFF \
    -DMYGUI_BUILD_PLUGINS=OFF \
    -DMYGUI_BUILD_UNITTESTS=OFF \
    -DMYGUI_BUILD_WRAPPER=OFF
  cmake --build "$MYGUI_ROOT/build" --parallel "$(sysctl -n hw.logicalcpu)"
  cmake --install "$MYGUI_ROOT/build"
fi

if ! mygui_install_ready; then
  echo "ERROR: MyGUI build completed without usable headers or library artifacts." >&2
  find "$MYGUI_INSTALL" -print 2>/dev/null | sort >&2 || true
  exit 1
fi

if [[ -d "$MYGUI_INSTALL/lib/MyGUIEngine.framework" ]]; then
  require_arm64_file "$MYGUI_INSTALL/lib/MyGUIEngine.framework/Versions/3.4.2/MyGUIEngine" "MyGUIEngine framework"
elif [[ -f "$MYGUI_INSTALL/lib/libMyGUIEngine.dylib" ]]; then
  require_arm64_file "$MYGUI_INSTALL/lib/libMyGUIEngine.dylib" "MyGUIEngine"
fi

echo "ArenaMP macOS: dependency architecture checks passed"
echo "ArenaMP macOS: MyGUI installation verified at $MYGUI_INSTALL"
brew list --versions cmake ninja ccache qt@5 boost sdl2-compat sdl3 openal-soft ffmpeg@4 open-scene-graph webp lz4 luajit freetype
