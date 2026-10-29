# AnyEngineParts 1.0.4

AnyEngineParts bundle 1.0.4 (EngineSound 0.4.2): saves always hold one of the game's own engine sounds.

- **Before:** on some launches EngineSound found the game's save functions too late and gave up on them. Worlds and
  saved vehicles then kept generated sounds, which players without the mod hear as silent or wrong engines.
- **Now:** EngineSound keeps looking for up to two minutes, so saves hold a stock sound and the real choice comes back
  when the mod is on.
- The other pieces are unchanged.
