# AnyMirror

> AnyMirror hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.2.0 / Steam build 26328453 and turns itself off on any other build.
> AnyMirror 1.1.0 requires AnyAPI 0.35.0 on that build; install it from the manager.

AnyMirror builds symmetric creations. With mirroring on, these tools work in pairs, mirrored across a plane
through your build:

- **Edge tools** (both): every edge you place is placed a second time on the other side.
- **Add plate tool**: every plate you add is added on the other side too. The mirrored edges have to be there
  already; if they are not, the plate is placed once and a message says so.
- **Remove tool**: removing an edge, a node or a plate also removes its mirror twin, when there is one.
  Components (parts) are not mirrored.

The plane shows as a see-through **mirror wall** on the vehicle so you can see where it is. AnyMirror does
nothing with any other tool.

## Keys

The keys only work while an edge, add plate or remove tool is equipped. Rebind them in AnyHelpers' Mod Controls tab.

| Key | Action |
| --- | --- |
| J | Mirror on / off |
| K | Show / hide the mirror wall |
| L | Cycle the mirror axis: X (left / right), Y (up / down), Z (front / back) |
| [ and ] | Move the plane back or forward half a grid step |
| \\ | Re-centre the plane on the build |

When you first turn mirroring on, the plane goes through the middle of the build on the X axis. A small
badge at the top of the screen shows MIRROR ON or OFF, WALL ON or OFF, the axis and the plane position, with the keys underneath.

Pieces that are their own mirror image (lying in the plane, or crossing it symmetrically) are placed or
removed once.

## Settings (AnyHelpers)

| Setting | Default |
| --- | --- |
| Show mirror status while a building tool is equipped | On |
| Show the mirror wall by default | On |
| Mirror wall colour | Tool blue (also Valid green, Hover, Group) |
| Mirror wall strength (%) | 35 (10 to 100) |

The game's overlay has no transparency control, so the wall is drawn as a grid lattice along the build's
grid lines. Wall strength sets how much of the wall the lattice covers; 100% is a solid wall.

## Co-op

Each mirrored piece goes to the host through the same request the tool sends, so the host checks and
replicates it like any other. Only the player who is building needs AnyMirror; the wall is only drawn
for them.

## Test plan

Build `AnyMirror.dll` with the native CMake project and copy it to `AnyAPI and Modding/mods/`. After each
step, the lines tagged `anymirror` in `anymaker_modding.log` (beside `game.exe`) are what we need.

1. Load a world. Expect `Ready after N s`.
2. Equip the edge tool on a vehicle and press J. Expect the badge and the wall through the middle.
3. Place an edge off the plane. Expect a second, mirrored edge. Place one in the plane: only one edge.
4. Press K to hide and show the wall, L to cycle axes, [ ] and \\ to move and re-centre it.
5. Repeat with the second edge tool. Switch to any other tool: no badge, no wall, no mirroring.
6. With the add plate tool, add a plate on one side where the mirrored edges exist. Expect a mirrored plate
   facing the mirrored way. Try one without mirrored edges: one plate and a "Plate not mirrored" message.
7. With the remove tool, remove an edge, a node and a plate on one side. Expect their twins to go too.
8. Move the wall strength slider: the lattice gets denser, and 100% is solid.
