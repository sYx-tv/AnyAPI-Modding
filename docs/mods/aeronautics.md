# AnyAeronautics

> AnyAeronautics hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> **Single player and host only** for now. In development: not in the catalog yet. Its own DLL
> (`AnyAeronautics.dll`), not part of AnyVehicleOverhaul.

AnyAeronautics is for things that fly. Its first part is the **Thrust Nozzle**: a jet exhaust that actually pushes.

## Why

The game's jet engine is a full chain: compressor, combustion chamber and exhaust (plus gearboxes for turboshaft
builds). The combustion chamber already has a `throttle` input, spools up and down, burns fuel and gets hot. Every
tick it hands the exhaust a thrust factor (how spun up it is, 0 to 1), and the stock exhaust turns that into a push
so small that a jet car or plane barely moves. The Thrust Nozzle turns the same factor into real thrust.

## Thrust Nozzle

A copy of the stock jet exhaust: same look, size, faces and mass, built from the game's own definition each time
the game starts. Fit it behind a jet combustion chamber in place of the stock exhaust.

- **Throttle:** the combustion chamber's `throttle` input, as for the stock engine. Wire it to a seat axis, a
  lever or a microcontroller.
- **Spool lag and fuel:** the stock engine's own. Thrust follows the engine's spool, so it builds as the engine
  winds up and fades as it winds down.
- **Thrust:** max thrust × spool². Idle gives a light push, full throttle the full rating. It pushes at the
  nozzle, toward the engine in front of it (the gas goes out the back), so an off-centre engine also turns the
  vehicle.

The first test build has three strengths, so the right amount can be picked in game:

| Part | Max thrust |
| --- | --- |
| Thrust Nozzle (5 kN) | 5,000 N |
| Thrust Nozzle (15 kN) | 15,000 N |
| Thrust Nozzle (40 kN) | 40,000 N |

| Channel | Direction | Value |
| --- | --- | --- |
| `thrust` | Output | Thrust right now, in kN |
| `spool` | Output | Engine spool 0..1 (the combustion chamber's speed factor) |
| stock exhaust channels | Output | Unchanged |

Stock exhausts are untouched, so helicopter and turboprop builds behave as before.

## How it works

The nozzle definitions are written to `AnyAPI and Modding/AnyAeronautics/anyaeronautics_parts.json` and added to
the game's parts right after the game adds its own (the route GpsPart, AnyLaser and AnyCVT use). A hook on the jet
exhaust's server tick runs the stock tick first, then, for Thrust Nozzles only, reads the thrust factor the
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

- A Properties tool row for max thrust per nozzle, in place of the three strengths.
- Afterburner and a matching jet sound.
- An all-in-one jet block if wanted.

## Troubleshooting

Look for `anyaeronautics` in `anymaker_modding.log`. On the first runs the nozzle logs its spool, thrust,
direction and the vehicle's mass (and thrust in g) for its first few pushes.
