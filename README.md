# ArenaMP

ArenaMP is a next-generation fork of TES3MP 0.8.1 based on OpenMW 0.47.0.

The repository contains the multiplayer client, dedicated server, server browser, launcher, updated HUD and GUI settings, graphical fixes, performance improvements, experimental tactical NPC behavior, and integration with EncoreMP 0.92 content.

> A legal copy of **The Elder Scrolls III: Morrowind** is required. Bethesda game assets are not included in this repository.

## Project Goals

ArenaMP aims to modernize the TES3MP/OpenMW experience while preserving compatibility with the TES3MP 0.8.1 networking protocol.

The main areas of development are:

- improved multiplayer usability;
- a cleaner and more configurable HUD;
- better graphical quality and performance;
- expanded engine-level settings;
- improved NPC combat behavior;
- more reliable Linux, Windows, and macOS builds;
- continued compatibility with existing TES3MP servers and scripts.

## Main Features

- TES3MP multiplayer client and dedicated server;
- integrated server browser;
- OpenMW launcher and configuration tools;
- extended HUD, GUI, Quick Loot, and camera settings;
- improved water rendering and caustics;
- HDR lighting and tone-mapping controls;
- configurable dynamic shadows;
- groundcover and grass rendering fixes;
- performance and scene-management optimizations;
- optional engine-level tactical NPC combat;
- EncoreMP 0.92 content integration;
- compatibility with the TES3MP 0.8.1 network protocol.

## Applications Produced by the Build

| File | Description |
|---|---|
| `tes3mp` | ArenaMP/TES3MP game client |
| `tes3mp-server` | Dedicated multiplayer server |
| `tes3mp-browser` | Server browser |
| `openmw-launcher` | Game, mod, plugin, and data path configuration |
| `openmw-wizard` | Settings import wizard, when enabled during compilation |

## ArenaMP Interface Improvements

### Quick Loot

ArenaMP includes a compact Quick Loot overlay for containers.

Features include:

- delayed display to prevent accidental activation while moving or fighting;
- the overlay appears only after the player stops and keeps looking at a container;
- configurable appearance delay;
- automatic hiding for empty containers;
- compact item rows with icons, names, and stack counts;
- cyclic list scrolling;
- mouse wheel and keyboard navigation;
- optional `W` and `S` navigation while the player is stationary;
- regular movement is not interrupted if the player is already walking or running;
- direct access to the standard container inventory;
- full-stack pickup support;
- automatic resizing for long item names;
- protection against camera zoom while the list is active;
- an option to disable Quick Loot completely.

Default delay setting:

```ini
[GUI]
quick loot stationary delay = 0.65
```

### Target Information HUD

The target information panel was redesigned to replace the original floating name label.

It can display:

- actor or creature name;
- level in the `1 lvl` format;
- current and maximum health;
- a compact health bar above the target;
- a separate minimal combat health bar near the player's HUD.

Additional behavior:

- dead actors no longer display a health bar;
- the target panel is enabled by default;
- the option is saved immediately;
- the setting is preserved after reconnecting or restarting the client;
- the compact panel can be disabled from the GUI settings.

### Container Improvements

Container windows now include:

- current weight and maximum capacity;
- a visual capacity bar;
- a live item search field;
- automatic list filtering while typing;
- reset of the search field when another container is opened;
- a cleaner bottom control area;
- removal of the `Dispose of Corpse` button from the default layout.

### ESC Menu and Chat

The in-game ESC menu includes ArenaMP branding:

```text
ArenaMP (fork TES3MP 0.8.1)
```

The menu chat history:

- appears in the same position as the normal multiplayer chat;
- keeps the same size after resolution or GUI scaling changes;
- supports scrolling and text selection;
- no longer uses the harsh inverted-color selection effect;
- uses a softer and more readable selection style.

## Graphics and Rendering

### HDR Lighting

ArenaMP provides runtime controls for:

- HDR lighting;
- tone mapping;
- exposure-related shader behavior;
- compatibility with the OpenMW shader pipeline.

Most supported settings can be changed without restarting the game.

### Dynamic Shadows

The shadow system includes expanded runtime controls.

Shadow categories can be enabled progressively:

```text
Disabled → Actor → NPC → Object → Terrain → Indoor
```

Each position includes the previous categories.

For example:

- `Actor` enables the player shadow;
- `NPC` additionally enables NPC and creature shadows;
- `Object` additionally enables object and static shadows;
- `Terrain` additionally enables terrain shadows;
- `Indoor` enables the full set, including interior shadows.

Shadow map quality can be adjusted through eight levels:

```text
64 → 128 → 256 → 512 → 1024 → 2048 → 4096 → 8192
```

Higher values improve shadow detail but increase GPU memory use and rendering cost.

Recommended scene-bound calculation:

```ini
[Shadows]
compute scene bounds = bounds
```

Possible modes:

- `bounds` — recommended balance between performance and accuracy;
- `primitives` — more accurate but more CPU-intensive;
- `none` — mainly useful for debugging.

### Groundcover and Grass

Groundcover rendering was corrected to work reliably with configured grass plugins.

Fixes include:

- corrected groundcover shader varying data;
- stable world-position transfer between vertex and fragment shaders;
- compatibility with caustics;
- preserved grass wind animation;
- preserved grass interaction and trampling behavior;
- correct use of `groundcover=` entries from `openmw.cfg`.

### Water and Caustics

ArenaMP contains fixes for water caustics disappearing after entering and leaving interiors.

The rendering state is rebuilt correctly after cell transitions, while preserving:

- animated caustics;
- water shader compatibility;
- interior/exterior transitions;
- groundcover integration.

### HUD Scaling

Several HUD elements were corrected to remain aligned after:

- resolution changes;
- GUI scaling changes;
- fullscreen/window mode changes;
- aspect-ratio changes.

## Performance Optimizations

ArenaMP includes multiple optimizations intended to reduce CPU and GPU overhead.

### UI Optimization

- removed unnecessary continuously running interface animations;
- reduced redundant widget updates;
- Quick Loot is not rebuilt while hidden;
- dynamic HUD elements update only when their displayed state changes;
- window dimensions are recalculated only when required;
- chat history is updated only when new content is received;
- minimized keyboard and mouse focus changes between HUD overlays.

### Shader Optimization

- optional HDR and shadow features can be disabled individually;
- shadow caster categories can be limited;
- shadow map resolution can be reduced on weaker GPUs;
- simplified shader paths remain available;
- groundcover and caustics share already calculated world-space data where possible;
- unnecessary shader state rebuilding is reduced.

### Scene Optimization

- distant rendering quality can be adjusted independently;
- simplified distant-object rendering is available;
- shadow distance and fade start can be configured;
- terrain, objects, NPCs, and interiors can be excluded from shadow casting;
- scene bounds can be calculated from aggregate bounds rather than every primitive.

### Multiplayer AI Settings

ArenaMP keeps NPC combat movement in the engine and synchronizes the selected
mechanics from the bundled server `config.lua`. This avoids a high-frequency Lua
loop issuing repeated AI packets for every actor. Pursuit distance, door pursuit,
maximum pursuers, tactical combat and related actor rules can be changed from the
launcher and are enforced identically for every connected client.


## Occlusion Culling

ArenaMP supports scene visibility optimization through occlusion culling concepts already used by the OpenMW/OpenSceneGraph rendering architecture and extends the project with settings intended to reduce unnecessary rendering.

Occlusion culling prevents geometry from being fully rendered when it is hidden behind other geometry.

Examples:

- objects behind buildings;
- actors behind terrain;
- interior rooms behind walls;
- distant static objects hidden by large scene structures.

### Benefits

When effective, occlusion culling can reduce:

- the number of submitted draw calls;
- vertex processing;
- fragment shading;
- shadow caster processing;
- overdraw in dense interiors and cities.

### Limitations

Occlusion culling is most useful when:

- the scene contains large opaque blockers;
- the player is inside buildings or narrow city streets;
- many objects are hidden behind terrain or structures.

It provides less benefit in:

- wide open landscapes;
- scenes with many transparent objects;
- areas where almost everything is visible;
- very small or rapidly moving objects.

### Recommended Usage

Occlusion culling should be used together with:

- distance culling;
- object paging;
- terrain paging;
- shadow distance limits;
- appropriate scene bounds;
- simplified distant-object rendering.

It should not replace normal distance-based visibility checks.

Incorrectly aggressive culling may cause objects to appear too late or disappear briefly, so conservative defaults are recommended.

## Experimental Tactical NPC Combat

ArenaMP includes experimental improvements for NPC combat behavior.

The engine-side combat controller can support:

- strafing;
- circling around the target;
- short retreats;
- moving jumps and evasive jumps;
- sneak approaches;
- flanking;
- pressure attacks;
- basic stuck detection;
- jumping out of blocked positions;
- pursuit distance limits.

The goal is to keep the normal combat package active while performing movement maneuvers, instead of constantly replacing `StartCombat` with temporary travel packages.

### Bundled ArenaMP Server Core

Windows, GNU/Linux and macOS packages contain the same server scripts from the
repository `server/` directory. Packaging no longer downloads a different
CoreScripts revision. Windows includes the native Lua DLL modules from the
provided core; Linux and macOS omit Windows DLLs and use the included portable
Lua JSON implementation when a native CJSON module is unavailable.

The launcher edits the active `server/scripts/config.lua` and exposes ArenaMP
controls for:

- tactical combat;
- weapon sheathe delay;
- pursuit through teleport doors;
- guaranteed and maximum pursuit distances;
- minimum pursuit chance and maximum number of pursuers;
- same-cell pursuit leash;
- follower aggression, collision avoidance and giving way;
- following over water;
- ArenaMP weapon, skill-book, enchantment and XP rules.

On first server start the launcher creates an active copy under `userdata/server`.
When the bundled core version changes, system scripts are updated while world
JSON data, `scripts/custom`, `customScripts.lua` and existing scalar `config.lua`
values are preserved. A backup of the previous config is retained beside it.
See `server/ARENAMP_CONFIG.md` for the complete setting list.

### Pursuit Through Doors

Door pursuit is intentionally limited.

NPCs are selected according to:

- distance from the player;
- actor type;
- combat state;
- configurable random chance;
- maximum number of pursuers.

Nearby enemies have a higher chance to follow.

Enemies farther away have a progressively lower chance, preventing every NPC in the area from transitioning through the same door.

Example settings:

```ini
[Game]
combat pursuit through doors = true
combat pursuit guaranteed distance = 200
combat pursuit door max distance = 800
combat pursuit minimum chance = 0.10
combat pursuit max actors = 3
```

Engine-level tactical AI and pursuit are experimental and should be tested with several connected clients before use on a production server.

## EncoreMP ESP Files

Ready-to-use EncoreMP plugins are included in the repository root:

- `EncoreMPV092.ESP` — main EncoreMP plugin;
- `EncoreMPV092newcontent.ESP` — additional content;
- `EncoreMPV092Spells1Base.ESP` — spells for the base game;
- `EncoreMPV092Spells2TRcore.ESP` — base game and Tamriel Rebuilt core;
- `EncoreMPV092Spells3TRall.ESP` — extended Tamriel Rebuilt setup.

Use only **one** `EncoreMPV092Spells*` plugin at a time.

The plugin order on the client must exactly match the server plugin order.

## Obtaining the Source Code

```bash
git clone --recurse-submodules https://github.com/MrZer0x0/TES3MP.git
cd TES3MP
```

For an existing clone:

```bash
git submodule sync --recursive
git submodule update --init --recursive
```

CrabNet/RakNet must be located at:

```text
extern/raknet
```

The current CI includes:

```text
CI/ensure-bundled-deps.sh
```

If the directory is missing or incomplete, the script attempts to retrieve the `TES3MP/CrabNet` dependency before CMake configuration.

## Linux x86_64 Build

The verified CI configuration uses Ubuntu 22.04 and Ninja.

```bash
CI/ensure-bundled-deps.sh
bash CI/linux/install-deps.sh
bash CI/linux/configure.sh
bash CI/linux/build-package.sh
```

The resulting archive is placed in `artifacts/`:

```text
ArenaMP-Linux-x86_64-<branch-or-tag>.tar.gz
```

## macOS arm64 Build

The verified configuration uses:

- an Apple Silicon runner;
- Homebrew dependencies;
- macOS 12 as the minimum deployment target.

```bash
CI/ensure-bundled-deps.sh
bash CI/macos-arm64/install-deps.sh
bash CI/macos-arm64/configure.sh
bash CI/macos-arm64/build-package.sh
```

The macOS build uses the bundled TinyXML version from `extern/oics`, so an external TinyXML repository is no longer cloned.

MyGUI is built into:

```text
${RUNNER_TEMP}/MyGUI/install
```

and cached by GitHub Actions.

The resulting `.dmg` or fallback `.tar.gz` archive is placed in `artifacts/`.

## Windows Build

The existing MSVC build scripts and `appveyor.yml` are retained.

Recommended environment:

- Visual Studio 2022 x64;
- Windows SDK;
- CMake;
- Ninja;
- required OpenMW/TES3MP dependencies.

After changing headers or CMake options, delete the old build directory before configuring again:

```text
MSVC2022_64
```

This prevents stale generated project files and cached dependency paths.

## GitHub Actions

The workflow is located at:

```text
.github/workflows/release-builds.yml
```

Any previous broken workflow should be replaced by this file, otherwise GitHub may continue executing outdated build steps.

The workflow contains two independent jobs:

- `Linux x86_64` on `ubuntu-22.04`;
- `macOS arm64` on `macos-26-arm64`.

It runs on:

- pushes to `Main`;
- pull requests;
- tags matching `v*`;
- manual `workflow_dispatch` runs.

Finished packages are uploaded as GitHub Actions artifacts.

## Main CMake Options

| Option | Linux CI | macOS CI | Description |
|---|---:|---:|---|
| `BUILD_OPENCS` | `OFF` | `OFF` | Do not include OpenMW-CS in CI release packages |
| `BUILD_WIZARD` | `OFF` | `OFF` | Do not build the import wizard in CI |
| `BUILD_UNITTESTS` | `OFF` | `OFF` | Do not build unit tests in release packages |
| `USE_SYSTEM_TINYXML` | `ON` | `OFF` | Linux uses the system package; macOS uses bundled TinyXML |
| `OPENMW_USE_SYSTEM_MYGUI` | `ON` | `ON` | Use an installed or separately built MyGUI |
| `OPENMW_USE_SYSTEM_OSG` | `ON` | `ON` | Use the system OpenSceneGraph |
| `OPENMW_USE_SYSTEM_BULLET` | `ON` | `ON` | Use the system Bullet library |

## Client Configuration

1. Start `openmw-launcher`.
2. Add the path to the installed Morrowind data files.
3. Enable `Morrowind.esm`, `Tribunal.esm`, and `Bloodmoon.esm` in the correct order.
4. Add the ESP files required by the server.
5. Verify that the client plugin order exactly matches the server.

Main configuration files:

```text
files/settings-default.cfg
files/tes3mp/tes3mp-client-default.cfg
files/tes3mp/tes3mp-server-default.cfg
```

User-specific configuration should preferably be stored in the normal OpenMW/TES3MP user data directory instead of modifying the packaged defaults.

## Common Build Errors

### `The submodules were not downloaded`

This usually means that `extern/raknet` is missing or contains only an empty directory.

Run:

```bash
CI/ensure-bundled-deps.sh
```

Then remove the build directory and configure again.

### `Remote branch 2.6.2 not found`

This is caused by an outdated macOS script that attempts to clone a non-existent TinyXML branch.

The current macOS dependency script no longer performs this clone and uses bundled TinyXML instead.

### macOS Cannot Find OpenAL, Qt, or LuaJIT

Remove the build directory, verify the installed Homebrew packages, and run:

```bash
CI/macos-arm64/install-deps.sh
CI/macos-arm64/configure.sh
```

### CPack Does Not Produce a `.dmg`

`CI/macos-arm64/build-package.sh` no longer fails the entire job only because the DragNDrop generator failed.

If DMG generation is unavailable, the script creates a fallback `.tar.gz` package from the staged installation.

### `use of undefined type 'MWMechanics::Movement'`

Ensure that the source file using `MWMechanics::Movement` includes:

```cpp
#include "../mwmechanics/movement.hpp"
```

A forward declaration from `world.hpp` is not sufficient when accessing the structure fields.

## Repository Structure

```text
apps/        applications and entry points
components/  shared engine subsystems
files/       resources and default configuration
extern/      bundled third-party libraries
CI/          dependency, configuration, and packaging scripts
cmake/       CMake modules
.github/     GitHub Actions workflows
server/      dedicated server scripts and configuration
```

## What to Include in an Issue

Please include:

- operating system and architecture;
- full build or runtime log;
- workflow and job name, or the exact local command;
- branch, tag, or commit hash;
- `build/CMakeCache.txt`, if generated;
- client and server plugin order for multiplayer issues;
- number of connected clients for synchronization issues;
- steps required to reproduce the problem.

## Current Status

The following features should be considered experimental until extensively tested:

- engine-level tactical combat;
- cross-door NPC pursuit;
- runtime shadow-category rebuilding;
- very large shadow maps such as `8192`;
- multiplayer synchronization of complex NPC movement maneuvers.

Testing with multiple simultaneous clients is strongly recommended before deploying these features on a public server.

## License

See `LICENSE` for licensing information.

Contributors are listed in `AUTHORS.md`.

Project history and notable changes are listed in `CHANGELOG.md`.
