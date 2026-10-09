# AnyMirror

> AnyMirror hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.23 / Steam build 25755694 and turns itself off on any other build.
> Prototype: not in the catalog yet.

AnyMirror builds symmetric creations. With either **edge tool** equipped and mirroring on, every edge you
place is placed a second time, mirrored across a plane through your build. The plane shows as a see-through
**mirror wall** on the vehicle so you can see where it is. AnyMirror does nothing with any other tool.

## Keys

The keys only work while an edge tool is equipped. Rebind them in AnyHelpers' Mod Controls tab.

| Key | Action |
| --- | --- |
| J | Mirror on / off |
| K | Show / hide the mirror wall |
| L | Cycle the mirror axis: X (left / right), Y (up / down), Z (front / back) |
| [ and ] | Move the plane back or forward half a grid step |
| \\ | Re-centre the plane on the build |

When you first turn mirroring on, the plane goes through the middle of the build on the X axis. A small
badge on screen shows whether mirroring is on, the axis and the plane position.

Edges that are their own mirror image (lying in the plane, or crossing it symmetrically) are placed once.

## Settings (AnyHelpers)

| Setting | Default |
| --- | --- |
| Show mirror status while an edge tool is equipped | On |
| Show the mirror wall by default | On |
| Mirror wall colour | Tool blue (also Valid green, Hover, Group) |
| Mirror wall strength (%) | 35 |

## Co-op

The mirrored edge goes to the host through the same request the edge tool sends, so the host checks and
replicates it like any other edge. Only the player who is building needs AnyMirror; the wall is only drawn
for them.

## Test plan

Build `AnyMirror.dll` with the native CMake project and copy it to `AnyAPI and Modding/mods/`. After each
step, the lines tagged `anymirror` in `anymaker_modding.log` (beside `game.exe`) are what we need.

1. Load a world. Expect `Ready after N s`.
2. Equip the edge tool on a vehicle and press J. Expect the badge and the wall through the middle.
3. Place an edge off the plane. Expect a second, mirrored edge. Place one in the plane: only one edge.
4. Press K to hide and show the wall, L to cycle axes, [ ] and \\ to move and re-centre it.
5. Repeat with the second edge tool. Switch to any other tool: no badge, no wall, no mirroring.
