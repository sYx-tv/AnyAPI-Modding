# AnyAPI 0.40.1 for Anymaker 0.1.27

Anymaker 0.1.27 (Steam build 26113663) changed the game files, so AnyAPI 0.39.2 turns itself
off on it. AnyAPI 0.40.1 is the API ported to the new build, with a few fixes the port audit
found and the graphics bug pass (0.40.0 was the port alone and was never published).

Update AnyAPI and your mods in the manager, then restart the game. The manager installs the API
that matches your game files.

This release also contains rebuilds of every mod that reads the game directly, because they
only run on the exact build they were made for:

- **AnyHelpers 1.0.2** (with AnyInventory, AnyMirror, AnyBalance and MCCode)
- **AnyEngineParts 1.0.1** (with EngineSound 0.4.1 and GpsPart)
- **AnyGraphics 1.0.3** (with AnyGraphics 0.32.0 and ThirdPerson)
- **AnyMap 1.0.1**
- **AnyCombat 0.1.1**

AnyStorage, AnyClock and AnyQuickWheel are unchanged and gain the new build in the catalog.

## Fixed by the port

- **AnyBalance:** the game's Properties tool changed, so the centre-of-mass overlay's checks
  were re-derived (the old bytes only matched the microcontroller tool now). The camera
  projection it checks moved in `game.exe`.
- **Graphics:** the game's graphics context moved (RVA 0x5e23d0, command list at +0x2650).
  Anti-aliasing, lighting, AO, GI, sky, contact shadows, lens and effects find the game's
  command list there; a check now refuses an address that does not hold a real command list.
- **Engine sounds:** the six `game.exe` audio calls the turbo, rotary and EngineSound sounds use
  all moved. The old addresses would have crashed the game at the first custom engine sound.
- **AnyHelpers ADMIN weather:** 0.1.27 scales rain and snow through a new 0.2 threshold, so the
  panel's Rain and Snow now write 0.76 to stay as strong as before.
- **GPS Sensor:** tech tier 3, like the game's own sensors since 0.1.27 (it drops from the 2nd
  bunker).
- **AnyMap:** creature blips resolve through a code signature that exists in this build, not
  only the fallback.

## Graphics (AnyAPI 0.40.1, AnyGraphics 0.32.0)

- **Sub-menus:** the Graphics section keeps the main choices on top and puts the rest in groups
  you open with Show and close with Hide. Changing anything in a group switches to Custom.
- **New looks:** the Soft & warm preset (Soft film tone curve, golden highlights, cool shade),
  Light colour (tints the game's own sun and shade lights, keeping their brightness) and Wind in
  trees (how hard trees and grass sway; only the look changes).
- **Sun shafts:** no more pumping as the camera moves, no glowing wall past the last shadow
  cascade, no blocky distant ground, and filtered shadows so foliage stops speckling.
- **Contact shadows:** no self-shadowing at grazing angles, no double darkening over the game's
  shadows, nothing on the ocean, softer edges.
- **Clouds:** the cloud layer is fixed in the world and drifts with the wind instead of
  following the camera. TAA + jitter is now TAA extra smooth.
- New for mod authors: scene controls version 3 (`anyapi_scene_controls_v3.h`) with light tints
  and wind scale.

## Other fixes in this release

- **HUD control hints** (used by AnyQuickWheel) could never install: two of their byte checks
  compared the wrong bytes. Both are fixed; the hint has not been seen in game yet.
- An unreadable `AnyAPI and Modding/mods` folder no longer crashes the game at start.
- The bundles also carry the 2026-10-20 bug pass that was not published yet:
  AnyEngineParts 0.2.2 and GpsPart 1.0.4 (turbo ring lists, stale part caches), AnyHelpers
  (Favorites deleted the wrong row, admin, inventory, mirror and MCCode fixes), AnyGraphics
  (photo mode head flag, world change) and AnyMap 0.28.1 (cache folder).
- **EngineSound 0.4.1:** saves only hold one of the game's three engine sounds, so worlds and saved
  vehicles still play with mods off (the generated sound comes back with EngineSound on), and turning
  the key off no longer leaves a whine sweeping down with the revs. [Notes](enginesound-0.4.1.md)

## How the port was checked

- `native/tools/game_update.py` refreshed 24 code patterns, re-bound every experimental-SDK
  mod and wrote the new build identity.
- A multi-agent audit then checked AnyAPI and every mod against the new game code: hard-coded
  offsets, virtual slots, enums, `game.exe` addresses and each patch-note change our mods touch.
- The pattern audit now also catches four kinds of breakage it missed this time: natives
  called through a plain `*_rva` constant (`--current-json`), a pattern whose former twin
  stopped matching, a pattern that runs into run-time relocation cells, and hand-written
  signatures outside the bindings (AnyMap's creature blips). The graphics context address is
  still checked by hand.
- The release planner now understands the mod bundles, so the next port bumps and packages
  them instead of the old standalone mods.

See [porting to a new game build](../development/game-updates.md).
