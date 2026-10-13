# AnyAPI 0.37.0 for Anymaker 0.1.25

Anymaker 0.1.25 (Steam build 25904830, the hotfix) changed the game files, so AnyAPI 0.36.0
turns itself off on it. AnyAPI 0.37.0 is the same API ported to the new build. It adds no
services and changes no service behaviour.

Update AnyAPI in the manager, then restart the game. The manager installs the API that
matches your game files.

This release also contains rebuilds of the mods that use the experimental SDK, because
they only run on the exact build they were made for:

- **AnyMirror 1.1.1**
- **EngineSound 0.2.2**
- **GpsPart 1.0.1**
- **ThirdPerson 0.1.1**

They have no other changes. The other mods are unchanged and gain the new build in the catalog.

What changed for the port:

- 20 native code patterns were refreshed after the update moved constants and stack slots.
- The inventory container's dependency cells moved by 8 bytes again (code end 25584 to
  25592). They are now written relative to `ST_CONTAINER_END`, which the update tool
  rewrites by itself next time.
- The hotfix added a field to the game's peer data: the player authority lookup and the
  server's item use resolution follow the authority actor to its new offsets.
- The SSAO call cell inside the scene composite moved with the function's new size.
- The `actor_use_ability_actor` legacy route follows the game's new null check.

Every pattern was checked against the new build with `native/tools/game_update_audit.py`;
see [porting to a new game build](../development/game-updates.md).
