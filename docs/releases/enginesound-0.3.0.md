# EngineSound 0.3.0

**Fix:** a vehicle with a preset or Custom sound that was spawned from a save could play the stock engine sound and the new sound at the same time. A game sound voice keeps the recording it started with, and a spawned vehicle's engine starts on the stock sound for a moment before its saved choice arrives. EngineSound now stops any engine voice whose recording no longer matches the chosen sound and lets it start again with the right one. The same fix covers changing the sound while the engine runs.

**Sound:** every engine type except Stock 1-3 now behaves like a running machine instead of a pitched loop:

- Start-up flare: the revs blip above idle when the engine catches, then settle.
- Rev limiter: full throttle at the top of the rev range bounces off the limiter in quick fuel cuts.
- Rev inertia: revs climb quicker than they fall; **Rev smoothing** makes it heavier.
- Uneven firing: a small wobble in pitch and loudness that grows with load.
- Overrun: coasting at high revs brings the deeper body layer forward.
- Crackle bursts: lift-off crackle comes in bursts of pops instead of single random pops, and a cold start pops once.

Stock sounds 0-2 and a neutral Custom tune are unchanged. Saved presets and Custom tunes keep their values. Guide: [EngineSound](../mods/engine-sound.md).
