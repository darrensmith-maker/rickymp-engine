Interactions Animated resources embedded in ArenaMP
===================================================

The animation and prop assets in this directory originate from the user-supplied
"Interactions Animated 59117 1.1" package. ArenaMP does not load the package's
OpenMW Lua scripts. The interaction state machine, QuickLoot hooks, multiplayer
animation replication, held-prop cleanup and pickup movement are implemented
natively in C++.

Embedded groups:
loot1, loot2, loot3, loot4, loot01, loot02, give-to-player, read-paper,
fireball-idle, petit, followme, wait, prayer1 and prayer2.

Native settings
---------------
There is intentionally no separate in-game Animations settings tab. These keys
are read directly from the [GUI] section of settings.cfg:

animated interactions = true
animated item pickup = true
animated pickup movement = true
animated doors = true
animated containers = true
animated quickloot = true
animated reading = true
animated barter handoff = true
interaction item speed = 1.0
interaction item cooldown = 0.0

The Z animation menu remains configurable through the normal Controls page and
uses three levels: body part, animation group, animation. One-shot interactions
temporarily interrupt a persistent pose; the pose is restored afterwards.
