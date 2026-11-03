#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
SLIDING="$ROOT_DIR/extern/raknet/Source/CCRakNetSlidingWindow.cpp"
UDT="$ROOT_DIR/extern/raknet/Source/CCRakNetUDT.cpp"

for source in "$SLIDING" "$UDT"; do
  test -f "$source"
  grep -q 'UNSET_CC_TIME = std::numeric_limits<CCTimeType>::max()' "$source"
  if grep -Fq '(CCTimeType) UNSET_TIME_US' "$source"; then
    echo "Unsafe negative floating-point to unsigned CCTimeType conversion remains in $source" >&2
    exit 1
  fi
done

assembly="$(mktemp -t arenamp-raknet-ack.XXXXXX.s)"
cleanup() { rm -f "$assembly"; }
trap cleanup EXIT

clang++ -std=c++17 -O2 -S \
  -I"$ROOT_DIR/extern/raknet/include/raknet" \
  -I"$ROOT_DIR/extern/raknet/Source" \
  "$SLIDING" -o "$assembly"

function_assembly="$(awk '
  /ShouldSendACKs/ { capture=1 }
  capture { print }
  capture && /End function/ { exit }
' "$assembly")"

if [[ -z "$function_assembly" ]]; then
  echo 'Could not locate CCRakNetSlidingWindow::ShouldSendACKs in generated ARM64 assembly.' >&2
  exit 1
fi

if grep -Eq '(^|[[:space:]])brk([[:space:]]|$)' <<<"$function_assembly"; then
  echo 'Apple Clang optimized RakNet ShouldSendACKs into a BRK trap.' >&2
  echo "$function_assembly" >&2
  exit 1
fi

echo 'RakNet ACK unset-time sentinel validation passed.'
