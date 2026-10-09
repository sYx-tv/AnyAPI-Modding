# AnyAPI 0.35.0 for Anymaker 0.1.24

Anymaker 0.1.24 (Steam build 25826614) changed the game files, so AnyAPI 0.34.1 turns
itself off on it. AnyAPI 0.35.0 is the same API ported to the new build. It adds no
services and changes no service behaviour; existing mods keep working with it.

Update AnyAPI in the manager, then restart the game. The manager installs the API that
matches your game files: 0.35.0 on Anymaker 0.1.24, 0.34.1 on 0.1.23.

EngineSound 0.2.1 and AnyMirror 1.0.0 are rebuilt for 0.1.24, because mods built on the
experimental SDK only run on the exact build they were made for. The other mods are
unchanged and work on both builds.

What changed for the port: 23 native code patterns were refreshed after the update moved
struct fields, stack slots and constants, the inventory container dependency offsets moved
by 8 bytes, the legacy event routes were regenerated, and the antialiasing pass follows the
graphics context to its new address. Every pattern was checked against the new build with
`native/tools/game_update_audit.py`; see [porting to a new game build](../development/game-updates.md).
