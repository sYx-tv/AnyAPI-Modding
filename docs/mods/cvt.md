# AnyCVT

> AnyCVT hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.28 / Steam build 26277967 and turns itself off on any other build.
> 0.0.1 is published ([mods-2026.10.29](../releases/anycvt-0.0.1.md)). A tester drove the Small CVT on 2026-10-29: it shifted and drove as described and kept
> its Properties settings over a save and reload. The medium and large and the outputs are untested.

AnyCVT adds a belt CVT for each engine size. Each one is the stock gearbox of that size with the same case, the same
engine face and the same weight, so it bolts straight onto the engine where the stock gearbox goes. There are no gears:
in D it slides its ratio to keep the engine at a set speed, and a centrifugal clutch lets the engine idle with the
vehicle stopped.

## The parts

| Part | Fits | Looks like | Tier | Spawn chance |
| --- | --- | --- | --- | --- |
| Small CVT | small engine | the stock small gearbox | 2 | 0.15 |
| Medium CVT | medium engine | the stock medium gearbox, with the small gearbox's shifter connector on top | 3 | 0.15 |
| Large CVT | large engine | the stock large gearbox, with the small gearbox's shifter connector on top | 4 | 0.075 |

They are a fixed length (no stretching: there are no gears to add). Ports, the engine face and the output face are the
stock gearbox's. The stock medium and large gearboxes shift by air, so their CVTs get the small gearbox's mechanical
shifter face in the middle of the top at the engine end, where the small one has it.

## Driving it

Wire a shifter (a lever or anything with a mechanical link) to the top connector, as on the stock small gearbox. Above
0.75 steps up and below -0.75 steps down, returning to the centre between steps: **R, N, D**. A new CVT starts in N.

* **D**: the ratio slides between the low and high ratio to hold the engine at the target RPM at full throttle, and
  lower at part throttle (down to the engage RPM with the throttle shut, so lifting off holds a little engine braking).
* **N**: the clutch is open.
* **R**: the low ratio, backwards.

The clutch is open below the engage RPM and closes smoothly over the next 20% (at least 250 rpm). R and D wait to
bite while the output still turns the other way faster than 2 rps. The stock Input, Output and Reverse rows still work:
Output / Input is a fixed stage after the belt, and Reverse swaps which way D turns.

## Properties

| Row | Range | Default | What |
| --- | --- | --- | --- |
| Low ratio | 1 to 60 | 24 | The pulling-away ratio (the stock first gear) |
| High ratio | 1 to 60 | 1 | The top ratio (the stock eighth gear) |
| Target RPM | 1,000 to 8,000 | 6,800 | Engine speed held at full throttle (stock engines make full power from 6,400) |
| Response | 1 to 10 | 5 | How fast the ratio slides: 1 takes about 1.7 s for most of a change, 10 about 0.17 s |
| Engage RPM | 400 to 5,000 | 1,500 | Where the clutch starts to bite |

Low and high can be entered either way round. Settings are saved with the part in the gearbox's own saved ratio
fields, so saves need no new format; a vehicle loaded without the mod simply loses its CVTs.

## Outputs

| Name | What |
| --- | --- |
| `ratio` | The ratio it is at (positive) |
| `engine_rpm` | The engine's speed |
| `target_rpm` | The engine speed it is holding for right now |
| `current_gear` | -1 R, 0 N, 1 D, as an integer (the stock gearbox's channel name, so its programs can read it) |
| `selector` | -1 R, 0 N, 1 D, as a fractional |
| `clutch_factor` | 0 open to 1 closed (stock) |

## How it works

Each CVT is a part of the game's own `gearbox` class, so the engine couples to it exactly as to a stock gearbox: each
engine tick sets the mass of the gearbox's torque node, reads its speed as the engine's and drives it. The torque
solver reads that node's gear ratio and clutch every tick, so a hook on the gearbox tick replaces the stock tick for
AnyCVT's parts and sets them from the CVT's numbers instead of a gear. A hook on the engine tick notes the engine's
speed and throttle for the CVT on its face. Hooks on the Properties list and the gear count swap the stock gear ratio
rows for AnyCVT's, and on the client the rows get their names from a hook on the row UI and the text lookup. Stock
gearboxes pass through untouched.

The definitions are built when the game starts from the game's own `gearbox`, `gearbox_b` and `gearbox_c` definitions
and written to `AnyAPI and Modding/AnyCVT/anycvt_parts.json`; the medium and large meshes are built from
`gear_box_b.mesh` and `gear_box_c.mesh` with the connector copied from `gear_box_a.mesh` and written to
`rom/meshes/components/anycvt_*.mesh`. No game asset ships with the mod. `native/tests/anycvt_logic_test.cpp` checks
the selector, the defaults, launching, sliding, N, R and the clutch, and the definitions and meshes built from made-up
stock files.

## Feasibility

| Piece | Status |
| --- | --- |
| New parts on the stock gearbox class that mate to the engine | Proven for the small (tester, 2026-10-29); likely for the medium and large |
| Sliding ratio and clutch every tick | Proven (tester, 2026-10-29: "worked mechanically perfect") |
| R, N, D on the stock shifter link | Proven (tester, 2026-10-29) |
| Shifter face on the medium and large | Likely: the small gearbox's face and node copied onto them |
| Properties rows and names | Rows and saving proven (tester changed Target and Engage RPM, kept over a reload); names likely |
| Outputs | Likely: `current_gear` through the gearbox's own integer channel lookup, the rest through its fractional one |
| Co-op | Likely: the CVTs run on the host; every player needs the mod for the parts to exist |

## Known limits

* No shift click: the stock gearbox's shift event is not sent.
* The stock tooltip shows "--" for the gear and ratio; read `ratio` on a microcontroller.
* A player without AnyCVT's client hooks sees the rows named Gear Ratio 1, Gear Ratio 2, Max, Stiffness and Min.
