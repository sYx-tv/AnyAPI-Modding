# AnyAPI 0.42.0 for Anymaker 0.2.0

AnyAPI 0.42.0 ports AnyAPI and every mod to Anymaker 0.2.0 (Steam build 26328453). Mods built
for 0.1.28 turn themselves off on the new game build, so update AnyAPI and your mods in the
manager, then restart the game.

This game update was a big one under the hood. The game's code file no longer carries function
and field names, so the port recovered them from the 0.1.28 build. Several data layouts grew
(the front-end UI, players, peers, world items and vehicle lights), damage events gained a value,
loot generation gained a value, and some method tables gained a slot. Every hook, offset and
code pattern AnyAPI and the mods use was re-checked against the new build.

- **AnyAPI 0.42.0** (API revision 49)
- **AnyHelpers bundle 1.1.2**: rebuilt for 0.2.0. Includes MCCode 0.3.2: clicking a part in the
  Code tab's Parts list adds that part's node in the node editor.
- **AnyEngineParts bundle 1.0.4**, **AnyGraphics bundle 1.1.2**, **AnyMap bundle 1.0.4**:
  rebuilt for 0.2.0, no other changes
- **AnyBattery 0.0.2**, **AnyCareer 0.0.10**, **AnyCombat 0.1.3**, **AnyCVT 0.0.2**,
  **AnyLights 0.2.3**, **AnyMoto 0.0.3**, **AnyPerformance 0.1.4**, **CustomGame 0.1.2**:
  rebuilt for 0.2.0, no other changes

AnyStorage, AnyClock and AnyQuickWheel keep their files and gain the new game build in the
catalog.
