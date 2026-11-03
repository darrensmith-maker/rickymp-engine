#!/usr/bin/env bash
set -euo pipefail

APP_PATH="${1:?usage: sanitize-bundle.sh <app bundle>}"
[[ -d "$APP_PATH" ]] || { echo "ERROR: bundle does not exist: $APP_PATH" >&2; exit 1; }

FORBIDDEN_RPATH_REGEX='^(/opt/homebrew|/usr/local|/Users/|/private/var/folders|.*/runner/work|.*/runner/_work)'
changed=0

while IFS= read -r -d '' item; do
  file -L "$item" | grep -q 'Mach-O' || continue

  while IFS= read -r rpath; do
    [[ -n "$rpath" ]] || continue
    if [[ "$rpath" =~ $FORBIDDEN_RPATH_REGEX ]]; then
      echo "Removing build-machine LC_RPATH from ${item#$APP_PATH/}: $rpath"
      install_name_tool -delete_rpath "$rpath" "$item"
      changed=$((changed + 1))
    fi
  done < <(
    otool -l "$item" | awk '
      $1 == "cmd" && $2 == "LC_RPATH" { in_rpath = 1; next }
      in_rpath && $1 == "path" { print $2; in_rpath = 0 }
    '
  )
done < <(find "$APP_PATH" -type f -print0)

echo "ArenaMP macOS: removed $changed build-machine LC_RPATH entr$( [[ $changed -eq 1 ]] && echo y || echo ies )."
