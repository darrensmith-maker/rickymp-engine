ArenaMW native integration of Consuming Animated 1.5.1
======================================================

Source package: user-supplied "Consuming Animated 59069 1.5.1".

ArenaMW is based on OpenMW 0.47 and does not provide the modern OpenMW Lua API
used by the package. The .omwscripts/Lua layer is therefore replaced by a
native C++ state machine in mwmechanics/animationenhancements.cpp.

Integrated behaviour
--------------------
* Potion drinking animation with held potion prop.
* Ingredient eating animation using the consumed item's own mesh.
* Dedicated drinkbone animations/models for vanilla alcohol and skooma bottles.
* Bug musk animation.
* Moon sugar + skooma pipe animation when a pipe is present in inventory.
* Hackle-lo/tanna smoking animation when a supported pipe is present.
* First-person, standard third-person and beast/KNA KF sources are embedded.
* NPC potion consumption uses the same native animation path.
* Props follow Weapon Bone / Shield Bone and are removed at discard/timed points.
* Bundled eat, shatter, bong and smoke sounds can be played directly from the VFS.
* Dynamic Animation bone transitions are used both entering and leaving consume animations.

Settings (settings.cfg, [GUI])
------------------------------
animated consuming = true
animated consuming npc = true
animated consuming speed = 1.0
animated consuming npc speed = 3.0

The original Lua settings UI and third-party Lua interfaces (Sun's Dusk, etc.)
are not required for this native 0.47 integration.

ArenaMW LuaPhysics audio integration (cumulative 009):
physics object sounds = true
animated consuming bottle shatter = true
