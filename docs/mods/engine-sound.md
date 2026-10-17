# EngineSound

> EngineSound hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> Its hooks are planned to move into a reviewed AnyAPI service.

EngineSound gives every engine a real, generated sound instead of a pitched game recording. Each cylinder's
exhaust pulse is made sample by sample and fired in the engine's real firing order, through its own exhaust
bank, the same way the AnyEngineParts rotary is made. The sound follows the engine you built:

- **Layout.** Inline engines (Engine, Engine B, Engine C), V engines (V Engine, V Engine B, V Engine C) and
  boxers (Opposed Engine, B, C) each sound like their layout: a cross-plane V8 burbles, a V-twin "potatoes",
  a flat-4 with unequal headers rumbles, an inline-6 is smooth.
- **Cylinders.** The count comes from how far the engine is stretched. Unstretched, an inline is a single,
  a V a V-twin and a boxer a flat-twin. Each stretch step adds one cylinder to an inline and a pair to a V or
  boxer (steps are 2, 3 and 5 cells for the small, medium and large blocks).
- **Size.** Bigger blocks sound deeper.

Rotary engines (AnyEngineParts) keep their own sound.

## Choosing a sound

Left click an engine while holding the **Properties Tool**. The **Engine sound** panel beside the window
names the engine (for example "V8 (medium block): Muscle") and lists the sounds for its layout. Click one to
pick it. The game's **Sound effect** slider moves with it.

| Slider value | Inline | V | Boxer |
| --- | --- | --- | --- |
| 0-2 | Game sound 1-3 | Game sound 1-3 | Game sound 1-3 |
| 3 | Stock | Stock | Stock |
| 4 | Sport | Muscle | Rally rumble |
| 5 | Street tuner | Exotic (flat-plane on a V8) | Air-cooled |
| 6 | Race | Race | Race |
| 7 | Diesel | Truck diesel | Diesel |
| 8 | Custom | Custom | Custom |

**Customize this sound** copies the chosen sound into Custom, where every part of it is a slider:

| Slider | What it does |
| --- | --- |
| Exhaust note | The pipe's deep boom, 50-170 Hz |
| Bark | The mid "bark" of each pulse |
| Rasp | The raspy edge of each pulse |
| Muffler | Straight pipe to quiet stock muffler |
| Cam lope | A lumpy, uneven idle that clears with revs |
| Pops and crackle | Pops when lifting off at speed and on the limiter |
| Intake roar | Breathing noise under load |
| Valvetrain | Valve tick and gear whine |
| Firing / headers | V8: cross-plane or flat-plane. V-twin: 45 to 90 degree. Boxer: equal headers to full rumble. Inline: tuned to raspy headers |
| Diesel | Diesel clatter, no pops |
| Volume | 25-200% |

Every sound cranks on the starter, catches, idles, bounces off the rev limiter at full throttle and pops
on the overrun (unless it is a diesel or its crackle is off).

## How it works

The game plays engines from recorded loops. For an engine on a generated sound, EngineSound silences those
loops and streams its own 48 kHz sound into a positional game voice, the way the AnyEngineParts sounds do. The
engine's speed and load drive the synth; the game's start speed (360 rpm) sounds like a 750 rpm idle and the
top stays 8,000 rpm. The engine's knock while damaged and its fuel leak still play.

The choice is stored in the engine's own sound setting. The host keeps it, saves it with the vehicle and sends
it to every player, so everyone with EngineSound hears the same engine. Custom tunes are packed into that same
number. On the host, EngineSound widens the setting's allowed range so these values are accepted, and tags the
row with the engine's layout, size and cylinders so every player's panel names the right sounds.

## Requirements and limits

- **Everyone needs it.** The host must run EngineSound for the generated sounds to be selectable. Players
  without it hear a game sound instead.
- **Saves.** A vehicle saved with a generated sound keeps that value. Sounds saved with EngineSound 0.3.0 or
  older open as this version's sound at the same slider value, and old Custom tunes open as a Custom tune
  that sounds different; pick again if it is not what you want.
- No AnyHelpers settings yet; the panel position is fixed.

## Test plan

Launch Anymaker from Steam with EngineSound installed. The lines tagged `enginesound` in
`anymaker_modding.log` (beside `game.exe`) are what we need.

1. Load a world. Expect `Ready after N s: generated sounds 3..7, Custom 8`.
2. Place each engine family (Engine, V Engine, Opposed Engine), open its Properties window and pick a
   sound. The panel names the engine and its layout's sounds; the log names the size, cylinders and sound.
3. Stretch an engine and confirm the cylinder count in the panel title changes and the sound with it.
4. Start, idle, rev, hold full throttle at the top, then lift off. Expect starter, idle lope on Muscle,
   limiter bounce, and pops on lift-off.
5. Press **Customize this sound**, then drag each slider while the engine runs.
6. Co-op: a second player joins with EngineSound. Both should hear the same sound.
7. Save, reload and confirm the choice is kept and only one sound plays.
