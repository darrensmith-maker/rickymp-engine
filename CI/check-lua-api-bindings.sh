#!/usr/bin/env bash
set -euo pipefail

root="${1:-.}"
lang_lua="$root/apps/openmw-mp/Script/LangLua/LangLua.cpp"
func_traits="$root/extern/LuaBridge/detail/FuncTraits.h"
types="$root/apps/openmw-mp/Script/Types.hpp"
functions_dir="$root/apps/openmw-mp/Script/Functions"

for path in "$lang_lua" "$func_traits" "$types"; do
  [[ -f "$path" ]] || { echo "ERROR: missing Lua API binding source: $path" >&2; exit 1; }
done

# The old dispatcher called every native C++ function through R(*)(...). That
# erases integer widths and the noexcept function type and is not ABI-safe on
# Apple Silicon. Regular functions must be registered directly through
# LuaBridge so it stores and calls the exact function pointer type.
grep -Fq 'void registerLuaFunctions(luabridge::Namespace& tes3mp)' "$lang_lua"
grep -Fq 'tes3mp.addFunction("StartTimer", ScriptFunctions::StartTimer);' "$lang_lua"
grep -Fq '#define SCRIPT_API_ENTRY(name, function) (tes3mp.addFunction(name, function), 0)' "$lang_lua"

if grep -Eq 'LuaFunctionDispatcher|FunctionEllipsis<ReturnType>|functionAddresses\[FunctionIndex\]' "$lang_lua"; then
  echo 'ERROR: ABI-unsafe type-erased Lua native dispatcher is still present.' >&2
  exit 1
fi

# C++17 makes noexcept part of a function pointer type. The bundled LuaBridge
# version predates that rule, so it needs this forwarding specialization for
# the many noexcept server API functions.
grep -Fq 'struct FuncTraits <R (*) (P...) noexcept, D> : FuncTraits <R (*) (P...), D>' "$func_traits"
grep -Fq 'defined(_MSVC_LANG) && _MSVC_LANG >= 201703L' "$func_traits"
grep -Fq '#define SCRIPT_API_ENTRY(name, function) {name, function}' "$types"

api_header_count=0
api_entry_count=0
while IFS= read -r header; do
  api_header_count=$((api_header_count + 1))
  count="$(grep -c 'SCRIPT_API_ENTRY(' "$header" || true)"
  api_entry_count=$((api_entry_count + count))

  if grep -Eq '^[[:space:]]*\{[[:space:]]*"[^"]+"[[:space:]]*,[[:space:]]*[A-Za-z_][A-Za-z0-9_]*::' "$header"; then
    echo "ERROR: untyped Lua API entry remains in $header" >&2
    exit 1
  fi
done < <(grep -Rl '^#define [A-Za-z_][A-Za-z0-9_]*API' "$functions_dir" --include='*.hpp' | sort)

[[ "$api_header_count" -ge 20 ]] || {
  echo "ERROR: unexpectedly few Lua API headers were validated: $api_header_count" >&2
  exit 1
}
[[ "$api_entry_count" -ge 700 ]] || {
  echo "ERROR: unexpectedly few Lua API entries were validated: $api_entry_count" >&2
  exit 1
}

echo "ArenaMP typed Lua API binding validation passed ($api_entry_count functions)"
