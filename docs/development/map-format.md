# Custom maps: how the world is stored, and the map pack format

Research notes for the Anymaker Map Maker (`mapmaker/`) and the [CustomMap mod](../mods/custom-map.md), for Anymaker 0.1.24 (Steam 25826614).
Sources are the regenerated SDK reference (types, fields and natives), `native/generate_road_graph.py`, and a
read of the installed game's `rom/data` on 2026-10-13. The grid maths below put all 136,159 placed points
(building spots, props, loot) inside their own tile.

## How the game builds its world

The world is a grid of triangles. There is no heightmap and no single world file.

- **One JSON file per triangle**: `rom/data/tiles/0/tile_<x>_<z>_<t>.json` (x and z zero-padded to three
  digits, e.g. `tile_091_111_0.json`). The stock world has 2,550 tiles with x 0-148 and z 0-140; everything
  without a file is open ocean. `rom/data/tile_scene.json` lists streaming chunks (`bounds_center`, `bounds_radius`).
- **Grid maths.** Cell `(x, z)` is the parallelogram spanned by `(512, 0, 0)` and `(256, 0, 256·√3)`
  (globals `g_tile_grid_x_dir`, `g_tile_grid_y_dir`). `t = 0` is its lower-left triangle and `t = 1` the
  upper-right one. A tile's origin is
  `(512x + 256z + (t ? 512 : 256), height · 32, 256·√3 · (z + (t ? 2/3 : 1/3)))`.
- **Terrain** comes from a library of 272 authored terrain pieces in `rom/data/tile_definitions.json`
  (`name`, `meshes` per LOD, `edge_a/b/c` edge identifiers; e.g. `tile_flat_a`, `tile_mountain_d_in`,
  `shallow_k`, `shipyard_d`). Each tile names its piece (`definition_name`), an `orientation` (0-2, omitted when 0)
  and a `height` in 32 m steps (-6 to 12 in the stock world, omitted when 0). Neighbouring pieces must have matching edges; the
  developers' editor has a "generate matching tiles" helper for this (`chunk_edit_panel.generate_matching_tiles`).
- **Roads, railways and paths** are cubic Bézier links (`junction_links`: `pos_a`, `pos_b`, `dir_a`, `dir_b`,
  `normal_a`, `normal_b`, `type`) in world coordinates. Types: `road`, `road_b`, `road_c`, `road_d`, `train_a`,
  `train_b`, `train_flyover_a`, `train_flyover_b`. They join at **junctions** (`junctions`: `definition_name`,
  tile-local `position`, `normal`, `rotation`) whose shapes (crossings, T-junctions, flyovers, tank traps...) are
  the 220 entries of `tile_junction_definitions.json`; its `path_link_definitions` give each link type's mesh.
- **Placed things** per tile (positions are world coordinates, rotations `rot` in radians):
  - `prop_spawn_zones`: `def`, `pos`, `rot`. 122,000 props from the 136 in `rom/data/prop_definitions.json`
    (`name`, mesh, collision `zones`, `mass`). Fences are rows of posts and panels (`fence_post_a`,
    `fence_panel_d`...); also barriers, containers, pallets, dividers, pipes.
  - `spawn_zones`: building spots with `pos`, `rot`, `size` (1-5), optional `foundation` (one of 52
    `spawn_zone_foundation_definitions`, e.g. `2a`) and `filter`: `house`, `town`, `industrial`, `farm`,
    `roadside`, `office`, `research`, `gas_station`, `sawmill`. At world creation, `server_scene.world_data.build`
    picks a building for each spot from the seed; buildings live in `rom/data/buildings/*.data` + `.meta`.
  - `loot_spawn_zones` (`pos`, `rot`, rarely `def`), `actor_spawn_zones` (`pos`, patrol `waypoints`),
    `dead_drop_spawn_zones` (`pos`, `rot`), and `nearest_ocean_pos`.
- **Loading.** Tiles stream in around players on worker threads (`*_environment_tile_task.execute`).
  Every client loads tiles from its own install, so all players need the same map.
- **Saves.** A save stores per-tile vehicles, actors, floor items, props and so on
  (`server_scene.save_data.tile`). Props that came from the map are written into the save once the tile
  has loaded, so a map change only fully applies to **new worlds**.
- **File access.** Every data file goes through a few game.exe natives: `file.read_string`
  (game.exe+0x3f080), `file.read_binary` (+0x3f0e0), `file.get_is_exists` (+0x3f140),
  `file.get_files_in_dir` (+0x3edf0). gcl code calls them through per-function call cells, so a mod can
  swap those cells to serve a map pack's files in place of `rom/data/tiles` without touching the install.
  The [CustomMap mod](../mods/custom-map.md) does this. Paths are `file.path` values: a store (0 embedded,
  1 system, 2 rom, 3 user) and a string.
- **The developers' editor ships in the game, switched off**: chunk (terrain), tile (junctions), prop,
  loot, actor spawn, dead drop, prefab and map-screenshot panels, toggled by `g_is_*_window` globals and
  drawn from `update_ui` in `main.gc`, which has no caller in the release build. It saves through
  `chunk_edit_panel.save_scene`, which writes each tile with `client_scene.environment_tile.save`.
  Maps are made with the separate Map Maker instead (`mapmaker/`).

## Map pack format (version 1)

A map pack is a folder, by default `<game>\AnyAPI and Modding\maps\<map name>\`:

```text
map.json
tiles/0/tile_<x>_<z>_<t>.json   (same format as the game's own tiles)
tiles/vegetation/...            (optional, same as rom/data/tiles/vegetation)
```

`map.json`:

| Key | Meaning |
|---|---|
| `format` | `1` |
| `name`, `author`, `description` | Shown in the editor and the in-game map picker |
| `base` | `stock` (started from the stock world) or `blank` |
| `game_build` | Steam build the pack was saved against (informational) |
| `tiles` | Number of tile files |
| `stock_vegetation` | Tiles (`tile_<x>_<z>_<t>`) whose terrain is unchanged from the stock world, so the game's trees and bushes for them still fit. CustomMap serves stock vegetation only for these; other tiles have none unless the pack has its own `tiles/vegetation` file. Missing key = keep all |
| `spawn` | Optional `[x, y, z]` in world metres: where players start a new game on this map, in every game mode. CustomMap returns it from `server_scene.get_spawn_position`. Missing key = the game's fixed start points |

The tiles folder is the **whole** world: tiles that are not in the pack are ocean. Tile files keep every
key the game wrote, including ones the editor does not understand yet, so a pack saved from the stock
world loads exactly like the stock world.
