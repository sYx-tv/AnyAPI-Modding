# AnyEngineParts 0.2.0

**New: the Dyno.** A small sensor part that records engine power and torque. Hold the throttle down for a pull and
the curves draw on screen; the last three pulls stay up so you can compare builds, with each pull's peak power and
the latest pull's torque, boost, engine temperature and knock. F6 hides the panel, holding F6 clears the pulls, and
the data port outputs rpm, power, torque and the latest peak. See [Dyno](../mods/dyno.md).

**Fix:** blow-off valves, wastegates, air filters and the intercooler no longer look see-through. Their faces were built facing inward, so the game hid their outsides.

**New looks:** blow-off valves have a slim "race port" body with a slotted vent crown, and wastegates a finned actuator can with two boost nipples. Paint works as before: the first colour on the body, the second on the cap.

**Two intercoolers:** the intercooler is now the **Race Core** (black core, bright fins, inlet stubs), and a second, the **Round Tank** (slim silver core between round tanks), works the same. Both keep even fins however tall they are stretched. Intercoolers already placed become the Race Core.

**Bigger air filters, pipe centred:** the small filter is now 3x3x3 and the large 4x4x4, each with its pipe in the middle of the bottom face.

**Turbos blow up at 0%:** a worn-out turbo now blows with a bang, shrapnel and smoke, and goes sooty. It makes no boost
and chokes the engine's air, so the engine runs on in limp mode (about a third of its power at most) until you replace
the turbo. The tooltip's **Condition** now shows a percentage, 100% new down to 0% blown; it used to show no value.

**Turbo ports fixed:**
- The 4x4 and 5x5 boost outlet and turbine inlet sat half off the part. They now sit on it: the outlet one cell below
  the top corner, the inlet across both cells of the bottom corner, each on a flange block. **On a vehicle built with
  0.1.0, re-lay the pipe on those two ports** (move it down a cell, or across a cell for the inlet).
- The 2x2's oil ports shared cells with its gas ports; they now sit one row back. Every turbo now shows its oil ports
  as brass-ringed fittings underneath.
- Every opening lines up with its pipe: the 2x2's intake and exhaust out moved to where their pipes join, and the
  5x5's eye moved onto its large pipe.
- No data-port mounts under the 4x4 and 5x5 intake and exhaust pipes, and the wastegates lost stray mounts that
  floated off the part.

**New: superchargers.** **Supercharger (Small)** (2x2x2) and **Supercharger (Large)** (4x4x4) are centrifugal
superchargers driven by the engine: put a stock Pulley Wheel on the torque spot on the back and belt it to an Engine
Wheel. Boost comes with no lag and climbs with engine speed; the Engine Wheel's size tunes it (the medium one gives full
boost at 7,000 RPM, the large one from about 4,000). The belt takes the power it needs from the engine. Each has its own
gear whine, with a **Supercharger volume** setting beside Turbo volume. Pipe one into a turbo to twincharge. See
[Superchargers](../mods/turbo.md#superchargers).

**The 2x2 and 4x4 turbos are retired.** The superchargers take their place. Vehicles that have them keep them, and they
work as before, but they no longer show in the part lists or loot.

**The Dyno names your boost setup** (single to quad turbo, compound, supercharged, twincharged) under its live
readout, and its power is now net of what superchargers take through their belts.

**New: compound turbos.** Pipe one turbo's boost into another turbo's intake and the second one adds its boost on top
of the first. One turbo still gives the game's full 2x; a compound goes past it, up to 3x at 186 kPa. See
[Twin and compound turbos](../mods/turbo.md#twin-and-compound-turbos).

**Intercoolers fill their whole part:** the model used to start a cell in from one end, leaving an empty cell of the
part's box with nothing drawn in it, so a gas port snapped there looked half off. The model now covers every cell, at
any size. Growing one no longer stretches its inlet stubs or rails: it repeats whole cells both ways.

**Intercoolers:** the turbo now looks for its intercooler past an inline blow-off valve too, and also through the part each
gas port is snapped onto. In testing, a turbo never found its intercooler, so its boost air stayed hot. If a boosting turbo
still finds none, the log lists the parts on its boost pipe so the cause shows up.

**Fixes from a full review:**
- A rare crash at startup: a hook could be called in the moment before it knew where to pass the call on. It now
  knows first. This was in the shared modding SDK, so every part (and any mod rebuilt with it) is covered.
- The Dyno could crash the game after reloading a save with two Dynos wired to a microcontroller, or after one
  stopped ticking for a few seconds. Dynos now keep their data port memory for the whole session.
- The Dyno's **Knock** row now means knock (a turbo's hot boost air). Low oil or an overheating engine used to show
  as knock on every pull.
- The Dyno's `clear_runs` input clears once each time it goes high, and clearing (or holding F6) keeps the pull being
  recorded. It used to wipe everything every second while held.
- The Dyno graph's torque labels now sit on the grid lines.
- Twin turbos sharing one blow-off valve: the second turbo surged on every lift. Both now see the valve open.
- A turbo whose oil ports the game would not make counted as running dry (fast wear, then a blow-up) with nothing to
  pipe. It now counts as oil fed, like a build without oil ports.
- `boost_limit` now caps boost as the `boost` output reads it, as this guide always said; it capped the absolute
  intake pressure, about 10 kPa lower. **If you wired it, boost now goes about 10 kPa higher for the same number.**
- `power_multiplier` now includes what hot boost air costs, so it matches the power the engine gets.
- A parked Rotary hands its sound voice back to the game after two seconds of silence.
- The three parts start on their own: one still looking for its game functions no longer delays the others.

Guide: [AnyEngineParts](../mods/engine-parts.md).
