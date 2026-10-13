# Custom maps: how the world is stored, and the map pack format

Research notes for the [MapEditor mod](../mods/map-editor.md) and the optional Map Maker viewer (`mapmaker/`), for Anymaker 0.1.24 (Steam 25826614).
Sources are the regenerated SDK reference (types, fields and natives) and `native/generate_road_graph.py`,
whose maths is validated against the shipped roads. Items marked **to confirm** wait on a
`mapmaker/tools/survey_world_data.py` run against the installed game.

## How the game builds its world

The world is a grid of triangles. There is no heightmap and no single world file.

- **One JSON file per triangle**: `rom/data/tiles/0/tile_<x>_<z>_<t>.json`. Missing files are open
  ocean (**to confirm**).
- **Grid maths.** Cell `(x, z)` is the parallelogram spanned by `(512, 0, 0)` and `(256, 0, 256·√3)`
  (globals `g_tile_grid_x_dir`, `g_tile_grid_y_dir`). `t = 0` is its lower-left triangle and `t = 1` the
  upper-right one. A tile's origin is
  `(512x + 256z + (t ? 512 : 256), height · 32, 256·√3 · (z + (t ? 2/3 : 1/3)))`.
- **Terrain** comes from a library of authored terrain pieces (`environment_tile_definition`: a mesh,
  three corner heights and three edge identifiers). Each tile names its piece (`definition_name`), an
  `orientation` (0-2) and a `height` in 32 m steps. Neighbouring pieces must have matching edges; the
  developers' editor has a "generate matching tiles" helper for this (`chunk_edit_panel.generate_matching_tiles`).
- **Roads and paths** are cubic Bézier links (`junction_links`: `pos_a`, `pos_b`, `dir_a`, `dir_b`,
  `type`) in world coordinates, joined by **junctions** (`junctions`: `definition_name`, tile-local
  `position`, `normal`, `rotation`) whose shapes come from `tile_junction_definitions.json`. Road meshes
  are generated in game from the link type (`path_link_definition`).
- **Placed things** per tile (in-memory names; the JSON keys are **to confirm**):
  - `prop_spawn_zones`: prop id, position, rotation. Props (light poles, fences, barriers...) come from
    the prop definitions (`prop_definition`: mesh, mass, zones). The developers' prop editor has a
    fence mode that repeats a prop along a line.
  - `spawn_zones`: building spots (position, rotation, `size`, `foundation`, `filter`). At world creation,
    `server_scene.world_data.build` assigns a building to each spot from the seed, so houses are the
    game's own buildings placed on spots.
  - `loot_spawn_zones`, `actor_spawn_zones` (with patrol waypoints), `dead_drop_spawn_zones`.
  - `vegetation_positions` (trees, **to confirm**).
- **Loading.** Tiles stream in around players on worker threads (`*_environment_tile_task.execute`).
  Every client loads tiles from its own install, so all players need the same map.
- **Saves.** A save stores per-tile vehicles, actors, floor items, props and so on
  (`server_scene.save_data.tile`). Props that came from the map are written into the save once the tile
  has loaded, so a map change only fully applies to **new worlds**.
- **File access.** Every data file goes through a few game.exe natives: `file.read_string`
  (game.exe+0x3f080), `file.read_binary` (+0x3f0e0), `file.get_is_exists` (+0x3f140),
  `file.get_files_in_dir` (+0x3edf0). gcl code calls them through per-function call cells, so a mod can
  swap those cells to serve a map pack's files in place of `rom/data/tiles` without touching the install.
  The [MapEditor mod](../mods/map-editor.md) does this. Paths are `file.path` values: a store (0 embedded,
  1 system, 2 rom, 3 user) and a string.
- **The developers' editor ships in the game, switched off**: chunk (terrain), tile (junctions), prop,
  loot, actor spawn, dead drop, prefab and map-screenshot panels, toggled by `g_is_*_window` globals and
  drawn from `update_ui` in `main.gc`, which has no caller in the release build. It saves through
  `chunk_edit_panel.save_scene`, which writes each tile with `client_scene.environment_tile.save`.
  The [MapEditor mod](../mods/map-editor.md) opens it in game; it is the main way to make maps.

## Map pack format (version 1)

A map pack is a folder, by default `<game>\AnyAPI and Modding\maps\<map name>\`:

```text
map.json
tiles/0/tile_<x>_<z>_<t>.json   (same format as the game's own tiles)
```

`map.json`:

| Key | Meaning |
|---|---|
| `format` | `1` |
| `name`, `author`, `description` | Shown in the editor and the in-game map picker |
| `base` | `stock` (started from the stock world) or `blank` |
| `game_build` | Steam build the pack was saved against (informational) |
| `tiles` | Number of tile files |

The tiles folder is the **whole** world: tiles that are not in the pack are ocean. Tile files keep every
key the game wrote, including ones the editor does not understand yet, so a pack saved from the stock
world loads exactly like the stock world.
