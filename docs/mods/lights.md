# AnyLights

> AnyLights hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.25 / Steam build 25904830 and turns itself off on any other build.
> Prototype: not in the catalog yet. Part of the AnyHelpers bundle.

AnyLights turns the stock paint tools into one paint tool for every colour, and makes lights glow the colour
they are painted.

## One paint tool

The game gives every palette colour its own paint tool. With AnyLights, hold **any** paint tool and right
click. A panel opens with:

- **The whole palette** (84 colours) as real swatches, in the game's order. Hovering a swatch previews it.
- **A large preview** of the colour with its hex code, and the glow a light painted that colour gets.
- **Modes**: Fill, Flood, Brush, Pencil, Erase brush and Erase pencil, with a one-line hint for each.

Click a swatch or a mode, then right click or press Esc to close the panel. The pick applies to every paint
tool you hold and is remembered between sessions (`anylights_paint.txt` in the mods folder).

**Ctrl+Z** undoes your last click with the paint tool on parts and on whole plate sides, one click per press.
Edges, links and brush or pencil pixel strokes can't be undone yet: Ctrl+Z after one of those does nothing
(it never reaches back to the click before). Undo history starts fresh in each world.

## Lights

Spot, omni and rotating lights glow the colour of their paint, at full brightness so dark paints still light
up. Greys, black and white paint give white light. Unpainted lights keep the stock warm white.

## Multiplayer

Painting sends only the game's own paint events with a palette colour, and the host accepts them as they are,
so paint is saved with the vehicle and every player sees it, with or without AnyLights. The host doesn't need
the mod. Light colours are worked out on each player's own game from that paint: players with AnyLights see
coloured lights, players without it see stock lights.

## Settings (AnyHelpers)

| Setting | Default |
| --- | --- |
| One paint tool for every colour | On |
| Lights glow their paint colour | On |

## Test plan

Build `AnyLights.dll` (or the AnyHelpers bundle) with the native CMake project. The lines tagged `anylights`
in `anymaker_modding.log` are what we need.

1. Load a world. Expect `Ready after N s` and, once you hold a paint tool, `palette: 84 colours` and
   `paint tool held: ...`.
2. Hold any paint tool and right click: the panel opens. Pick a colour far from the tool's own, close the
   panel and fill a part, a plate and an edge: they take the picked colour. The hotbar icon follows it.
3. Try every mode from the panel, including flood and both erasers.
4. Ctrl+Z after a fill: the part goes back (log: `undo: repainted ...`).
5. Paint a spot, an omni and a rotating light red: each glows red (log: `... light: N paint slots ...`).
6. Save and reload: paint and light colours come back.
7. Co-op: a second player (with or without AnyLights) sees the paint; with AnyLights they also see the light colours.
