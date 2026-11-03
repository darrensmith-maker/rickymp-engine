# CoreArenaMP v20 — Data Tamper Guard Only

Enabled:
- server-side validation of player numeric/gameplay values;
- NaN/Inf and absurd-range rejection;
- semantic negative-value rejection for fields where negatives are invalid;
- validation of attributes, skills, dynamic stats, level, bounty, reputation, inventory item counts/charges, spells/effects, cooldowns, equipment, attacks/casts and related player numeric state;
- transactional rollback of Player packet fields when a protected player-data packet is rejected;
- secondary client-side outgoing numeric sanity checks.

Disabled by request:
- speedhack / teleport detection;
- packet rate limits;
- NPC/container interaction leases and concurrency blocking;
- extra actor/cell authority enforcement;
- generic BasePacket hardening and added packet-size/integrity hooks;
- CharGen/cell-transition filtering;
- behavioral anti-cheat checks.

Stock TES3MP packet routing and object/actor/world interaction behavior are preserved.
