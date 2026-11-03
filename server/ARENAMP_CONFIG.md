# ArenaMP server mechanics

The bundled CoreScripts are the authoritative server core for the Windows,
GNU/Linux and macOS builds. The launcher edits `scripts/config.lua`; the server
then sends the mapped OpenMW `[Game]` settings to every connected player.

## NPC combat and pursuit

- `arenaTacticalCombat` enables ArenaMP tactical combat behaviour.
- `arenaCombatWeaponSheatheDelay` controls the post-combat sheathe delay.
- `arenaCombatPursuitThroughDoors` enables pursuit through teleport doors.
- `arenaCombatPursuitGuaranteedDistance` is the distance where door pursuit is guaranteed.
- `arenaCombatPursuitDoorMaxDistance` is the maximum distance considered for door pursuit.
- `arenaCombatPursuitMinimumChance` is the pursuit chance at the maximum distance.
- `arenaCombatPursuitMaxActors` limits the number of NPCs crossing one door transition.
- `arenaCombatPursuitMaxDistance` is the same-cell pursuit leash; `0` means unlimited.

## Other ArenaMP mechanics

The same section controls follower aggression, collision avoidance, giving way,
following over water, actor processing range, looting during death animations,
weapon/shield sheathing, graphic herbalism, long-blade Agility scaling,
two-handed accuracy, staff accuracy, skill-book level limits, constant-effect
difficulty and the global XP multiplier.

Values are clamped to launcher-supported ranges when the server starts. The
server-side values override corresponding local client settings while connected.

## Friendly fire

`friendlyFireMode` controls player-versus-player harmful effects and accepts
three canonical values:

- `disabled` - players cannot damage or apply harmful magic effects to other players;
- `enabled` - unrestricted player-versus-player damage;
- `group` - damage is allowed against other players, but blocked between allies
  created with `/invite` and `/join`.

The default is `group`. Weapon attacks, hand-to-hand, arrows, on-strike
enchantments, touch/target/area spells, reflected effects and lasting harmful
effects use the same rule. Beneficial magic is not blocked.

### Lua interface

CoreScripts expose both native `tes3mp` functions and the higher-level
`friendlyFire` module:

```lua
local mode = friendlyFire.GetMode()
local grouped = friendlyFire.ArePlayersGrouped(firstPid, secondPid)
local canDamage = friendlyFire.CanDamage(attackerPid, targetPid)

-- Change the live rule and resend settings to connected players.
friendlyFire.SetMode("disabled")
```

Native equivalents are:

```lua
tes3mp.SetFriendlyFireMode("group")
tes3mp.GetFriendlyFireMode()
tes3mp.ArePlayersAllied(firstPid, secondPid)
tes3mp.IsFriendlyFireAllowed(attackerPid, targetPid)
```

Custom scripted damage should call `friendlyFire.CanDamage()` before changing
a player's health or applying a harmful scripted effect.

## Client language and server localization

The first editable setting in `server/scripts/config.lua` is:

```lua
config.serverLanguage = "AUTO"
```

Supported values:

- `AUTO` - use the normalized language reported by each client;
- `RU` - force Russian server messages for every player;
- `EN` - force English server messages for every player.

The launcher exposes the same choice as **Server language** in the General
server-settings form. Saving the form updates `config.serverLanguage`.

Clients report a normalized language flag in `PlayerBaseInfo`. Russian clients
use `RU`; English and every other language use `EN`. The raw client flag remains
available as `tes3mp.GetLanguage(pid)`, `localization.GetClientLanguage(pid)` and
`Players[pid].language`. `localization.GetLanguage(pid)` returns the effective
language after applying `config.serverLanguage`.

`config.defaultLanguage` is the fallback used only in `AUTO` mode when no valid
client flag is available.


Runtime Lua helpers are also available:

```lua
localization.GetServerLanguage()       -- AUTO, RU or EN
localization.SetServerLanguage("RU")   -- changes the live rule until restart
localization.GetClientLanguage(pid)    -- raw RU/EN client flag
localization.GetLanguage(pid)          -- effective RU/EN language
```

CoreScripts load `server/scripts/localization.lua` and the bundled
`server/scripts/locales/core.lua` dictionary. External scripts can register a
namespace with `localization.RegisterDictionary()` or load a Lua dictionary
module with `localization.LoadDictionary()`.

```lua
localization.RegisterDictionary("example", {
    EN = { greeting = "Hello, {name}!" },
    RU = { greeting = "Привет, {name}!" }
})

localization.Message(pid, "example", "greeting", { name = Players[pid].name })
```

The multiplayer protocol is `806`; client and server must be updated together.
