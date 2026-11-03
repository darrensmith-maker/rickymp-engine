#!/usr/bin/env bash
set -euo pipefail

CACHE="${1:?usage: check-ffmpeg-cache.sh <CMakeCache.txt> <ffmpeg prefix>}"
FFMPEG_PREFIX="${2:?usage: check-ffmpeg-cache.sh <CMakeCache.txt> <ffmpeg prefix>}"

[[ -f "$CACHE" ]] || { echo "ERROR: CMake cache does not exist: $CACHE" >&2; exit 1; }
[[ -d "$FFMPEG_PREFIX" ]] || { echo "ERROR: FFmpeg prefix does not exist: $FFMPEG_PREFIX" >&2; exit 1; }

components=(AVCODEC AVFORMAT AVUTIL SWRESAMPLE SWSCALE)
failed=0

for component in "${components[@]}"; do
  key="FFmpeg_${component}_LIBRARY:FILEPATH="
  line="$(grep -F "$key" "$CACHE" | tail -n 1 || true)"
  if [[ -z "$line" ]]; then
    echo "ERROR: missing CMake cache entry: $key" >&2
    failed=1
    continue
  fi

  value="${line#*=}"
  case "$value" in
    "$FFMPEG_PREFIX"/lib/lib*.dylib|"$FFMPEG_PREFIX"/lib/lib*.a)
      ;;
    *-NOTFOUND|NOTFOUND)
      echo "ERROR: FFmpeg ${component} library was not found: $value" >&2
      failed=1
      ;;
    *)
      echo "ERROR: FFmpeg ${component} came from outside ffmpeg@4: $value" >&2
      failed=1
      ;;
  esac
done

# Only inspect real include-directory cache entries. Ignore pkg-config INTERNAL
# metadata such as *_PKGCONF_LIBRARIES and *_PKGCONF_STATIC_LIBRARIES: those are
# dependency names, not selected library paths.
while IFS= read -r line; do
  [[ -n "$line" ]] || continue
  value="${line#*=}"
  case "$value" in
    "$FFMPEG_PREFIX"/include|"$FFMPEG_PREFIX"/include/*)
      ;;
    *-NOTFOUND|NOTFOUND)
      echo "ERROR: unresolved FFmpeg include directory: $line" >&2
      failed=1
      ;;
    *)
      echo "ERROR: FFmpeg headers came from outside ffmpeg@4: $line" >&2
      failed=1
      ;;
  esac
done < <(grep -E '^FFmpeg_[A-Z0-9_]+_INCLUDE_DIR:PATH=' "$CACHE" || true)

if [[ "$failed" -ne 0 ]]; then
  echo 'Relevant FFmpeg cache entries:' >&2
  grep -E '^FFmpeg_[A-Z0-9_]+_(LIBRARY:FILEPATH|INCLUDE_DIR:PATH)=' "$CACHE" >&2 || true
  exit 1
fi

printf 'ArenaMP macOS: FFmpeg cache uses only %s\n' "$FFMPEG_PREFIX"
