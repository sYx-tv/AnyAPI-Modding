# Turbo (AnyEngineParts)

> The turbo parts are part of AnyEngineParts, inside the [AnyVehicleOverhaul](vehicle-overhaul.md) download. They hook game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> **Single player only** for now.

Turbo adds two exhaust-driven turbochargers, **Turbo 3x3** (3x3x2) and **Turbo 6x6** (6x6x2), the faces of the
stock Small and Large Impellers, and two belt-driven [superchargers](#superchargers), **Supercharger (Small)** and
**Supercharger (Large)**. The Turbo 2x2, 4x4 and 5x5 are gone: the superchargers took the small ones' place and the
5x5 grew into the 6x6 (the stock Large Impeller's size), so all four of its large pipes sit in the middle of their
faces. A saved vehicle that had one loads without it. Each size comes in three looks that work the same: **G-Series** (steel, black
wastegate can), **Precision** (black, red can) and **EFR** (dark cast, blue can, a heat shield over the turbine). Each
has a big compressor mouth with the wheel showing, a turbine housing on the back, a built-in wastegate and a cast
foot under the compressor that mounts on the bottom face. Paint colour 1 sets the compressor housing, colour 2 the
wastegate can. Every opening sits exactly where its pipe joins.

Every look also comes as a **Twin Scroll** version (id ends `_twin`): a divided turbine housing with a split flange.
It keeps the exhaust pulses apart, so the turbine gets more out of light exhaust: it spools about 20-30% quicker, makes
noticeably more boost low in the revs, and its best pressure ratio is 3% lower at the very top.

## Hooking one up

| Port | Where | Connect to |
| --- | --- | --- |
| Intake in (cold side) | Middle of the front face | Nothing needed: it draws from the open air. A pipe here can only add air. |
| Boost out (cold side) | Top far corner, facing sideways (6x6: across both cells, the cell below the top) | The engine's air intake |
| Exhaust in (hot side) | Bottom near corner, facing the other way (6x6: across both cells) | The engine's exhaust |
| Exhaust out (hot side) | Middle of the back face, opposite the intake | A gas vent (the tailpipe) |
| Oil in, oil out | Underneath, a brass-ringed port under each housing | The engine's oil, through an oil cooler (see [Upkeep](#upkeep-oil-air-filter-heat-and-wear)) |

Exhaust expanding through the turbine (hot inlet to hot outlet) spins the shaft, and the compressor on the same
shaft pushes air into the engine's intake with a boost that climbs with the square of the shaft's speed. Engines
already turn intake pressure into power: ambient gives 1x, and an intake pressure of 98 kPa (the game's scale) gives
the full 2x, and past that power keeps rising with boost: there is **no boost cap** (AnyVehicleOverhaul 2.0). A
built-in wastegate holds 88 kPa (12.8 psi) out of the box and sends the exhaust the shaft does not need round the
turbine. Turn it up with a boost target (see [Turning up the boost](#turning-up-the-boost)); what the engine can take
is set by its own rating.

## Turning up the boost

- **Boost target:** wire a psi value into a turbo's `boost_target_psi` input (0 or unwired: the wastegate spring's
  12.8 psi). The turbo holds that boost if its exhaust can spin it fast enough.
- **Boost Controller** (a 1-cell sensor part): sets the target for every turbo on its vehicle. Wire a psi value into
  its `target_psi` (a microcontroller, a dial or a switch; 0 keeps the springs). `scramble` above 0.5 adds
  `scramble_psi` (default 5) on top, for a push-to-pass button. A turbo's own `boost_target_psi` input wins over it.
  Outputs `boost_psi` and `target_set`.
- **Boost Gauge** reads the vehicle's highest boost (`boost_psi`, `boost_kpa`, `boost_bar`), the target
  (`target_psi`) and the peak (`peak_psi`; `reset_peak` above 0.5 clears it). **Oil Gauge** reads the hottest
  engine's `oil_pressure_psi`, `oil_temperature`, `oil_level` (0-1; under 0.5 the engine wears), `coolant_temperature`
  and `engine_temperature`. The game has no oil pump, so oil pressure is worked out like a real gauge reads: it rises
  with RPM, reads high on cold oil and low on very hot oil, and drops off with a low sump. Wire either to a display
  or a microcontroller. All three drop as electronics loot after the 2nd bunker.
- **Each size has a ceiling:** the 3x3's compressor tops out at a pressure ratio of about 3.3 (about 34 psi), the
  6x6's at about 4.2 (about 47 psi). Past the spring boost the shaft needs much more exhaust energy, so big boost
  needs a big engine at high RPM, and it lags more.
- **Airflow decides:** engines draw air by size and RPM. Twins share the exhaust and the air. Match the total turbine
  size to the engine: in the sim, a large V12 holds about 39 psi on four 6x6s; on one it backs up its exhaust
  (losing more power than the boost adds), and on six each turbine gets too little exhaust. Compounds go further.
- **Back pressure** still costs power the game's way: above 39 kPa (4 on the game's scale) of exhaust pressure before
  the turbines.

### Twin screw supercharger

`supercharger_twin_screw`, tier 4 loot. A positive displacement blower: it moves a set amount of air every turn, so its
boost is what it moves over what the engine swallows. That comes in from low RPM and stays flat to the redline. Stretch
it to fit the engine (2 to 16 cells wide and long, 2 or 3 tall): a bigger blower makes more boost. Belt it from an
Engine Wheel like the centrifugal ones; inside it has a 2.5:1 drive, so the small, medium and large wheels turn its
rotors at 1.0, 1.55 and 2.67 times engine speed. Outlet: the large pipe on the bottom, to the air manifold. Intake: the
large pipe on the back face (an air filter fits). Its bypass opens off throttle, a relief valve stops it near 29 psi,
and a Boost Controller caps it lower. It compresses inside the rotors, so it runs cooler and takes less belt power
than a Roots would. A Roots blower is written but on hold.

### Engine ratings and knock

Every engine has a rating, the boost factor (`power_multiplier`) it takes with 40 °C charge air before it knocks:

| Engine | Rating |
| --- | --- |
| Small / medium / large block | 2.0 / 2.5 / 3.0 |
| Inline | +0.15 (the stoutest block) |
| V | +0 |
| Boxer | -0.1 (long crank) |
| Each cylinder past 6 | -0.02 (down to -0.2) |
| Rotary | 2.3 |
| Built engine | stock + 1.0 (about 13 psi more) |

Hot charge air lowers the rating: 98 °C (one turbo, no intercooler) takes about 40% of the boost part off it, so
intercoolers matter. Past the rating the knock control pulls timing: only 30% of the extra boost still makes power,
and the engine takes damage, faster the further past it runs (10% over: a damage step about every 9 minutes; 30%
over: about every 100 s; 60% over: about every 30 s). An engine that is still knocking 25% or more past its rating
when its last damage step lands **blows**: it makes no power until you repair it. The log names the engine's rating
on its `engine intake` line.

**Built engines** are forged versions of the nine stock engines (inline, V and boxer, small, medium and large): the
same engine in every way (power, fuel, size, ports, sound) with a rating 1.0 higher. They are their own parts
(`built_` + the stock id, for example `built_v_engine_c`), tier 4 loot at 0.075. The mod makes them from the game's
own engine entries when the game starts, so they follow game updates.

The Turbo 6x6 uses large pipes and the 3x3 small pipes. No gas tank is needed. The compressor inlet is open to the air like a real turbo intake.

**Make the turbo the only way in and out.** The game evens out the pressure across everything joined by pipes at
once, every tick, and a stock vent passes far more gas than a turbo: about 15 to 900 L/s for each unit of pressure,
depending on its size, against 5 to 33 L/s through a turbine. So:

- The engine's air manifolds get their air only from the turbo's boost outlet. A vent or air filter on that side lets
  the boost straight out (put the air filter on the turbo's intake instead).
- The engine's exhaust manifolds go only to the turbo's exhaust inlet, and the tailpipe vent goes on the turbo's
  exhaust out. A vent before the turbine lets the exhaust skip it, and the turbo never spools.
- This counts for every air and exhaust manifold on the engine: one more manifold with its own vent does the same.

Big engines need big turbos. A turbo much smaller than its engine chokes the exhaust: the back pressure takes away
much of what the boost adds (a 4-rotor Rotary wants a 6x6).

## Twin and compound turbos

**Twin turbos** (side by side): join the engine's exhaust to both hot inlets and both boost outlets to the engine's
air intake. The game evens out an engine's exhaust across all its exhaust manifolds and everything piped to them, so
the two turbines share it evenly, so each gets half. On a 4-rotor, one 6x6 reaches full boost a little sooner than two
3x3s (about 1 s against 2 s, flooring it at 3,500 RPM) and backs up less exhaust; two 6x6s each get too little and lag
(about 3 s, and only to about 84 kPa). One blow-off valve on the shared boost pipe serves both.

**Compound turbos** (one after the other): pipe the first turbo's boost outlet into the second turbo's intake, and the
second turbo's boost outlet to the engine. The second turbo breathes the first one's boost and adds its own on top,
so together they pass the 2x one turbo gives on its spring: the engine's power carries on rising with intake
pressure (two stages on their springs make about 23 on the game's scale, 3.4x), with no cap. An intercooler between the two (and one after the second) keeps
the air cool: the second turbo heats air that is already hot.

- **Run the exhaust through both turbines in a row** for quick boost, the way real compounds are plumbed: from the
  engine into the second turbo's exhaust inlet, out of it into the first turbo's exhaust inlet, then the tailpipe.
  Splitting the exhaust between the two (as for twins) backs up a little less exhaust, but each turbine gets half, so
  the boost takes several seconds to build.
- On a 4-rotor, two 6x6s in a row reached about 160 kPa (2.7x) 2 s after flooring it at 4,000 RPM in testing with the
  game's engine model; split, they took about 6 s to get there. A 3x3 as the second stage chokes a 4-rotor's exhaust
  (the back pressure costs more power than the boost adds). On a 2-rotor, a 6x6 first stage and a 3x3 second stage
  worked best: about 155 kPa, 2.5x.
- The second turbo turns slower than the first (its `spool` reads about 0.6 at full boost): breathing boosted air, it
  makes more boost for its speed.
- The air filter goes on the first turbo's intake; the second turbo counts it as its own.
- A `boost_limit` on the second turbo caps the total boost at its outlet; on the first, the boost between the two.
- The log says which turbo found which: `compound second stage, breathing the boost of Turbo ...`.

## Superchargers

A supercharger is a centrifugal compressor driven by the engine through a belt, so it has no lag: its boost follows
the engine's speed the moment it changes. **Supercharger (Small)** is 2x2x2 with a small boost outlet; **Supercharger
(Large)** is the same unit at twice the size, 4x4x4, with a large outlet. Both look like a ProCharger head unit: a polished
snail-shell housing on the front and a black finned gear case behind it. Paint colour 1 sets the housing, colour 2 the gear
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

| Engine Wheel | Full boost (88 kPa, 2x power) from | Boost at the 8,000 RPM fuel cut |
| --- | --- | --- |
| Small | never: about half boost at the fuel cut | about 54% |
| Medium | 7,000 RPM | full |
| Large | about 4,000 RPM | full |

Like any centrifugal compressor, it makes most of its boost near the top of its speed: on the medium Engine Wheel it
gives about a quarter of full boost at 3,500 RPM. For boost from the middle of the rev range use the large Engine
Wheel, or twincharge it with a turbo. A Roots or screw supercharger, whose boost is flat from low revs, is a different
machine that AnyEngineParts does not have.

Squeezing the air takes work, and the belt takes it from the engine: the work of compressing it (the compressor is
about 70% efficient where it runs) plus 5% in its gears. At full boost and full flow the small one takes about 1.7 kW
and the large about 4.6 kW, so the engine revs a little slower than its power alone would give. The **Dyno** shows
power after that cost, and `drive_power` reads it in kW.

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

Lag is the shaft's, not a timer: the exhaust has to spin the turbine and compressor wheels up to speed, and the boost
climbs with the square of that speed. More exhaust (more revs, a bigger engine, more throttle) spins them up faster,
and a bigger turbo's heavier shaft needs more of it. Off throttle the shaft coasts down on its own; surge (no blow-off
valve) slows it hard.

| Size | Turbine sized for | Shaft speed at full boost | Suits | 90% boost after flooring it at 3,500 RPM |
| --- | --- | --- | --- | --- |
| 3x3 | 22 L/s of exhaust | 160,000 rpm | a 1- or 2-rotor, or a short small engine | about 1.5 s on a 2-rotor, 3 s on a 1-rotor |
| 6x6 | 65 L/s of exhaust | 90,000 rpm | a 3- or 4-rotor, or a long small engine | about 1.4 s on a 4-rotor |

Pick the size whose turbine flow is close to your engine's exhaust L/s. A matched turbo gives some boost from about
2,500 RPM and full boost soon after you floor it, well before the redline. From there its built-in wastegate holds
full boost (88 kPa) to the redline and sends the spare exhaust round the turbine, which keeps back pressure down; the
`wastegate` output reads how far it is open. The ECU's boost target (or `boost_target_psi`) sets what it holds.

A turbo that is too small for its engine chokes it: it spools fast, but the exhaust backs up (a 3x3 on a 4-rotor holds
5 to 8 units, 49 to 76 kPa), and above 39 kPa of exhaust pressure the engine loses power, as it does with any
restricted exhaust. A turbo that is too big barely spools: a 6x6 on a 2-rotor makes about 37 kPa at 2,500 RPM and
needs about 5 s to reach full boost at 3,500.

## Blow-off valve

A companion part, in the gas parts with the turbos:

- **Turbo Blow-Off Valve (Small)** (1 cell, small pipe, for the 3x3) and **(Large)** (2x2x2, large
  pipe, for the 6x6). Inline: pipe the turbo's boost outlet into its **plain end** (it shows as *Gas
  node*) and its **blue-ringed end** (*Air node*) into the engine's air intake. Boost passes straight through.
  When you lift off the throttle, boost trapped in the pipe is higher than the turbo can hold; the valve vents it
  out of the vent slots in its crown, so the turbo keeps its spin. Without
  one the compressor **surges**: the boost pushes back through it and the turbo loses speed fast. Properties
  Tool: **Stiffness** (0-50 kPa, default 5), the extra boost it holds before opening. Up to 15 kPa it stops
  surge completely; stiffer springs let more and more surge through while they vent (at 50 the turbo surges
  almost as if there were no valve, with the valve's hiss on top), and **Sound effect** (see Sounds below); they reset to the defaults when the vehicle loads (the game's binary save format has no room for extra values). The large valve
  vents 2.5 times faster. Outputs `open` (0/1), `flow` (L/s) and `pressure` (kPa).
- **No wastegate part** (removed in the AnyVehicleOverhaul dev build): every turbo has a built-in wastegate. Set its
  boost with the ECU's boost target. Antilag and launch control (two-step) are rows on the ECU (see
  [AnyVehicleOverhaul](vehicle-overhaul.md)). A saved vehicle loses any old wastegate part when it loads.

The blow-off valves are HKS-style, with a flared trumpet top; they start with a black body and a red band, and both paint. While you place
a turbo or valve, the preview shows these starting colours. The turbo's own `surge`
output is 1 while it surges.

### Launch control (two-step)

Launch control lives on the ECU: set **Launch RPM** and turn **Launch** on (or wire its input to a clutch or gear
switch). The engine holds at that RPM however far you press the throttle. The fuel cuts at the RPM and comes back
150 RPM below, so it bounces on the limiter like a real launch control. With the ECU's **Antilag** on as well, the
cut fuel goes into the exhaust and the turbo is spooled (with bangs) before you go.

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
- **Air filter.** Each comes small (small pipe, 3x3x3) and large (large pipe, 4x4x4) in three looks that work the
  same: **Cone** (a pleated red cone, chrome lid), **Mesh Sock** (a black sock over a stack) and **Round Drum** (a
  chrome drum with a black lid). The pipe is in the middle of the bottom. Pipe the neck to the turbo's intake. Clean air makes the turbo wear four times slower.
  A filter clogs slowly as it works, about 10 hours of full boost to go fully dirty, and a clogged filter costs up to 15% of the boost. Place a new one to replace it. A new
  filter starts in its look's colours, and both colours paint.
- **Condition.** Down to half, the turbo performs like new. Below that its boost falls (to 60% near the end).
  At 0% it **blows up**: a bang, shrapnel and a hiss of smoke, and both housings go sooty. A blown turbo makes no boost
  and its wrecked compressor chokes the engine's air: the engine still runs through it, but gets only about a third of
  the air it wants at full revs (for an engine matched to the turbo), so it idles and crawls in limp mode. Replace the
  turbo (a new one starts at 100%) to get the power back. The tooltip shows the condition (see below).
- Wear is saved with each world, in `anyengineparts_wear.json` in the game's working folder, which the game copies
  into the save slot. A turbo moved to another vehicle (split or welded) starts fresh.

## Intercooler and hot boost air

Compressing air heats it: at full boost the air leaving the turbo is about 78 °C over the air outside (more when
the compressor runs far from its best flow, and a compound's second stage heats air that is already hot). With
AnyEngineParts every engine, stock or the rotary, feels its intake air's temperature. The boost part of its power
scales with the air's density against 30 °C air, so hot boost makes less power and cold boost makes more. Boost
air over 60 °C also knocks a boosted engine, which slowly damages it. Without an intercooler, full boost
costs a damage step about every 8 to 9 minutes.

An intercooler fixes that. There are three, which work the same and differ only in looks: **Bar and Plate**
(a black bar core, cast ribbed tanks), **Tube and Fin** (a silver tube core, black welded tanks) and **Fabricated
Race** (bright core, fabricated aluminium tanks). Like the radiator, it has no pipes of its own: snap gas ports onto its faces,
small or large, wherever suits the build. Run one from the turbo's boost and one to the engine's air intake. All its
ports share one volume of air, so the boost passes straight through. It is 3 cells wide, 2 tall and 1 deep, and the
model fills every cell of it. It has an end tank on each side, like a car's, and grows like the radiator: wider and
taller one cell at a time. The tanks stay at the ends and the inlet stubs and rails keep their shape at any size.
A bigger core cools better, and so does slower air: a 3x2 core takes about three quarters of the heat out of a 3x3
turbo's boost. The tanks paint.

## Sounds

Turbos and blow-off valves have their own sounds in place of the impeller hum, made by the mod as it plays:

- **Spool whistle**: a whine that rises with turbine speed, plus the rush of air and intake hiss. Each size has
  its own voice: the 3x3 screams high, the 6x6 whooshes deep.
- **Surge** (no blow-off valve, throttle lifted): "stu-stu-stu", quick puffs of air as boost pushes back through
  the compressor, slowing as the boost runs out. Bigger turbos puff deeper and slower.
- **Blow-off**: a burst when it opens, then quiet as the trapped boost runs out, even if you stay off the
  throttle. Pick the valve's sound in the Properties Tool (**Sound effect**): 0 whoosh, 1 flutter, 2 chirp,
  3 dump (deep). Each is voiced for the turbo venting through it: a 3x3's is short and sharp, a 6x6's long
  and deep.

- **Antilag**: bangs and crackles in the exhaust while the ECU's antilag fires.
- **Blowing up**: once, when a turbo wears out: a crack and a boom, shrapnel pinging, the wheel screeching to a stop
  and a hiss of boost and smoke. A turbo that was already blown when you load in stays quiet.
- **Supercharger whine**: the buzz of its straight-cut step-up gears, there from idle and sliding up with the engine,
  the impeller's thin whistle above it and the gulp of air. It fades to silence when the belt stops. The large one
  sings lower. Off the throttle it surges like a turbo unless a blow-off valve on its outlet pipe vents the boost, just
  as real centrifugal superchargers need a bypass valve.

Sounds follow the vehicle volume slider and are heard by every player near the part.
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
| `power_multiplier` | Output | The engine power multiplier this boost gives, 1 none, 2 at 12.8 psi, no cap, after what hot boost air costs (before the engine's knock control) |
| `spool` | Output | The share of full boost the shaft's speed can make, 0 to 1 (its speed over full-boost speed, squared) |
| `rpm` | Output | Shaft speed, rpm (a supercharger's impeller) |
| `wastegate` | Output | How far the turbo's built-in wastegate is open, 0 to 1 (it opens once the shaft is fast enough for full boost) |
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
| `boost_target_psi` | Input | The boost (psi over the air outside) its built-in wastegate holds, up to the compressor's ceiling (0 or unwired: 12.8) |
| `boost_target` | Output | The boost target it holds now, psi |

## Getting it

- **Survival:** world loot. The Turbo 3x3 and the small supercharger, blow-off valve and air filter drop
  after the 2nd bunker (tech tier 3). The Turbo 6x6, the large supercharger, blow-off valve and air filter,
  and the intercoolers drop after the 3rd bunker (tech tier 4). As with the stock parts, new loot carries
  the tier the world has reached, so each tier drops until the next bunker is done. They are mechanical loot
  (industrial, construction, aircraft and bunker loot spots) and turn up about a quarter less often than the
  game's own parts of the same tier.
- **Sandbox and Creative:** with the gas parts.

## How it works

- The part definitions are built in the DLL ([`turbo_logic.h`](../../native/turbo_logic.h)). At start-up they
  are written to `AnyAPI and Modding/Turbo/turbochargers.json` and added to the game's vehicle components right
  after the game adds its own. The turbos' ids are `turbocharger_3` and `turbocharger_6`, class `gas_pump_centrifugal`
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
- Each tick the turbo runs a real turbocharger's physics on the game's gas: exhaust through the turbine by
  Stodola's law (flow with the square root of the pressure difference), the work it gives the shaft from its
  expansion (the game's 300 °C exhaust stands for about 900 °C), a shaft with each size's inertia, and a
  centrifugal compressor whose pressure ratio goes with the square of its speed (1.87, full boost, at full speed),
  with a simple efficiency map. The compressor takes the work of compressing the air it moves from the shaft. The
  game's gas needs about nine pipe-fulls of air to reach full boost where a real pipe needs under one, so the
  compressor pays for only a tenth of the air that fills its boost pipe, and the full price of what the engine
  breathes. A built-in wastegate holds the shaft at the speed that makes full boost for the flow.
- Sounds: the server packs each turbo's shaft speed and surge (and each blow-off valve's venting) into the impeller's
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
  factor carries on to 3x at 19, which only a compound reaches. Two-step uses the same call: on a cut tick the factor
  is 0, and the game's fuel burn scales with it. If the hooks are not found, engines ignore intake temperature as
  before.
- Superchargers are the impeller class with its own two gas nodes and its torque node. Their tick is the mod's: it
  reads the shaft's speed (`vehicle_torque_node.get_rps`), sets the head from it, pushes the air, and puts the work
  that took back on the shaft as a drag torque (`apply_torque`, times the torque system's newtons per joule), as the
  stock impeller does with its own, smaller, drag. Every half second each turbo and supercharger also notes what its
  boost pipe feeds (an engine's air manifold, which the engine tick hook records, or the next stage's intake); the
  Dyno names the boost setup from that.
- The 2x2, 4x4 and 5x5 turbos are no longer defined, so the game drops them from a saved vehicle as it loads.
- The hot side's gas is not saved; it refills from the pipes after loading. Saves that contain a turbo need
  AnyVehicleOverhaul (or the older AnyEngineParts) installed to load it.

Source: [`turbo_mod.cpp`](../../native/turbo_mod.cpp) (the AnyEngineParts piece of `AnyVehicleOverhaul.dll`), bindings in
[`turbo_bindings.h`](../../native/turbo_bindings.h), tests in
[`tests/turbo_logic_test.cpp`](../../native/tests/turbo_logic_test.cpp).
