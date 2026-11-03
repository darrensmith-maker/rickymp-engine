#!/usr/bin/env bash
set -euo pipefail

parse_loaded_dylibs() {
  # Parse only Mach-O load commands. `otool -L` also prints LC_ID_DYLIB as
  # the first entry for dylibs/frameworks. That install name identifies the
  # file itself and is not a dependency that must resolve inside the bundle.
  awk '''
    $1 == "cmd" {
      load = ($2 == "LC_LOAD_DYLIB" ||
              $2 == "LC_LOAD_WEAK_DYLIB" ||
              $2 == "LC_REEXPORT_DYLIB" ||
              $2 == "LC_LOAD_UPWARD_DYLIB" ||
              $2 == "LC_LAZY_LOAD_DYLIB")
      next
    }
    load && $1 == "name" {
      print $2
      load = 0
    }
  '''
}

if [[ "${1:-}" == "--self-test" ]]; then
  parsed="$({
    cat <<'EOF'
Load command 0
          cmd LC_ID_DYLIB
      cmdsize 72
         name @executable_path/../Frameworks/libqcocoa.dylib (offset 24)
Load command 1
          cmd LC_LOAD_DYLIB
      cmdsize 88
         name @executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore (offset 24)
Load command 2
          cmd LC_LOAD_WEAK_DYLIB
      cmdsize 56
         name /usr/lib/libobjc.A.dylib (offset 24)
EOF
  } | parse_loaded_dylibs)"
  expected=$'@executable_path/../Frameworks/QtCore.framework/Versions/5/QtCore\n/usr/lib/libobjc.A.dylib'
  [[ "$parsed" == "$expected" ]] || {
    printf 'ERROR: Mach-O dependency parser self-test failed.\nExpected:\n%s\nActual:\n%s\n' "$expected" "$parsed" >&2
    exit 1
  }
  echo "ArenaMP macOS: Mach-O dependency parser self-test passed"
  exit 0
fi

APP_PATH="${1:-}"
VERIFY_SIGNATURE="${2:-unsigned}"
REPORT_PATH="${3:-artifacts/macos-bundle-validation.txt}"

if [[ -z "$APP_PATH" || ! -d "$APP_PATH" ]]; then
  echo "ERROR: application bundle was not provided or does not exist: $APP_PATH" >&2
  exit 1
fi

mkdir -p "$(dirname "$REPORT_PATH")"
: > "$REPORT_PATH"

log() {
  printf '%s\n' "$*" | tee -a "$REPORT_PATH"
}

fail() {
  log "ERROR: $*"
  exit 1
}

log "ArenaMP macOS bundle validation"
log "bundle=$APP_PATH"
log "host_arch=$(uname -m)"
log "host_version=$(sw_vers -productVersion)"

PLIST="$APP_PATH/Contents/Info.plist"
[[ -f "$PLIST" ]] || fail "Info.plist is missing"
plutil -lint "$PLIST" | tee -a "$REPORT_PATH"
BUNDLE_EXECUTABLE="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$PLIST")"
[[ -n "$BUNDLE_EXECUTABLE" ]] || fail "CFBundleExecutable is empty"
[[ -x "$APP_PATH/Contents/MacOS/$BUNDLE_EXECUTABLE" ]] || fail "CFBundleExecutable does not exist: $BUNDLE_EXECUTABLE"

for required in openmw-launcher openmw-wizard tes3mp tes3mp-server tes3mp-browser; do
  [[ -x "$APP_PATH/Contents/MacOS/$required" ]] || fail "required executable is missing: $required"
done
for required_server_file in \
    Contents/Resources/server/scripts/serverCore.lua \
    Contents/Resources/server/scripts/config.lua \
    Contents/Resources/server/data/banlist.json \
    Contents/Resources/server/data/requiredDataFiles.json \
    Contents/Resources/server/ARENAMP_CORE_VERSION.txt; do
    if [[ ! -f "$APP_PATH/$required_server_file" ]]; then
        echo "ERROR: missing bundled ArenaMP server core file: $required_server_file" >&2
        exit 1
    fi
done
for required_data_dir in cell custom map player recordstore world; do
    [[ -d "$APP_PATH/Contents/Resources/server/data/$required_data_dir" ]] || \
        fail "missing bundled ArenaMP server data directory: $required_data_dir"
done
if find "$APP_PATH/Contents/Resources/server" -type f -iname '*.dll' -print -quit | grep -q .; then
    echo "ERROR: Windows-only Lua DLLs were packaged into the macOS server core." >&2
    exit 1
fi
if [[ -e "$APP_PATH/Contents/MacOS/server" ]]; then
    fail "ArenaMP server data must not be placed inside Contents/MacOS"
fi

for required in tes3mp-client-default.cfg tes3mp-server-default.cfg; do
  [[ -f "$APP_PATH/Contents/Resources/$required" ]] || fail "required configuration is missing from Resources: $required"
done

[[ -d "$APP_PATH/Contents/PlugIns" ]] || fail "Contents/PlugIns is missing"
[[ -d "$APP_PATH/Contents/PlugIns/osgPlugins" ]] \
  || fail "version-neutral OSG plugin directory is missing from the application bundle"
find "$APP_PATH/Contents/PlugIns/osgPlugins" -type f -name '*osgdb_imageio*' -print -quit | grep -q . \
  || fail "osgdb_imageio plugin is missing from the application bundle"
if find "$APP_PATH/Contents/PlugIns" -mindepth 1 -maxdepth 1 -type d -name 'osgPlugins-*.*' -print -quit | grep -q .; then
  fail "versioned OSG plugin directory with periods is not code-signing safe"
fi
find "$APP_PATH/Contents/PlugIns" -type f -path '*/platforms/*qcocoa*' -print -quit | grep -q . \
  || fail "Qt Cocoa platform plugin is missing from the application bundle"

[[ ! -e "$APP_PATH/Contents/lib/MyGUIEngine.framework" ]] \
  || fail "temporary MyGUI staging framework remains in Contents/lib"
find "$APP_PATH/Contents/Frameworks/MyGUIEngine.framework" -type f \
  -path '*/Versions/*/MyGUIEngine' -print -quit | grep -q . \
  || fail "fixed MyGUI framework is missing from Contents/Frameworks"
SDL3_BUNDLED="$APP_PATH/Contents/Frameworks/libSDL3.dylib"
[[ -f "$SDL3_BUNDLED" && ! -L "$SDL3_BUNDLED" ]] \
  || fail "SDL3 runtime required by sdl2-compat is missing or is only a symlink in Contents/Frameworks"
file -L "$SDL3_BUNDLED" | grep -q 'Mach-O' \
  || fail "bundled SDL3 runtime is not a Mach-O dylib"
lipo -archs "$SDL3_BUNDLED" | tr ' ' '\n' | grep -qx arm64 \
  || fail "bundled SDL3 runtime does not contain arm64"
SDL3_ID="$(otool -D "$SDL3_BUNDLED" 2>/dev/null | tail -n +2 | head -1)"
[[ "$SDL3_ID" == '@rpath/libSDL3.dylib' ]] \
  || fail "bundled SDL3 has an unexpected install name: $SDL3_ID"
for alias in \
    Contents/Frameworks/libSDL3.0.dylib \
    Contents/Frameworks/libSDL3-3.0.dylib \
    Contents/Frameworks/libSDL3-3.0.0.dylib \
    Contents/MacOS/libSDL3.dylib \
    Contents/MacOS/libSDL3.0.dylib \
    Contents/MacOS/libSDL3-3.0.dylib \
    Contents/MacOS/libSDL3-3.0.0.dylib; do
  [[ -L "$APP_PATH/$alias" && -f "$APP_PATH/$alias" ]] \
    || fail "SDL3 compatibility alias is missing or broken: $alias"
done

MACHO_LIST="$(mktemp)"
trap 'rm -f "$MACHO_LIST"' EXIT
while IFS= read -r -d '' item; do
  if file "$item" | grep -q 'Mach-O'; then
    printf '%s\n' "$item" >> "$MACHO_LIST"
  fi
done < <(find "$APP_PATH" -type f -print0)

[[ -s "$MACHO_LIST" ]] || fail "bundle contains no Mach-O files"
log "Mach-O files: $(wc -l < "$MACHO_LIST" | tr -d ' ')"

while IFS= read -r item; do
  archs="$(lipo -archs "$item" 2>/dev/null || true)"
  log "ARCH $archs :: ${item#$APP_PATH/}"
  tr ' ' '\n' <<< "$archs" | grep -qx arm64 || fail "non-arm64 Mach-O file: $item ($archs)"
done < "$MACHO_LIST"

FORBIDDEN_REGEX='/opt/homebrew|/usr/local|/Users/runner|/private/var/folders|/runner/work|/runner/_work'
while IFS= read -r item; do
  log "DEPS ${item#$APP_PATH/}"
  deps="$(otool -L "$item" 2>&1)"
  printf '%s\n' "$deps" | tee -a "$REPORT_PATH"

  # Use LC_LOAD_* commands for validation instead of every line from
  # `otool -L`. This excludes LC_ID_DYLIB (the file's own install name),
  # while retaining normal, weak, re-exported and upward dependencies.
  load_commands="$(otool -l "$item" 2>&1)"
  loaded_deps="$(printf '%s\n' "$load_commands" | parse_loaded_dylibs)"
  if printf '%s\n' "$loaded_deps" | grep -E "$FORBIDDEN_REGEX"; then
    fail "unbundled build-machine dependency remains in $item"
  fi

  while IFS= read -r dependency; do
    [[ -n "$dependency" ]] || continue
    case "$dependency" in
      /System/Library/*|/usr/lib/*|/Library/Apple/System/Library/*)
        ;;
      /*)
        fail "external absolute dependency remains in $item: $dependency"
        ;;
      @loader_path/*)
        candidate="$(dirname "$item")/${dependency#@loader_path/}"
        [[ -e "$candidate" ]] || fail "unresolved @loader_path dependency in $item: $dependency"
        ;;
      @executable_path/*)
        candidate="$APP_PATH/Contents/MacOS/${dependency#@executable_path/}"
        [[ -e "$candidate" ]] || fail "unresolved @executable_path dependency in $item: $dependency"
        ;;
      @rpath/*)
        relative="${dependency#@rpath/}"
        if [[ ! -e "$APP_PATH/Contents/Frameworks/$relative"            && ! -e "$APP_PATH/Contents/MacOS/$relative"            && ! -e "$(dirname "$item")/$relative" ]]; then
          fail "unresolved @rpath dependency in $item: $dependency"
        fi
        ;;
      *)
        fail "unexpected dependency path in $item: $dependency"
        ;;
    esac
  done < <(printf '%s\n' "$loaded_deps")

  # LC_RPATH entries are separate from LC_LOAD_* dependencies. A binary can
  # therefore have clean dependency names while retaining a build-machine
  # Homebrew/temp search path.
  if printf '%s\n' "$load_commands" | grep -E "$FORBIDDEN_REGEX"; then
    printf '%s\n' "$load_commands" | grep -B2 -A2 -E "$FORBIDDEN_REGEX" | tee -a "$REPORT_PATH" || true
    fail "build-machine LC_RPATH remains in $item"
  fi
done < "$MACHO_LIST"

if [[ "$VERIFY_SIGNATURE" == "signed" ]]; then
  # Verify every signed code object explicitly first, then perform a recursive
  # verification of the complete application. The OSG directory is intentionally
  # version-neutral (no periods), so codesign no longer mistakes it for a bundle.
  while IFS= read -r item; do
    log "SIGNATURE ${item#$APP_PATH/}"
    codesign --verify --strict --verbose=2 "$item" 2>&1 | tee -a "$REPORT_PATH"
  done < "$MACHO_LIST"

  if [[ -d "$APP_PATH/Contents/Frameworks" ]]; then
    while IFS= read -r -d '' bundle; do
      log "BUNDLE SIGNATURE ${bundle#$APP_PATH/}"
      codesign --verify --strict --verbose=2 "$bundle" 2>&1 | tee -a "$REPORT_PATH"
    done < <(find "$APP_PATH/Contents/Frameworks" -mindepth 1 -maxdepth 1 -type d -name '*.framework' -print0)
  fi
  while IFS= read -r -d '' bundle; do
    log "BUNDLE SIGNATURE ${bundle#$APP_PATH/}"
    codesign --verify --strict --verbose=2 "$bundle" 2>&1 | tee -a "$REPORT_PATH"
  done < <(find "$APP_PATH/Contents" -mindepth 1 -type d \( -name '*.app' -o -name '*.xpc' -o -name '*.appex' -o -name '*.bundle' \) -print0)

  codesign --verify --strict --verbose=2 "$APP_PATH" 2>&1 | tee -a "$REPORT_PATH"
  codesign --verify --deep --strict --verbose=2 "$APP_PATH" 2>&1 | tee -a "$REPORT_PATH"
  codesign -dv --verbose=4 "$APP_PATH" 2>&1 | tee -a "$REPORT_PATH"
fi

log "ArenaMP macOS bundle validation passed"
