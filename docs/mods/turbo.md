# Turbo (AnyEngineParts)

> The turbo parts are part of [AnyEngineParts](engine-parts.md). They hook game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> AnyEngineParts 0.1.0 is a prototype: it needs AnyAPI 0.37.0 or newer on that build. **Single player only** for now.

Turbo adds four exhaust-driven turbochargers: **Turbo 2x2**, **3x3**, **4x4** and **5x5**. Each is N cells wide,
N tall and 2 thick. The front cell is the stock impeller's compressor housing, scaled to the size; the back cell is
the turbine housing, the same impeller mirrored. A new turbo starts with a steel cold side and a cast-iron hot side,
and the paint tool colours each side on its own, like any stock part.

## Hooking one up

| Port | Where | Connect to |
| --- | --- | --- |
| Intake in (cold side) | Middle of the front face | Nothing needed: it draws from the open air. A pipe here can only add air. |
| Boost out (cold side) | Top corner of the front housing, facing sideways | The engine's air intake |
| Exhaust in (hot side) | Bottom corner of the back housing, facing the other way | The engine's exhaust |
| Exhaust out (hot side) | Middle of the back face | A gas vent (the tailpipe) |
| Oil in, oil out | Underneath, side by side | The engine's oil, through an oil cooler (see [Upkeep](#upkeep-oil-air-filter-heat-and-wear)) |

Exhaust flowing from the hot inlet to the hot outlet spins the shaft. The shaft's spool sets how much pressure
the cold side pushes into the engine's intake. Engines already turn intake pressure into power: ambient gives
1x, and an intake pressure of 98 kPa (the game's scale) gives the full 2x.

Turbos 4x4 and 5x5 use large pipes; 2x2 and 3x3 use small pipes. No gas tank is needed. The compressor inlet is open to the air like a real turbo intake, because the game's vents and filters only pass about 13.5 L/s, far less than a spooled turbo draws.

## Sizes and lag

| Size | Exhaust for full boost | Spool-up time | Shaft speed at full boost |
| --- | --- | --- | --- |
| 2x2 | 10 L/s | about 0.5 s | 220,000 rpm |
| 3x3 | 22 L/s | about 0.9 s | 160,000 rpm |
| 4x4 | 40 L/s | about 1.5 s | 120,000 rpm |
| 5x5 | 65 L/s | about 2.4 s | 90,000 rpm |

Pick the size whose full-boost flow is close to your engine's exhaust L/s at the throttle you use. Boost starts
building at a quarter of that flow and reaches full boost at the rated flow. Bigger turbos take longer to spool
(more lag) and spool down at half their spool-up time.

A turbo that is too small for its engine chokes it: exhaust backs up, and above 39 kPa of exhaust pressure the
engine loses power, as it does with any restricted exhaust. A turbo that is too big barely spools.

## Blow-off valve and wastegate

Two companion parts, in the gas parts with the turbos:

- **Turbo Blow-Off Valve (Small)** (1 cell, small pipe, for the 2x2 and 3x3) and **(Large)** (2x2x2, large
  pipe, for the 4x4 and 5x5). Inline: pipe the turbo's boost outlet into its **plain end** (it shows as *Gas
  node*) and its **blue-ringed end** (*Air node*) into the engine's air intake. Boost passes straight through.
  When you lift off the throttle, boost trapped in the pipe is higher than the turbo can hold; the valve vents it
  out of the vent slots in its crown, so the turbo keeps its spin. Without
  one the compressor **surges**: the boost pushes back through it and the turbo loses speed fast. Properties
  Tool: **Stiffness** (0-50 kPa, default 5), the extra boost it holds before opening. Up to 15 kPa it stops
  surge completely; stiffer springs let more and more surge through while they vent (at 50 the turbo surges
  almost as if there were no valve, with the valve's hiss on top), and **Sound effect** (see Sounds below); they reset to the defaults when the vehicle loads (the game's binary save format has no room for extra values). The large valve
  vents 2.5 times faster. Outputs `open` (0/1), `flow` (L/s) and `pressure` (kPa).
- **Turbo Wastegate (Small)** (1 cell, small pipe) and **(Large)** (2x2x2, large pipe, flows 2.5 times as much).
  Exhaust from the manifold goes in underneath; the side port goes to a vent or the turbo's exhaust outlet pipe. Its `open` input (0 to 1, from a data link or microcontroller) lets exhaust bypass the turbine,
  which caps boost. Its `antilag` input (1 on, 0 off) turns on antilag for the turbo on the same exhaust pipe:
  whenever the engine is running but its exhaust alone would let the turbo slow down (off throttle, idling), fuel
  burnt in the exhaust holds the turbo at 80% spool, with bangs and crackles from the wastegate. Wire it to a
  switch, or build your own logic in a microcontroller (only on a launch, only above some speed). Outputs `flow`
  (L/s) and `pressure` (kPa).

The valves start with a bright steel body and a red cap; paint the body and the cap separately. While you place
a turbo or valve, the preview shows these starting colours. The turbo's own `surge`
output is 1 while it surges.

## Upkeep: oil, air filter, heat and wear

A turbo wears out, and how fast depends on how it is looked after. Wear follows how hard it works: the
shaft's spool, which follows the engine's exhaust. Idling it barely wears; with the engine off it does not
wear at all. The times below are for full boost the whole time:

| Setup | Time to half condition |
| --- | --- |
| Oil fed and an air filter | about 20 hours |
| Oil fed, no filter | about 5 hours |
| Filter, no oil | about 1.5 hours |
| Neither | about 15 minutes |

- **Oil.** Pipe oil into the turbo's oil-in port and back out of the oil-out port. Whatever oil reaches the inlet
  passes through, up to 0.8 L/s, and leaves hotter; an oil cooler in the loop keeps it cool. At 0.5 L/s or more
  the oil cools the turbo fully. Polluted oil wears it faster, and the turbo ages its oil very slowly, so keep an oil
  filter in the loop. Water or fuel in the line does not count as oil.
- **Heat.** Exhaust heats the turbine and the shaft heats with speed (four times as much with no oil). With oil
  a turbo at full boost sits around 140 °C; dry it climbs to about 400 °C. Above 250 °C it wears faster, the
  hotter the faster.
- **Air filter.** The **Turbo Air Filter** (small pipe, 2x2x2) and **Turbo Air Filter (Large)** (large pipe,
  3x3x3) are cone filters. Pipe the neck to the turbo's intake. Clean air makes the turbo wear four times slower.
  A filter clogs slowly as it works, about 10 hours of full boost to go fully dirty, and a clogged filter costs up to 15% of the boost. Place a new one to replace it. A new
  filter starts with a red element and steel caps, and both paint.
- **Condition.** Down to half, the turbo performs like new. Below that its boost falls (to 60% near the end),
  and at 0 it has seized: no boost, and the shaft no longer turns. A new turbo is at 1.
- Wear is saved with each world, in `anyengineparts_wear.json` in the game's working folder, which the game copies
  into the save slot. A turbo moved to another vehicle (split or welded) starts fresh.

## Intercooler and hot boost air

Compressing air heats it: at full boost the air leaving the turbo is about 120 °C over the air outside. With
AnyEngineParts every engine, stock or the rotary, feels its intake air's temperature. The boost part of its power
scales with the air's density against 30 °C air, so hot boost makes less power and cold boost makes more. Boost
air over 90 °C also knocks a boosted engine, which slowly damages it. Without an intercooler, full boost
costs a damage step about every 6 to 7 minutes.

The **Turbo Intercooler** fixes that. Like the radiator, it has no pipes of its own: snap gas ports onto its faces,
small or large, wherever suits the build. Run one from the turbo's boost and one to the engine's air intake. All its
ports share one volume of air, so the boost passes straight through. It has an end tank on each side, like a car's,
and stretches like the radiator: wider one cell at a time (the tanks stay at the ends) and taller. A bigger core cools better, and so does slower air: a 3x2 core takes about
three quarters of the heat out of a 3x3 turbo's boost. The tanks paint.

## Sounds

Turbos and blow-off valves have their own sounds in place of the impeller hum, made by the mod as it plays:

- **Spool whistle**: a whine that rises with turbine speed, plus the rush of air and intake hiss. Each size has
  its own voice: the 2x2 screams high, the 5x5 whooshes deep.
- **Surge** (no blow-off valve, throttle lifted): "stu-stu-stu", quick puffs of air as boost pushes back through
  the compressor, slowing as the boost runs out. Bigger turbos puff deeper and slower.
- **Blow-off**: a burst when it opens, then quiet as the trapped boost runs out, even if you stay off the
  throttle. Pick the valve's sound in the Properties Tool (**Sound effect**): 0 whoosh, 1 flutter, 2 chirp,
  3 dump (deep). Each is voiced for the turbo venting through it: a 2x2's is short and sharp, a 5x5's long
  and deep.

- **Antilag**: irregular gunshot bangs and crackles from the wastegate while antilag fires; deeper on bigger turbos.

The wastegate is otherwise silent. Sounds follow the vehicle volume slider and are heard by every player near the part.
With AnyHelpers installed, **Mod Settings > AnyEngineParts > Turbo volume** sets how loud all of them are, from 0 to
200 % (100 is normal). It applies to your game only.

## Tooltip

Looking at a turbo shows its intake flow, turbine speed and boost (the pressure it adds over the intake), in the
game's own units, then its **condition** (100 % new, 0 % seized) and **temperature** (see Upkeep).

## Data channels

Wire them from a data port, a data link or a microcontroller. Pressures use the game's kPa scale (9.8 per unit).

| Channel | Direction | Value |
| --- | --- | --- |
| `intake_pressure` | Output | Pressure at the cold outlet, kPa (absolute: 9.8 is the air outside) |
| `boost` | Output | Boost over the air outside, kPa (full boost is about 88) |
| `boost_psi` | Output | The same in psi (full boost is about 12.8), for a boost gauge |
| `exhaust_pressure` | Output | Pressure at the hot inlet, kPa |
| `power_multiplier` | Output | The engine power multiplier this boost gives, 1 to 2 |
| `spool` | Output | Shaft spool, 0 to 1 |
| `rpm` | Output | Shaft speed |
| `exhaust_flow`, `intake_flow` | Output | Hot-side and cold-side flow, L/s |
| `exhaust_temperature` | Output | Gas temperature at the hot inlet, °C |
| `surge` | Output | 1 while the compressor surges (boost pushing back, no blow-off valve venting), else 0 |
| `antilag` | Output | 1 while antilag is firing to hold the spool, else 0 |
| `oil_flow` | Output | Oil through the turbo, L/s (0.5 or more cools it fully) |
| `turbo_temperature` | Output | The turbo's temperature, °C (above 250 it wears faster) |
| `condition` | Output | 1 new, 0 seized; below 0.5 it loses boost |
| `air_filter` | Output | 1 with an air filter on the intake, else 0 |
| `boost_air_temperature` | Output | The boost air after any intercooler, °C (what the engine breathes) |
| `boost_limit` | Input | A wastegate: caps boost at this pressure in kPa (0 or unwired: no cap) |

## Getting it

- **Survival:** world loot at tech tier 3 in the gas category, like the stock impellers.
- **Sandbox and Creative:** with the gas parts.

## How it works

- The four part definitions are built in the DLL ([`turbo_logic.h`](../../native/turbo_logic.h)). At start-up they
  are written to `AnyAPI and Modding/Turbo/turbochargers.json` and added to the game's vehicle components right
  after the game adds its own. Their ids are `turbocharger_2` to `turbocharger_5`, class `gas_pump_centrifugal`
  (the stock impeller's), with four gas logic nodes instead of two.
- The meshes are built at start-up from the game's `rom/meshes/components/turbo_a.mesh` and written beside it as
  `turbo_turbocharger_N.mesh` ([`turbo_mesh.h`](../../native/turbo_mesh.h)). If the stock mesh is not in the
  expected format, that size uses the plain impeller look.
- The blow-off valve meshes are built from simple shapes at start-up ([`turbo_valve_mesh.h`](../../native/turbo_valve_mesh.h))
  and written beside them as `turbo_turbo_blow_off_valve(_large).mesh`. A valve joins its two gas nodes into one
  pipe the way a stock pipe bracket does: the base `get_gas_flood_nodes` is hooked and answers for the valves with
  the bracket's own function. If that is not found, the valve evens out its two sides every tick instead.
- The impeller's server create, destroy, tick, tooltip and `get_data_f64` are hooked. When a turbo is created, the mod
  creates its hot side with the game's own functions: two gas contents and two gas nodes registered in the
  vehicle, so pipes and pressure treat them like any other port. Stock impellers are untouched.
- Each tick the turbo lets exhaust through its hot side in proportion to the pressure drop, spools from that
  flow with its size's lag, and runs its compressor with a head set by the spool (full spool gives the engine's
  full boost).
- Sounds: the server packs each turbo's spool and surge (and each blow-off valve's venting) into the impeller's
  own replicated speed value. The client impeller tick, which only plays the hum, is hooked for the mod's parts:
  it synthesizes 48 kHz audio ([`turbo_sound.h`](../../native/turbo_sound.h)) and streams it into a positional
  voice effect, the way the game plays voice chat. If the hooks are not found, the parts keep the hum.
- Oil ports are made like the stock fuel manifold's: two liquid contents and nodes for the turbo's two liquid
  logic nodes, which sit after its four gas nodes so saved pipes keep their meaning. The upkeep model is in
  [`turbo_health.h`](../../native/turbo_health.h), the wear file in [`turbo_wear_store.h`](../../native/turbo_wear_store.h).
- The air filter is a stock gas vent class with its own mesh; the turbo finds it on its intake pipe. The intercooler
  is a stock gas tank class with the radiator's stretch keys, port surfaces and three mesh tiles
  ([`turbo_upkeep_mesh.h`](../../native/turbo_upkeep_mesh.h)). The turbo finds it on its boost pipe through the
  gas ports' shared content, and sets the temperature of the air it pushes.
- Hot boost air: the server engine tick and `math.remap` are hooked. The engine tick notes its air manifolds'
  temperature; the one remap call it makes for its boost factor, `remap(pressure, 1, 10, 1, 2)`, is scaled for
  that temperature, and knock is added to the engine's own damage counter afterwards. If the hooks are not found,
  engines ignore intake temperature as before.
- The hot side's gas is not saved; it refills from the pipes after loading. Saves that contain a turbo need
  AnyEngineParts installed to load it.

Source: [`turbo_mod.cpp`] (built into `AnyEngineParts.dll`)(../../native/turbo_mod.cpp), bindings in
[`turbo_bindings.h`](../../native/turbo_bindings.h), tests in
[`tests/turbo_logic_test.cpp`](../../native/tests/turbo_logic_test.cpp).
