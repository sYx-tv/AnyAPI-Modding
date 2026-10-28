# AnyBattery

> AnyBattery hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.27 / Steam build 26113663 and turns itself off on any other build.
> 0.0.1 is a test build: nothing below has been tried in game yet.

AnyBattery adds one part, the **Battery Pack**: a battery you place and stretch to size the way you place a liquid
tank. It is placed **empty**. Charge it from an alternator or a solar panel on the same electric network, and it
powers lights, motors and everything else like a stock battery.

## Size and charge

The smallest pack is 2 x 2 x 2 cells (16 cm cube) and holds **4 kWh**, the same as the stock Battery. Stretch it
on any axis, up to 16 cells (1.28 m) each way. Every cell adds 0.5 kWh, the stock batteries' energy density:

| Size (cells) | Holds |
| --- | --- |
| 2 x 2 x 2 | 4 kWh |
| 3 x 2 x 2 | 6 kWh |
| 4 x 4 x 2 | 16 kWh |
| 4 x 4 x 4 | 32 kWh |
| 16 x 16 x 16 | 2,048 kWh |

It looks like the stock Battery sitting in its cradle, scaled up: a dark block in a tray, a red and a blue square
terminal in a channel across the top, and two hold-down straps you can paint (paint slot 1). A bigger pack costs more to build, like a stretched tank. Electric ports fit on its bottom and its four sides; the
top carries the terminals. The tooltip shows the charge in Wh and the power in or out, and a data port or
microcontroller can read `energy_store`, `energy_capacity` (both in joules), `electric_consumption` (W) and
`charge_percent` (0 to 100).

The charge is saved with the world. A crash that damages a pack empties it, as it does a stock battery (see Mod
Settings to change that).

## Mod Settings

| Setting | Default | What it does |
| --- | --- | --- |
| Energy per cell (kWh) | 0.5 | How much a pack holds per cell, 0.1 to 5. Changing it changes every pack at once; a pack holding more than its new size allows is cut down to full. |
| Crash damage | Empties it (stock) | What a damaging crash does to the charge: empties it, loses half, or keeps it. |
| Self-discharge (% per hour) | 0 | Charge a pack loses by itself, per hour of play, as a percentage of what it holds. |
| Charge when placed (%) | 0 | Charge of a newly placed pack. 0 means placed dead. |

In co-op the host's settings apply: the packs, like every battery, run on the host.

## Getting it

World loot from the first bunker on (tech tier 2, mechanical loot, spawn chance 0.15), and with the electric parts
in Sandbox and Creative.

## How it works

The pack is the game's own battery holder (class `battery`) without a battery slot. The electric network already
fills and drains the holder's store; the stock holder copies its capacity from the battery item fitted in it. A hook
on the holder's server tick gives a pack its capacity from its size instead, and keeps its charge: the stock
holder saves its charge inside the battery item, so AnyBattery saves it in `anybattery_charge.json` in the world's
working folder, which the game copies into the save slot (the same way AnyEngineParts saves turbo wear). Packs are
keyed by vehicle and part id, so a pack that moves to another vehicle (a split or weld) keeps the charge it has in
the game. A second hook adds the `charge_percent` output. Stock batteries pass through untouched.

The definition is written to `AnyAPI and Modding/AnyBattery/anybattery_pack.json` and its generated mesh to
`rom/meshes/components/anybattery_pack.mesh` when the game starts. The mesh keeps every vertex off the middle of the
part on each axis, where the game splits a stretched mesh, so the block and tray grow with the pack, and the terminals
and straps stay at the ends. `native/tests/anybattery_logic_test.cpp` checks the capacity, the charge kept between ticks,
the crash and self-discharge settings, the save file, the definition and the mesh.

## Feasibility

| Piece | Status |
| --- | --- |
| Placing and stretching it like a tank | Likely: the liquid tank's stretch modes and faces, the same kind of definition as the stretchable Rotary and intercooler |
| Charging from alternators and solar panels, powering parts | Likely: the stock holder's own store on the stock network |
| Placed empty | Proven in code: a new holder starts at 0 |
| Charge saved with the world | Likely: the turbo wear file's method |
| Co-op | Likely: the charge lives on the host, like every stock battery; every player needs the mod for the part to exist |
| Mesh stretching cleanly | Unproven: depends on where the game splits a stretched mesh (taken as the middle of the unstretched part) |

## Known limits

* A pack saved inside a creation (not a world) is not charged when spawned: only worlds save the charge.
* No charge-rate limit yet: a pack takes whatever the network gives it, like a stock battery.
