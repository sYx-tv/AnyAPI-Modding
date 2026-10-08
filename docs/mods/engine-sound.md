# EngineSound (prototype)

> **Prototype, not in the catalog.** EngineSound hooks game functions directly through the
> [experimental SDK](../../sdk/experimental/README.md). It only activates on Anymaker 0.1.23 /
> Steam build 25755694. Once tested, its hooks move into a reviewed AnyAPI service before release.

EngineSound adds four extra engine sound presets and a **Custom** sound with sliders. You choose them in the
engine's **Properties Tool** window: left click an engine while holding the Properties Tool, then move the
**Sound effect** slider past the three stock sounds.

| Slider value | Sound |
| --- | --- |
| 0, 1, 2 | Stock sounds (unchanged) |
| 3 | Muscle V8 |
| 4 | Smooth inline |
| 5 | Tamed sport |
| 6 | Big diesel |
| 7 | Custom |

A small **Engine sound** panel appears at the right of the screen while the window is open and names the
current choice. When it shows **Custom**, drag its sliders:

| Slider | What it does |
| --- | --- |
| Base sound | Which game recording the engine loops (stock 1-3, idle, electric, marine, aircraft) |
| Idle pitch | Pitch at idle, relative to stock (0.40x-1.30x) |
| Rev range | How much higher the pitch climbs at full revs (0.5x-2.0x of stock) |
| Rev curve | Below 1 the pitch rises early; above 1 it stays low and climbs late |
| Volume | Main engine loudness (0-150%) |
| Idle layer | Loudness of the separate idle/operate layer (0-140%) |
| Smoothing | How lazily pitch follows the revs (0-0.35 s) |
| Backfire pops | Keep or silence the game's exhaust pops |

At neutral settings Custom sounds exactly like the stock sound it is based on.

## How it works

The game plays engines from recorded loops and changes their pitch and volume with the engine's speed and
load. EngineSound changes which loop plays and reshapes that pitch and volume on each player's machine.

The choice is stored in the engine's own sound setting. The host keeps it, saves it with the vehicle and sends
it to every player, so everyone with EngineSound hears the same engine. Custom slider values are packed into
that same number. On the host, EngineSound widens the setting's allowed range so these values are accepted.

## Requirements and limits

- **Everyone needs it.** The host must run EngineSound for presets and Custom to be selectable. Players
  without it hear a stock sound instead.
- **Saves.** A vehicle saved with a preset or Custom keeps that value. Without EngineSound installed, the game
  treats it as an unknown sound index; check what it plays before relying on that.
- No new sound files: every preset uses recordings already in the game. A synthesized engine is a later
  experiment.
- No AnyHelpers settings yet; the panel position is fixed.

## Test plan (first run is diagnostic)

Build `EngineSound.dll` with the native CMake project and copy it to `AnyAPI and Modding/mods/`.
Logging is verbose in this build. After each step, the lines tagged `enginesound` in
`anymaker_modding.log` (beside `game.exe`) are what we need.

1. Load a world. Expect `Ready after N s`. If you see `could not locate game functions` or `build does not
   match`, stop there and send the log.
2. Open an engine's Properties window. Expect `host engine sound row: label=324 range 0..2`. Any other range
   means the stock numbering differs; the mod then leaves the slider alone and says so.
3. Run an engine at each stock sound 0, 1 and 2. Expect `sound index N -> vanilla effect E` and `voice ...`
   lines giving each voice's effect, volume and speed range.
4. Pick 3-6 and listen. Pick 7 and drag each slider while the engine runs.
5. Co-op: a second player joins with EngineSound, then each player changes the sound. Both should hear the
   same thing. Also try a joining player without the mod.
6. Save, reload and confirm the choice is kept.
