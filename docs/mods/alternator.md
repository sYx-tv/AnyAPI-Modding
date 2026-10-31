# AnyAlternator

> AnyAlternator hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> 0.0.1 is a test build: nothing below has been tried in game yet.

AnyAlternator adds three bigger alternators beside the stock one. The bigger they are, the more they make and the
harder they are to turn: each one needs a minimum shaft speed before it charges, wastes more of the power it takes
from the shaft, and weighs more. They pair with [AnyBattery](battery.md)'s Battery Pack.

## The sizes

| Part | Size (cells) | Out at 100 rps | Takes from the shaft | Efficiency | Starts charging | Tier |
| --- | --- | --- | --- | --- | --- | --- |
| Alternator (stock) | 2 x 2 x 3 | 1.1 kW | 1.5 kW | 75% | any speed | 2 |
| Medium Alternator | 3 x 3 x 4 | 5 kW | 7.1 kW | 70% | 10 rps | 2 |
| Large Alternator | 4 x 4 x 6 | 15 kW | 23 kW | 65% | 20 rps | 2 |
| Heavy Alternator | 5 x 5 x 8 | 40 kW | 67 kW | 60% | 30 rps | 3 |

Output rises in a straight line from the cut-in speed to full output at 100 rps, the stock alternator's top. Below
the cut-in it makes nothing and does not drag the shaft. Like the stock one, it only drags while it has somewhere to
put the energy: with every battery on the network full, it spins free.

They look like the stock alternator grown to size, with the shaft on the +z end (the middle cell on the 3 x 3 and
5 x 5, one cell off the middle on the 4 x 4, as the stock one is off its middle). Electric ports fit on every other
face. The whine drops in pitch the bigger they are.

## Inputs and outputs

| Name | Kind | What |
| --- | --- | --- |
| `enabled` | input | 0 switches it off: no charging and no drag (like a real alternator's field switch). Unconnected means on. |
| `rps` | output | Shaft speed (stock) |
| `power` | output | Watts going to the network (stock, now at this alternator's efficiency) |
| `energy_capacity`, `energy_store` | output | Its small internal buffer, in joules (stock) |
| `load_percent` | output | Output as a percentage of its rating (0 to 100) |
| `shaft_power` | output | Watts it takes from the shaft |

## Getting it

World loot (electronics, like the stock alternator): the Medium (spawn chance 0.15) and Large (0.1) from the first
bunker on (tech tier 2), the Heavy (0.075) from tier 3. Also with the electric parts in Sandbox and Creative.

## How it works

Each part is the game's own alternator (class `alternator`) with a bigger `alternator_wattage`, so the game's own
tick charges the network and brakes the shaft. That tick takes `e = min(wattage x factor x dt, room in its buffer)`
from the shaft and gives the network three quarters of it. A hook on the tick sets the room in the buffer to the
energy AnyAlternator wants before the game's tick runs (nothing below the cut-in or when switched off, a ramp from the
cut-in to 100 rps), then puts the real energy back plus `e x efficiency`. The game's brake then costs the shaft exactly
`e`. The tooltip and the `power` output are corrected to the real output. A second hook adds the data channels, and a
client hook lowers the pitch of the alternator whine for the bigger parts. Stock alternators pass through untouched.

The definitions are written to `AnyAPI and Modding/AnyAlternator/anyalternator_parts.json` and the meshes to
`rom/meshes/components/anyalternator_*.mesh` when the game starts. The meshes are built from the game's own
`alternator_a_motor.mesh`: the body is scaled to size, the hub is moved onto the torque cell on a short collar, and
the old opening is closed. No game asset ships with the mod. `native/tests/anyalternator_logic_test.cpp` checks the
sizes, the output curve, one tick against a model of the game's tick, the definitions and the mesh.

## Feasibility

| Piece | Status |
| --- | --- |
| New parts on the stock alternator class | Proven route (GpsPart, AnyBattery reuse stock classes the same way) |
| Output and drag through the buffer's room | Likely: read from the 0.1.28 tick code, unchanged in 0.2.0; untested in game |
| Port and torque face layout | Likely: copied from the stock alternator's faces and grown |
| `enabled` input | Likely: the same data-slot method as AnyMoto's `assist` input |
| Lower whine | Likely: the voice update hook EngineSound uses |
| Co-op | Likely: the alternators run on the host; every player needs the mod for the parts to exist |

## Known limits

* Switched off or below the cut-in, the whine still plays: the client hears the shaft speed, not the output.
* The cut-in uses the shaft speed from the tick before, so the alternator reacts one tick late (1/60 s).
