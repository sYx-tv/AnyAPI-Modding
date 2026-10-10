# AnyAPI 0.35.0 for Anymaker 0.1.24, with AnyMirror 1.0.0

Anymaker 0.1.24 (Steam build 25826614) changed the game files, so AnyAPI 0.34.1 turns
itself off on it. AnyAPI 0.35.0 is the same API ported to the new build. It adds no
services and changes no service behaviour. Existing mods keep working with it, except
AnyBalance: its centre-of-mass overlay does not show on 0.1.24 yet, and a fix follows.

Update AnyAPI in the manager, then restart the game. The manager installs the API that
matches your game files.

This release also contains:

- **AnyMirror 1.0.0**, a new mod: mirrored edge building with either edge tool, a
  toggleable mirror wall and keybinds. Guide: [AnyMirror](../mods/mirror.md).
- **EngineSound 0.2.1**, rebuilt for 0.1.24 with no other changes.

AnyMirror and EngineSound use the experimental SDK, so they only run on the exact build
they were made for. The other mods are unchanged.

What changed for the port: 23 native code patterns were refreshed after the update moved
struct fields, stack slots and constants, the inventory container dependency offsets moved
by 8 bytes, the legacy event routes were regenerated, and the antialiasing pass follows the
graphics context to its new address. Every pattern was checked against the new build with
`native/tools/game_update_audit.py`; see [porting to a new game build](../development/game-updates.md).
