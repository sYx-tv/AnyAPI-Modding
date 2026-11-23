# AnyAeronautics

> AnyAeronautics hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> **Single player and host only** for now. In development: not in the catalog yet. Its own DLL
> (`AnyAeronautics.dll`), not part of AnyVehicleOverhaul.

AnyAeronautics is for things that fly. Its first parts are two **thrust nozzles**, the **Cone Nozzle** and the **Fighter Nozzle**: straight jet exhausts that actually push.

## Why

The game's jet engine is a full chain: compressor, combustion chamber and exhaust (plus gearboxes for turboshaft
builds). The combustion chamber already has a `throttle` input, spools up and down, burns fuel and gets hot. Every
tick it hands the exhaust a thrust factor (how spun up it is, 0 to 1), and the stock exhaust turns that into a push
so small that a jet car or plane barely moves. The nozzles turn the same factor into real thrust.

## Cone Nozzle and Fighter Nozzle

Copies of the stock jet exhaust (same size, faces and mass, built from the game's own definition each time the game
starts) with their own straight nozzle meshes. The stock exhaust's outlet bends up about 45° like a turboshaft
exhaust, so a jet built on it would look one way and push another. Fit a nozzle behind a jet combustion chamber in
place of the stock exhaust.

- **Cone Nozzle:** a grey octagonal cone tapering to a round outlet, a plain turbojet. About 47 kN at full throttle on the stock engine.
- **Fighter Nozzle:** a dark gunmetal can with a ring of twelve overlapping petals, like a fighter jet. More push from the same fuel: about 59 kN.

Both looks were picked by Nate from three rendered options (2026-11-23). The meshes are built at start-up from simple
shapes, with only the file header taken from the game's own `jet_exhaust_a.mesh`, and written beside the stock part
meshes as `anyaero_nozzle_cone_a.mesh` and `anyaero_nozzle_fighter_a.mesh`.

- **Throttle:** the combustion chamber's `throttle` input, as for the stock engine. Wire it to a seat axis, a
  lever or a microcontroller.
- **Thrust from fuel burned:** thrust = fuel burned per second / the nozzle's thrust specific fuel consumption
  (Cone 0.10, Fighter 0.08 kg per N per hour, real turbojet values). The combustion chamber's `fuel_flow` is read as
  m³/s of petrol (750 kg/m³); the first test read 0.00176 at full throttle and 0.00032 at 17 % throttle. So
  throttle, running out of fuel, damage and power taken off by a gearbox all show in the thrust.
- **Spool lag:** thrust follows that target with a lag (1.6 s going up, 0.8 s coming down).
- **Where it pushes:** at the nozzle, toward the engine in front of it (the gas goes out the back), so an
  off-centre engine also turns the vehicle.

The first test (2026-11-23) used engine speed instead. Its log showed why that was wrong: the engine sat at about
half speed whether the throttle was at 17 % or 100 %, so thrust barely changed with throttle, while fuel flow and
torque dropped about 5×. Test vehicles weighed about 10 t.

| Channel | Direction | Value |
| --- | --- | --- |
| `thrust` | Output | Thrust right now, in kN |
| `spool` | Output | The combustion chamber's speed factor, 0..1 |
| stock exhaust channels | Output | Unchanged |

Stock exhausts are untouched, so helicopter and turboprop builds behave as before.

## How it works

The nozzle definitions are written to `AnyAPI and Modding/AnyAeronautics/anyaeronautics_parts.json` and added to
the game's parts right after the game adds its own (the route GpsPart, AnyLaser and AnyCVT use). A hook on the jet
exhaust's server tick runs the stock tick first, then, for our nozzles only, reads the thrust factor the
combustion chamber wrote and applies the force through the exhaust's own impulse activator
(`physics_util.impulse_activator.add_force` with a world position), the same call the stock exhaust makes. A hook on
the exhaust's `get_data_f64` serves `thrust` and `spool`.

## Status

| What | Status |
| --- | --- |
| Parts pushing vehicles through `add_force` | **Proven** in the game's code (control fins, rotors, the stock exhaust) |
| Throttle from a seat, lever or MC | **Proven** (stock combustion chamber input) |
| AnyAeronautics calling `add_force` itself | **Likely**, first test pending |
| Thrust direction from the engine in front of the nozzle | **Likely**, first test pending |
| Co-op joiners feel the push | **Likely** (physics runs on the host) |

## Planned

- A Properties tool row for max thrust per nozzle.
- Afterburner and a matching jet sound.
- An all-in-one jet block if wanted.

## Troubleshooting

Look for `anyaeronautics` in `anymaker_modding.log`. At start it says whether both meshes were built (`meshes: ours, ours`). On the first runs each nozzle logs its spool, thrust,
direction and the vehicle's mass (and thrust in g) for its first few pushes.
