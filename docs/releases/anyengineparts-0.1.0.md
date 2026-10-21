# AnyEngineParts 0.1.0

**New mod.** AnyEngineParts adds engine parts to Anymaker 0.1.25 in one mod. Guide: [AnyEngineParts](../mods/engine-parts.md).

- **Turbochargers** in four sizes (2x2x2 to 5x5x2), spooled by the engine's exhaust, with turbo lag, boost data channels and their own whistle and surge sounds. [Turbo guide](../mods/turbo.md).
- **Blow-off valves** (Small and Large) with a spring and sound picked in the Properties Tool, and a **wastegate** with microcontroller-controlled antilag.
- **Upkeep:** oil in and oil out ports under each turbo, a cone air filter, turbo heat and wear saved with each world, and a stretchable **intercooler** with snap-on gas ports and end tanks. Looking at a turbo shows its condition and temperature. Hot boost air costs every engine power and can knock it.
- **Rotary engine**, stretchable from 1 to 4 rotors in the small engine's footprint, with its own sound.
- **Turbo volume** in Mod Settings (needs AnyHelpers), 0 to 200 %.

It replaces the Turbo and Rotary prototypes. Their part ids are unchanged, so vehicles built with them load as before; remove `Turbo.dll` and `Rotary.dll` if you installed them by hand.

Experimental: it hooks the game directly and turns itself off on any build but Anymaker 0.1.25 / Steam build 25904830.
