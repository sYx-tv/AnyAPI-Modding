# AnyEngineParts

> AnyEngineParts hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> It needs AnyAPI 0.37.0 or newer. Install it from the manager.

AnyEngineParts adds engine parts to the game, in one mod (`AnyEngineParts.dll`):

- [Turbochargers](turbo.md) (3x3x2 and 6x6x2), driven by the engine's exhaust, with Small and Large
  inline blow-off valves, a wastegate with microcontroller-controlled antilag, boost data channels and
  synthesized whistle, surge, blow-off and antilag sounds. Twin, compound and twincharged setups work.
- [Superchargers](turbo.md#superchargers) (Small 2x2x2 and Large 4x4x4), belted to the engine with a stock Pulley
  Wheel: boost with no lag, tuned by the Engine Wheel's size, with their own gear whine.
- Turbo upkeep: oil ports, a cone air filter, turbo heat and wear saved with each world, and a stretchable
  intercooler. Hot boost air now costs every engine power and can knock it.
- A [rotary engine](rotary.md), stretchable from 1 to 4 rotors in the small engine's footprint, with its own
  housing mesh and synthesized sound. It works alongside EngineSound.
- A [Dyno](dyno.md) sensor part that records power and torque pulls and draws them on screen.

It replaces the separate Turbo and Rotary prototypes. The part ids are unchanged, so vehicles built with those
load as before. Remove `Turbo.dll` and `Rotary.dll` when installing it, or their parts are hooked twice.

Each part set starts on its own: if one cannot find the game functions it needs, the others still work. Their
lines in `anymaker_modding.log` keep the tags `turbo`, `rotary` and `dyno`.

Source: [`engine_parts_mod.cpp`](../../native/engine_parts_mod.cpp), which starts
[`turbo_mod.cpp`](../../native/turbo_mod.cpp), [`rotary_mod.cpp`](../../native/rotary_mod.cpp) and
[`dyno_mod.cpp`](../../native/dyno_mod.cpp).
