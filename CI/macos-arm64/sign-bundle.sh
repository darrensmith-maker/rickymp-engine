#!/usr/bin/env bash
set -euo pipefail

APP_PATH="${1:-}"
if [[ -z "$APP_PATH" || ! -d "$APP_PATH" ]]; then
  echo "ERROR: application bundle was not provided or does not exist: $APP_PATH" >&2
  exit 1
fi

MACHO_LIST="$(mktemp)"
NESTED_BUNDLES="$(mktemp)"
cleanup() {
  rm -f "$MACHO_LIST" "$NESTED_BUNDLES"
}
trap cleanup EXIT

# Sign leaf Mach-O files explicitly. Do not use codesign --deep: the OpenSceneGraph
# plugin directory is a plain data directory containing .so modules, not a macOS
# bundle, and --deep incorrectly tries to validate it as a nested bundle.
while IFS= read -r -d '' item; do
  if file "$item" | grep -q 'Mach-O'; then
    printf '%s\n' "$item" >> "$MACHO_LIST"
  fi
done < <(find "$APP_PATH" -type f -print0)

if [[ ! -s "$MACHO_LIST" ]]; then
  echo "ERROR: no Mach-O files found in application bundle: $APP_PATH" >&2
  exit 1
fi

signed_macho=0
while IFS= read -r item; do
  codesign --force --sign - --timestamp=none "$item"
  signed_macho=$((signed_macho + 1))
done < "$MACHO_LIST"

# Sign real nested bundle containers after their leaf code. Frameworks are all
# direct children of Contents/Frameworks in this package. Other recognized
# bundle types are collected separately; plain osgPlugins-* directories are
# deliberately excluded because they are not bundles.
if [[ -d "$APP_PATH/Contents/Frameworks" ]]; then
  while IFS= read -r -d '' bundle; do
    printf '%s\n' "$bundle" >> "$NESTED_BUNDLES"
  done < <(find "$APP_PATH/Contents/Frameworks" -mindepth 1 -maxdepth 1 -type d -name '*.framework' -print0)
fi
while IFS= read -r -d '' bundle; do
  printf '%s\n' "$bundle" >> "$NESTED_BUNDLES"
done < <(find "$APP_PATH/Contents" -mindepth 1 -type d \( -name '*.app' -o -name '*.xpc' -o -name '*.appex' -o -name '*.bundle' \) -print0)

signed_bundles=0
if [[ -s "$NESTED_BUNDLES" ]]; then
  # Reverse lexical order is sufficient for the current non-nested framework
  # layout and keeps any deeper named bundle before a parent with the same prefix.
  LC_ALL=C sort -r -u "$NESTED_BUNDLES" | while IFS= read -r bundle; do
    codesign --force --sign - --timestamp=none "$bundle"
  done
  signed_bundles="$(LC_ALL=C sort -u "$NESTED_BUNDLES" | wc -l | tr -d ' ')"
fi

# Seal the outer application last. Verification is intentionally shallow here;
# validate-bundle.sh explicitly verifies every leaf Mach-O and every real nested
# bundle, avoiding codesign's incorrect traversal into osgPlugins-* directories.
codesign --force --sign - --timestamp=none "$APP_PATH"
codesign --verify --strict --verbose=2 "$APP_PATH"

echo "ArenaMP macOS: signed $signed_macho Mach-O files, $signed_bundles nested bundles, and the application bundle"
