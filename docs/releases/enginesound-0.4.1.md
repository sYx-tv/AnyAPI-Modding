# EngineSound 0.4.1

**Saves play without the mod.** A world or saved vehicle now only holds one of the game's three engine sounds, so
it still plays with mods off, for example after a game update turns them off. Its engines get the game's first
sound. Before, an engine on a generated sound or Custom kept a number the game has no sound for, and without
EngineSound it could go silent or play the wrong sound.

The real choice is kept beside the save, so it comes back when EngineSound is on again:

- **Worlds:** in a small `enginesound_sounds.json` that travels inside the save.
- **Saved vehicles:** under its own key in the vehicle file, which the game ignores.

A sound changed while the mod was off is kept. Worlds and vehicles saved before 0.4.1 hold the generated sound
until they are saved again.

**Fix:** turning the key off left a high whine that swept down with the revs while the engine wound down. The
starter motor's sound played whenever the crank turned without combustion; now it only plays while a starter
turns a stopped engine over, and a winding-down engine just breathes, as on a fuel cut.

**Fix:** after loading a second world, an engine could take another engine's layout, size and cylinder count
(or be treated as the rotary) and sound wrong. The engine type cache now checks the part id each time.

Ships in the AnyEngineParts bundle 1.0.1. Guide: [EngineSound](../mods/engine-sound.md).
