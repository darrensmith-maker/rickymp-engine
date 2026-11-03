#!/usr/bin/env bash
set -euo pipefail

UTILS_CPP="components/openmw-mp/Utils.cpp"
[[ -f "$UTILS_CPP" ]] || {
  echo "ERROR: missing $UTILS_CPP" >&2
  exit 1
}

# Modern macOS APIs and fopen() use POSIX '/' paths. The old Carbon/HFS ':'
# conversion turns ./server/scripts/serverCore.lua into
# ./server:scripts:serverCore.lua and makes the bundled dedicated server abort.
python3 - <<'PY'
from pathlib import Path
text = Path('components/openmw-mp/Utils.cpp').read_text(encoding='utf-8')
start = text.index('std::string Utils::convertPath')
end = text.index('\n}', start) + 2
body = text[start:end]
if '__APPLE__' in body or "_SEP_ ':'" in body or "replace(str.begin(), str.end(), '/', ':')" in body:
    raise SystemExit("ERROR: legacy macOS HFS ':' path conversion remains in Utils::convertPath")
if "replace(str.begin(), str.end(), '/', '\\\\');" not in body:
    raise SystemExit("ERROR: Windows path conversion is missing from Utils::convertPath")
print('ArenaMP macOS: POSIX server path conversion validation passed')
PY
