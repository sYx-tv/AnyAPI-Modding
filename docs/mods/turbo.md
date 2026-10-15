# Turbo (AnyEngineParts)

> The turbo parts are part of [AnyEngineParts](engine-parts.md). They hook game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> AnyEngineParts 0.1.0 is a prototype: it needs AnyAPI 0.37.0 or newer on that build. **Single player only** for now.

Turbo adds two exhaust-driven turbochargers, **Turbo 3x3** and **Turbo 5x5**, and two belt-driven
[superchargers](#superchargers), **Supercharger (Small)** and **Supercharger (Large)**. The Turbo 2x2 and 4x4 are
retired: vehicles that have them keep them and they work as before, but they no longer show in the part lists or
loot. Each turbo is N cells wide, N tall and 2 thick. The front cell is the stock impeller's compressor housing, scaled to the size; the back cell is
the turbine housing, the same impeller mirrored. A new turbo starts with a steel cold side and a cast-iron hot side,
and the paint tool colours each side on its own, like any stock part. Every opening on the housing sits exactly where
its pipe joins: on the 4x4 and 5x5 the boost outlet and the turbine inlet are large pipes that span both cells, so a
cast flange block carries them across the top and bottom corners.

## Hooking one up

| Port | Where | Connect to |
| --- | --- | --- |
| Intake in (cold side) | Middle of the front face (the 2x2: its lower near cell; the 5x5: just above and beyond the centre, where its large pipe fits) | Nothing needed: it draws from the open air. A pipe here can only add air. |
| Boost out (cold side) | Top far corner, facing sideways (4x4 and 5x5: across both cells, the cell below the top) | The engine's air intake |
| Exhaust in (hot side) | Bottom near corner, facing the other way (4x4 and 5x5: across both cells) | The engine's exhaust |
| Exhaust out (hot side) | Middle of the back face, opposite the intake | A gas vent (the tailpipe) |
| Oil in, oil out | Underneath, a brass-ringed port under each housing (the 2x2: under the far row) | The engine's oil, through an oil cooler (see [Upkeep](#upkeep-oil-air-filter-heat-and-wear)) |

Exhaust flowing from the hot inlet to the hot outlet spins the shaft. The shaft's spool sets how much pressure
the cold side pushes into the engine's intake. Engines already turn intake pressure into power: ambient gives
1x, and an intake pressure of 98 kPa (the game's scale) gives the full 2x. One turbo stops there; two in a row
(a [compound](#twin-and-compound-turbos)) can take it up to 3x.

Turbos 4x4 and 5x5 use large pipes; 2x2 and 3x3 use small pipes. No gas tank is needed. The compressor inlet is open to the air like a real turbo intake.

**Make the turbo the only way in and out.** The game evens out the pressure across everything joined by pipes at
once, every tick, and a stock vent passes far more gas than a turbo: about 15 to 900 L/s for each unit of pressure,
depending on its size, against 5 to 33 L/s through a turbine. So:

- The engine's air manifolds get their air only from the turbo's boost outlet. A vent or air filter on that side lets
  the boost straight out (put the air filter on the turbo's intake instead).
- The engine's exhaust manifolds go only to the turbo's exhaust inlet, and the tailpipe vent goes on the turbo's
  exhaust out. A vent before the turbine lets the exhaust skip it, and the turbo never spools.
- This counts for every air and exhaust manifold on the engine: one more manifold with its own vent does the same.
- A wastegate is the one bypass that belongs there: it opens only as far as you tell it to.

Big engines need big turbos. A turbo much smaller than its engine chokes the exhaust: the back pressure takes away
much of what the boost adds (a 4-rotor Rotary wants a 5x5 or two 4x4s).

## Twin and compound turbos

**Twin turbos** (side by side): join the engine's exhaust to both hot inlets and both boost outlets to the engine's
air intake. The game evens out an engine's exhaust across all its exhaust manifolds and everything piped to them, so
the two turbines share it evenly. Two 3x3s spool sooner than one 5x5 and make about the same boost on an engine that
feeds them both fully (44 L/s of exhaust or more). One blow-off valve on the shared boost pipe serves both.

**Compound turbos** (one after the other): pipe the first turbo's boost outlet into the second turbo's intake, and the
second turbo's boost outlet to the engine. The second turbo breathes the first one's boost and adds its own on top,
so together they pass the 2x one turbo can give: the engine's power carries on rising with intake pressure up to 3x
at 186 kPa (19 on the game's scale), and no further. An intercooler between the two (and one after the second) keeps
the air cool: the second turbo heats air that is already hot.

- Split the exhaust between the two turbines, as for twins. Running it through one turbine and then the other works
  too, but backs the exhaust up into the engine, which costs power.
- Two of the same size work best. In testing on the bench model, two 3x3s on 50 L/s of exhaust reached about 160 kPa,
  2.66x.
- The air filter goes on the first turbo's intake; the second turbo counts it as its own.
- A `boost_limit` on the second turbo caps the total boost at its outlet; on the first, the boost between the two.
- The log says which turbo found which: `compound second stage, breathing the boost of Turbo ...`.

## Superchargers

A supercharger is a centrifugal compressor driven by the engine through a belt, so it has no lag: its boost follows
the engine's speed the moment it changes. **Supercharger (Small)** is 2x2x2 with a small boost outlet; **Supercharger
(Large)** is the same unit at twice the size, 4x4x4, with a large outlet. Both are a satin cast head unit: a ribbed
snail-shell housing on the front and a finned gear case behind it. Paint colour 1 sets the housing, colour 2 the gear
case.

| Port | Where | Connect to |
| --- | --- | --- |
| Intake | Middle of the front face, a large pipe on both sizes | Nothing needed: it draws from the open air (an air filter goes here) |
| Boost out | On top, rising off the snail | The engine's air intake, or a turbo's intake to twincharge |
| Torque spot | The back face, bottom corner, on the input shaft | A stock **Pulley Wheel**, belted to an Engine Wheel |
| Data ports | Either side | The same channels as a turbo, plus `drive_power` |

Hook it up like an Impeller: put a Pulley Wheel on the torque spot and run a belt from it to an Engine Wheel on the
engine with the belt tool. The belt turns the pulley 2.5, 3.875 or 6.675 times as fast as the engine on the small,
medium or large Engine Wheel, and the boost goes with the square of the pulley's speed. The Engine Wheel is how you
tune it, the way a real supercharger is tuned by its pulley:

| Engine Wheel | Full boost (98 kPa, 2x power) from | Boost at the 8,000 RPM fuel cut |
| --- | --- | --- |
| Small | never: about half boost at the fuel cut | about 54% |
| Medium | 7,000 RPM | full |
| Large | about 4,000 RPM | full |

Squeezing the air takes work, and the belt takes it from the engine: about the air's flow times its boost, over 55%
efficiency. At full boost the small one takes about 2 kW and the large about 6 kW, so the engine revs a little slower
than its power alone would give. The **Dyno** shows power after that cost, and `drive_power` reads it in kW.

- No oil, no wear and no heat of its own: its gear case carries its own oil. An air filter on its intake still keeps
  the dust out of the engine's air, and clogs with use like a turbo's.
- Off throttle at speed, boost pushes back through it in surges like a turbo's. A blow-off valve on its boost pipe
  takes that, and `boost_limit` caps its boost the same way.
- It heats the air it squeezes like a turbo does, so an intercooler on its boost pipe pays off.
- **Twincharging**: pipe the supercharger's boost into a turbo's intake (or a turbo's into the supercharger's). The
  supercharger gives boost from low RPM while the turbo spools, and the two add up past 2x like a
  [compound](#twin-and-compound-turbos).
- Without a belt (or with the game functions it needs not found), it passes air like a stopped impeller and makes
  no boost.

## Sizes and lag

| Size | Exhaust for full boost | Spool-up time | Shaft speed at full boost |
| --- | --- | --- | --- |
| 3x3 | 22 L/s | about 0.9 s | 160,000 rpm |
| 5x5 | 65 L/s | about 2.4 s | 90,000 rpm |
| 2x2 (retired) | 10 L/s | about 0.5 s | 220,000 rpm |
| 4x4 (retired) | 40 L/s | about 1.5 s | 120,000 rpm |

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
- **Air filter.** The **Turbo Air Filter** (small pipe, 3x3x3) and **Turbo Air Filter (Large)** (large pipe,
  4x4x4) are cone filters, the pipe in the middle of the bottom. Pipe the neck to the turbo's intake. Clean air makes the turbo wear four times slower.
  A filter clogs slowly as it works, about 10 hours of full boost to go fully dirty, and a clogged filter costs up to 15% of the boost. Place a new one to replace it. A new
  filter starts with a red element and steel caps, and both paint.
- **Condition.** Down to half, the turbo performs like new. Below that its boost falls (to 60% near the end).
  At 0% it **blows up**: a bang, shrapnel and a hiss of smoke, and both housings go sooty. A blown turbo makes no boost
  and its wrecked compressor chokes the engine's air: the engine still runs through it, but gets only about a third of
  the air it wants at full revs (for an engine matched to the turbo), so it idles and crawls in limp mode. Replace the
  turbo (a new one starts at 100%) to get the power back. The tooltip shows the condition (see below).
- Wear is saved with each world, in `anyengineparts_wear.json` in the game's working folder, which the game copies
  into the save slot. A turbo moved to another vehicle (split or welded) starts fresh.

## Intercooler and hot boost air

Compressing air heats it: at full boost the air leaving the turbo is about 120 °C over the air outside. With
AnyEngineParts every engine, stock or the rotary, feels its intake air's temperature. The boost part of its power
scales with the air's density against 30 °C air, so hot boost makes less power and cold boost makes more. Boost
air over 90 °C also knocks a boosted engine, which slowly damages it. Without an intercooler, full boost
costs a damage step about every 6 to 7 minutes.

An intercooler fixes that. There are two, which work the same and differ only in looks: the **Turbo Intercooler
(Race Core)**, a black core with bright fins and an inlet stub on each tank, and the **Turbo Intercooler (Round
Tank)**, a slim silver core between round tanks. Like the radiator, it has no pipes of its own: snap gas ports onto its faces,
small or large, wherever suits the build. Run one from the turbo's boost and one to the engine's air intake. All its
ports share one volume of air, so the boost passes straight through. It is 3 cells wide, 2 tall and 1 deep, and the
model fills every cell of it. It has an end tank on each side, like a car's, and grows like the radiator: wider and
taller one cell at a time. The tanks stay at the ends and the inlet stubs and rails keep their shape at any size.
A bigger core cools better, and so does slower air: a 3x2 core takes about three quarters of the heat out of a 3x3
turbo's boost. The tanks paint.

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
- **Blowing up**: once, when a turbo wears out: a crack and a boom, shrapnel pinging, the wheel screeching to a stop
  and a hiss of boost and smoke. A turbo that was already blown when you load in stays quiet.
- **Supercharger whine**: the buzz of its straight-cut step-up gears, there from idle and sliding up with the engine,
  the impeller's thin whistle above it and the gulp of air. It fades to silence when the belt stops. The large one
  sings lower. Off the throttle it surges like a turbo unless a blow-off valve on its outlet pipe vents the boost, just
  as real centrifugal superchargers need a bypass valve.

The wastegate is otherwise silent. Sounds follow the vehicle volume slider and are heard by every player near the part.
With AnyHelpers installed, **Mod Settings > AnyEngineParts > Turbo volume** sets how loud the turbo sounds, blow-off
valves and antilag are, and **Supercharger volume** the supercharger whine, each from 0 to 200 % (100 is normal).
They apply to your game only.

## Tooltip

Looking at a turbo shows its intake flow, turbine speed and boost (the pressure it adds over the intake), in the
game's own units, then its **condition** (100% new, 0% blown, in whole percent like the stock filters; a worn turbo
reads at least 1% until it blows, and a blown one shows 0% in the game's warning colour) and **temperature** (see Upkeep).
A supercharger shows its flow, impeller speed and boost.

## Data channels

Wire them from a data port, a data link or a microcontroller. Pressures use the game's kPa scale (9.8 per unit).

| Channel | Direction | Value |
| --- | --- | --- |
| `intake_pressure` | Output | Pressure at the cold outlet, kPa (absolute: 9.8 is the air outside) |
| `boost` | Output | Boost over the air outside, kPa (full boost is about 88; a compound's second turbo reads the total) |
| `boost_psi` | Output | The same in psi (full boost is about 12.8), for a boost gauge |
| `exhaust_pressure` | Output | Pressure at the hot inlet, kPa |
| `power_multiplier` | Output | The engine power multiplier this boost gives, 1 to 2 (up to 3 on a compound's second turbo), after what hot boost air costs |
| `spool` | Output | Shaft spool, 0 to 1 |
| `rpm` | Output | Shaft speed |
| `exhaust_flow`, `intake_flow` | Output | Hot-side and cold-side flow, L/s |
| `exhaust_temperature` | Output | Gas temperature at the hot inlet, °C |
| `surge` | Output | 1 while the compressor surges (boost pushing back, no blow-off valve venting), else 0 |
| `antilag` | Output | 1 while antilag is firing to hold the spool, else 0 |
| `oil_flow` | Output | Oil through the turbo, L/s (0.5 or more cools it fully) |
| `turbo_temperature` | Output | The turbo's temperature, °C (above 250 it wears faster) |
| `condition` | Output | 1 new, 0 blown; below 0.5 it loses boost |
| `air_filter` | Output | 1 with an air filter on the intake, else 0 |
| `boost_air_temperature` | Output | The boost air after any intercooler, °C (what the engine breathes) |
| `drive_power` | Output | Supercharger: the power its belt takes from the engine, kW (0 on a turbo) |
| `boost_limit` | Input | A wastegate: caps boost (as the `boost` output reads it) at this pressure in kPa (0 or unwired: no cap) |

## Getting it

- **Survival:** world loot at tech tier 3 in the gas category, like the stock impellers (turbos and superchargers).
- **Sandbox and Creative:** with the gas parts.

## How it works

- The part definitions are built in the DLL ([`turbo_logic.h`](../../native/turbo_logic.h)). At start-up they
  are written to `AnyAPI and Modding/Turbo/turbochargers.json` and added to the game's vehicle components right
  after the game adds its own. The turbos' ids are `turbocharger_2` to `turbocharger_5`, class `gas_pump_centrifugal`
  (the stock impeller's), with four gas logic nodes instead of two; the superchargers' are `supercharger_small` and
  `supercharger_large`, the same class with its two. Their meshes are built from simple shapes
  ([`turbo_supercharger_mesh.h`](../../native/turbo_supercharger_mesh.h)).
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
- Every half second each turbo walks the pipes joined to its intake and boost outlet, across inline blow-off valves and
  intercoolers. A turbo whose intake pipe holds another turbo's boost outlet is that turbo's second stage: it takes in
  the first stage's air and temperature instead of the outside air's.
- The air filter is a stock gas vent class with its own mesh; the turbo finds it on its intake pipe. The intercooler
  is a stock gas tank class, one cell deep like the radiator, with nine mesh tiles the game repeats along its width
  and height ([`turbo_upkeep_mesh.h`](../../native/turbo_upkeep_mesh.h)). The game lays tiles out from the part's
  cell 0 whatever its zone says, and the intercooler's cells start at -1, so its first tile is two cells wide. The turbo finds it on its boost pipe, past an inline
  blow-off valve too, through the gas ports' shared content (the tank's own `add_mating_component` gives a snapped-on
  port's node the tank's content) or the part a port is snapped onto, and sets the temperature of the air it pushes.
  A boosting turbo that finds none logs the parts on its boost pipe once a minute (`no intercooler on its boost pipe`).
- Hot boost air: the server engine tick and `math.remap` are hooked. The engine tick notes its air manifolds'
  temperature; the one remap call it makes for its boost factor, `remap(pressure, 1, 10, 1, 2)`, is scaled for
  that temperature, and knock is added to the engine's own damage counter afterwards. Past the game's 10 (2x) the
  factor carries on to 3x at 19, which only a compound reaches. If the hooks are not found,
  engines ignore intake temperature as before.
- Superchargers are the impeller class with its own two gas nodes and its torque node. Their tick is the mod's: it
  reads the shaft's speed (`vehicle_torque_node.get_rps`), sets the head from it, pushes the air, and puts the work
  that took back on the shaft as a drag torque (`apply_torque`, times the torque system's newtons per joule), as the
  stock impeller does with its own, smaller, drag. Every half second each turbo and supercharger also notes what its
  boost pipe feeds (an engine's air manifold, which the engine tick hook records, or the next stage's intake); the
  Dyno names the boost setup from that.
- The retired 2x2 and 4x4 turbos keep their definitions (the game drops a saved part whose definition is gone) with
  `tech_tier` -1 and `spawn_probability` 0, which keeps them out of the part lists and loot.
- The hot side's gas is not saved; it refills from the pipes after loading. Saves that contain a turbo need
  AnyEngineParts installed to load it.

Source: [`turbo_mod.cpp`](../../native/turbo_mod.cpp) (built into `AnyEngineParts.dll`), bindings in
[`turbo_bindings.h`](../../native/turbo_bindings.h), tests in
[`tests/turbo_logic_test.cpp`](../../native/tests/turbo_logic_test.cpp).
