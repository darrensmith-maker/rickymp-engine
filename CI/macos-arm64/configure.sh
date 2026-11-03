#!/usr/bin/env bash
set -euo pipefail

rm -rf build stage artifacts/dmg-root
mkdir -p build stage artifacts

QT_PREFIX="$(brew --prefix qt@5)"
FFMPEG_PREFIX="$(brew --prefix ffmpeg@4)"
OPENAL_PREFIX="$(brew --prefix openal-soft)"
OSG_PREFIX="$(brew --prefix open-scene-graph)"
SDL_PREFIX="$(brew --prefix sdl2-compat)"
SDL3_PREFIX="$(brew --prefix sdl3)"
LZ4_PREFIX="$(brew --prefix lz4)"
LUAJIT_PREFIX="$(brew --prefix luajit)"
BOOST_PREFIX="$(brew --prefix boost)"
WEBP_PREFIX="$(brew --prefix webp)"
MYGUI_PREFIX="${RUNNER_TEMP:-$HOME}/MyGUI/install"
MYGUI_INCLUDE_DIR="$MYGUI_PREFIX/include/MYGUI"
CRABNET_PREFIX="${RUNNER_TEMP:-$HOME}/CrabNet"

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
if [[ -z "$OSG_PLUGIN_DIR" || ! -d "$OSG_PLUGIN_DIR" ]]; then
  echo "ERROR: OpenSceneGraph plugin directory was not found under $OSG_PREFIX/lib" >&2
  find "$OSG_PREFIX/lib" -print 2>/dev/null | sort >&2 || true
  exit 1
fi

OSG_IMAGEIO_PLUGIN=""
for candidate in "$OSG_PLUGIN_DIR/osgdb_imageio.so" "$OSG_PLUGIN_DIR/osgdb_imageio.dylib"; do
  if [[ -f "$candidate" ]]; then
    OSG_IMAGEIO_PLUGIN="$candidate"
    break
  fi
done
if [[ -z "$OSG_IMAGEIO_PLUGIN" ]]; then
  echo "ERROR: Homebrew OpenSceneGraph ImageIO plugin was not found under $OSG_PLUGIN_DIR" >&2
  find "$OSG_PLUGIN_DIR" -type f -print 2>/dev/null | sort >&2 || true
  exit 1
fi

if [[ ! -f "$MYGUI_INCLUDE_DIR/MyGUI.h" ]]; then
  echo "ERROR: MyGUI headers were not found under $MYGUI_INCLUDE_DIR" >&2
  exit 1
fi

# MyGUI 3.4.2 does not install MyGUIConfig.cmake. Feed the repository's
# FindMyGUI.cmake module the installed header and library paths directly.
if [[ -d "$MYGUI_PREFIX/lib/MyGUIEngine.framework" ]]; then
  MYGUI_LIBRARY="$MYGUI_PREFIX/lib/MyGUIEngine.framework"
elif [[ -f "$MYGUI_PREFIX/lib/libMyGUIEngine.dylib" ]]; then
  MYGUI_LIBRARY="$MYGUI_PREFIX/lib/libMyGUIEngine.dylib"
elif [[ -f "$MYGUI_PREFIX/lib/libMyGUIEngine.a" ]]; then
  MYGUI_LIBRARY="$MYGUI_PREFIX/lib/libMyGUIEngine.a"
else
  echo "ERROR: MyGUIEngine library was not found under $MYGUI_PREFIX/lib" >&2
  find "$MYGUI_PREFIX" -print 2>/dev/null | sort >&2 || true
  exit 1
fi

OPENAL_LIBRARY="$OPENAL_PREFIX/lib/libopenal.dylib"
OPENAL_INCLUDE_DIR="$OPENAL_PREFIX/include/AL"
if [[ ! -f "$OPENAL_LIBRARY" || ! -f "$OPENAL_INCLUDE_DIR/al.h" ]]; then
  echo "ERROR: Homebrew OpenAL Soft headers or library are missing." >&2
  find "$OPENAL_PREFIX" -print 2>/dev/null | sort >&2 || true
  exit 1
fi

# Homebrew sdl2-compat implements the SDL2 ABI on top of SDL3. The SDL3
# runtime is therefore a required transitive dependency of every packaged
# ArenaMP executable, even though the source code still includes SDL2 headers.
SDL3_LIBRARY="$SDL3_PREFIX/lib/libSDL3.dylib"
if [[ ! -f "$SDL3_LIBRARY" ]]; then
  echo "ERROR: SDL3 runtime library required by sdl2-compat was not found: $SDL3_LIBRARY" >&2
  find "$SDL3_PREFIX/lib" -maxdepth 1 -print 2>/dev/null | sort >&2 || true
  exit 1
fi

WEBP_LIBRARY=""
SHARPYUV_LIBRARY=""
for candidate in "$WEBP_PREFIX"/lib/libwebp*.dylib; do
  [[ -f "$candidate" ]] || continue
  WEBP_LIBRARY="$candidate"
  break
done
for candidate in "$WEBP_PREFIX"/lib/libsharpyuv*.dylib; do
  [[ -f "$candidate" ]] || continue
  SHARPYUV_LIBRARY="$candidate"
  break
done
if [[ -z "$WEBP_LIBRARY" || -z "$SHARPYUV_LIBRARY" ]]; then
  echo "ERROR: Homebrew WebP runtime libraries required by osgdb_imageio are missing." >&2
  find "$WEBP_PREFIX/lib" -maxdepth 1 -type f -print 2>/dev/null | sort >&2 || true
  exit 1
fi

PREFIX_PATH="${QT_PREFIX};${MYGUI_PREFIX};${BOOST_PREFIX};${OSG_PREFIX};${FFMPEG_PREFIX};${SDL_PREFIX};${SDL3_PREFIX};${OPENAL_PREFIX};${LZ4_PREFIX};${LUAJIT_PREFIX};${WEBP_PREFIX}"
BUNDLE_SEARCH_DIRS="${MYGUI_PREFIX}/lib;${QT_PREFIX}/lib;${BOOST_PREFIX}/lib;${OSG_PREFIX}/lib;${OSG_PLUGIN_DIR};${FFMPEG_PREFIX}/lib;${SDL_PREFIX}/lib;${SDL3_PREFIX}/lib;${OPENAL_PREFIX}/lib;${LZ4_PREFIX}/lib;${LUAJIT_PREFIX}/lib;${WEBP_PREFIX}/lib"

# Keep keg-only and versioned Homebrew packages ahead of preinstalled SDK
# copies. This prevents accidental use of system OpenAL or FFmpeg 8.
export MYGUI_HOME="$MYGUI_PREFIX"
export FFMPEG_HOME="$FFMPEG_PREFIX"
export SDL2DIR="$SDL_PREFIX"
export PATH="$QT_PREFIX/bin:$FFMPEG_PREFIX/bin:$PATH"
export PKG_CONFIG_PATH="$MYGUI_PREFIX/lib/pkgconfig:$FFMPEG_PREFIX/lib/pkgconfig:$SDL_PREFIX/lib/pkgconfig:$SDL3_PREFIX/lib/pkgconfig:$QT_PREFIX/lib/pkgconfig:$OPENAL_PREFIX/lib/pkgconfig:$LUAJIT_PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export LDFLAGS="-L$MYGUI_PREFIX/lib -L$FFMPEG_PREFIX/lib -L$SDL_PREFIX/lib -L$SDL3_PREFIX/lib -L$QT_PREFIX/lib -L$OPENAL_PREFIX/lib -L$LUAJIT_PREFIX/lib ${LDFLAGS:-}"
export CPPFLAGS="-I$MYGUI_INCLUDE_DIR -I$FFMPEG_PREFIX/include -I$SDL_PREFIX/include/SDL2 -I$QT_PREFIX/include -I$OPENAL_PREFIX/include -I$LUAJIT_PREFIX/include ${CPPFLAGS:-}"

cat <<INFO
ArenaMP macOS configuration:
  Qt 5:                  $QT_PREFIX
  FFmpeg 4:              $FFMPEG_PREFIX
  SDL2 compatibility:   $SDL_PREFIX
  SDL3 runtime:          $SDL3_LIBRARY
  OpenAL Soft:           $OPENAL_PREFIX
  MyGUI headers:         $MYGUI_INCLUDE_DIR
  MyGUI library:         $MYGUI_LIBRARY
  OSG plugins:           $OSG_PLUGIN_DIR
  OSG ImageIO plugin:    $OSG_IMAGEIO_PLUGIN
  WebP runtime:           $WEBP_LIBRARY
  SharpYUV runtime:       $SHARPYUV_LIBRARY
  Deployment target:     ${MACOSX_DEPLOYMENT_TARGET:-15.0}
INFO

cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}" \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="${MACOSX_DEPLOYMENT_TARGET:-15.0}" \
  -DCMAKE_FIND_FRAMEWORK=LAST \
  -DCMAKE_FIND_APPBUNDLE=NEVER \
  -DCMAKE_MACOSX_RPATH=ON \
  -DCMAKE_PREFIX_PATH="$PREFIX_PATH" \
  -DMyGUI_INCLUDE_DIR:PATH="$MYGUI_INCLUDE_DIR" \
  -DMyGUI_LIBRARY:FILEPATH="$MYGUI_LIBRARY" \
  -DMYGUI_STATIC:BOOL=OFF \
  -DOPENAL_INCLUDE_DIR:PATH="$OPENAL_INCLUDE_DIR" \
  -DOPENAL_LIBRARY:FILEPATH="$OPENAL_LIBRARY" \
  -DCMAKE_INSTALL_PREFIX="${GITHUB_WORKSPACE:-$PWD}/stage" \
  -DCMAKE_C_COMPILER_LAUNCHER=ccache \
  -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
  -DOPENMW_OSX_DEPLOYMENT=ON \
  -DOPENMW_OSX_USE_IMAGEIO_PLUGIN:BOOL=ON \
  -DOPENMW_OSX_BUNDLE_SEARCH_DIRS:STRING="$BUNDLE_SEARCH_DIRS" \
  -DOSGPlugins_LIB_DIR:PATH="$OSG_PLUGIN_DIR" \
  -DOSGPlugins_DONT_FIND_DEPENDENCIES:BOOL=ON \
  -DBUILD_OPENCS=OFF \
  -DBUILD_WIZARD=ON \
  -DBUILD_UNITTESTS=OFF \
  -DBUILD_BENCHMARKS=OFF \
  -DBUILD_DOCS=OFF \
  -DUSE_SYSTEM_TINYXML=OFF \
  -DOPENMW_USE_SYSTEM_MYGUI=ON \
  -DOPENMW_USE_SYSTEM_OSG=ON \
  -DOPENMW_USE_SYSTEM_BULLET:BOOL=OFF \
  -DBULLET_STATIC:BOOL=ON \
  -DUSE_DOUBLE_PRECISION:BOOL=ON \
  -DRakNet_INCLUDE_DIR="$CRABNET_PREFIX/include/raknet" \
  -DRakNet_LIBRARY_RELEASE="$CRABNET_PREFIX/build/lib/libRakNetLibStatic.a" \
  -DRakNet_LIBRARY_DEBUG="$CRABNET_PREFIX/build/lib/libRakNetLibStatic.a" \
  -DLuaJit_INCLUDE_DIR="$LUAJIT_PREFIX/include/luajit-2.1" \
  -DLuaJit_LIBRARY="$LUAJIT_PREFIX/lib/libluajit-5.1.dylib"

CACHE=build/CMakeCache.txt

require_cache_exact() {
  local expected="$1"
  local message="$2"
  grep -Fqx "$expected" "$CACHE" || {
    echo "ERROR: $message" >&2
    grep -F "${expected%%:*}" "$CACHE" >&2 || true
    exit 1
  }
}

require_cache_exact "MyGUI_INCLUDE_DIR:PATH=$MYGUI_INCLUDE_DIR" "CMake did not use the expected MyGUI include directory."
require_cache_exact "MyGUI_LIBRARY:FILEPATH=$MYGUI_LIBRARY" "CMake did not use the expected MyGUI library."
require_cache_exact "OPENAL_INCLUDE_DIR:PATH=$OPENAL_INCLUDE_DIR" "CMake did not use Homebrew OpenAL Soft headers."
require_cache_exact "OPENAL_LIBRARY:FILEPATH=$OPENAL_LIBRARY" "CMake did not use Homebrew OpenAL Soft."
require_cache_exact 'OPENMW_USE_SYSTEM_BULLET:BOOL=OFF' 'macOS configuration selected a system Bullet build.'
require_cache_exact 'USE_DOUBLE_PRECISION:BOOL=ON' 'bundled Bullet is not configured for double precision.'
require_cache_exact 'OPENMW_OSX_USE_IMAGEIO_PLUGIN:BOOL=ON' 'OpenSceneGraph ImageIO plugin mode is disabled.'
require_cache_exact 'BUILD_WIZARD:BOOL=ON' 'macOS configuration disabled openmw-wizard.'
require_cache_exact 'OSGPlugins_DONT_FIND_DEPENDENCIES:BOOL=ON' 'legacy JPEG/PNG plugin dependencies were not disabled.'

if grep -Eq 'Boost_SYSTEM_LIBRARY[^=]*=(Boost_SYSTEM_LIBRARY[^-]|.*NOTFOUND)' "$CACHE"; then
  echo 'ERROR: configuration still requires a separate Boost.System library.' >&2
  exit 1
fi

if ! grep -Fq "$OSG_IMAGEIO_PLUGIN" "$CACHE"; then
  echo 'ERROR: CMake did not select the expected osgdb_imageio plugin.' >&2
  grep -i 'imageio\|OSGPlugins' "$CACHE" >&2 || true
  exit 1
fi

# Validate only the real FILEPATH/INCLUDE_DIR cache entries. The CMake
# module also writes *_PKGCONF_*:INTERNAL metadata containing bare dependency
# names; those values must not be mistaken for libraries selected outside the
# requested Homebrew prefix.
bash CI/macos-arm64/check-ffmpeg-cache.sh "$CACHE" "$FFMPEG_PREFIX"

if grep -E '(^|:)OSGPlugins_osgdb_(jpeg|png)_LIBRARY.*NOTFOUND' "$CACHE"; then
  echo 'ERROR: obsolete osgdb_jpeg/osgdb_png requirements are still active.' >&2
  exit 1
fi

# Do not reject every *-NOTFOUND cache entry. Single-configuration Release
# builds legitimately leave optional Debug variants unresolved (for example
# MyGUI_Debug_LIBRARY), while all required packages have already passed their
# find_package(... REQUIRED) checks. The exact checks above validate the
# libraries that are actually selected for this Release bundle.

echo 'ArenaMP macOS: dependency selection and bundle configuration verified.'
