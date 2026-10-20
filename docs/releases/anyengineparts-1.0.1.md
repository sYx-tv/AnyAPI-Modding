# AnyEngineParts bundle 1.0.1

Fixes from the bug pass, and saves that play without the mod.

- **EngineSound 0.4.1:** worlds and saved vehicles only hold one of the game's three engine sounds, so they
  still play with mods off (for example after a game update turns them off); the real choice comes back with
  EngineSound on. After a second world loaded, an engine could also take another engine's layout and cylinder
  count; fixed. [Notes](enginesound-0.4.1.md).
- **AnyEngineParts 0.2.2:** a turbo on a big or often-edited vehicle could miss its air filter (and wear 4x
  faster), its blow-off valve, antilag, two-step or intercooler ports. The game's part lists wrap round their
  buffer and are now read that way.
- **GpsPart 1.0.4:** a GPS Sensor could lose its outputs after another world loaded; fixed.

Saved vehicles and part ids are unchanged. Anymaker 0.1.25 only.
