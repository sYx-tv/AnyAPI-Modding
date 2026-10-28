# AnyAPI 0.41.0 for Anymaker 0.1.28

AnyAPI 0.41.0 ports AnyAPI and every mod to Anymaker 0.1.28 (Steam build 26277967). Mods built
for 0.1.27 turn themselves off on the new game build, so update AnyAPI and your mods in the
manager, then restart the game.

The game update was small: no data layouts, enums or game.exe functions we use changed. A few
game functions only changed their numbers (healing items, the large acid creature's abilities,
gas pumps and tanks, the server loop's settings file name), and the mods that find those
functions by their code were re-bound.

- **AnyAPI 0.41.0** (API revision 48)
- **AnyEngineParts bundle 1.0.3**, **AnyGraphics bundle 1.1.1**, **AnyHelpers bundle 1.1.1**,
  **AnyMap bundle 1.0.3**: rebuilt for 0.1.28, no other changes
- **AnyCareer 0.0.9**, **AnyCombat 0.1.2**, **AnyMoto 0.0.2**, **AnyPerformance 0.1.3**,
  **CustomGame 0.1.1**: rebuilt for 0.1.28, no other changes
- **AnyBattery 0.0.1** (new): a Battery Pack you place and stretch like a fuel tank. 4 kWh at the
  smallest size, more as you stretch it. It starts empty: charge it from an alternator or solar
  panels. The charge is saved with each world. Found in loot after the first bunker, like alternators.
- **AnyLights 0.2.2**: painted lights now glow their paint colour (headlights, beacon, bar and siren
  lights). In co-op, another player's paint tool no longer opens your palette or paints for you.

AnyBattery 0.0.1 and AnyLights 0.2.2 were ready as the mods-2026.10.28 release for 0.1.27 when
the game updated; they ship here instead. AnyStorage, AnyClock and AnyQuickWheel keep their files
and gain the new game build in the catalog.
