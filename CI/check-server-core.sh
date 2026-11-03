#!/usr/bin/env bash
set -euo pipefail

root="${1:-.}"
config="$root/server/scripts/config.lua"

required=(
  "$root/server/scripts/serverCore.lua"
  "$config"
  "$root/server/data/.gitignore"
  "$root/server/data/banlist.json"
  "$root/server/data/requiredDataFiles.json"
  "$root/server/lib/lua/dkjson.lua"
  "$root/server/ARENAMP_CORE_VERSION.txt"
  "$root/server/ARENAMP_CONFIG.md"
)
for path in "${required[@]}"; do
  [[ -f "$path" ]] || { echo "ERROR: missing ArenaMP server core file: $path" >&2; exit 1; }
done

required_data_dirs=(cell custom map player recordstore world)
for directory in "${required_data_dirs[@]}"; do
  path="$root/server/data/$directory"
  [[ -d "$path" ]] || { echo "ERROR: missing ArenaMP server data directory: $path" >&2; exit 1; }
  [[ -f "$path/.gitkeep" ]] || { echo "ERROR: missing tracked ArenaMP directory marker: $path/.gitkeep" >&2; exit 1; }
done
[[ -d "$root/server/scripts/custom" ]] || { echo "ERROR: missing custom scripts directory" >&2; exit 1; }
[[ -f "$root/server/scripts/custom/.gitkeep" ]] || { echo "ERROR: missing custom scripts directory marker" >&2; exit 1; }

# The repository has a generic `data` ignore rule. Guard against accidentally
# dropping the server seed files from Git again. --no-index checks ignore rules
# even when a file is already tracked.
if command -v git >/dev/null 2>&1 && git -C "$root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  tracked_server_files=(
    server/data/.gitignore
    server/data/banlist.json
    server/data/requiredDataFiles.json
    server/data/cell/.gitkeep
    server/data/custom/.gitkeep
    server/data/map/.gitkeep
    server/data/player/.gitkeep
    server/data/recordstore/.gitkeep
    server/data/world/.gitkeep
    server/scripts/custom/.gitkeep
  )
  for path in "${tracked_server_files[@]}"; do
    if git -C "$root" check-ignore --no-index -q "$path"; then
      echo "ERROR: ArenaMP server core file is ignored by Git: $path" >&2
      exit 1
    fi
    if ! git -C "$root" ls-files --error-unmatch "$path" >/dev/null 2>&1; then
      echo "ERROR: ArenaMP server core file is not tracked by Git: $path" >&2
      echo "Run: git add .gitignore server/data server/scripts/custom/.gitkeep" >&2
      exit 1
    fi
  done
fi

grep -q '"Morrowind.esm"' "$root/server/data/requiredDataFiles.json"
grep -q '"Tribunal.esm"' "$root/server/data/requiredDataFiles.json"
grep -q '"Bloodmoon.esm"' "$root/server/data/requiredDataFiles.json"
grep -q '"playerNames"' "$root/server/data/banlist.json"
grep -q '"ipAddresses"' "$root/server/data/banlist.json"

if command -v luajit >/dev/null 2>&1; then
  tmp="$(mktemp)"
  trap 'rm -f "$tmp"' EXIT
  luajit -b "$config" "$tmp"
elif command -v luac >/dev/null 2>&1; then
  luac -p "$config"
fi

python_bin="$(command -v python3 || command -v python || true)"
[[ -n "$python_bin" ]] || { echo "ERROR: Python is required to validate the server core" >&2; exit 1; }
"$python_bin" - "$config" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
assignments = {
    "arenaTacticalCombat": "true",
    "arenaCombatWeaponSheatheDelay": "2.0",
    "arenaCombatPursuitThroughDoors": "true",
    "arenaCombatPursuitGuaranteedDistance": "200",
    "arenaCombatPursuitDoorMaxDistance": "800",
    "arenaCombatPursuitMinimumChance": "0.10",
    "arenaCombatPursuitMaxActors": "3",
    "arenaCombatPursuitMaxDistance": "4000",
    "arenaFollowersAttackOnSight": "true",
    "arenaNpcAvoidCollisions": "true",
    "arenaNpcGiveWay": "true",
    "arenaAllowActorsFollowOverWater": "true",
    "arenaActorsProcessingRange": "4000",
    "arenaCanLootDuringDeathAnimation": "true",
    "arenaWeaponSheathing": "true",
    "arenaShieldSheathing": "false",
    "arenaGraphicHerbalism": "true",
    "arenaGlobalXpMultiplier": "1.0",
}
for key, value in assignments.items():
    if not re.search(rf"^config\.{re.escape(key)}\s*=\s*{re.escape(value)}\s*$", text, re.M):
        raise SystemExit(f"missing or unexpected default for config.{key}")

mapped = [
    "tactical combat",
    "combat weapon sheathe delay",
    "combat pursuit through doors",
    "combat pursuit guaranteed distance",
    "combat pursuit door max distance",
    "combat pursuit minimum chance",
    "combat pursuit max actors",
    "combat pursuit max distance",
    "followers attack on sight",
    "NPCs avoid collisions",
    "NPCs give way",
    "allow actors to follow over water surface",
    "actors processing range",
    "can loot during death animation",
    "weapon sheathing",
    "shield sheathing",
    "graphic herbalism",
    "long blades use agility for damage scaling",
    "two handed weapons receive an accuracy penalty",
    "staves receive accuracy bonus instead of two handed penalty",
    "skill books have level limit",
    "use new constant effect difficulty logic",
    "global XP gain multiplier",
]
for setting in mapped:
    if f'setGameSetting("{setting}"' not in text:
        raise SystemExit(f"ArenaMP game setting is not mapped: {setting}")

for token in ("clampNumber", "clampInteger", "tes3mp.SendSettings"):
    if token == "tes3mp.SendSettings":
        continue
    if token not in text:
        raise SystemExit(f"missing config validation helper: {token}")
PY

# Release packaging must use this repository's bundled core, never fetch a
# different CoreScripts revision while assembling an artifact.
if grep -R -E 'git clone .*CoreScripts|github.com/TES3MP/CoreScripts' \
    "$root/CI/linux/package-portable.sh" "$root/CI/make_package.ubuntu.sh"; then
  echo "ERROR: release packaging still downloads a separate CoreScripts tree" >&2
  exit 1
fi

grep -q 'OpenMW_SOURCE_DIR}/server/' "$root/CMakeLists.txt"
grep -q 'OpenMW_SOURCE_DIR}/server/' "$root/cmake/CMakeLists.txt"
grep -q 'PATTERN "\*.dll" EXCLUDE' "$root/CMakeLists.txt"
grep -q 'PATTERN "\*.dll" EXCLUDE' "$root/cmake/CMakeLists.txt"
grep -Fq 'Contents/Resources/server' "$root/CMakeLists.txt"
grep -Fq 'Contents/Resources/server' "$root/cmake/CMakeLists.txt"
if grep -Fq 'DESTINATION "${APP_BUNDLE_NAME}/Contents/MacOS/server"' \
    "$root/CMakeLists.txt" "$root/cmake/CMakeLists.txt"; then
  echo "ERROR: macOS server resources are installed inside Contents/MacOS" >&2
  exit 1
fi
# The launcher runs the bundled server tree directly. On macOS the tree is
# inside Contents/Resources; on Windows and Linux it is next to the binaries.
grep -q 'serverRuntimeBasePath' "$root/apps/launcher/serverdialog.cpp"
grep -Fq 'QStringLiteral("../Resources")' "$root/apps/launcher/serverdialog.cpp"
grep -Fq 'serverBaseDir.filePath(QStringLiteral("server"))' "$root/apps/launcher/serverdialog.cpp"
grep -Fq 'mProcess->setWorkingDirectory(workingDirectory)' "$root/apps/launcher/serverdialog.cpp"

# Launcher integration: userdata stores only the editable cfg. The server core,
# scripts, libraries and mutable server data must never be copied to or run
# from userdata/server.
grep -q 'preparePortableServer' "$root/apps/launcher/serverdialog.cpp"
grep -q 'runtimeDataBasePath' "$root/apps/launcher/serverdialog.cpp"
grep -Fq 'QDir(userDataPath).filePath(QStringLiteral("tes3mp-server.cfg"))'     "$root/apps/launcher/serverdialog.cpp"
grep -Fq 'QStringLiteral("./server")' "$root/apps/launcher/serverdialog.cpp"
grep -Fq 'No files or directories are created under userdata/server.'     "$root/apps/launcher/serverdialog.cpp"
if grep -q 'mergeLuaScalarAssignments' "$root/apps/launcher/serverdialog.cpp"; then
  echo "ERROR: launcher still contains userdata/server core migration logic" >&2
  exit 1
fi
for widget in \
  arenaTacticalCombatCheckBox \
  arenaCombatPursuitThroughDoorsCheckBox \
  arenaCombatPursuitGuaranteedDistanceSpinBox \
  arenaCombatPursuitDoorMaxDistanceSpinBox \
  arenaCombatPursuitMinimumChanceDoubleSpinBox \
  arenaCombatPursuitMaxActorsSpinBox \
  arenaCombatPursuitMaxDistanceSpinBox \
  arenaActorsProcessingRangeSpinBox \
  arenaGraphicHerbalismCheckBox \
  arenaGlobalXpMultiplierDoubleSpinBox; do
  grep -q "name=\"$widget\"" "$root/files/ui/playpage.ui"
  grep -q "$widget" "$root/apps/launcher/playpage.cpp"
done

bash "$root/CI/check-lua-api-bindings.sh" "$root"

echo "ArenaMP bundled server core validation passed"
