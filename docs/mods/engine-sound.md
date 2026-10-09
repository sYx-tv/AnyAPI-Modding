# EngineSound

> EngineSound hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.24 / Steam build 25826614 and turns itself off on any other build.
> Its hooks are planned to move into a reviewed AnyAPI service.

EngineSound adds seven engine sound presets and a **Custom** tuner. You choose them in the engine's
**Properties Tool** window: left click an engine while holding the Properties Tool, then move the
**Sound effect** slider past the three stock sounds.

| Slider value | Sound |
| --- | --- |
| 0, 1, 2 | Stock sounds (unchanged) |
| 3 | Muscle V8 |
| 4 | Smooth inline |
| 5 | Tamed sport |
| 6 | Big diesel |
| 7 | Built 2JZ (turbo inline-6) |
| 8 | Powerstroke (turbo diesel V8) |
| 9 | Hemi (supercharged V8) |
| 10 | Custom |

A small **Engine sound** panel appears at the right of the screen while the window is open and names the
current choice. On a preset or stock sound it shows **Customize this sound**, which copies that sound into
Custom so you can tune from it. On Custom, drag its sliders:

| Slider | What it does |
| --- | --- |
| Engine type | Character of the engine: stock 1-3, inline-4, inline-6, V6, cross-plane V8, flat-plane V8, V10, V12, boxer-4, diesel inline-6, diesel V8, big-rig diesel, rotary, electric. Picks the recordings and their base pitch |
| Idle pitch | Pitch at idle, relative to the type (0.40x-1.30x) |
| Rev range | How much higher the pitch climbs at full revs (0.6x-2.0x of stock) |
| Rev curve | Early: pitch rises fast. Late: stays low and screams at the top |
| Volume | Main engine loudness (0-175%) |
| Body layer | Loudness of the deeper second layer under the main sound (0-140%) |
| Cam lope | The lumpy, uneven idle of a big cam. Fades out as revs rise |
| Induction | None, turbo (whistle that spools with load), supercharger (whine that follows revs) or twin turbo (faster spool) |
| Boost noise | How loud the turbo or supercharger is |
| Lift-off crackle | Exhaust pops and crackles when you lift off the throttle at high revs |
| Throttle response | How much louder and harder the engine sounds under load than coasting |
| Rev smoothing | Makes the pitch follow the revs a little lazily, like a heavy flywheel |

Turbo engines also play a blow-off hiss when you lift off after building boost. At neutral settings Custom
sounds exactly like stock sound 1.

## How it works

The game plays engines from recorded loops and changes their pitch and volume with the engine's speed and
load. EngineSound changes which loops play, reshapes their pitch and volume, adds cam lope, uses the engine's
spare knock voice for turbo or supercharger noise (only while the engine is undamaged), and plays pops and
blow-off as one-shot game sounds.

The choice is stored in the engine's own sound setting. The host keeps it, saves it with the vehicle and sends
it to every player, so everyone with EngineSound hears the same engine. All twelve Custom sliders are packed
into that same number. On the host, EngineSound widens the setting's allowed range so these values are
accepted. Crackle timing is random per player, so pops land at slightly different moments on each machine.

## Requirements and limits

- **Everyone needs it.** The host must run EngineSound for presets and Custom to be selectable. Players
  without it hear a stock sound instead.
- **Saves.** A vehicle saved with a preset or Custom keeps that value. Without EngineSound installed, the game
  treats it as an unknown sound index; check what it plays before relying on that. Custom tunes from the
  first prototype build are not read by this one and fall back to stock.
- No new sound files: every sound uses recordings already in the game. A synthesized engine is a later
  experiment.
- No AnyHelpers settings yet; the panel position is fixed.

## Test plan

Install EngineSound from the manager, or build `EngineSound.dll` with the native CMake project and copy it
to `AnyAPI and Modding/mods/`. Logging is verbose in 0.2.0. After each step, the lines tagged `enginesound` in
`anymaker_modding.log` (beside `game.exe`) are what we need.

1. Load a world. Expect `Ready after N s ... audio manager found`.
2. Open an engine's Properties window. The native slider runs 0-10 and the panel names each value.
3. Pick 7, 8 and 9 and rev the engine, then lift off sharply at high revs. Expect a turbo whistle on 7 and 8,
   a supercharger whine on 9, blow-off hiss on 7 and 8, and crackle on 7 and 9 (`one-shot effect` lines).
4. Press **Customize this sound** on a preset, then drag each slider while the engine runs.
5. Co-op: a second player joins with EngineSound, then each player changes the sound. Both should hear the
   same thing.
6. Save, reload and confirm the choice is kept.
