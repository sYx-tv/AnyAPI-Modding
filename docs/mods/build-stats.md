# AnyBuildStats

> AnyBuildStats reads the vehicle through the [experimental SDK](../../sdk/experimental/README.md), so its
> part counts only work on Anymaker 0.1.23 / Steam build 25755694. Prototype: not in the catalog yet.

AnyBuildStats shows a spec sheet for the creation you aim the **Properties Tool** at, in a card above the
[AnyBalance](balance.md) card (or at the bottom left):

- Mass, size (length, width and height) and centre of mass, from AnyAPI's creation balance service
- Part counts: components, edges, plates and nodes
- **Power to the wheels** in hp, and hp per tonne (host only, see below)
- The most common part categories

Press **F9** (rebindable in AnyHelpers' Mod Controls tab) to copy the sheet to the clipboard as text, ready
to paste into chat or a post. For creations with several bodies, part counts are for the heaviest body.

### Power to the wheels

Only engines and electric motors whose drivetrain reaches a wheel, train wheel or track sprocket count;
alternators, pumps and anything else are left out. Electric motors count at their rated power. The game stores
no rated power for combustion engines, so each engine counts at the highest output it has made while
connected (torque × speed), measured since the vehicle was loaded: rev or drive it once to fill it in. The
card notes "engines: peak seen" when engines are included. Gearbox and friction losses are not subtracted.

The drivetrain only exists on the host, so other players see "host only" here.

AnyBuildStats only reads; nothing is sent to other players, and no other player needs it.

## Settings (AnyHelpers)

| Setting | Default |
| --- | --- |
| Show build stats with the Properties tool | On |
| Show part categories | On |
| Card position | Right, above AnyBalance |
| Card opacity (%) | 90 |

## Test plan

Build `AnyBuildStats.dll` with the native CMake project and copy it to `AnyAPI and Modding/mods/`. The lines
tagged `anybuildstats` in `anymaker_modding.log` are what we need.

1. Load a world. Expect `Ready after N s`.
2. Equip the Properties Tool and aim at a creation. Expect the card with mass, size and part counts, and the
   first `sampled vehicle` lines in the log.
3. Add and remove a few parts. The counts should follow within half a second.
4. Press F9 and paste into a text editor.
5. As host, aim at a vehicle with electric motors driving wheels: "To wheels" shows their hp. Start and rev a
   combustion engine connected to wheels: the hp rises to its peak (log: `engine output`, `drives the wheels`).
