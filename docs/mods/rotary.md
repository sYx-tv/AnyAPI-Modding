# Rotary engine (AnyEngineParts)

> The rotary engine is part of [AnyEngineParts](engine-parts.md). It hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.28 / Steam build 26277967 and turns itself off on any other build.
> **Prototype, single player only** for now: built from source, not in the catalog yet.

Rotary adds a new part, the **Rotary Engine**. It is the stock small engine cell for cell (same size, ports,
manifolds, gearboxes and engine wheels, same air, fuel, oil and coolant needs, starts at 6 rps), with a rounded
rotary housing, 25% more power and its own synthesized sound.

## Rotors

Stretch it along its length like the small engine. Every 2 cells is one more rotor, from **1 to 4 rotors**
(2 to 8 cells long). Each rotor housing sits between side plates, with spark plugs on the left, under a flat deck
that runs the whole length. The deck, the full-height covers at each end, flush pads low on each side and the oil pan
underneath keep every mounting face flat, so manifolds of any size sit on it as they do on the small engine. It is
drawn in the stock engines' style and paints like them: the housings take the first colour, the covers, plates and
deck the second.

| Rotors | Length | Max power | Small engine of the same length |
| --- | --- | --- | --- |
| 1 | 2 cells | 18 kW | 14.4 kW |
| 2 | 4 cells | 36 kW | 28.8 kW |
| 3 | 6 cells | 54 kW | 43.2 kW |
| 4 | 8 cells | 72 kW | 57.6 kW |

The game gives an engine 400 W per cell of its volume, and its fuel and air use, shaft inertia and friction scale
with the same volume. Rotary raises a rotary's volume by 25% when it is created, so all of those go up together:
it is a stronger engine, not a free one.

## Sound

The rotary's stock engine voices are silenced and the mod plays its own: every rotor face firing once per shaft
turn, through the exhaust's resonances. At the 6 rps start speed a 2-rotor fires 12 times a second, a lumpy
**brap brap brap** that lopes and misses a little; revving up smooths it into a buzz and then the 8000 rpm scream.
Lifting off at high revs pops and crackles. While the starter cranks it you hear compression puffs, then the
first pulses bark as it catches.

Pick the port with the engine's own **Sound effect** row in the Properties Tool:

| Sound effect | Port | Character |
| --- | --- | --- |
| 0 (default) | Bridge port | Lumpy, choppy idle; the classic brap |
| 1 | Street port | Smoother, cleaner idle |
| 2 | Peripheral port | Wild: heavy lope, misfires and lots of pops |

The knock (when damaged) and leak sounds stay stock. The sound follows the vehicle volume slider and plays from
the engine's position. It works next to EngineSound: EngineSound tunes the other engines, Rotary voices its own.

## Getting it

- **Survival:** world loot after the 3rd bunker (tech tier 4, mechanical loot), about a quarter less often than the
  game's own tier 4 parts, like the other AnyEngineParts parts.
- **Sandbox and Creative:** with the other engines.

## How it works

- At start-up the mod writes three mesh tiles (`rotary_engine_0_0_0/1/2.mesh`, generated in code by
  [`rotary_mesh.h`](../../native/rotary_mesh.h)) into `rom/meshes/components` and the part definition
  (`AnyAPI and Modding/Rotary/rotary_engine.json`, from [`rotary_logic.h`](../../native/rotary_logic.h)), and adds
  the definition right after the game adds its own. Id `rotary_engine`, class `engine`, `ext_max` [0, 0, 6].
- A hook on the server engine's create raises the rotary's volume by 25%.
- On the client, a hook on the engine's tick reads the replicated speed, load and sound index, renders the
  sound ([`rotary_sound.h`](../../native/rotary_sound.h), 48 kHz mono) and streams it into a positional game voice;
  a hook on the game's voice update silences the rotary's stock running and power voices.
- `native/tests/rotary_logic_test.cpp` checks the definition, meshes and sound levels; run it with
  `--wav <dir>` to write the sound previews.
