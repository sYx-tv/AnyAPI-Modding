# Dyno

> Part of [AnyEngineParts](engine-parts.md). It hooks game functions directly through the
> [experimental SDK](../../sdk/experimental/README.md), so it only activates on Anymaker 0.1.27 / Steam build
> 26113663. **Single player / host only** for now.

The **Dyno** is a small sensor part that records your engine's power and torque and draws the curves on screen. Put it
anywhere on a vehicle; an orange bar chart on its top tells it apart from the Compass and GPS Sensors.

## Making a pull

1. Start the engine and get near the vehicle (within 40 m). The Dyno panel appears in the top right of the screen.
2. Hold the throttle fully down (90 % or more). The panel shows **REC** and draws the pull as the RPM climbs.
3. Let off, or let the engine reach the 8,000 RPM fuel cut, which ends the pull like a real dyno run. A pull that
   covered at least 1,500 RPM over at least a second is kept. The next pull starts once you have let off.

The last three pulls stay on the graph so you can compare builds: the oldest is grey, then blue, and the newest
orange. **Solid lines are power, dashed lines are torque**, and the red line is the game's 8,000 RPM fuel cut. Beside
the graph are each pull's peak power and the latest pull's peak torque, boost, engine temperature and whether the
engine knocked. Knock is the turbos' knock from hot boost air (see [Turbo](turbo.md)); the game's other engine damage,
from low oil or overheating, is not counted as knock.

Under the live readout the panel names the boost setup it sees on the vehicle: **Naturally aspirated**, **Single**,
**Twin**, **Triple** or **Quad turbo**, **Compound turbo**, **Supercharged**, **Twin supercharged** or
**Twincharged** (a turbo and a supercharger), with the counts when there are more. Each kept pull's log line ends with it.

A pull can be made on the road or with the wheels off the ground: the game's engine makes the same power at a given
RPM either way, so the numbers match.

**F6** hides or shows the panel (rebind it in Controls under AnyEngineParts). **Hold F6 for a second** to forget the
kept pulls (a pull being recorded carries on).

## What it measures

Every number comes from the game's own engine:

- **Power** is the energy each engine turns into motion each tick (times 60 ticks per second), added up over every
  engine on the vehicle, less what [superchargers](turbo.md#superchargers) take through their belts. It is shown in hp
  (1 hp = 745.7 W).
- **Torque** is power divided by engine speed (2π × revolutions per second), in N·m.
- **RPM** is the fastest engine's speed. **Boost** is the engine's intake pressure factor (1 = none, 2 = the most
  boost the game counts). **Engine** is the hottest engine's temperature.

Power at the wheels is not measured yet.

## Data channels

| Channel | Direction | Value |
| --- | --- | --- |
| `rpm` | Output | Engine speed now (fastest engine) |
| `power_hp`, `power_kw` | Output | Power now, all engines |
| `torque_nm` | Output | Torque now |
| `boost` | Output | Intake pressure factor, 1 to 2 |
| `engine_temperature` | Output | Hottest engine, °C |
| `recording` | Output | 1 while a pull is being recorded |
| `peak_hp`, `peak_rpm` | Output | The latest pull's peak power and the RPM it came at |
| `peak_torque_nm` | Output | The latest pull's peak torque |
| `run_count` | Output | Pulls kept, 0 to 3 |
| `clear_runs` | Input | Going above 0.5 forgets the kept pulls (once each time; a pull being recorded carries on) |

## Getting it

- **Survival:** world loot after the 3rd bunker (tech tier 4, mechanical loot), about a quarter less often than the
  game's own tier 4 parts, like the other AnyEngineParts parts.
- **Sandbox and Creative:** in the sensor container.

Source: [`dyno_mod.cpp`](../../native/dyno_mod.cpp), [`dyno_logic.h`](../../native/dyno_logic.h),
[`dyno_mesh.h`](../../native/dyno_mesh.h).
