# EngineSound 0.4.2

**Saves always hold a stock sound.** On some launches the game's save functions resolved a little later than
the sound ones, and EngineSound gave up on them. Saves then kept generated sounds that the game can't play without
the mod, so players without it heard silent or wrong engines. EngineSound now looks again for up to two minutes.
